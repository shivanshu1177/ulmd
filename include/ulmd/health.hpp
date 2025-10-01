#pragma once

#include <cstdint>
#include <atomic>

namespace ulmd {

enum class HealthStatus {
    HEALTHY = 0,
    DEGRADED = 1,
    UNHEALTHY = 2
};

struct HealthMetrics {
    std::atomic<uint64_t> messages_processed{0};
    std::atomic<uint64_t> errors_count{0};
    std::atomic<uint64_t> last_activity_ts{0};
    std::atomic<HealthStatus> status{HealthStatus::HEALTHY};
};

/**
 * @brief Initialize health monitoring
 * @param metrics Health metrics structure
 */
void health_init(HealthMetrics* metrics);

/**
 * @brief Update health status based on metrics
 * @param metrics Health metrics structure
 * @param current_ts Current timestamp
 */
void health_update(HealthMetrics* metrics, uint64_t current_ts);

/**
 * @brief Write health status to file
 * @param metrics Health metrics structure
 * @param path Health status file path
 * @return true on success, false on failure
 */
bool health_write_status(const HealthMetrics* metrics, const char* path);

} // namespace ulmd