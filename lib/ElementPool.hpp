#pragma once

#include <cstdlib>
#include <vector>
#include <memory>
#include <unistd.h>
#include <algorithm>
#include <cstdint>
#include <atomic>
#include <thread>

// Global sharded pool with shards
template <typename T>
class GlobalShardedPool
{
private:
    size_t num_shards_ = 1;
    static constexpr size_t INITIAL_SIZE = 4096;
    static constexpr size_t MAX_FREE_SIZE = 2048;

    // Individual shard containing a memory pool with its own lock and free list
    struct alignas(64) Shard
    {
        std::atomic<bool> lock_{false};
        T *pool_ = nullptr;
        size_t capacity_ = 0;
        size_t size_ = 0;
        T *free_top_ = nullptr;
        size_t free_count_ = 0;
        std::vector<T *> garbage_;

        ~Shard()
        {
            if (pool_)
                free(pool_);
            for (auto p : garbage_)
                free(p);
        }

        // Spin lock implementation for thread synchronization
        void lock()
        {
            while (lock_.exchange(true, std::memory_order_acquire))
            {
                while (lock_.load(std::memory_order_relaxed))
                {
                    std::this_thread::yield();
                }
            }
        }

        void unlock()
        {
            lock_.store(false, std::memory_order_release);
        }

        // Expand the memory pool and performs memory preheating
        void expand()
        {
            if (pool_)
                garbage_.push_back(pool_);
            capacity_ = capacity_ == 0 ? INITIAL_SIZE : capacity_ * 2;
            size_ = 0;
            pool_ = static_cast<T *>(std::aligned_alloc(64, capacity_ * sizeof(T)));
            if (!pool_)
                throw std::bad_alloc();

            const size_t PREHEAT_BYTES = 512ULL * 1024ULL * 1024ULL;
            size_t to_touch = std::min(capacity_ * sizeof(T), PREHEAT_BYTES);
            long page_size = sysconf(_SC_PAGESIZE);
            char *base = reinterpret_cast<char *>(pool_);
            for (size_t i = 0; i < to_touch; i += static_cast<size_t>(page_size))
            {
                base[i] = 0;
            }
        }

        // Allocate a batch of objects, first from free list, then from new pool space
        void allocate_batch(T **output, size_t count)
        {
            size_t allocated = 0;

            while (allocated < count && free_top_ != nullptr)
            {
                T *elem = free_top_;
                free_top_ = reinterpret_cast<T **>(free_top_)[0];
                output[allocated++] = elem;
                free_count_--;
            }

            while (allocated < count)
            {
                if (size_ >= capacity_)
                {
                    expand();
                }
                output[allocated++] = &pool_[size_++];
            }
        }

        // Deallocate a batch of objects to the free list (up to MAX_FREE_SIZE limit)
        void deallocate_batch(T **input, size_t count)
        {
            for (size_t i = 0; i < count && free_count_ < MAX_FREE_SIZE; ++i)
            {
                reinterpret_cast<T **>(input[i])[0] = free_top_;
                free_top_ = input[i];
                free_count_++;
            }
        }
    };

    std::unique_ptr<Shard[]> shards_;
    std::atomic<size_t> counter_{0};

public:
    GlobalShardedPool(size_t num_shards = 64) : num_shards_(num_shards)
    {
        shards_.reset(new Shard[num_shards_]);
    }

    // Allocate a batch of objects, distributing across shards for load balancing
    void allocate_batch(T **output, size_t count)
    {
        size_t shard_id = counter_.fetch_add(1, std::memory_order_relaxed) % num_shards_;
        Shard &shard = shards_[shard_id];

        shard.lock();
        shard.allocate_batch(output, count);
        shard.unlock();
    }

    // Deallocate a batch of objects, distributing across shards
    void deallocate_batch(T **input, size_t count)
    {
        if (count == 0)
            return;

        size_t ns = num_shards_;
        size_t items_per_shard = count / ns;
        size_t remaining = count % ns;
        size_t input_index = 0;

        for (size_t shard_id = 0; shard_id < ns; ++shard_id)
        {
            size_t shard_count = items_per_shard + (shard_id < remaining ? 1 : 0);

            if (shard_count > 0)
            {
                Shard &shard = shards_[shard_id];
                shard.lock();
                shard.deallocate_batch(&input[input_index], shard_count);
                shard.unlock();
                input_index += shard_count;
            }
        }
    }

    // Pre-allocate memory pools for all shards to reduce allocation latency
    void reserve()
    {
        for (size_t i = 0; i < num_shards_; ++i)
        {
            shards_[i].lock();
            if (shards_[i].capacity_ == 0)
            {
                shards_[i].expand();
            }
            shards_[i].unlock();
        }
    }
};

// Thread-local pool for specific type T
template <typename T>
class ThreadLocalElementPool
{
private:
    static constexpr size_t CACHE_SIZE = 1024;

