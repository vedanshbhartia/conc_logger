#pragma once
#include "log_entry.hpp"

class Sink {
public:
    virtual ~Sink() = default;
    virtual void write(const LogEntry& entry) = 0;
    virtual void flush() {}
};
