#include "../include/logger.hpp"
#include "../include/sinks/stdout_sink.hpp"
#include "../include/sinks/file_sink.hpp"
#include <omp.h>
#include <vector>
#include <string>
#include <sstream>
#include <chrono>
#include <thread>

#include "../include/sink.hpp"
#include <vector>
#include <mutex>

// A simple in-memory sink useful for testing
class MemorySink : public Sink {
public:
    void write(const LogEntry& entry) override {
        std::ostringstream oss;
        oss << "[" << to_string(entry.level) << "] " << entry.message;
        std::lock_guard<std::mutex> lk(mutex_);
        lines_.push_back(oss.str());
    }

    std::vector<std::string> lines() const {
        std::lock_guard<std::mutex> lk(mutex_);
        return lines_;
    }

private:
    mutable std::mutex       mutex_;
    std::vector<std::string> lines_;
};

int main() {
    // 1. Basic async logger with stdout + file sinks
    {
        auto mem    = std::make_shared<MemorySink>();
        auto stdout = std::make_shared<StdoutSink>();
        auto file   = std::make_shared<FileSink>("app.log", /*append=*/false);

        LoggerPolicy policy;
        policy.queue_capacity  = 1024;
        policy.overflow_policy = OverflowPolicy::DROP_NEWEST;
        policy.async           = true;

        Logger logger("net.server",
                      {stdout, file, mem},
                      LogLevel::DEBUG,
                      policy);

        logger.info("server starting up");
        logger.debug("bound to port 8080");
        logger.warn("connection pool near capacity", /*dump=*/true);
        logger.error("failed to reach upstream", /*dump=*/true);

        // 2. Logging from multiple OpenMP threads
        #pragma omp parallel num_threads(4)
        {
            int tid = omp_get_thread_num();
            std::ostringstream oss;
            oss << "request handled by thread " << tid;
            logger.info(oss.str());

            if (tid == 2) {
                logger.warn("thread 2 saw high latency", /*dump=*/true);
            }
        }

        logger.flush();

        std::cout << "\n--- MemorySink captured " << mem->lines().size() << " entries ---\n";
        for (auto& l : mem->lines()) std::cout << "  " << l << '\n';
    }

    // 4. Overflow policy demo: fill the queue faster than it drains
    {
        LoggerPolicy tight;
        tight.queue_capacity  = 8;
        tight.overflow_policy = OverflowPolicy::DROP_OLDEST;
        tight.async           = true;

        Logger logger("stress.test",
                      {std::make_shared<StdoutSink>()},
                      LogLevel::INFO,
                      tight);

        std::cout << "\n--- Overflow demo (DROP_OLDEST, capacity=8) ---\n";
        for (int i = 0; i < 20; ++i) {
            logger.info("burst message " + std::to_string(i));
        }
        logger.flush();
    }

    // 5. Synchronous mode (no background thread)
    {
        LoggerPolicy sync_policy;
        sync_policy.async = false;

        Logger logger("db.client",
                      {std::make_shared<StdoutSink>()},
                      LogLevel::WARN,
                      sync_policy);

        std::cout << "\n--- Sync mode (min_level=WARN) ---\n";
        logger.info("this should be filtered out");
        logger.warn("slow query detected");
        logger.error("connection dropped", /*dump=*/true);
        logger.fatal("unrecoverable state");
    }

    return 0;
}
