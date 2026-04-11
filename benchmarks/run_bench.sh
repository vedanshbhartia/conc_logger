#!/bin/bash
# Sweeps over thread counts and optionally grabs perf counters.
# Run from project root: bash benchmarks/run_bench.sh

set -e

BIN=./bench_throughput
MSGS=50000
THREADS=(1 2 4 8 16)
PERF_THREADS=4

if [ ! -f "$BIN" ]; then
    echo "build first: make bench SPDLOG=1"
    exit 1
fi

echo "=== throughput sweep ==="
echo "variant,threads,msgs_per_thread,total_msgs,throughput_mps"
for t in "${THREADS[@]}"; do
    $BIN "$t" "$MSGS" | tail -n +2
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

if [ -n "$PERF" ]; then
    echo ""
    echo "=== perf counters ($PERF_THREADS threads, per variant) ==="
    EVENTS="cache-misses,cache-references,stalled-cycles-frontend,stalled-cycles-backend,instructions,cycles"
    for variant in conc_logger naive spdlog; do
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
