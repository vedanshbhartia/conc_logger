#pragma once
#include "log_level.hpp"
#include <string>
#include <chrono>

struct LogEntry {
    LogLevel                               level;
    std::string                            module;
    std::string                            message;
    std::chrono::system_clock::time_point  timestamp;
    int                                    omp_thread_id;  // OpenMP thread that called log()
    std::string                            thread_dump;    // empty unless requested
};
