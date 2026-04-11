#pragma once
#include <cstddef>

enum class OverflowPolicy {
    DROP_NEWEST,
    DROP_OLDEST,
    BLOCK,
};

struct LoggerPolicy {
    std::size_t    queue_capacity   = 4096;
    OverflowPolicy overflow_policy  = OverflowPolicy::DROP_NEWEST;
    bool           async            = true;  // false = synchronous, no background thread
};