    GlobalShardedPool<T> &global_pool_;
    T *cache_[CACHE_SIZE];
    size_t count_ = 0;

    // Refill local cache from global pool
    void refill()
    {
        global_pool_.allocate_batch(cache_, CACHE_SIZE);
        count_ = CACHE_SIZE;
    }

    // Return half of cache to global pool when cache is full
    void return_half()
    {
        size_t half = CACHE_SIZE / 2;
        global_pool_.deallocate_batch(cache_, half);

        for (size_t i = 0; i < half; ++i)
        {
            cache_[i] = cache_[half + i];
        }
        count_ = half;
    }

public:
    ThreadLocalElementPool(GlobalShardedPool<T> &global_pool) : global_pool_(global_pool)
    {
        refill();
    }

    ~ThreadLocalElementPool()
    {
        if (count_ > 0)
        {
            global_pool_.deallocate_batch(cache_, count_);
        }
    }

    // Allocate an object from local cache or refill from global pool
    inline T *allocate()
    {
        if (count_ > 0)
        {
            return cache_[--count_];
        }
        refill();
        return cache_[--count_];
    }

    // Deallocate an object to local cache or return to global pool if full
    inline void deallocate(T *p)
    {
        if (count_ < CACHE_SIZE)
        {
            cache_[count_++] = p;
            return;
        }

        return_half();
        cache_[count_++] = p;
    }
};

// Provides the original ElementPool API
template <typename T>
class ElementPool : private ThreadLocalElementPool<T>
{
private:
    static GlobalShardedPool<T> *&getGlobalPoolPtr()
    {
        static GlobalShardedPool<T> *global_pool = nullptr;
        return global_pool;
    }
    static GlobalShardedPool<T> &getGlobalPool(size_t num_shards = 0)
    {
        auto &global_pool = getGlobalPoolPtr();
        if (!global_pool)
        {
            if (num_shards == 0)
                num_shards = 64;
            global_pool = new GlobalShardedPool<T>(num_shards);
        }
        return *global_pool;
    }

public:
    // 只允许主线程在多线程前调用，后续调用无效
    static void init_shards(size_t num_shards)
    {
        auto &global_pool = getGlobalPoolPtr();
        if (!global_pool)
        {
            global_pool = new GlobalShardedPool<T>(num_shards);
        }
    }

public:
    ElementPool() : ThreadLocalElementPool<T>(getGlobalPool()) {}
    ~ElementPool() = default;

    inline T *allocate()
    {
        return ThreadLocalElementPool<T>::allocate();
    }

    void deallocate(T *obj)
    {
        ThreadLocalElementPool<T>::deallocate(obj);
    }

    // Pre-allocate memory pools
    static void reserve(size_t num_shards = 0)
    {
        getGlobalPool(num_shards).reserve();
    }
};

// Global sharded page pool
class GlobalShardedPagePool
{
private:
    size_t num_shards_ = 1;
    static constexpr size_t INITIAL_SIZE = 16384;
    static constexpr size_t MAX_FREE_SIZE = 8192;
    static constexpr size_t PAGE_SIZE = 4096;

    struct alignas(64) Shard
    {
        std::atomic<bool> lock_{false};
        char *pool_ = nullptr;
        size_t capacity_ = 0;
        size_t size_ = 0;
        char *free_top_ = nullptr;
        size_t free_count_ = 0;
        std::vector<char *> garbage_;

        ~Shard()
        {
            if (pool_)
                free(pool_);
            for (auto p : garbage_)
                free(p);
        }

        void lock()
        {
            while (lock_.exchange(true, std::memory_order_acquire))
            {
                while (lock_.load(std::memory_order_relaxed))
                {
                    std::this_thread::yield();
                }
            }
        }

        void unlock()
        {
            lock_.store(false, std::memory_order_release);
        }

        void expand()
        {
            if (pool_)
                garbage_.push_back(pool_);
            capacity_ = capacity_ == 0 ? INITIAL_SIZE : capacity_ * 2;
            size_ = 0;
            size_t pool_size = capacity_ * PAGE_SIZE;
            pool_ = static_cast<char *>(aligned_alloc(4096, pool_size));
            if (!pool_)
                throw std::runtime_error("Failed to allocate memory for PagePool");

            const size_t PREHEAT_BYTES = 512ULL * 1024ULL * 1024ULL;
            size_t to_touch = std::min(pool_size, PREHEAT_BYTES);
            long page_size = sysconf(_SC_PAGESIZE);
            char *base = pool_;
            for (size_t i = 0; i < to_touch; i += static_cast<size_t>(page_size))
            {
                base[i] = 0;
            }
        }

