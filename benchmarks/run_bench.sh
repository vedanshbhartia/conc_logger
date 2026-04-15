#!/bin/bash
# Sweeps over thread counts and optionally grabs perf counters.
# Run from project root: bash benchmarks/run_bench.sh

set -e

BIN=./bench_throughput
MSGS=50000
SLOW_MSGS=2000    # slow-sink variants: each msg costs IO_US microsecs, so keep count low
THREADS=(1 2 4 8 16 32 64 128)
PERF_THREADS=16

if [ ! -f "$BIN" ]; then
    echo "build first: make bench SPDLOG=1"
    exit 1
fi

# Detect which variants are compiled in by doing a dry run with 1 msg
# conc_logger and naive are always present; spdlog_* require HAVE_SPDLOG.
NULL_VARIANTS=("conc_logger" "naive")
for v in "spdlog"; do
    if $BIN 1 1 "$v" 2>/dev/null | grep -q "^${v},"; then
        NULL_VARIANTS+=("$v")
    fi
done

# File-sink variants - spdlog_file only present when compiled with HAVE_SPDLOG
FILE_VARIANTS=("conc_file" "naive_file")
for v in "spdlog_file"; do
    if $BIN 1 1 "$v" 2>/dev/null | grep -q "^${v},"; then
        FILE_VARIANTS+=("$v")
    fi
done

# Slow-sink producer and total arrays — spdlog variants detected the same way
SLOW_PRODUCER_VARIANTS=("naive_slowsink_producer" "conc_slowsink_producer")
SLOW_TOTAL_VARIANTS=("naive_slowsink_total" "conc_slowsink_total")
for v in "spdlog_slowsink_producer"; do
    if $BIN 1 1 "$v" 2>/dev/null | grep -q "^${v},"; then
        SLOW_PRODUCER_VARIANTS+=("$v")
    fi
done
for v in "spdlog_slowsink_total"; do
    if $BIN 1 1 "$v" 2>/dev/null | grep -q "^${v},"; then
        SLOW_TOTAL_VARIANTS+=("$v")
    fi
done

echo "Active null-sink variants: ${NULL_VARIANTS[*]}"
echo "Active file-sink variants: ${FILE_VARIANTS[*]}"
echo "Active slow-sink producer variants: ${SLOW_PRODUCER_VARIANTS[*]}"
echo "Active slow-sink total variants:    ${SLOW_TOTAL_VARIANTS[*]}"
echo ""

echo "=== null-sink throughput sweep (queue/lock overhead only) ==="
echo "variant,threads,msgs_per_thread,total_msgs,throughput_mps"
for t in "${THREADS[@]}"; do
    for v in "${NULL_VARIANTS[@]}"; do
        $BIN "$t" "$MSGS" "$v" | tail -n +2
    done
done

echo ""
echo "=== file-sink throughput sweep (tmpfs/page-cache writes) ==="
echo "variant,threads,msgs_per_thread,total_msgs,throughput_mps"
for t in "${THREADS[@]}"; do
    for v in "${FILE_VARIANTS[@]}"; do
        $BIN "$t" "$MSGS" "$v" | tail -n +2
    done
done

echo ""
echo "=== simulated-slow-sink: producer throughput (${IO_US:-20} microsecs/write, time until app threads are free) ==="
echo "=== naive: producers blocked for full I/O   conc/spdlog: producers return after sub-microsecs enqueue ==="
echo "variant,threads,msgs_per_thread,total_msgs,throughput_mps"
for t in "${THREADS[@]}"; do
    for v in "${SLOW_PRODUCER_VARIANTS[@]}"; do
        $BIN "$t" "$SLOW_MSGS" "$v" | tail -n +2
    done
done

echo ""
echo "=== simulated-slow-sink: total throughput (includes background drain / flush) ==="
echo "=== all variants pay the same I/O cost; difference shows in producer latency above ==="
echo "variant,threads,msgs_per_thread,total_msgs,throughput_mps"
for t in "${THREADS[@]}"; do
    for v in "${SLOW_TOTAL_VARIANTS[@]}"; do
        $BIN "$t" "$SLOW_MSGS" "$v" | tail -n +2
    done
done

# perf is kernel-version specific; try a few known paths before giving up
PERF=""
for p in perf \
          /usr/lib/linux-tools/6.8.0-107-generic/perf \
          /usr/lib/linux-tools/6.8.0-45-generic/perf; do
    if "$p" --version &>/dev/null 2>&1; then
        PERF="$p"
        break
    fi
done

ALL_VARIANTS=("${NULL_VARIANTS[@]}" "${FILE_VARIANTS[@]}"
              "${SLOW_PRODUCER_VARIANTS[@]}" "${SLOW_TOTAL_VARIANTS[@]}")

if [ -n "$PERF" ]; then
    echo ""
    echo "=== perf counters ($PERF_THREADS threads, per variant) ==="
    EVENTS="cache-misses,cache-references,stalled-cycles-frontend,stalled-cycles-backend,instructions,cycles"
    for variant in "${ALL_VARIANTS[@]}"; do
        echo ""
        echo "-- $variant --"
        "$PERF" stat -e "$EVENTS" \
            $BIN "$PERF_THREADS" "$MSGS" "$variant" 2>&1 \
            | grep -E "cache|stall|instruction|cycle|seconds|elapsed"
    done
else
    echo ""
    echo "perf not available -- skipping hardware counters"
fi
