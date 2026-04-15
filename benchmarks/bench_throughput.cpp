// Throughput benchmark: conc_logger vs naive mutex logger vs spdlog (optional).
//
// Null-sink variants measure logger overhead (queue contention, lock traffic,
// formatting) without disk speed interference.
//
// File-sink variants (conc_file / naive_file) measure the real-world case where
// each write hits a real std::ofstream.  The naive logger holds its mutex for the
// full I/O duration; conc_logger offloads I/O to a background thread so producer
// threads return within microseconds regardless of disk latency.
//
// Usage:  ./bench_throughput <threads> <msgs_per_thread>
// Output: CSV to stdout
//
// Compile flags:
//   -DHAVE_SPDLOG     enable spdlog variant   (needs spdlog headers + lib)

#include "../include/logger.hpp"
#include "../include/sink.hpp"
#include "../include/policy.hpp"
#include "../include/sinks/file_sink.hpp"

#include <omp.h>
#include <chrono>
#include <cstdio>
#include <cstdio>   // std::remove
#include <fstream>
#include <cstdlib>
#include <mutex>
#include <string>
#include <thread>

#ifndef IO_US
#  define IO_US 20   // simulated I/O latency in microseconds (override with -DIO_US=<N>)
#endif

#ifdef HAVE_SPDLOG
#include <spdlog/spdlog.h>
#include <spdlog/async.h>
#include <spdlog/sinks/null_sink.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/base_sink.h>
#endif

// Null sink for conc_logger - throws everything away so I/O never skews numbers
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

// ── conc_logger ───────────────────────────────────────────────────────────────
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

// Custom spdlog sink that sleeps IO_US microsecs per write, mirroring
// SimulatedSlowSink above so the three slow-sink variants are directly comparable.
template <typename Mutex>
class SpdlogSlowSink : public spdlog::sinks::base_sink<Mutex> {
protected:
    void sink_it_(const spdlog::details::log_msg& msg) override {
        std::this_thread::sleep_for(std::chrono::microseconds(IO_US));
        sink_hole_ ^= static_cast<uint8_t>(msg.payload.size());
    }
    void flush_() override {}
private:
    volatile uint8_t sink_hole_{0};
};
using SpdlogSlowSink_mt = SpdlogSlowSink<std::mutex>;

static double bench_spdlog_file(int nthreads, int nmsgs) {
    const std::string path = "/tmp/bench_spdlog_file.log";
    spdlog::init_thread_pool(1 << 17, 1);
    auto sink   = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path, /*truncate=*/true);
    auto logger = std::make_shared<spdlog::async_logger>(
        "bench_file", sink, spdlog::thread_pool(),
        spdlog::async_overflow_policy::block);

    auto t0 = wall_clock::now();
    #pragma omp parallel num_threads(nthreads)
    {
        for (int i = 0; i < nmsgs; ++i)
            logger->info("msg {}", i);
    }
    logger->flush();
    spdlog::drop("bench_file");

    double elapsed = std::chrono::duration<double>(wall_clock::now() - t0).count();
    std::remove(path.c_str());
    return (nthreads * nmsgs) / elapsed;
}

static double bench_spdlog_slowsink_producer(int nthreads, int nmsgs) {
    spdlog::init_thread_pool(1 << 17, 1);
    auto sink   = std::make_shared<SpdlogSlowSink_mt>();
    auto logger = std::make_shared<spdlog::async_logger>(
        "bench_slow_p", sink, spdlog::thread_pool(),
        spdlog::async_overflow_policy::block);

    auto t0 = wall_clock::now();
    #pragma omp parallel num_threads(nthreads)
    {
        for (int i = 0; i < nmsgs; ++i)
            logger->info("msg {}", i);
    }
    // Measure only producer enqueue time — background thread continues draining.
    double producer_elapsed =
        std::chrono::duration<double>(wall_clock::now() - t0).count();

    logger->flush();   // drain before destruction, outside the timed window
    spdlog::drop("bench_slow_p");
    return (nthreads * nmsgs) / producer_elapsed;
}

static double bench_spdlog_slowsink_total(int nthreads, int nmsgs) {
    spdlog::init_thread_pool(1 << 17, 1);
    auto sink   = std::make_shared<SpdlogSlowSink_mt>();
    auto logger = std::make_shared<spdlog::async_logger>(
        "bench_slow_t", sink, spdlog::thread_pool(),
        spdlog::async_overflow_policy::block);

    auto t0 = wall_clock::now();
    #pragma omp parallel num_threads(nthreads)
    {
        for (int i = 0; i < nmsgs; ++i)
            logger->info("msg {}", i);
    }
    logger->flush();   // includes full background drain time

    double elapsed = std::chrono::duration<double>(wall_clock::now() - t0).count();
    spdlog::drop("bench_slow_t");
    return (nthreads * nmsgs) / elapsed;
}
#endif

