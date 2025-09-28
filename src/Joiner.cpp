#include "Joiner.hpp"
#include "DMMTrie.hpp"
#include <chrono>
#include <unordered_set>

Joiner::Joiner(Master* master) : master_(master) {
    joiner_thread_ = thread(std::bind(&Joiner::run, this));
#ifdef JOINER_LOG
    PrintLog("STARTED");
#endif
}
VDLS* Joiner::GetValueStore() {
    return master_->GetValueStore();
}
bool Joiner::WaitForOldVersion(uint64_t version) {
    bool done[master_->MAX_REGION_NUM];
    memset(done, false, sizeof(done));
    uint8_t done_count = 0;
    bool last_try = false;

    while (true) {
        uint8_t i = master_->MAX_REGION_NUM - 1;
        // PrintLog("RUN: " + to_string(!stop_) + " RUNNING: " + to_string(master_->running_region_num_));
        while (true) {
            i = (i + 1) % master_->MAX_REGION_NUM;
            if (done[i]) continue;
            auto& region = master_->regions_.at(i);
            auto& region_buffer = region->buffer_;
            // PrintLog("Thread " + std::to_string(i) + " version: " + to_string(version) + " region->commited_version_: " + to_string(region->commited_version_) + " buffer_size: " + to_string(region_buffer.size()) + " Buffer Front: " + to_string(region_buffer.front().first));  
            // if (region_buffer.size() > 0) {
            auto& front = region_buffer.front();
            if (front.first == 0) {
                return false;
            }
            if (front.first <= region->commited_version_) {
#ifdef JOINER_LOG 
                PrintLog(to_string(i) + " Done");
#endif
                buffer_.insert(buffer_.end(), std::make_move_iterator(front.second.begin()), std::make_move_iterator(front.second.end()));
                // for (auto& item : front.second) {
                //     buffer_.push_back(move(item));
                // }
                region_buffer.pop_front();
                // PrintLog("Thread " + std::to_string(i) + " Size: " + std::to_string(region_buffer.size()));
                done_count++;
                done[i] = true;
            }
            else
                this_thread::sleep_for(chrono::milliseconds(25));

            // }
            // else
            //     PrintLog("MISS");
            if (done_count == master_->MAX_REGION_NUM) {
                return true;
            }
        }
        // this_thread::sleep_for(std::chrono::milliseconds(100));
        // if (stop_) {
        //     PrintLog("LAST CHECK");
        //     bool all_done = true;
        //     for(auto &region_buffer : master_->bottomup_buffers_) {
        //         if (region_buffer.size() > 0 && region_buffer.front().first != 0) {

        //             PrintLog("LAST TRY SET");
        //             all_done = false;
        //             last_try = true;
        //             break;
        //         }
        //     }
        //     if (all_done) {
        //         PrintLog("ALL DONE");
        //         return false;
        //     }
        // }
    }
}

