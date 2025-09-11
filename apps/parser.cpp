#include <ulmd/ring.hpp>
#include <ulmd/health.hpp>
#include <ulmd/telemetry.hpp>
#include <ulmd/metrics.hpp>
#include <ulmd/shutdown.hpp>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <signal.h>
#include <unistd.h>
#include <cctype>
#include <arpa/inet.h>
#include <atomic>
#include <chrono>

#ifdef ULMD_LINUX
#include <x86intrin.h>
#elif ULMD_MACOS
#include <mach/mach_time.h>
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

static_assert(sizeof(ParsedMessage) <= 128, "ParsedMessage must be <= 128 bytes");

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
    return std::chrono::high_resolution_clock::now().time_since_epoch().count();
#endif
}

static void sanitize_string(char* str, size_t max_len) {
    for (size_t i = 0; i < max_len && str[i] != '\0'; i++) {
        if (!std::isalnum(str[i]) && str[i] != '_' && str[i] != '-' && str[i] != '.') {
            str[i] = '_';
        }
    }
}

static uint32_t read_be32(const void* ptr) {
    uint32_t val;
    memcpy(&val, ptr, sizeof(val));
    return ntohl(val);
}

static uint64_t read_be64(const void* ptr) {
    const uint32_t* p = static_cast<const uint32_t*>(ptr);
    uint32_t high, low;
    memcpy(&high, p, sizeof(high));
    memcpy(&low, p + 1, sizeof(low));
    return (static_cast<uint64_t>(ntohl(high)) << 32) | ntohl(low);
}

static uint16_t read_be16(const void* ptr) {
    uint16_t val;
    memcpy(&val, ptr, sizeof(val));
    return ntohs(val);
}