// ── FileSink variants ─────────────────────────────────────────────────────────
//
// conc_file / naive_file write to /tmp (tmpfs on most Linux boxes).  The kernel
// page-cache absorbs the writes sub-microsecond, so these variants mainly show
// queue / lock overhead — not disk contention.
//
// conc_slowsink / naive_slowsink inject a configurable per-write sleep that
// models real rotating-disk or high-load SSD latency (default IO_US = 20 microsecs,
// matching the middle of the 5–50 microsecs range described in the design doc).
// Override with -DIO_US=<N> at compile time.
//
// naive_file/naive_slowsink: one global mutex held for the *entire* write —
//   every thread serialises on both formatting and I/O latency.
// conc_file/conc_slowsink:  application threads only enqueue (sub-microsecs);
//   the dedicated background thread owns all I/O.

// A sink that sleeps IO_US microsecs on every write to model real disk I/O latency.
// It also appends to an in-memory string so the compiler cannot optimise the
// work away, but keeps memory bounded by rotating a fixed-size buffer.
class SimulatedSlowSink : public Sink {
public:
    void write(const LogEntry& entry) override {
        // Simulate the kernel page-writeback + disk latency.
        std::this_thread::sleep_for(std::chrono::microseconds(IO_US));
        // Touch the message so the store is not dead-code-eliminated.
        sink_hole_ ^= static_cast<uint8_t>(entry.message.size());
    }
    void flush() override {}
private:
    volatile uint8_t sink_hole_{0};
};

// Naive logger with a SimulatedSlowSink — mutex held for full write duration.
struct NaiveSlowLogger {
    std::mutex mu;
    SimulatedSlowSink sink;
    LogEntry scratch;   // reused to avoid repeated allocation

    void log(const std::string& msg) {
        scratch.message = msg;
        scratch.level   = LogLevel::INFO;
        std::lock_guard<std::mutex> lk(mu);
        sink.write(scratch);   // I/O (sleep) happens under the lock
    }
};

struct NaiveFileLogger {
    std::mutex    mu;
    std::ofstream file;

    explicit NaiveFileLogger(const std::string& path) {
        file.open(path, std::ios::out | std::ios::trunc);
        if (!file.is_open())
            throw std::runtime_error("NaiveFileLogger: cannot open " + path);
    }

    void log(const std::string& msg) {
        std::lock_guard<std::mutex> lk(mu);
        // Intentional: mutex held across the full buffered write + newline.
        file << msg << '\n';
    }
};

static double bench_naive_file(int nthreads, int nmsgs) {
    const std::string path = "/tmp/bench_naive_file.log";
    NaiveFileLogger logger(path);

    auto t0 = wall_clock::now();

    #pragma omp parallel num_threads(nthreads)
    {
        for (int i = 0; i < nmsgs; ++i)
            logger.log("msg " + std::to_string(i));
    }
    {
        std::lock_guard<std::mutex> lk(logger.mu);
        logger.file.flush();
    }

    double elapsed = std::chrono::duration<double>(wall_clock::now() - t0).count();
    std::remove(path.c_str());
    return (nthreads * nmsgs) / elapsed;
}

static double bench_conc_file(int nthreads, int nmsgs) {
    const std::string path = "/tmp/bench_conc_file.log";

    LoggerPolicy pol;
    pol.queue_capacity  = 1 << 17;
    pol.overflow_policy = OverflowPolicy::BLOCK;
    pol.async           = true;

    Logger logger("bench_file",
                  {std::make_shared<FileSink>(path, /*append=*/false)},
                  LogLevel::INFO, pol);

    auto t0 = wall_clock::now();

    #pragma omp parallel num_threads(nthreads)
    {
        for (int i = 0; i < nmsgs; ++i)
            logger.info("msg " + std::to_string(i));
    }
    logger.flush();

    double elapsed = std::chrono::duration<double>(wall_clock::now() - t0).count();
    std::remove(path.c_str());
    return (nthreads * nmsgs) / elapsed;
}

// ── Simulated-slow-sink variants ──────────────────────────────────────────────
//
// Two metrics matter here:
//
//   *_producer  — time until all producer threads have returned from log().
//                 For naive: producers are blocked for the full I/O duration.
//                 For conc:  producers return after a short enqueue (sub-microsecs).
//
//   *_total     — time until the last byte is drained (includes flush()).
//                 Both variants pay the same total I/O; the difference is WHO
//                 blocks: application threads (naive) or background thread (conc).
//
// The ratio  naive_producer / conc_producer  is the "producer tax" of the
// synchronous design: how much compute time you lose waiting for slow I/O.