        void allocate_batch(char **output, size_t count)
        {
            size_t allocated = 0;

            while (allocated < count && free_top_ != nullptr)
            {
                char *page = free_top_;
                free_top_ = *reinterpret_cast<char **>(free_top_);
                output[allocated++] = page;
                free_count_--;
            }

            while (allocated < count)
            {
                if (size_ >= capacity_)
                {
                    expand();
                }
                output[allocated++] = &pool_[(size_++) * PAGE_SIZE];
            }
        }

        void deallocate_batch(char **input, size_t count)
        {
            for (size_t i = 0; i < count && free_count_ < MAX_FREE_SIZE; ++i)
            {
                *reinterpret_cast<char **>(input[i]) = free_top_;
                free_top_ = input[i];
                free_count_++;
            }
        }
    };

    std::unique_ptr<Shard[]> shards_;
    std::atomic<size_t> counter_{0};

public:
    GlobalShardedPagePool(size_t num_shards = 64) : num_shards_(num_shards)
    {
        shards_.reset(new Shard[num_shards_]);
    }

    void allocate_batch(char **output, size_t count)
    {
        size_t shard_id = counter_.fetch_add(1, std::memory_order_relaxed) % num_shards_;
        Shard &shard = shards_[shard_id];

        shard.lock();
        shard.allocate_batch(output, count);
        shard.unlock();
    }

    void deallocate_batch(char **input, size_t count)
    {
        if (count == 0)
            return;

        size_t ns = num_shards_;
        size_t items_per_shard = count / ns;
        size_t remaining = count % ns;
        size_t input_index = 0;

        for (size_t shard_id = 0; shard_id < ns; ++shard_id)
        {
            size_t shard_count = items_per_shard + (shard_id < remaining ? 1 : 0);

            if (shard_count > 0)
            {
                Shard &shard = shards_[shard_id];
                shard.lock();
                shard.deallocate_batch(&input[input_index], shard_count);
                shard.unlock();
                input_index += shard_count;
            }
        }
    }

    // Pre-allocate page pools for all shards
    void reserve()
    {
        for (size_t i = 0; i < num_shards_; ++i)
        {
            shards_[i].lock();
            if (shards_[i].capacity_ == 0)
            {
                shards_[i].expand();
            }
            shards_[i].unlock();
        }
    }
};

// Thread-local page pool
class ThreadLocalPagePool
{
private:
    static constexpr size_t CACHE_SIZE = 512;

    GlobalShardedPagePool &global_pool_;
    char *cache_[CACHE_SIZE];
    size_t count_ = 0;

    void refill()
    {
        global_pool_.allocate_batch(cache_, CACHE_SIZE);
        count_ = CACHE_SIZE;
    }

    void return_half()
    {
        size_t half = CACHE_SIZE / 2;
        global_pool_.deallocate_batch(cache_, half);

        for (size_t i = 0; i < half; ++i)
        {
            cache_[i] = cache_[half + i];
        }
        count_ = half;
    }

public:
    ThreadLocalPagePool(GlobalShardedPagePool &global_pool) : global_pool_(global_pool)
    {
        refill();
    }

    ~ThreadLocalPagePool()
    {
        if (count_ > 0)
        {
            global_pool_.deallocate_batch(cache_, count_);
        }
    }

    // Allocate a page from local cache or refill from global pool
    inline char *allocate()
    {
        if (count_ > 0)
        {
            return cache_[--count_];
        }
        refill();
        return cache_[--count_];
    }

    // Deallocate a page to local cache or return to global pool if full
    inline void deallocate(char *p)
    {
        if (count_ < CACHE_SIZE)
        {
            cache_[count_++] = p;
            return;
        }

        return_half();
        cache_[count_++] = p;
    }
};

// Provides the original PagePool API
class PagePool : private ThreadLocalPagePool
{
private:
    static GlobalShardedPagePool *&getGlobalPoolPtr()
    {
        static GlobalShardedPagePool *global_pool = nullptr;
        return global_pool;
    }
    static GlobalShardedPagePool &getGlobalPool(size_t num_shards = 0)
    {
        auto &global_pool = getGlobalPoolPtr();
        if (!global_pool)
        {
            if (num_shards == 0)
                num_shards = 64;
            global_pool = new GlobalShardedPagePool(num_shards);
        }
        return *global_pool;
    }

public:
    // 只允许主线程在多线程前调用，后续调用无效
    static void init_shards(size_t num_shards)
    {
        auto &global_pool = getGlobalPoolPtr();
        if (!global_pool)
        {
            global_pool = new GlobalShardedPagePool(num_shards);
        }
    }

public:
    PagePool() : ThreadLocalPagePool(getGlobalPool()) {}
    ~PagePool() = default;

    inline char *allocate()
    {
        return ThreadLocalPagePool::allocate();
    }

    void deallocate(char *page)
    {
        ThreadLocalPagePool::deallocate(page);
    }

    static void reserve()
    {
        getGlobalPool().reserve();
    }
};