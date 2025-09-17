#pragma once

#include <cstdint>

namespace ulmd {

/**
 * @brief Run risk worker for one processing cycle
 * @param ring_name SPSC ring name to consume from
 * @param csv_path Output CSV file path for latency logging
 * @param tsc_hz TSC frequency in Hz for cycle conversion
 * @return Number of messages processed in this cycle
 * @invariant Single consumer only on specified ring
 * @invariant CSV output matches test harness header exactly
 * @invariant Captures dequeue timestamp and computes latencies
 * @performance O(n) where n is messages available in ring
 */
uint64_t run_once(const char* ring_name, const char* csv_path, double tsc_hz);

} // namespace ulmd