static uint32_t crc32_ieee(const uint8_t* data, size_t len) {
    static const uint32_t table[256] = {
        0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA, 0x076DC419, 0x706AF48F, 0xE963A535, 0x9E6495A3,
        0x0EDB8832, 0x79DCB8A4, 0xE0D5E91E, 0x97D2D988, 0x09B64C2B, 0x7EB17CBD, 0xE7B82D07, 0x90BF1D91,
        0x1DB71064, 0x6AB020F2, 0xF3B97148, 0x84BE41DE, 0x1ADAD47D, 0x6DDDE4EB, 0xF4D4B551, 0x83D385C7,
        0x136C9856, 0x646BA8C0, 0xFD62F97A, 0x8A65C9EC, 0x14015C4F, 0x63066CD9, 0xFA0F3D63, 0x8D080DF5,
        0x3B6E20C8, 0x4C69105E, 0xD56041E4, 0xA2677172, 0x3C03E4D1, 0x4B04D447, 0xD20D85FD, 0xA50AB56B,
        0x35B5A8FA, 0x42B2986C, 0xDBBBC9D6, 0xACBCF940, 0x32D86CE3, 0x45DF5C75, 0xDCD60DCF, 0xABD13D59,
        0x26D930AC, 0x51DE003A, 0xC8D75180, 0xBFD06116, 0x21B4F4B5, 0x56B3C423, 0xCFBA9599, 0xB8BDA50F,
        0x2802B89E, 0x5F058808, 0xC60CD9B2, 0xB10BE924, 0x2F6F7C87, 0x58684C11, 0xC1611DAB, 0xB6662D3D,
        0x76DC4190, 0x01DB7106, 0x98D220BC, 0xEFD5102A, 0x71B18589, 0x06B6B51F, 0x9FBFE4A5, 0xE8B8D433,
        0x7807C9A2, 0x0F00F934, 0x9609A88E, 0xE10E9818, 0x7F6A0DBB, 0x086D3D2D, 0x91646C97, 0xE6635C01,
        0x6B6B51F4, 0x1C6C6162, 0x856530D8, 0xF262004E, 0x6C0695ED, 0x1B01A57B, 0x8208F4C1, 0xF50FC457,
        0x65B0D9C6, 0x12B7E950, 0x8BBEB8EA, 0xFCB9887C, 0x62DD1DDF, 0x15DA2D49, 0x8CD37CF3, 0xFBD44C65,
        0x4DB26158, 0x3AB551CE, 0xA3BC0074, 0xD4BB30E2, 0x4ADFA541, 0x3DD895D7, 0xA4D1C46D, 0xD3D6F4FB,
        0x4369E96A, 0x346ED9FC, 0xAD678846, 0xDA60B8D0, 0x44042D73, 0x33031DE5, 0xAA0A4C5F, 0xDD0D7CC9,
        0x5005713C, 0x270241AA, 0xBE0B1010, 0xC90C2086, 0x5768B525, 0x206F85B3, 0xB966D409, 0xCE61E49F,
        0x5EDEF90E, 0x29D9C998, 0xB0D09822, 0xC7D7A8B4, 0x59B33D17, 0x2EB40D81, 0xB7BD5C3B, 0xC0BA6CAD,
        0xEDB88320, 0x9ABFB3B6, 0x03B6E20C, 0x74B1D29A, 0xEAD54739, 0x9DD277AF, 0x04DB2615, 0x73DC1683,
        0xE3630B12, 0x94643B84, 0x0D6D6A3E, 0x7A6A5AA8, 0xE40ECF0B, 0x9309FF9D, 0x0A00AE27, 0x7D079EB1,
        0xF00F9344, 0x8708A3D2, 0x1E01F268, 0x6906C2FE, 0xF762575D, 0x806567CB, 0x196C3671, 0x6E6B06E7,
        0xFED41B76, 0x89D32BE0, 0x10DA7A5A, 0x67DD4ACC, 0xF9B9DF6F, 0x8EBEEFF9, 0x17B7BE43, 0x60B08ED5,
        0xD6D6A3E8, 0xA1D1937E, 0x38D8C2C4, 0x4FDFF252, 0xD1BB67F1, 0xA6BC5767, 0x3FB506DD, 0x48B2364B,
        0xD80D2BDA, 0xAF0A1B4C, 0x36034AF6, 0x41047A60, 0xDF60EFC3, 0xA867DF55, 0x316E8EEF, 0x4669BE79,
        0xCB61B38C, 0xBC66831A, 0x256FD2A0, 0x5268E236, 0xCC0C7795, 0xBB0B4703, 0x220216B9, 0x5505262F,
        0xC5BA3BBE, 0xB2BD0B28, 0x2BB45A92, 0x5CB36A04, 0xC2D7FFA7, 0xB5D0CF31, 0x2CD99E8B, 0x5BDEAE1D,
        0x9B64C2B0, 0xEC63F226, 0x756AA39C, 0x026D930A, 0x9C0906A9, 0xEB0E363F, 0x72076785, 0x05005713,
        0x95BF4A82, 0xE2B87A14, 0x7BB12BAE, 0x0CB61B38, 0x92D28E9B, 0xE5D5BE0D, 0x7CDCEFB7, 0x0BDBDF21,
        0x86D3D2D4, 0xF1D4E242, 0x68DDB3F8, 0x1FDA836E, 0x81BE16CD, 0xF6B9265B, 0x6FB077E1, 0x18B74777,
        0x88085AE6, 0xFF0F6A70, 0x66063BCA, 0x11010B5C, 0x8F659EFF, 0xF862AE69, 0x616BFFD3, 0x166CCF45,
        0xA00AE278, 0xD70DD2EE, 0x4E048354, 0x3903B3C2, 0xA7672661, 0xD06016F7, 0x4969474D, 0x3E6E77DB,
        0xAED16A4A, 0xD9D65ADC, 0x40DF0B66, 0x37D83BF0, 0xA9BCAE53, 0xDEBB9EC5, 0x47B2CF7F, 0x30B5FFE9,
        0xBDBDF21C, 0xCABAC28A, 0x53B39330, 0x24B4A3A6, 0xBAD03605, 0xCDD70693, 0x54DE5729, 0x23D967BF,
        0xB3667A2E, 0xC4614AB8, 0x5D681B02, 0x2A6F2B94, 0xB40BBE37, 0xC30C8EA1, 0x5A05DF1B, 0x2D02EF8D
    };
    
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++) {
        crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFF;
}

static bool parse_ulmd(const uint8_t* data, size_t data_size, ParsedMessage& msg) {
    if (!data || data_size < 64) return false;
    
    uint32_t magic = read_be32(data + 0);
    if (magic != 0x554C4D44) return false; // "ULMD"
    
    uint8_t version = data[4];
    if (version != 1) return false;
    
    uint32_t expected_crc = read_be32(data + 60);
    uint32_t actual_crc = crc32_ieee(data, 60);
    if (expected_crc != actual_crc) return false;
    
    msg.seq_no = read_be64(data + 8);
    msg.send_ts_ns = read_be64(data + 16);
    memcpy(msg.symbol, data + 24, 7);
    msg.symbol[7] = '\0'; // Ensure null termination
    msg.bid_px = static_cast<int64_t>(read_be64(data + 32));
    msg.bid_sz = read_be32(data + 40);
    msg.ask_px = static_cast<int64_t>(read_be64(data + 44));
    msg.ask_sz = read_be32(data + 52);
    msg.flags = read_be16(data + 6);
    msg.recv_tsc = 0; // Initialize recv_tsc
    
    return true;
}

