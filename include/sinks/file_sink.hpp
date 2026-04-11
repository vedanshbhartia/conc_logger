#pragma once
#include "../sink.hpp"
#include "../log_level.hpp"
#include <fstream>
#include <iomanip>
#include <sstream>
#include <mutex>
#include <stdexcept>
#include <ctime>

class FileSink : public Sink {
public:
    explicit FileSink(const std::string& path, bool append = true) {
        auto mode = std::ios::out | (append ? std::ios::app : std::ios::trunc);
        file_.open(path, mode);
        if (!file_.is_open()) {
            throw std::runtime_error("FileSink: cannot open " + path);
        }
    }

    void write(const LogEntry& entry) override {
        std::time_t t  = std::chrono::system_clock::to_time_t(entry.timestamp);
        std::tm     tm = *std::localtime(&t);

        std::lock_guard<std::mutex> lk(mutex_);
        file_ << std::put_time(&tm, "%Y-%m-%d %H:%M:%S")
              << " [" << to_string(entry.level) << "]"
              << " [" << entry.module << "]"
              << " [T" << entry.omp_thread_id << "]"
              << "  " << entry.message;

        if (!entry.thread_dump.empty()) {
            file_ << entry.thread_dump;
        }
        file_ << '\n';
    }

    void flush() override {
        std::lock_guard<std::mutex> lk(mutex_);
        file_.flush();
    }

private:
    std::ofstream file_;
    std::mutex    mutex_;
};
