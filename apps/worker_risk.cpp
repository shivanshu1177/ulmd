#include <ulmd/ring.hpp>
#include <ulmd/telemetry.hpp>
#include <ulmd/metrics.hpp>
#include <ulmd/shutdown.hpp>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <signal.h>
#include <chrono>
#include <thread>
#include <cctype>
#include <atomic>
#include <cinttypes>

#ifdef ULMD_LINUX
#include <x86intrin.h>
#include <sched.h>
#elif ULMD_MACOS
#include <mach/mach_time.h>
#include <sys/sysctl.h>
#endif

struct ParsedMessage {
    uint64_t seq_no;
    uint64_t send_ts_ns;
    uint64_t enq_tsc;
    char symbol[8];
    int64_t bid_px;
    uint32_t bid_sz;
    int64_t ask_px;
    uint32_t ask_sz;
    uint16_t flags;
    uint64_t recv_tsc; // Store recv_tsc directly
} __attribute__((packed));

static std::atomic<bool> running{true};

static void signal_handler(int sig) {
    (void)sig;
    running.store(false, std::memory_order_relaxed);
}

static uint64_t get_timestamp() {
#ifdef ULMD_LINUX
    return __rdtsc();
#elif ULMD_MACOS
    return mach_absolute_time();
#else
    return 0;
#endif
}

static int get_cpu_core() {
#ifdef ULMD_LINUX
    return sched_getcpu();
#elif ULMD_MACOS
    return 0; // Simplified for macOS
#else
    return 0;
#endif
}

static double timestamp_to_ns(uint64_t timestamp) {
#ifdef ULMD_MACOS
    static mach_timebase_info_data_t timebase = {0, 0};
    if (timebase.denom == 0) {
        mach_timebase_info(&timebase);
    }
    return static_cast<double>(timestamp) * timebase.numer / timebase.denom;
#else
    // For Linux, assume TSC frequency
    return static_cast<double>(timestamp) * 1e9 / 2400000000.0;
#endif
}

static void sanitize_string(char* str, size_t max_len) {
    if (!str || max_len == 0) return;
    
    // Ensure string is null-terminated within bounds
    str[max_len - 1] = '\0';
    
    for (size_t i = 0; i < max_len - 1 && str[i] != '\0'; i++) {
        if (!std::isalnum(str[i]) && str[i] != '_' && str[i] != '-' && str[i] != '.' && str[i] != '/') {
            str[i] = '_';
        }
    }
}

static void usage() {
    printf("Usage: worker_risk --ring <name> --csv <path> --tsc-hz <hz> [--flush-ms <ms>]\n");
    printf("  --ring <name>     SPSC ring name\n");
    printf("  --csv <path>      CSV output file path\n");
    printf("  --tsc-hz <hz>     TSC frequency in Hz\n");
    printf("  --flush-ms <ms>   Flush interval in milliseconds\n");
}

