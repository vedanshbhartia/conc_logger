#pragma once
#include "../sink.hpp"
#include "../log_level.hpp"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <mutex>
#include <ctime>

class StdoutSink : public Sink {
public:
    void write(const LogEntry& entry) override {
        std::string line = format(entry);

        // Protect std::cout from interleaved output across threads
        std::lock_guard<std::mutex> lk(mutex_);
        std::cout << line << '\n';
    }

    void flush() override {
        std::lock_guard<std::mutex> lk(mutex_);
        std::cout.flush();
    }

private:
    static std::string format(const LogEntry& entry) {
        std::time_t t  = std::chrono::system_clock::to_time_t(entry.timestamp);
        std::tm     tm = *std::localtime(&t);

        std::ostringstream oss;
        oss << std::put_time(&tm, "%H:%M:%S")
            << " [" << to_string(entry.level) << "]"
            << " [" << entry.module << "]"
            << " [T" << entry.omp_thread_id << "]"
            << "  " << entry.message;

        if (!entry.thread_dump.empty()) {
            oss << entry.thread_dump;
        }
        return oss.str();
    }

    std::mutex mutex_;
};
