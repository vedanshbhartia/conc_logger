#!/bin/bash
# Sweeps over thread counts and optionally grabs perf counters.
# Run from project root: bash benchmarks/run_bench.sh

set -e

BIN=./bench_throughput
MSGS=50000
SLOW_MSGS=2000    # slow-sink variants: each msg costs IO_US microsecs, so keep count low
THREADS=(1 2 4 8 16)
PERF_THREADS=4

if [ ! -f "$BIN" ]; then
    echo "build first: make bench SPDLOG=1"
    exit 1
fi

# Detect which variants are compiled in by doing a dry run with 1 msg
NULL_VARIANTS=("conc_logger" "naive")

if $BIN 1 1 "$v" 2>/dev/null | grep -q "^${v},"; then
        NULL_VARIANTS+=("$v")
fi

# File-sink variants are always compiled in (no optional flag needed)
FILE_VARIANTS=("conc_file" "naive_file")

echo "Active null-sink variants: ${NULL_VARIANTS[*]}"
echo "Active file-sink variants: ${FILE_VARIANTS[*]}"
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
echo "=== naive: producers blocked for full I/O   conc: producers return after sub-microsecs enqueue ==="
echo "variant,threads,msgs_per_thread,total_msgs,throughput_mps"
for t in "${THREADS[@]}"; do
    $BIN "$t" "$SLOW_MSGS" naive_slowsink_producer | tail -n +2
    $BIN "$t" "$SLOW_MSGS" conc_slowsink_producer  | tail -n +2
done

echo ""
echo "=== simulated-slow-sink: total throughput (includes background drain / flush) ==="
echo "=== both pay same I/O cost; difference shows in producer latency above ==="
echo "variant,threads,msgs_per_thread,total_msgs,throughput_mps"
for t in "${THREADS[@]}"; do
    $BIN "$t" "$SLOW_MSGS" naive_slowsink_total | tail -n +2
    $BIN "$t" "$SLOW_MSGS" conc_slowsink_total  | tail -n +2
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
              "naive_slowsink_producer" "conc_slowsink_producer"
              "naive_slowsink_total"    "conc_slowsink_total")

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