static double bench_naive_slowsink_producer(int nthreads, int nmsgs) {
    NaiveSlowLogger logger;

    auto t0 = wall_clock::now();
    #pragma omp parallel num_threads(nthreads)
    {
        for (int i = 0; i < nmsgs; ++i)
            logger.log("msg " + std::to_string(i));
    }
    // No flush needed — every write already completed under the mutex.
    double elapsed = std::chrono::duration<double>(wall_clock::now() - t0).count();
    return (nthreads * nmsgs) / elapsed;
}

static double bench_conc_slowsink_producer(int nthreads, int nmsgs) {
    LoggerPolicy pol;
    pol.queue_capacity  = 1 << 17;   // large enough to absorb all messages
    pol.overflow_policy = OverflowPolicy::BLOCK;
    pol.async           = true;

    Logger logger("bench_slow_p",
                  {std::make_shared<SimulatedSlowSink>()},
                  LogLevel::INFO, pol);

    auto t0 = wall_clock::now();
    #pragma omp parallel num_threads(nthreads)
    {
        for (int i = 0; i < nmsgs; ++i)
            logger.info("msg " + std::to_string(i));
    }
    // Measure only the time until producers finish enqueueing.
    // Background thread continues draining but we don't wait for it here.
    double producer_elapsed =
        std::chrono::duration<double>(wall_clock::now() - t0).count();

    logger.flush(); // drain before destruction, but outside the timed window
    return (nthreads * nmsgs) / producer_elapsed;
}

static double bench_naive_slowsink_total(int nthreads, int nmsgs) {
    // Same as _producer for naive: producers ARE the I/O path.
    return bench_naive_slowsink_producer(nthreads, nmsgs);
}

static double bench_conc_slowsink_total(int nthreads, int nmsgs) {
    LoggerPolicy pol;
    pol.queue_capacity  = 1 << 17;
    pol.overflow_policy = OverflowPolicy::BLOCK;
    pol.async           = true;

    Logger logger("bench_slow_t",
                  {std::make_shared<SimulatedSlowSink>()},
                  LogLevel::INFO, pol);

    auto t0 = wall_clock::now();
    #pragma omp parallel num_threads(nthreads)
    {
        for (int i = 0; i < nmsgs; ++i)
            logger.info("msg " + std::to_string(i));
    }
    logger.flush(); // includes background drain time

    double elapsed = std::chrono::duration<double>(wall_clock::now() - t0).count();

    return (nthreads * nmsgs) /elapsed;
}

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

    // ── null-sink variants (measure queue/lock overhead only) ──────────────────
    if (want("conc_logger")) report("conc_logger", bench_conc(nthreads, nmsgs));
    if (want("naive"))       report("naive",       bench_naive(nthreads, nmsgs));
#ifdef HAVE_SPDLOG
    if (want("spdlog"))      report("spdlog",      bench_spdlog(nthreads, nmsgs));
#endif

    // ── real FileSink variants (tmpfs — models buffered page-cache writes) ─────
    if (want("conc_file"))       report("conc_file",       bench_conc_file(nthreads, nmsgs));
    if (want("naive_file"))      report("naive_file",      bench_naive_file(nthreads, nmsgs));
#ifdef HAVE_SPDLOG
    if (want("spdlog_file"))     report("spdlog_file",     bench_spdlog_file(nthreads, nmsgs));
#endif

    // ── simulated-slow-sink variants (IO_US microsecs per write, default 20 microsecs) ─────
    // *_producer: time until app threads finish their log() calls.
    //   naive = serialised through I/O; conc = just enqueue, returns fast.
    // *_total: time including full background drain (same I/O cost for both).
    if (want("naive_slowsink_producer")) report("naive_slowsink_producer", bench_naive_slowsink_producer(nthreads, nmsgs));
    if (want("conc_slowsink_producer"))  report("conc_slowsink_producer",  bench_conc_slowsink_producer(nthreads, nmsgs));
    if (want("naive_slowsink_total"))    report("naive_slowsink_total",    bench_naive_slowsink_total(nthreads, nmsgs));
    if (want("conc_slowsink_total"))     report("conc_slowsink_total",     bench_conc_slowsink_total(nthreads, nmsgs));
#ifdef HAVE_SPDLOG
    if (want("spdlog_slowsink_producer")) report("spdlog_slowsink_producer", bench_spdlog_slowsink_producer(nthreads, nmsgs));
    if (want("spdlog_slowsink_total"))    report("spdlog_slowsink_total",    bench_spdlog_slowsink_total(nthreads, nmsgs));
#endif

    return 0;
}
