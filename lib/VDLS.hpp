#ifndef _VDLS_HPP_
#define _VDLS_HPP_

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <atomic>
#include <deque>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <queue>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <tuple>
#include <vector>
#include <condition_variable>
#include <cstring>
#include <chrono>
#include <filesystem>
#include "third_party/blockingconcurrentqueue.h"  // 包含阻塞无锁队列库

using namespace std;

class VDLS {
public:
    VDLS(string file_path = "/mnt/c/Users/qyf/Desktop/LETUS_prototype/data/", string prefix = "")
        : file_path_(file_path + "data_file_" + prefix + "_"),
          write_map_(MAP_FAILED),
          read_map_(MAP_FAILED),
          read_map_fileID_(-1),
          stop_flag_(false) {
        write_thread_ = thread(&VDLS::WriteThreadValue, this);
    }

    ~VDLS() {
        if (write_thread_.joinable()) {
            write_thread_.join();
        }
        if (read_map_ != MAP_FAILED) {
            munmap(read_map_, MaxFileSize);
        }
    }
    void Stop(){
        write_queue_.enqueue({ nullptr, -1, 0, 0 }); // 发送退出信号
        stop_flag_.store(true);
    }
    tuple<int, size_t, size_t> WriteValue(uint64_t version, const string& key, const string& value) {
        auto record_ptr = make_unique<string>(to_string(version) + "," + key + "," + value + "\n");
        size_t record_size = record_ptr->size();
        size_t offset;
        if(current_offset_ + record_size > MaxFileSize){
             current_fileID_ ++;
             current_offset_ = 0;
             offset = 0;
        }
        offset = current_offset_;
        current_offset_ += record_size;
        // 入队无锁队列
        write_queue_.enqueue({ move(record_ptr), current_fileID_, offset, record_size });
        return make_tuple(current_fileID_, offset, record_size);
    }
    
    void WriteThreadValue() {
        OpenAndMapWriteFile();
        while (true) {
            WriteTask task;
            // 阻塞等待任务
            write_queue_.wait_dequeue(task);
            
            // 检查退出信号
            if (task.fileID == -1) {
                break;
            }

            if (task.fileID != fileID_) {
                // 切换文件
                if (write_map_ != MAP_FAILED) {
                    msync(write_map_, MaxFileSize, MS_SYNC);
                    munmap(write_map_, MaxFileSize);
                }
                fileID_ = task.fileID;
                offset_ = 0; // 重置偏移量
                OpenAndMapWriteFile();
            }
            memcpy(static_cast<char*>(write_map_) + task.offset, 
                   task.record_ptr->c_str(), 
                   task.size);
            offset_ += task.size; // 更新当前偏移量
        }
    }
    string ReadValue(const tuple<uint64_t, uint64_t, uint64_t>& location) {
        uint64_t fileID, offset, size;
        tie(fileID, offset, size) = location;

        // 检查是否需要重新映射文件
        if (fileID != read_map_fileID_) {
            if (read_map_ != MAP_FAILED) {
                munmap(read_map_, MaxFileSize);
                read_map_ = MAP_FAILED;
            }
            OpenAndMapReadFile(fileID);
            read_map_fileID_ = fileID;
        }
        while(1){
            if(fileID_ > fileID) break;
            if(fileID_ == fileID && offset_ > offset + size) break; 
            this_thread::sleep_for(chrono::milliseconds(10));  // 短暂等待后重试
        }
        // 从映射区域读取数据
        string line(static_cast<char*>(read_map_) + offset, size);

        stringstream ss(line);
        string temp, value;

        getline(ss, temp, ',');  // 版本号
        getline(ss, temp, ',');  // 键
        getline(ss, value);      // 值

        return value;
    }

private:
    struct WriteTask {
        unique_ptr<string> record_ptr;  // 使用unique_ptr存储字符串指针
        int fileID;
        size_t offset;
        size_t size;
    };

    string file_path_;
    const uint64_t MaxFileSize = 64 * 1024 * 1024; // 64MB
    int current_fileID_ = 0;
    size_t current_offset_ = 0;
    int fileID_ = 0;  // 用于跟踪当前文件ID
    int offset_ = 0; // 用于跟踪当前偏移量
    moodycamel::BlockingConcurrentQueue<WriteTask, moodycamel::ConcurrentQueueDefaultTraits> write_queue_;
    thread write_thread_;
    //thread file_thread_;
    void* write_map_;
    void* read_map_;
    int64_t read_map_fileID_;
    atomic<bool> stop_flag_;

    void OpenAndMapWriteFile() {
        string filename = file_path_ + to_string(fileID_) + ".dat";

        // 打开或创建文件
        int fd = open(filename.c_str(), O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
        if (fd == -1) {
            throw runtime_error("Cannot open or create file: " + filename);
        }

        // 确保文件至少有 MaxFileSize 大小
        ftruncate(fd, MaxFileSize);

        // 内存映射文件为写映射区域
        write_map_ = mmap(nullptr, MaxFileSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (write_map_ == MAP_FAILED) {
            close(fd);
            throw runtime_error("Memory map for writing failed: " + filename);
        }

        close(fd);
    }

    void OpenAndMapReadFile(uint64_t fileID) {
        string filename = file_path_ + to_string(fileID) + ".dat";
        while (!filesystem::exists(filename)) { 
            if (stop_flag_.load()) {
                throw runtime_error("File not found and stop flag set: " + filename);
            }
            this_thread::sleep_for(chrono::milliseconds(10));  // 短暂等待后重试
        }
        // 打开文件
        int fd = open(filename.c_str(), O_RDONLY);
        if (fd == -1) {
            throw runtime_error("Cannot open file for reading: " + filename);
        }

        // 直接映射MaxFileSize文件大小
        read_map_ = mmap(nullptr, MaxFileSize, PROT_READ, MAP_SHARED, fd, 0);
        if (read_map_ == MAP_FAILED) {
            close(fd);
            throw runtime_error("Memory map for reading failed: " + filename);
        }

        close(fd);
    }
};
#endif