int main(int argc, char* argv[]) {
    const char* ring_name = nullptr;
    const char* csv_path = nullptr;
    double tsc_hz = 0.0;
    uint32_t flush_ms = 1000;
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--ring") == 0 && i + 1 < argc) {
            ring_name = argv[++i];
        } else if (strcmp(argv[i], "--csv") == 0 && i + 1 < argc) {
            csv_path = argv[++i];
        } else if (strcmp(argv[i], "--tsc-hz") == 0 && i + 1 < argc) {
            tsc_hz = atof(argv[++i]);
        } else if (strcmp(argv[i], "--flush-ms") == 0 && i + 1 < argc) {
            flush_ms = static_cast<uint32_t>(atoi(argv[++i]));
        } else if (strcmp(argv[i], "--help") == 0) {
            usage();
            return 0;
        }
    }
    
    if (!ring_name || !csv_path || tsc_hz <= 0.0) {
        usage();
        return 1;
    }
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    ulmd::Spsc* ring = ulmd::spsc_attach_shared(ring_name);
    if (!ring) {
        char safe_ring_name[256];
        if (ring_name) {
            size_t len = strnlen(ring_name, sizeof(safe_ring_name) - 1);
            if (len < sizeof(safe_ring_name)) {
                memcpy(safe_ring_name, ring_name, len);
                safe_ring_name[len] = '\0';
                sanitize_string(safe_ring_name, sizeof(safe_ring_name));
            } else {
                strncpy(safe_ring_name, "<invalid>", sizeof(safe_ring_name) - 1);
                safe_ring_name[sizeof(safe_ring_name) - 1] = '\0';
            }
        } else {
            strncpy(safe_ring_name, "<null>", sizeof(safe_ring_name) - 1);
            safe_ring_name[sizeof(safe_ring_name) - 1] = '\0';
        }
        fprintf(stderr, "Failed to attach to ring buffer\n");
        ulmd::log_error("worker_risk", "Failed to attach to ring buffer");
        return 1;
    }
    
    FILE* csv_file = fopen(csv_path, "w");
    if (!csv_file) {
        perror("fopen");
        ulmd::log_error("worker_risk", "Failed to open CSV file");
        ulmd::spsc_destroy(ring);
        return 1;
    }
    
    fprintf(csv_file, "seq_no,symbol,recv_tsc,enq_tsc,deq_tsc,recv_ns,egress_ns,lat_ingress_to_worker_ns,lat_parse_to_worker_ns,ingress_core,worker_core,ring_slots,ring_occupancy_max,dropped,crc_ok\n");
    
    char safe_ring_name[256], safe_csv_path[512];
    if (ring_name) {
        size_t len = strnlen(ring_name, sizeof(safe_ring_name) - 1);
        if (len < sizeof(safe_ring_name)) {
            memcpy(safe_ring_name, ring_name, len);
            safe_ring_name[len] = '\0';
        } else {
            strncpy(safe_ring_name, "<invalid>", sizeof(safe_ring_name) - 1);
            safe_ring_name[sizeof(safe_ring_name) - 1] = '\0';
        }
    } else {
        strncpy(safe_ring_name, "<null>", sizeof(safe_ring_name) - 1);
        safe_ring_name[sizeof(safe_ring_name) - 1] = '\0';
    }
    if (csv_path) {
        size_t len = strnlen(csv_path, sizeof(safe_csv_path) - 1);
        if (len < sizeof(safe_csv_path)) {
            memcpy(safe_csv_path, csv_path, len);
            safe_csv_path[len] = '\0';
        } else {
            strncpy(safe_csv_path, "<invalid>", sizeof(safe_csv_path) - 1);
            safe_csv_path[sizeof(safe_csv_path) - 1] = '\0';
        }
    } else {
        strncpy(safe_csv_path, "<null>", sizeof(safe_csv_path) - 1);
        safe_csv_path[sizeof(safe_csv_path) - 1] = '\0';
    }
    sanitize_string(safe_ring_name, sizeof(safe_ring_name));
    sanitize_string(safe_csv_path, sizeof(safe_csv_path));
    printf("worker_risk: ring attached, csv configured, tsc_hz=%.0f\n", tsc_hz);
    fflush(stdout);
    
    ParsedMessage msg;
    uint32_t ring_slots = ulmd::spsc_size(ring);
    uint32_t ring_occupancy_max = 0;
    uint64_t dropped = 0;
    int worker_core = get_cpu_core();
    
    ulmd::PerformanceMetrics perf_metrics;
    ulmd::metrics_init(&perf_metrics);
    
    uint64_t total_messages = 0;
    uint64_t total_bytes = 0;
    
    // Register shutdown handlers
    ulmd::register_shutdown_handler([&]() {
        if (csv_file) {
            fflush(csv_file);
            fclose(csv_file);
        }
        ulmd::metrics_write(&perf_metrics, "/tmp/worker_risk_metrics.txt");
    });
    
    auto last_flush = std::chrono::steady_clock::now();
    auto last_metrics = std::chrono::steady_clock::now();
    
    while (running.load(std::memory_order_relaxed) && !ulmd::shutdown_requested()) {
        uint64_t deq_tsc = get_timestamp();
        
        if (ulmd::spsc_try_pop(ring, &msg)) {
            uint32_t current_occupancy = ulmd::spsc_occupancy(ring);
            if (current_occupancy > ring_occupancy_max) {
                ring_occupancy_max = current_occupancy;
            }
            
            // Calculate latencies using consistent timestamp conversion
            double recv_ns = timestamp_to_ns(msg.recv_tsc);
            double egress_ns = timestamp_to_ns(deq_tsc);
            double lat_ingress_to_worker_ns = timestamp_to_ns(deq_tsc - msg.recv_tsc);
            double lat_parse_to_worker_ns = timestamp_to_ns(deq_tsc - msg.enq_tsc);
            
            // Record performance metrics
            ulmd::metrics_record_latency(&perf_metrics.e2e_latency, static_cast<uint64_t>(lat_parse_to_worker_ns));
            
            total_messages++;
            total_bytes += sizeof(ParsedMessage);
            
            char symbol_str[9];
            // Safely copy symbol with strict bounds checking
            size_t copy_len = 0;
            const size_t max_copy = sizeof(symbol_str) - 1;
            const size_t symbol_max = sizeof(msg.symbol) < 8 ? sizeof(msg.symbol) : 7;
            
            for (size_t i = 0; i < symbol_max && copy_len < max_copy; i++) {
                char c = msg.symbol[i];
                if (c == '\0') break;
                // Only allow alphanumeric and safe characters
                if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || 
                    (c >= '0' && c <= '9') || c == '_' || c == '-') {
                    symbol_str[copy_len++] = c;
                } else {
                    symbol_str[copy_len++] = '_'; // Replace unsafe chars
                }
            }
            symbol_str[copy_len] = '\0';
            // Additional sanitization
            sanitize_string(symbol_str, sizeof(symbol_str));
            
            // Use safe format string with bounds checking
            int result = fprintf(csv_file, "%" PRIu64 ",%.7s,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%.0f,%.0f,%.0f,%.0f,0,%d,%" PRIu32 ",%" PRIu32 ",%" PRIu64 ",1\n",
                    msg.seq_no, symbol_str, msg.recv_tsc, msg.enq_tsc, deq_tsc,
                    recv_ns, egress_ns, lat_ingress_to_worker_ns, lat_parse_to_worker_ns,
                    worker_core, ring_slots, ring_occupancy_max, dropped);
            if (result < 0) {
                ulmd::log_error("worker_risk", "Failed to write CSV record");
            }
        } else {
            // No messages available, sleep briefly to avoid busy waiting
            std::this_thread::sleep_for(std::chrono::microseconds(1));
        }
        
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_flush).count() >= flush_ms) {
            fflush(csv_file);
            last_flush = now;
        }
        
        // Update metrics every 5 seconds
        if (now - last_metrics >= std::chrono::seconds(5)) {
            auto now_ts = std::chrono::duration_cast<std::chrono::seconds>(
                now.time_since_epoch()).count();
            ulmd::metrics_update_throughput(&perf_metrics.throughput, total_messages, total_bytes, now_ts);
            ulmd::metrics_write(&perf_metrics, "/tmp/worker_risk_metrics.txt");
            last_metrics = now;
        }
    }
    
    // Execute shutdown handlers before exit
    ulmd::execute_shutdown_handlers();
    
    // Don't destroy shared memory ring - it's managed by ringctl
    return 0;
}