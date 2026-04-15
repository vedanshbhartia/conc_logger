CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -fopenmp
INCLUDES := -I include

SRC      := src/logger.cpp
EXAMPLE  := examples/main.cpp
TARGET   := conc_logger

BENCH_SRC := benchmarks/bench_throughput.cpp
BENCH_BIN := bench_throughput

# opt-in spdlog: make bench SPDLOG=1
ifdef SPDLOG
SPDLOG_FLAGS := -DHAVE_SPDLOG -I/usr/include
SPDLOG_LIBS  :=
endif

.PHONY: all clean run bench

all: $(TARGET)

$(TARGET): $(SRC) $(EXAMPLE)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $^ -o $@

bench: $(BENCH_BIN)

$(BENCH_BIN): $(SRC) $(BENCH_SRC)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $^ -o $@ $(SPDLOG_FLAGS)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET) $(BENCH_BIN) app.log
