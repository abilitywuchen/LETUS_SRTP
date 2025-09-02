#pragma once

#include <cstdlib>
#include <cstring>
#include <vector>
#include <sys/mman.h>
#include <unistd.h>
#include <algorithm>
#include <cstdint>
template <typename T>
class ElementPool {
    public:
    ElementPool() = default;
    ~ElementPool() {
        if (pool_) {
            free(pool_);
            capacity_ = 0;
            size_ = 0;
        }
        for (auto p : garbage) {
            free(p);
        }
    }
    
    inline T* allocate() {
        if (!free_list.empty()) {
            T* obj = free_list.back();
            free_list.pop_back();
            return obj;
        }
        if (size_ >= capacity_)
            reserve();
        return &pool_[size_++];
    }
    void deallocate(T* obj) {
        free_list.push_back(obj);
    }
    void reserve() {
        // std::cout << "Reserving " << capacity_ << std::endl;
        if (pool_) {
            garbage.push_back(pool_);
        }
        capacity_ = capacity_ == 0 ? 8192 : capacity_ * 2;
        size_ = 0;
        pool_ = (T*)malloc(capacity_ * sizeof(T)); 
        // memset(pool_, 0, capacity_ * sizeof(T));
        if (pool_ == nullptr) {
            exit(0);
        }
        // Fixed preheat size: 512MB for optimal performance
        const size_t PREHEAT_BYTES = 512ULL * 1024ULL * 1024ULL;
        size_t preheat_bytes = PREHEAT_BYTES;
        size_t to_touch = std::min(capacity_ * sizeof(T), preheat_bytes);
        long page_size = sysconf(_SC_PAGESIZE);
        char* base = reinterpret_cast<char*>(pool_);
        for (size_t i = 0; i < to_touch; i += static_cast<size_t>(page_size)) {
            base[i] = 0;
        }
        // madvise(pool_, capacity_ * sizeof(T), MADV_WILLNEED);
        // std::cout << "Reserved " << capacity_ << std::endl;
    }

    private:
    T* pool_ = nullptr;
    size_t capacity_ = 0;
    size_t size_ = 0;
    std::vector<T*> garbage;
    std::vector<T*> free_list; // 只记录被释放的块 首选查找被释放的
};

class PagePool {
    public:
    PagePool() = default;
    ~PagePool() {
        if (pool_) {
            free(pool_);
            capacity_ = 0;
            size_ = 0;
        }
        for (auto p : garbage) {
            free(p);
        }
    }
    
    inline char* allocate() {
        if (!free_list.empty()) {
            char* page = free_list.back();
            free_list.pop_back();
            return page;
        }
        if (size_ >= capacity_)
            reserve();
        return &pool_[(size_++) * PAGE_SIZE];
    }
    void deallocate(char* page) {
        free_list.push_back(page);
    }
    void reserve() {
        // std::cout << "Reserving " << capacity_ << std::endl;
        if (pool_) {
            garbage.push_back(pool_);
        }
        capacity_ = capacity_ == 0 ? 131072 : capacity_ * 2;
        size_ = 0;
        size_t pool_size = capacity_ * PAGE_SIZE;
        pool_ = (char*)aligned_alloc(4096, pool_size);
        // memset(pool_, 0, capacity_ * sizeof(T));
        if (pool_ == nullptr) {
            throw std::runtime_error("Failed to allocate memory for PagePool");
            exit(0);
        }
        // Fixed preheat size: 512MB for optimal performance
        const size_t PREHEAT_BYTES = 512ULL * 1024ULL * 1024ULL;
        size_t preheat_bytes = PREHEAT_BYTES;
        size_t to_touch = std::min(pool_size, preheat_bytes);
        long page_size = sysconf(_SC_PAGESIZE);
        char* base = pool_;
        for (size_t i = 0; i < to_touch; i += static_cast<size_t>(page_size)) {
            base[i] = 0;
        }
        // madvise(pool_, pool_size, MADV_WILLNEED);
        // std::cout << "Reserved " << capacity_ << std::endl;
    }

    private:
    char* pool_ = nullptr;
    size_t capacity_ = 0;
    size_t size_ = 0;
    std::vector<char*> garbage;
    std::vector<char*> free_list;
};