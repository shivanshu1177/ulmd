#include <ulmd/health.hpp>
#include <cstdio>
#include <chrono>
#include <cinttypes>

namespace ulmd {

void health_init(HealthMetrics* metrics) {
    if (!metrics) return;
    
    auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    
    metrics->messages_processed.store(0);
    metrics->errors_count.store(0);
    metrics->last_activity_ts.store(now);
    metrics->status.store(HealthStatus::HEALTHY);
}

void health_update(HealthMetrics* metrics, uint64_t current_ts) {
    if (!metrics) return;
    
    uint64_t last_activity = metrics->last_activity_ts.load();
    uint64_t errors = metrics->errors_count.load();
    uint64_t messages = metrics->messages_processed.load();
    
    // Check for stale activity (no activity for 30 seconds)
    if (current_ts > last_activity && (current_ts - last_activity) > 30) {
        metrics->status.store(HealthStatus::UNHEALTHY);
        return;
    }
    
    // Check error rate (>10% errors)
    if (messages > 0 && (errors * 100.0 / messages) > 10.0) {
        metrics->status.store(HealthStatus::DEGRADED);
        return;
    }
    
    // Check for recent activity (activity within 5 seconds)
    if (current_ts - last_activity <= 5) {
        metrics->status.store(HealthStatus::HEALTHY);
    }
}

bool health_write_status(const HealthMetrics* metrics, const char* path) {
    if (!metrics || !path) return false;
    
    // Validate path safety - only allow /tmp/ paths and prevent traversal
    if (strncmp(path, "/tmp/", 5) != 0 || strstr(path, "..") || strstr(path, "//")) {
        return false;
    }
    
    // Ensure path doesn't exceed reasonable length
    if (strlen(path) > 256) return false;
    
    FILE* f = fopen(path, "w");
    if (!f) return false;
    
    const char* status_str;
    switch (metrics->status.load()) {
        case HealthStatus::HEALTHY: status_str = "HEALTHY"; break;
        case HealthStatus::DEGRADED: status_str = "DEGRADED"; break;
        case HealthStatus::UNHEALTHY: status_str = "UNHEALTHY"; break;
        default: status_str = "UNKNOWN"; break;
    }
    
    // Add timestamp for better monitoring
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    
    if (fprintf(f, "timestamp=%" PRId64 "\n", static_cast<int64_t>(time_t)) < 0 ||
        fprintf(f, "status=%s\n", status_str) < 0 ||
        fprintf(f, "messages_processed=%" PRIu64 "\n", metrics->messages_processed.load()) < 0 ||
        fprintf(f, "errors_count=%" PRIu64 "\n", metrics->errors_count.load()) < 0 ||
        fprintf(f, "last_activity_ts=%" PRIu64 "\n", metrics->last_activity_ts.load()) < 0) {
        fclose(f);
        return false;
    }
    
    // Calculate and write error rate
    uint64_t messages = metrics->messages_processed.load();
    uint64_t errors = metrics->errors_count.load();
    double error_rate = (messages > 0) ? (100.0 * errors / messages) : 0.0;
    
    if (fprintf(f, "error_rate_percent=%.2f\n", error_rate) < 0) {
        fclose(f);
        return false;
    }
    
    if (fclose(f) != 0) return false;
    return true;
}

} // namespace ulmd