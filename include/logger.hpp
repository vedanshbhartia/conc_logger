#pragma once
#include "log_level.hpp"
#include "log_entry.hpp"
#include "sink.hpp"
#include "policy.hpp"
#include "bounded_queue.hpp"

#include <string>
#include <vector>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>

class Logger {
public:
    Logger(std::string module,
           std::vector<std::shared_ptr<Sink>> sinks,
           LogLevel min_level = LogLevel::INFO,
           LoggerPolicy policy = {});

    ~Logger();

    void log(LogLevel level, const std::string& msg, bool with_thread_dump = false);

    void trace(const std::string& msg);
    void debug(const std::string& msg);
    void info (const std::string& msg);
    void warn (const std::string& msg, bool dump = false);
    void error(const std::string& msg, bool dump = false);
    void fatal(const std::string& msg, bool dump = true);

    void flush();

private:
    void        drain_loop();
    void        dispatch(const LogEntry& entry);
    std::string capture_thread_dump() const;

    std::string module_;
    std::vector<std::shared_ptr<Sink>> sinks_;
    LogLevel min_level_;
    LoggerPolicy policy_;

    BoundedQueue<LogEntry> queue_;
    std::thread worker_;

    std::atomic<std::size_t> pending_{0};
    std::mutex drained_mutex_;
    std::condition_variable drained_cv_;
};