void Joiner::WriteAllBufferItems() {
    // std::cout << "WriteAllBufferItems" << std::endl;

    string pid = "";

    // get the latest version number of a page
    uint64_t page_version = GetPageVersion({ 0, 0, false, pid }).first;
    PageKey pagekey = { version_, 0, false, pid },
        old_pagekey = { page_version, 0, false, pid };
    BasePage* page = GetPage(old_pagekey);  // load the page into lru cache

    if (page == nullptr) {
        // GetPage returns nullptr means that the pid is new
        // page = new BasePage(this, nullptr, pid);
        Node *empty_root = new IndexNode(0, "", 0);
        page = new (pool_.allocate()) BasePage(this, empty_root, pid);
        // PrintLog("Creating new page " + pid);
        // page = pool_.allocate();
        //   page->SetAttribute(this, nullptr, pid);
        PutPage(pagekey, page);  // add the newly generated page into cache
    }

    DeltaPage* deltapage = GetDeltaPage(pid);

    //   for (const auto& item : buffer_) {
    //       std::string nibbles = item.nibbles_;
    //       tuple<uint64_t, uint64_t, uint64_t> location;
    //       string value, child_hash;
    //       if (nibbles.size() == 2) {  // indexnode + indexnode
    //         BasePage* base = master_->GetPage({ version_, 0, false, nibbles });
    //         child_hash = base->GetRoot()->GetHash();
    //       }
    //       else {  // (indexnode + leafnode) or leafnode
    //         PrintLog("Commit Phase 2-1-2");
    //         value = item.value_;
    //         value_store_ = GetValueStore();
    //         // location = value_store_->WriteValue(version_, nibbles, value);
    //       }
    //       page->UpdatePage(version_, location, value, nibbles, child_hash,
    //           deltapage, pagekey);
    //   }

    // 计算最大长度
    size_t max_len = 6;

    // 逐层处理：从 max_len 开始，每次减少 2，直到达到 2（包含根页写入）
    for (size_t layer_len = max_len; layer_len >= 2; layer_len -= 2) {
        // 收集本层需要的子页 pid（layer_len - 2）
        std::unordered_set<std::string> needed_pages;
        size_t layer_original_size = buffer_.size();
        size_t pid_len = layer_len - 2;
        for (size_t i = 0; i < layer_original_size; ++i) {
            const auto& item = buffer_[i];
            if (item.nibbles_.size() != layer_len) continue;
            // 根页面额外处理
            if (pid_len == 0) continue;
            needed_pages.insert(item.nibbles_.substr(0, pid_len));
        }

        // 为本次所需的 pid 创建或复用对应页
        for (const auto &pid : needed_pages) {
            uint64_t pre_version = GetPageVersion({0, 0, false, pid}).first;
            PageKey old_pagekey = {pre_version, 0, false, pid};
            PageKey new_pagekey = {version_, 0, false, pid};
            if (pre_version != 0) {
                BasePage* old_page = GetPage(old_pagekey);
                if (old_page != nullptr) {
                    UpdatePageKey(old_pagekey, new_pagekey);
                    continue;
                }
            }
            BasePage *page = GetPage(new_pagekey);
            if (!page) {
                Node *empty_root = new IndexNode(0, "", 0);
                BasePage *newpage = new (pool_.allocate()) BasePage(this, empty_root, pid);
                PutPage(new_pagekey, newpage);
            }
        }

        // 处理属于当前长度的条目，把它们写入对应子页，并生成上层条目
        for (size_t i = 0; i < layer_original_size; ++i) {
            const auto& item = buffer_[i];
            if (item.nibbles_.size() != layer_len) continue;

            // 直接写入根页, 不生成上级条目
            if (pid_len == 0) {
                UpdatePageKey(old_pagekey, pagekey);
                BasePage* target_root = GetPage(pagekey);
                if (target_root) {
                    target_root->UpdatePage(version_, item.location_, item.value_, item.nibbles_, item.child_hash_, deltapage, pagekey);
                }
                continue;
            }

            string pid = item.nibbles_.substr(0, pid_len);
            PageKey target_pagekey = {version_, 0, false, pid};
            BasePage* target_page = GetPage(target_pagekey);
            if (target_page){
                string nibbles = item.nibbles_.substr(pid_len);
                target_page->UpdatePage(version_, item.location_, item.value_, nibbles, item.child_hash_, deltapage, target_pagekey);
                string child_hash = target_page->GetRoot()->GetHash();
                string upper_nibbles = pid;
                BufferItem root_item(item.location_, "", upper_nibbles, child_hash);
                buffer_.push_back(root_item);
            }
        }
    }
    buffer_.clear();
    // PrintLog("Version " + to_string(version_) + " committed");
}

void Joiner::run() {
    // return;
    while (true) {
        // PrintLog("WaitForOldVersion");
        if (!WaitForOldVersion(version_)) {
            // PrintLog("Stopped | " + GetCurrentTimeStamp(3));
            // stopped_ = true;
            return;
        }
#ifdef TIMESTAMP_LOG
        PrintLog("COMMIT START <" + to_string(version_) + "> | " + GetCurrentTimeStamp(3));
#endif
        // PrintLog("WriteAllBufferItems");
        WriteAllBufferItems();
#ifdef TIMESTAMP_LOG
        PrintLog("COMMIT DONE <" + to_string(version_) + "> | " + GetCurrentTimeStamp(3));
#endif
#ifdef JOINER_LOG 
        PrintLog("Version " + to_string(version_) + " Joined");
#endif
        version_++;
    }
}

void Joiner::Stop() {
    stop_ = true;
}

void Joiner::Join() {
    stop_ = true;
    joiner_thread_.join();
#ifdef JOINER_LOG
    PrintLog("JOINED");
#endif
}