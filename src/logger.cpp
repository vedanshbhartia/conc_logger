#include "../include/logger.hpp"
#include <omp.h>
#include <sstream>

Logger::Logger(std::string module,
               std::vector<std::shared_ptr<Sink>> sinks,
               LogLevel min_level,
               LoggerPolicy policy)
    : module_(std::move(module))
    , sinks_(std::move(sinks))
    , min_level_(min_level)
    , policy_(policy)
    , queue_(policy.queue_capacity)
{
    if (policy_.async) {
        worker_ = std::thread(&Logger::drain_loop, this);
    }
}

Logger::~Logger() {
    if (policy_.async) {
        queue_.stop();
        if (worker_.joinable()) worker_.join();
    }
    for (auto& s : sinks_) s->flush();
}

void Logger::log(LogLevel level, const std::string& msg, bool with_thread_dump) {
    if (level < min_level_) return;

    LogEntry entry;
    entry.level         = level;
    entry.module        = module_;
    entry.message       = msg;
    entry.timestamp     = std::chrono::system_clock::now();
    entry.omp_thread_id = omp_get_thread_num();

    if (with_thread_dump) {
        entry.thread_dump = capture_thread_dump();
    }

    if (!policy_.async) {
        dispatch(entry);
        return;
    }

    pending_.fetch_add(1, std::memory_order_relaxed);
    int undelivered = queue_.push(std::move(entry), policy_.overflow_policy);
    if (undelivered > 0) {
        if (pending_.fetch_sub(static_cast<std::size_t>(undelivered),
                               std::memory_order_acq_rel) == static_cast<std::size_t>(undelivered)) {
            drained_cv_.notify_all();
        }
    }
}

void Logger::trace(const std::string& msg)             { log(LogLevel::TRACE, msg); }
void Logger::debug(const std::string& msg)             { log(LogLevel::DEBUG, msg); }
void Logger::info (const std::string& msg)             { log(LogLevel::INFO,  msg); }
void Logger::warn (const std::string& msg, bool dump)  { log(LogLevel::WARN,  msg, dump); }
void Logger::error(const std::string& msg, bool dump)  { log(LogLevel::ERROR, msg, dump); }
void Logger::fatal(const std::string& msg, bool dump)  { log(LogLevel::FATAL, msg, dump); }

void Logger::flush() {
    if (!policy_.async) {
        for (auto& s : sinks_) s->flush();
        return;
    }
    {
        std::unique_lock<std::mutex> lk(drained_mutex_);
        drained_cv_.wait(lk, [this] {
            return pending_.load(std::memory_order_acquire) == 0;
        });
    }
    for (auto& s : sinks_) s->flush();
}

void Logger::drain_loop() {
    std::deque<LogEntry> batch;
    while (queue_.drain(batch)) {
        for (auto& entry : batch) {
            dispatch(entry);
            if (pending_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                drained_cv_.notify_all();
            }
        }
        batch.clear();
    }
    // Drain any remaining entries that arrived before stop()
    while (!queue_.empty()) {
        if (!queue_.drain(batch)) break;
        for (auto& entry : batch) {
            dispatch(entry);
            if (pending_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                drained_cv_.notify_all();
            }
        }
        batch.clear();
    }
}

void Logger::dispatch(const LogEntry& entry) {
    int n = static_cast<int>(sinks_.size());
    if (n == 0) return;

    #pragma omp parallel for schedule(static) num_threads(n) if(n > 1)
    for (int i = 0; i < n; ++i) {
        sinks_[i]->write(entry);
    }
}

std::string Logger::capture_thread_dump() const {
    std::ostringstream oss;
    oss << "\n  [thread-dump]"
        << " omp_thread="    << omp_get_thread_num()
        << " num_threads="   << omp_get_num_threads()
        << " max_threads="   << omp_get_max_threads()
        << " in_parallel="   << (omp_in_parallel() ? "yes" : "no")
        << " nesting_level=" << omp_get_level()
        << " active_level="  << omp_get_active_level();
    return oss.str();
}
