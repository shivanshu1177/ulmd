#include <ulmd/metrics.hpp>
#include <cstdio>
#include <cinttypes>

namespace ulmd {

void metrics_init(PerformanceMetrics* metrics) {
    if (!metrics) return;
    
    // Initialize histogram buckets
    for (int i = 0; i < 10; i++) {
        metrics->parse_latency.buckets[i].store(0);
        metrics->e2e_latency.buckets[i].store(0);
    }
    
    metrics->parse_latency.total_count.store(0);
    metrics->parse_latency.total_latency_ns.store(0);
    metrics->e2e_latency.total_count.store(0);
    metrics->e2e_latency.total_latency_ns.store(0);
    
    metrics->throughput.messages_per_sec.store(0);
    metrics->throughput.bytes_per_sec.store(0);
    metrics->throughput.last_update_ts.store(0);
    metrics->throughput.message_count.store(0);
    metrics->throughput.byte_count.store(0);
}

void metrics_record_latency(LatencyHistogram* hist, uint64_t latency_ns) {
    if (!hist) return;
    
    int bucket = 9; // invalid bucket by default
    
    if (latency_ns < 1000) bucket = 0;           // 0-1us
    else if (latency_ns < 10000) bucket = 1;     // 1-10us
    else if (latency_ns < 100000) bucket = 2;    // 10-100us
    else if (latency_ns < 1000000) bucket = 3;   // 100us-1ms
    else if (latency_ns < 10000000) bucket = 4;  // 1-10ms
    else if (latency_ns < 100000000) bucket = 5; // 10-100ms
    else if (latency_ns < 1000000000) bucket = 6; // 100ms-1s
    else if (latency_ns < 10000000000ULL) bucket = 7; // 1-10s
    else if (latency_ns < 100000000000ULL) bucket = 8; // 10s+
    
    hist->buckets[bucket].fetch_add(1);
    hist->total_count.fetch_add(1);
    hist->total_latency_ns.fetch_add(latency_ns);
}

void metrics_update_throughput(ThroughputMetrics* metrics, uint64_t messages, uint64_t bytes, uint64_t current_ts) {
    if (!metrics) return;
    
    uint64_t last_ts = metrics->last_update_ts.load();
    if (current_ts > last_ts) {
        uint64_t time_diff = current_ts - last_ts;
        if (time_diff > 0) {
            uint64_t prev_messages = metrics->message_count.load();
            uint64_t prev_bytes = metrics->byte_count.load();
            
            // Prevent underflow
            uint64_t msg_diff = (messages >= prev_messages) ? messages - prev_messages : 0;
            uint64_t byte_diff = (bytes >= prev_bytes) ? bytes - prev_bytes : 0;
            
            metrics->messages_per_sec.store(msg_diff / time_diff);
            metrics->bytes_per_sec.store(byte_diff / time_diff);
        }
        
        metrics->last_update_ts.store(current_ts);
        metrics->message_count.store(messages);
        metrics->byte_count.store(bytes);
    }
}

bool metrics_write(const PerformanceMetrics* metrics, const char* path) {
    if (!metrics || !path) return false;
    
    FILE* f = fopen(path, "w");
    if (!f) return false;
    
    // Write latency histograms
    if (fprintf(f, "parse_latency_histogram=") < 0) { fclose(f); return false; }
    for (int i = 0; i < 10; i++) {
        if (fprintf(f, "%" PRIu64, metrics->parse_latency.buckets[i].load()) < 0) { fclose(f); return false; }
        if (i < 9 && fprintf(f, ",") < 0) { fclose(f); return false; }
    }
    if (fprintf(f, "\n") < 0) { fclose(f); return false; }
    
    if (fprintf(f, "e2e_latency_histogram=") < 0) { fclose(f); return false; }
    for (int i = 0; i < 10; i++) {
        if (fprintf(f, "%" PRIu64, metrics->e2e_latency.buckets[i].load()) < 0) { fclose(f); return false; }
        if (i < 9 && fprintf(f, ",") < 0) { fclose(f); return false; }
    }
    if (fprintf(f, "\n") < 0) { fclose(f); return false; }
    
    // Write averages
    uint64_t parse_count = metrics->parse_latency.total_count.load();
    uint64_t e2e_count = metrics->e2e_latency.total_count.load();
    
    if (fprintf(f, "parse_latency_avg_ns=%" PRIu64 "\n", 
            parse_count > 0 ? metrics->parse_latency.total_latency_ns.load() / parse_count : 0) < 0) { fclose(f); return false; }
    if (fprintf(f, "e2e_latency_avg_ns=%" PRIu64 "\n", 
            e2e_count > 0 ? metrics->e2e_latency.total_latency_ns.load() / e2e_count : 0) < 0) { fclose(f); return false; }
    
    // Write throughput
    if (fprintf(f, "messages_per_sec=%" PRIu64 "\n", metrics->throughput.messages_per_sec.load()) < 0) { fclose(f); return false; }
    if (fprintf(f, "bytes_per_sec=%" PRIu64 "\n", metrics->throughput.bytes_per_sec.load()) < 0) { fclose(f); return false; }
    if (fprintf(f, "total_messages=%" PRIu64 "\n", metrics->throughput.message_count.load()) < 0) { fclose(f); return false; }
    if (fprintf(f, "total_bytes=%" PRIu64 "\n", metrics->throughput.byte_count.load()) < 0) { fclose(f); return false; }
    
    if (fclose(f) != 0) return false;
    return true;
}

} // namespace ulmd