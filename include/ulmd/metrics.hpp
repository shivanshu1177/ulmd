#pragma once

#include <cstdint>
#include <atomic>

namespace ulmd {

struct LatencyHistogram {
    std::atomic<uint64_t> buckets[10];  // 0-1us, 1-10us, 10-100us, 100us-1ms, 1-10ms, 10-100ms, 100ms-1s, 1-10s, 10s+, invalid
    std::atomic<uint64_t> total_count{0};
    std::atomic<uint64_t> total_latency_ns{0};
};

struct ThroughputMetrics {
    std::atomic<uint64_t> messages_per_sec{0};
    std::atomic<uint64_t> bytes_per_sec{0};
    std::atomic<uint64_t> last_update_ts{0};
    std::atomic<uint64_t> message_count{0};
    std::atomic<uint64_t> byte_count{0};
};

struct PerformanceMetrics {
    LatencyHistogram parse_latency;
    LatencyHistogram e2e_latency;
    ThroughputMetrics throughput;
};

/**
 * @brief Initialize performance metrics
 * @param metrics Performance metrics structure
 */
void metrics_init(PerformanceMetrics* metrics);

/**
 * @brief Record latency sample
 * @param hist Latency histogram
 * @param latency_ns Latency in nanoseconds
 */
void metrics_record_latency(LatencyHistogram* hist, uint64_t latency_ns);

/**
 * @brief Update throughput metrics
 * @param metrics Throughput metrics
 * @param messages Message count
 * @param bytes Byte count
 * @param current_ts Current timestamp in seconds
 */
void metrics_update_throughput(ThroughputMetrics* metrics, uint64_t messages, uint64_t bytes, uint64_t current_ts);

/**
 * @brief Write metrics to file
 * @param metrics Performance metrics
 * @param path Output file path
 * @return true on success, false on failure
 */
bool metrics_write(const PerformanceMetrics* metrics, const char* path);

} // namespace ulmd