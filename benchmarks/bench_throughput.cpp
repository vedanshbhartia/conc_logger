// Throughput benchmark: conc_logger vs naive mutex logger vs spdlog (optional).
//
// All variants use a null sink so I/O doesn't skew the numbers -- we're measuring
// logger overhead (queue contention, lock traffic) not disk speed.
//
// Usage:  ./bench_throughput <threads> <msgs_per_thread>
// Output: CSV to stdout
//
// Compile with -DHAVE_SPDLOG if spdlog is available.

#include "../include/logger.hpp"
#include "../include/sink.hpp"
#include "../include/policy.hpp"

#include <omp.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>

#ifdef HAVE_SPDLOG
#include <spdlog/spdlog.h>
#include <spdlog/async.h>
#include <spdlog/sinks/null_sink.h>
#endif

// throws everything away -- we only want to measure the logger path
class NullSink : public Sink {
public:
    void write(const LogEntry&) override {}
    void flush() override {}
};

// what you'd write before knowing anything about concurrency:
// one global lock, every thread fights for it
struct NaiveLogger {
    std::mutex mu;
    void log(const std::string& msg) {
        std::lock_guard<std::mutex> lk(mu);
        (void)msg;
    }
};

using wall_clock = std::chrono::steady_clock;

static double bench_conc(int nthreads, int nmsgs) {
    LoggerPolicy pol;
    pol.queue_capacity  = 1 << 17;  // big enough that we won't drop under load
    pol.overflow_policy = OverflowPolicy::BLOCK;
    pol.async           = true;

    Logger logger("bench", {std::make_shared<NullSink>()}, LogLevel::INFO, pol);

    auto t0 = wall_clock::now();

    #pragma omp parallel num_threads(nthreads)
    {
        for (int i = 0; i < nmsgs; ++i)
            logger.info("msg " + std::to_string(i));
    }
    logger.flush();

    double elapsed = std::chrono::duration<double>(wall_clock::now() - t0).count();
    return (nthreads * nmsgs) / elapsed;
}

static double bench_naive(int nthreads, int nmsgs) {
    NaiveLogger logger;

    auto t0 = wall_clock::now();

    #pragma omp parallel num_threads(nthreads)
    {
        for (int i = 0; i < nmsgs; ++i)
            logger.log("msg " + std::to_string(i));
    }

    double elapsed = std::chrono::duration<double>(wall_clock::now() - t0).count();
    return (nthreads * nmsgs) / elapsed;
}

#ifdef HAVE_SPDLOG
static double bench_spdlog(int nthreads, int nmsgs) {
    spdlog::init_thread_pool(1 << 17, 1);
    auto sink   = std::make_shared<spdlog::sinks::null_sink_mt>();
    auto logger = std::make_shared<spdlog::async_logger>(
        "bench", sink, spdlog::thread_pool(),
        spdlog::async_overflow_policy::block);

    auto t0 = wall_clock::now();

    #pragma omp parallel num_threads(nthreads)
    {
        for (int i = 0; i < nmsgs; ++i)
            logger->info("msg {}", i);
    }
    logger->flush();
    spdlog::drop("bench");

    double elapsed = std::chrono::duration<double>(wall_clock::now() - t0).count();
    return (nthreads * nmsgs) / elapsed;
}
#endif

int main(int argc, char** argv) {
    int         nthreads = argc > 1 ? std::atoi(argv[1]) : 4;
    int         nmsgs    = argc > 2 ? std::atoi(argv[2]) : 50000;
    const char* only     = argc > 3 ? argv[3] : nullptr;  // e.g. "conc_logger", "naive", "spdlog"

    std::printf("variant,threads,msgs_per_thread,total_msgs,throughput_mps\n");

    auto report = [&](const char* name, double tput) {
        std::printf("%s,%d,%d,%d,%.0f\n",
                    name, nthreads, nmsgs, nthreads * nmsgs, tput);
        std::fflush(stdout);
    };

    auto want = [&](const char* name) {
        return only == nullptr || std::string(only) == name;
    };

    if (want("conc_logger")) report("conc_logger", bench_conc(nthreads, nmsgs));
    if (want("naive"))       report("naive",       bench_naive(nthreads, nmsgs));
#ifdef HAVE_SPDLOG
    if (want("spdlog"))      report("spdlog",      bench_spdlog(nthreads, nmsgs));
#endif

    return 0;
}
