#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cinttypes>
#include <vector>
#include <string>
#include <chrono>
#include <thread>
#include <ulmd/config.hpp>

#ifdef ULMD_LINUX
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <time.h>
#elif ULMD_MACOS
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <mach/mach_time.h>
#endif

struct UlmdFrame {
    uint32_t magic;
    uint8_t version;
    uint8_t msg_type;
    uint16_t flags;
    uint64_t seq_no;
    uint64_t send_ts_ns;
    char symbol[8];
    int64_t bid_px;
    uint32_t bid_sz;
    int64_t ask_px;
    uint32_t ask_sz;
    uint32_t reserved;
    uint32_t crc32;
} __attribute__((packed));

static_assert(sizeof(UlmdFrame) == 64, "UlmdFrame must be 64 bytes");

static void write_be32(void* ptr, uint32_t val) {
    if (!ptr) return;
    *static_cast<uint32_t*>(ptr) = htonl(val);
}

static void write_be64(void* ptr, uint64_t val) {
    if (!ptr) return;
    uint32_t* p = static_cast<uint32_t*>(ptr);
    p[0] = htonl(static_cast<uint32_t>(val >> 32));
    p[1] = htonl(static_cast<uint32_t>(val & 0xFFFFFFFF));
}

static void write_be16(void* ptr, uint16_t val) {
    if (!ptr) return;
    *static_cast<uint16_t*>(ptr) = htons(val);
}

static uint32_t crc32_ieee(const uint8_t* data, size_t len) {
    if (!data) return 0;
    
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

static uint64_t get_timestamp_ns() {
#ifdef ULMD_LINUX
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
        return 0;
    }
    return static_cast<uint64_t>(ts.tv_sec) * 1000000000ULL + ts.tv_nsec;
#elif ULMD_MACOS
    static mach_timebase_info_data_t timebase = {0, 0};
    if (timebase.denom == 0) {
        if (mach_timebase_info(&timebase) != KERN_SUCCESS) {
            return 0;
        }
    }
    uint64_t mach_time = mach_absolute_time();
    // Prevent overflow by checking if multiplication would overflow
    if (timebase.numer == 0 || timebase.denom == 0) return 0;
    if (mach_time > UINT64_MAX / timebase.numer) {
        // Use safer division order to prevent overflow
        return (mach_time / timebase.denom) * timebase.numer;
    }
    return mach_time * timebase.numer / timebase.denom;
#endif
}

static void usage() {
    printf("Usage: ulmdsim [--config <file>] [--port <port>] [--qps <qps>] [--symbols <csv>] [--burst <n>]\n");
    printf("  --config <file>   Configuration file path\n");
    printf("  --port <port>     UDP port to send to\n");
    printf("  --qps <qps>       Messages per second\n");
    printf("  --symbols <csv>   Comma-separated symbol list\n");
    printf("  --burst <n>       Send n messages then exit (0=run for 5s)\n");
}

int main(int argc, char* argv[]) {
    ulmd::Config config;
    int qps = 0;
    std::string symbols_str;
    int burst = 0;
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
            if (!ulmd::load_config(argv[++i], config)) {
                fprintf(stderr, "Failed to load config file\n");
                return 1;
            }
        } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            config.port = static_cast<uint16_t>(atoi(argv[++i]));
        } else if (strcmp(argv[i], "--qps") == 0 && i + 1 < argc) {
            qps = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--symbols") == 0 && i + 1 < argc) {
            symbols_str = argv[++i];
        } else if (strcmp(argv[i], "--burst") == 0 && i + 1 < argc) {
            burst = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--help") == 0) {
            usage();
            return 0;
        }
    }
    
    if (config.port == 0 || qps <= 0 || symbols_str.empty()) {
        usage();
        return 1;
    }
    
    std::vector<std::string> symbols;
    size_t pos = 0;
    while (pos < symbols_str.length()) {
        size_t comma = symbols_str.find(',', pos);
        if (comma == std::string::npos) comma = symbols_str.length();
        symbols.push_back(symbols_str.substr(pos, comma - pos));
        pos = comma + 1;
    }
    
    if (symbols.empty()) {
        fprintf(stderr, "No symbols provided\n");
        return 1;
    }
    
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("socket");
        return 1;
    }
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(config.port);
    if (inet_pton(AF_INET, config.bind_address.c_str(), &addr.sin_addr) <= 0) {
        close(sock);
        return 1;
    }
    
    uint8_t frame_buf[64];
    uint64_t seq_no = 1;
    size_t symbol_idx = 0;
    
    auto start_time = std::chrono::steady_clock::now();
    auto next_send = start_time;
    if (qps <= 0 || qps > 1000000) {
        fprintf(stderr, "Error: QPS must be positive and <= 1000000 (got %d)\n", qps);
        close(sock);
        return 1;
    }
    auto interval = std::chrono::nanoseconds(1000000000LL / qps);
    
    int messages_sent = 0;
    bool run_for_time = (burst == 0);
    int target_messages = run_for_time ? INT32_MAX : burst;
    
    while (messages_sent < target_messages) {
        if (run_for_time) {
            auto elapsed = std::chrono::steady_clock::now() - start_time;
            if (elapsed >= std::chrono::seconds(5)) break;
        }
        
        write_be32(frame_buf + 0, 0x554C4D44); // "ULMD"
        frame_buf[4] = 1; // version
        frame_buf[5] = 1; // msg_type
        write_be16(frame_buf + 6, 0); // flags
        write_be64(frame_buf + 8, seq_no);
        write_be64(frame_buf + 16, get_timestamp_ns());
        
        const std::string& symbol = symbols[symbol_idx];
        memset(frame_buf + 24, 0, 8);
        memcpy(frame_buf + 24, symbol.c_str(), std::min(symbol.length(), size_t(8)));
        
        write_be64(frame_buf + 32, 1000000000LL); // bid_px (1.0 in nanodollars)
        write_be32(frame_buf + 40, 100); // bid_sz
        write_be64(frame_buf + 44, 1001000000LL); // ask_px (1.001 in nanodollars)
        write_be32(frame_buf + 52, 100); // ask_sz
        write_be32(frame_buf + 56, 0); // reserved
        
        uint32_t crc = crc32_ieee(frame_buf, 60);
        write_be32(frame_buf + 60, crc);
        
        ssize_t sent = sendto(sock, frame_buf, 64, 0, 
                             reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
        if (sent != 64) {
            perror("sendto");
            close(sock);
            return 1;
        }
        
        seq_no++;
        symbol_idx = (symbol_idx + 1) % symbols.size();
        messages_sent++;
        
        if (burst > 0) {
            // Burst mode - send as fast as possible
            continue;
        }
        
        // Sustained mode - rate limit
        next_send += interval;
        auto now = std::chrono::steady_clock::now();
        if (next_send > now) {
            std::this_thread::sleep_until(next_send);
        }
    }
    
    close(sock);
    return 0;
}