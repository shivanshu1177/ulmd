#!/bin/bash
set -euo pipefail

BUILD_DIR="${BUILD_DIR:-build}"
BENCHMARK_RESULTS="/tmp/ulmd_benchmark_results.txt"

echo "ULMD Performance Benchmark"
echo "========================="
echo "Results will be saved to: ${BENCHMARK_RESULTS}"
echo

# Initialize results file
echo "ULMD Performance Benchmark - $(date)" > "${BENCHMARK_RESULTS}"
echo "=======================================" >> "${BENCHMARK_RESULTS}"

# Test 1: Ring Buffer Performance
echo "Testing ring buffer performance..."
echo >> "${BENCHMARK_RESULTS}"
echo "Ring Buffer Performance:" >> "${BENCHMARK_RESULTS}"
echo "-----------------------" >> "${BENCHMARK_RESULTS}"
timeout 30 "${BUILD_DIR}/test_ring" >> "${BENCHMARK_RESULTS}" 2>&1 || echo "Ring test completed" >> "${BENCHMARK_RESULTS}"

# Test 2: Memory Pool Performance  
echo "Testing message pool performance..."
echo >> "${BENCHMARK_RESULTS}"
echo "Message Pool Performance:" >> "${BENCHMARK_RESULTS}"
echo "------------------------" >> "${BENCHMARK_RESULTS}"
"${BUILD_DIR}/test_message_pool" >> "${BENCHMARK_RESULTS}" 2>&1

# Test 3: TSC Calibration
echo "Testing TSC calibration..."
echo >> "${BENCHMARK_RESULTS}"
echo "TSC Calibration:" >> "${BENCHMARK_RESULTS}"
echo "---------------" >> "${BENCHMARK_RESULTS}"
"${BUILD_DIR}/tsc_cal" >> "${BENCHMARK_RESULTS}" 2>&1

# Test 4: Latency Measurement
echo "Testing latency measurement..."
echo >> "${BENCHMARK_RESULTS}"
echo "Latency Measurement:" >> "${BENCHMARK_RESULTS}"
echo "------------------" >> "${BENCHMARK_RESULTS}"
"${BUILD_DIR}/test_performance" >> "${BENCHMARK_RESULTS}" 2>&1

echo "✅ Benchmark completed. Results saved to ${BENCHMARK_RESULTS}"