static void usage() {
    printf("Usage: parser --ring <name> --tsc-hz <hz>\n");
    printf("  --ring <name>   SPSC ring name\n");
    printf("  --tsc-hz <hz>   TSC frequency in Hz\n");
}

int main(int argc, char* argv[]) {
    const char* ring_name = nullptr;
    double tsc_hz = 0.0;
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--ring") == 0 && i + 1 < argc) {
            ring_name = argv[++i];
        } else if (strcmp(argv[i], "--tsc-hz") == 0 && i + 1 < argc) {
            tsc_hz = atof(argv[++i]);
        } else if (strcmp(argv[i], "--help") == 0) {
            usage();
            return 0;
        }
    }
    
    if (!ring_name || tsc_hz <= 0.0) {
        usage();
        return 1;
    }
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    ulmd::Spsc* ring = ulmd::spsc_attach_shared(ring_name);
    
    if (!ring) {
        char safe_ring_name[256];
        strncpy(safe_ring_name, ring_name, sizeof(safe_ring_name) - 1);
        safe_ring_name[sizeof(safe_ring_name) - 1] = '\0';
        sanitize_string(safe_ring_name, sizeof(safe_ring_name));
        fprintf(stderr, "Failed to attach to ring %.200s\n", safe_ring_name);
        return 1;
    }
    
    char safe_ring_name[256];
    strncpy(safe_ring_name, ring_name, sizeof(safe_ring_name) - 1);
    safe_ring_name[sizeof(safe_ring_name) - 1] = '\0';
    sanitize_string(safe_ring_name, sizeof(safe_ring_name));
    printf("parser: ring=%.200s attach=ok stdin=ULMD/64B ready\n", safe_ring_name);
    fflush(stdout);
    
    ulmd::HealthMetrics health;
    ulmd::health_init(&health);
    
    ulmd::PerformanceMetrics perf_metrics;
    ulmd::metrics_init(&perf_metrics);
    
    uint64_t total_messages = 0;
    uint64_t total_bytes = 0;
    
    // Register shutdown handlers after variables are declared
    ulmd::register_shutdown_handler([&]() {
        ulmd::metrics_write(&perf_metrics, "/tmp/parser_metrics.txt");
        ulmd::health_write_status(&health, "/tmp/parser_health.status");
    });
    
    uint8_t buffer[64];
    uint64_t recv_tsc;
    auto last_health_check = std::chrono::steady_clock::now();
    while (running.load(std::memory_order_relaxed) && !ulmd::shutdown_requested()) {
        uint64_t start_tsc = get_timestamp();
        
        // Read recv_tsc (8 bytes) then message (64 bytes)
        ssize_t tsc_bytes = read(STDIN_FILENO, &recv_tsc, sizeof(recv_tsc));
        if (tsc_bytes == 0) break; // EOF
        if (tsc_bytes != sizeof(recv_tsc)) continue;
        
        ssize_t bytes = read(STDIN_FILENO, buffer, 64);
        if (bytes == 0) break; // EOF
        if (bytes != 64) continue;
        
        ParsedMessage msg;
        uint64_t parse_start = get_timestamp();
        if (!parse_ulmd(buffer, 64, msg)) {
            health.errors_count.fetch_add(1);
            ulmd::log_error("parser", "Invalid ULMD frame format");
            continue;
        }
        uint64_t parse_end = get_timestamp();
        
        msg.enq_tsc = get_timestamp();
        // Store recv_tsc directly in the message
        msg.recv_tsc = recv_tsc;
        
        if (!ulmd::spsc_try_push(ring, &msg)) {
            // Ring full, drop message
            health.errors_count.fetch_add(1);
            ulmd::log_error("parser", "Ring buffer full - message dropped");
            continue;
        }
        
        // Record performance metrics
        uint64_t parse_latency = parse_end - parse_start;
        ulmd::metrics_record_latency(&perf_metrics.parse_latency, parse_latency);
        
        total_messages++;
        total_bytes += 64;
        
        health.messages_processed.fetch_add(1);
        auto now_ts = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        health.last_activity_ts.store(now_ts);
        
        // Update metrics every 5 seconds
        auto now = std::chrono::steady_clock::now();
        if (now - last_health_check >= std::chrono::seconds(5)) {
            ulmd::health_update(&health, now_ts);
            ulmd::health_write_status(&health, "/tmp/parser_health.status");
            
            ulmd::metrics_update_throughput(&perf_metrics.throughput, total_messages, total_bytes, now_ts);
            ulmd::metrics_write(&perf_metrics, "/tmp/parser_metrics.txt");
            
            last_health_check = now;
        }
    }
    
    // Execute shutdown handlers before exit
    ulmd::execute_shutdown_handlers();
    
    // Don't destroy shared memory ring - it's managed by ringctl
    return 0;
}