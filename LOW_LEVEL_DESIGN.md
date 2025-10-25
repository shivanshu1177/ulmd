# ULMD Low-Level Design Document

## 1. Executive Summary

This document provides detailed low-level design specifications for the ULMD (Ultra Low Latency Market Data) system based on comprehensive codebase analysis. The system implements a high-performance, lock-free message processing pipeline with sub-microsecond latency capabilities.

## 2. System Architecture Overview

### 2.1 Process Architecture
```
┌─────────────┐    ┌─────────────┐    ┌─────────────┐
│   ulmdsim   │    │   ingress   │    │   parser    │
│  (UDP Send) │───▶│ (UDP Recv)  │───▶│ (Validate)  │
└─────────────┘    └─────────────┘    └─────────────┘
                           │                   │
                           ▼                   ▼
                   ┌─────────────┐    ┌─────────────┐
                   │ Timestamp   │    │ Ring Buffer │
                   │ Capture     │    │ (SPSC)      │
                   └─────────────┘    └─────────────┘
                                              │
                                              ▼
                                      ┌─────────────┐
                                      │ worker_risk │
                                      │ (Process)   │
                                      └─────────────┘
                                              │
                                              ▼
                                      ┌─────────────┐
                                      │ CSV Output  │
                                      │ & Metrics   │
                                      └─────────────┘
```

### 2.2 Data Flow Pipeline
1. **ulmdsim** → UDP packets (64 bytes)
2. **ingress** → Timestamp capture + stdout pipe
3. **parser** → Protocol validation + ring buffer enqueue
4. **worker_risk** → Latency measurement + CSV output

## 3. Component Detailed Design

### 3.1 Ingress Service (`apps/ingress.cpp`)

#### 3.1.1 Core Functionality
- **Purpose**: UDP packet reception with precise timestamp capture
- **Threading**: Single-threaded event loop
- **I/O Model**: Non-blocking UDP sockets

#### 3.1.2 Key Data Structures
```cpp
// Configuration
struct Config {
    std::string bind_address = "127.0.0.1";
    uint16_t port = 12345;
    double tsc_hz = 3000000000.0;
    uint32_t sleep_us = 10;
};

// Health monitoring
struct HealthMetrics {
    std::atomic<uint64_t> messages_processed{0};
    std::atomic<uint64_t> errors_count{0};
    std::atomic<uint64_t> last_activity_ts{0};
    std::atomic<HealthStatus> status{HealthStatus::HEALTHY};
};
```

#### 3.1.3 Critical Algorithms

**Timestamp Capture:**
```cpp
static uint64_t get_timestamp() {
#ifdef ULMD_LINUX
    return __rdtsc();
#elif ULMD_MACOS
    return mach_absolute_time();
#else
    return std::chrono::high_resolution_clock::now().time_since_epoch().count();
#endif
}
```

**Main Processing Loop:**
```cpp
while (running && !shutdown_requested()) {
    uint64_t recv_tsc = get_timestamp();  // Capture immediately
    ssize_t bytes = recv(sock, buffer, 64, 0);
    
    if (bytes == 64) {
        // Write timestamp header + message to stdout
        fwrite(&recv_tsc, sizeof(recv_tsc), 1, stdout);
        fwrite(buffer, 1, 64, stdout);
        fflush(stdout);
    }
}
```

#### 3.1.4 Performance Optimizations
- **Non-blocking I/O**: Prevents blocking on empty queues
- **Immediate timestamping**: Minimizes timestamp drift
- **Zero-copy output**: Direct stdout pipe to parser
- **CPU sleep**: Prevents busy-waiting when no data available

### 3.2 Parser Service (`apps/parser.cpp`)

#### 3.2.1 Core Functionality
- **Purpose**: ULMD protocol parsing and validation
- **Input**: Timestamped messages from ingress via stdin
- **Output**: Validated messages to SPSC ring buffer

#### 3.2.2 Message Format Handling

**Input Format (from ingress):**
```
[8 bytes: recv_tsc][64 bytes: ULMD message]
```

**ULMD Protocol Structure:**
```cpp
struct UlmdFrame {
    uint32_t magic;        // 0x554C4D44 ("ULMD")
    uint8_t version;       // Protocol version (1)
    uint8_t msg_type;      // Message type
    uint16_t flags;        // Control flags
    uint64_t seq_no;       // Sequence number
    uint64_t send_ts_ns;   // Send timestamp
    char symbol[8];        // Trading symbol
    int64_t bid_px;        // Bid price (nanodollars)
    uint32_t bid_sz;       // Bid size
    int64_t ask_px;        // Ask price (nanodollars)
    uint32_t ask_sz;       // Ask size
    uint32_t reserved;     // Reserved field
    uint32_t crc32;        // CRC32 checksum
} __attribute__((packed));
```

**Internal Message Structure:**
```cpp
struct ParsedMessage {
    uint64_t seq_no;       // Message sequence
    uint64_t send_ts_ns;   // Original timestamp
    uint64_t enq_tsc;      // Parser timestamp
    char symbol[8];        // Trading symbol
    int64_t bid_px;        // Bid price
    uint32_t bid_sz;       // Bid size
    int64_t ask_px;        // Ask price
    uint32_t ask_sz;       // Ask size
    uint16_t flags;        // Message flags
    uint64_t recv_tsc;     // Ingress timestamp
} __attribute__((packed));
```

#### 3.2.3 Validation Algorithms

**CRC32 Validation:**
```cpp
static uint32_t crc32_ieee(const uint8_t* data, size_t len) {
    static const uint32_t table[256] = { /* IEEE CRC32 table */ };
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++) {
        crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFF;
}
```

**Protocol Validation:**
```cpp
static bool parse_ulmd(const uint8_t* data, size_t data_size, ParsedMessage& msg) {
    if (!data || data_size < 64) return false;
    
    // Magic number validation
    uint32_t magic = read_be32(data + 0);
    if (magic != 0x554C4D44) return false; // "ULMD"
    
    // Version validation
    uint8_t version = data[4];
    if (version != 1) return false;
    
    // CRC validation
    uint32_t expected_crc = read_be32(data + 60);
    uint32_t actual_crc = crc32_ieee(data, 60);
    if (expected_crc != actual_crc) return false;
    
    // Extract fields with endianness conversion
    msg.seq_no = read_be64(data + 8);
    msg.send_ts_ns = read_be64(data + 16);
    // ... additional field extraction
    
    return true;
}
```

#### 3.2.4 Ring Buffer Integration
- **Type**: Single Producer Single Consumer (SPSC)
- **Capacity**: Configurable (default 4096 slots)
- **Element Size**: 128 bytes (ParsedMessage)
- **Shared Memory**: Inter-process communication

### 3.3 Worker Risk Service (`apps/worker_risk.cpp`)

#### 3.3.1 Core Functionality
- **Purpose**: Latency measurement and risk processing
- **Input**: ParsedMessage from SPSC ring buffer
- **Output**: CSV records with precise latency metrics

#### 3.3.2 Latency Measurement System

**Timestamp Conversion (macOS):**
```cpp
static double timestamp_to_ns(uint64_t timestamp) {
#ifdef ULMD_MACOS
    static mach_timebase_info_data_t timebase = {0, 0};
    if (timebase.denom == 0) {
        mach_timebase_info(&timebase);
    }
    return static_cast<double>(timestamp) * timebase.numer / timebase.denom;
#else
    return static_cast<double>(timestamp) * 1e9 / 2400000000.0;
#endif
}
```

**Latency Calculations:**
```cpp
// End-to-end latency (ingress to worker)
double lat_ingress_to_worker_ns = timestamp_to_ns(deq_tsc - msg.recv_tsc);

// Parser to worker latency
double lat_parse_to_worker_ns = timestamp_to_ns(deq_tsc - msg.enq_tsc);
```

#### 3.3.3 CSV Output Format
```
seq_no,symbol,recv_tsc,enq_tsc,deq_tsc,recv_ns,egress_ns,lat_ingress_to_worker_ns,lat_parse_to_worker_ns,ingress_core,worker_core,ring_slots,ring_occupancy_max,dropped,crc_ok
```

### 3.4 Ring Buffer Implementation (`src/ring.cpp`)

#### 3.4.1 SPSC Ring Buffer Design

**Core Structure:**
```cpp
struct Spsc {
    std::atomic<uint32_t> head{0};           // Producer index
    char pad1[64 - sizeof(std::atomic<uint32_t>)]; // Cache line padding
    std::atomic<uint32_t> tail{0};           // Consumer index
    char pad2[64 - sizeof(std::atomic<uint32_t>)]; // Cache line padding
    uint32_t mask;                           // Size mask (size - 1)
    uint32_t elem_size;                      // Element size in bytes
    bool is_shared;                          // Shared memory flag
    char data[];                             // Ring buffer data
};
```

#### 3.4.2 Lock-Free Algorithms

**Push Operation:**
```cpp
bool spsc_try_push(Spsc* ring, const void* data) {
    if (!ring || !data || ring->elem_size == 0) return false;
    
    uint32_t head = ring->head.load(std::memory_order_relaxed);
    uint32_t tail = ring->tail.load(std::memory_order_acquire);
    
    // Check if ring is full
    if (((head + 1) & ring->mask) == (tail & ring->mask)) {
        return false;
    }
    
    // Bounds checking for security
    uint32_t index = head & ring->mask;
    if (index > ring->mask) return false;
    
    size_t offset = static_cast<size_t>(index) * ring->elem_size;
    size_t total_size = static_cast<size_t>(ring->mask + 1) * ring->elem_size;
    if (offset + ring->elem_size > total_size) return false;
    
    // Copy data and update head
    memcpy(ring->data + offset, data, ring->elem_size);
    ring->head.store(head + 1, std::memory_order_release);
    return true;
}
```

**Pop Operation:**
```cpp
bool spsc_try_pop(Spsc* ring, void* data) {
    if (!ring || !data || ring->elem_size == 0) return false;
    
    uint32_t tail = ring->tail.load(std::memory_order_relaxed);
    uint32_t head = ring->head.load(std::memory_order_acquire);
    
    // Check if ring is empty
    if (tail == head) return false;
    
    // Bounds checking for security
    uint32_t index = tail & ring->mask;
    if (index > ring->mask) return false;
    
    size_t offset = static_cast<size_t>(index) * ring->elem_size;
    size_t total_size = static_cast<size_t>(ring->mask + 1) * ring->elem_size;
    if (offset + ring->elem_size > total_size) return false;
    
    // Copy data and update tail
    memcpy(data, ring->data + offset, ring->elem_size);
    ring->tail.store(tail + 1, std::memory_order_release);
    return true;
}
```

#### 3.4.3 Shared Memory Management

**Creation:**
```cpp
Spsc* spsc_create_shared(const char* name, uint32_t slots, uint32_t elem_size) {
    if (!is_safe_shm_name(name) || (slots & (slots - 1)) != 0 || 
        slots == 0 || elem_size == 0) return nullptr;
    
    size_t size = sizeof(Spsc) + slots * elem_size;
    
    int fd = shm_open(name, O_CREAT | O_EXCL | O_RDWR, 0666);
    if (fd < 0) return nullptr;
    
    if (ftruncate(fd, size) < 0) {
        close(fd);
        shm_unlink(name);
        return nullptr;
    }
    
    Spsc* ring = (Spsc*)mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    
    if (ring == MAP_FAILED) {
        shm_unlink(name);
        return nullptr;
    }
    
    // Initialize ring buffer
    ring->head.store(0);
    ring->tail.store(0);
    ring->mask = slots - 1;
    ring->elem_size = elem_size;
    ring->is_shared = true;
    return ring;
}
```

### 3.5 Market Data Simulator (`apps/ulmdsim.cpp`)

#### 3.5.1 Core Functionality
- **Purpose**: Generate synthetic market data for testing
- **Protocol**: ULMD binary format
- **Transport**: UDP multicast/unicast

#### 3.5.2 Message Generation

**Frame Construction:**
```cpp
// ULMD frame assembly
write_be32(frame_buf + 0, 0x554C4D44);     // Magic "ULMD"
frame_buf[4] = 1;                          // Version
frame_buf[5] = 1;                          // Message type
write_be16(frame_buf + 6, 0);              // Flags
write_be64(frame_buf + 8, seq_no);         // Sequence number
write_be64(frame_buf + 16, get_timestamp_ns()); // Timestamp
memcpy(frame_buf + 24, symbol.c_str(), min(symbol.length(), 8)); // Symbol
write_be64(frame_buf + 32, 1000000000LL);  // Bid price
write_be32(frame_buf + 40, 100);           // Bid size
write_be64(frame_buf + 44, 1001000000LL);  // Ask price
write_be32(frame_buf + 52, 100);           // Ask size
write_be32(frame_buf + 56, 0);             // Reserved
uint32_t crc = crc32_ieee(frame_buf, 60);
write_be32(frame_buf + 60, crc);           // CRC32
```

#### 3.5.3 Rate Control
```cpp
// Precise rate limiting
auto interval = std::chrono::nanoseconds(1000000000LL / qps);
next_send += interval;
auto now = std::chrono::steady_clock::now();
if (next_send > now) {
    std::this_thread::sleep_until(next_send);
}
```

### 3.6 Health Monitoring System (`src/health.cpp`)

#### 3.6.1 Health Metrics Structure
```cpp
struct HealthMetrics {
    std::atomic<uint64_t> messages_processed{0};
    std::atomic<uint64_t> errors_count{0};
    std::atomic<uint64_t> last_activity_ts{0};
    std::atomic<HealthStatus> status{HealthStatus::HEALTHY};
};

enum class HealthStatus {
    HEALTHY = 0,
    DEGRADED = 1,
    UNHEALTHY = 2
};
```

#### 3.6.2 Health Assessment Algorithm
```cpp
void health_update(HealthMetrics* metrics, uint64_t current_ts) {
    uint64_t last_activity = metrics->last_activity_ts.load();
    uint64_t errors = metrics->errors_count.load();
    uint64_t messages = metrics->messages_processed.load();
    
    // Stale activity check (30 seconds)
    if (current_ts > last_activity && (current_ts - last_activity) > 30) {
        metrics->status.store(HealthStatus::UNHEALTHY);
        return;
    }
    
    // Error rate check (>10%)
    if (messages > 0 && (errors * 100.0 / messages) > 10.0) {
        metrics->status.store(HealthStatus::DEGRADED);
        return;
    }
    
    // Recent activity check (5 seconds)
    if (current_ts - last_activity <= 5) {
        metrics->status.store(HealthStatus::HEALTHY);
    }
}
```

### 3.7 Performance Metrics System (`src/metrics.cpp`)

#### 3.7.1 Latency Histogram
```cpp
struct LatencyHistogram {
    std::atomic<uint64_t> buckets[10];  // Latency buckets
    std::atomic<uint64_t> total_count{0};
    std::atomic<uint64_t> total_latency_ns{0};
};

// Bucket ranges: 0-1μs, 1-10μs, 10-100μs, 100μs-1ms, 1-10ms, 
//                10-100ms, 100ms-1s, 1-10s, 10s+, invalid
```

#### 3.7.2 Throughput Tracking
```cpp
struct ThroughputMetrics {
    std::atomic<uint64_t> messages_per_sec{0};
    std::atomic<uint64_t> bytes_per_sec{0};
    std::atomic<uint64_t> last_update_ts{0};
    std::atomic<uint64_t> message_count{0};
    std::atomic<uint64_t> byte_count{0};
};
```

## 4. Security Implementation

### 4.1 Input Validation

**String Sanitization:**
```cpp
void sanitize_for_log(const char* input, char* output, size_t output_size) {
    size_t i = 0, j = 0;
    while (input[i] && j < output_size - 1) {
        char c = input[i];
        // Only allow safe printable ASCII
        if (c >= 32 && c <= 126 && c != '\\' && c != '"' && c != '\'' && 
            c != '<' && c != '>' && c != '&' && c != '%' && c != '$') {
            output[j++] = c;
        } else {
            output[j++] = '_';
        }
        i++;
    }
    output[j] = '\0';
}
```

**Path Validation:**
```cpp
static bool is_safe_path(const char* path) {
    if (!path || strlen(path) == 0 || strlen(path) > 255) return false;
    
    // Reject dangerous patterns
    if (strstr(path, "../") || strstr(path, "..\\") ||
        strstr(path, "/etc/") || strstr(path, "/root/") ||
        path[0] != '/' || strstr(path, "//")) {
        return false;
    }
    
    // Only allow /tmp/ paths
    if (strncmp(path, "/tmp/", 5) != 0) return false;
    
    return true;
}
```

### 4.2 Memory Safety

**Bounds Checking:**
```cpp
// Ring buffer bounds validation
uint32_t index = head & ring->mask;
if (index > ring->mask) return false;

size_t offset = static_cast<size_t>(index) * ring->elem_size;
size_t total_size = static_cast<size_t>(ring->mask + 1) * ring->elem_size;
if (offset + ring->elem_size > total_size) return false;
```

**Integer Overflow Protection:**
```cpp
// Message pool overflow check
if (capacity > UINT32_MAX / sizeof(void*) || 
    capacity > UINT32_MAX / msg_size) {
    return nullptr;
}
```

## 5. Performance Optimizations

### 5.1 Cache Line Alignment
```cpp
struct Spsc {
    std::atomic<uint32_t> head{0};
    char pad1[64 - sizeof(std::atomic<uint32_t>)]; // Prevent false sharing
    std::atomic<uint32_t> tail{0};
    char pad2[64 - sizeof(std::atomic<uint32_t>)]; // Prevent false sharing
    // ...
};
```

### 5.2 Memory Ordering
- **Relaxed ordering**: For non-critical loads
- **Acquire/Release**: For producer-consumer synchronization
- **Sequential consistency**: Avoided for performance

### 5.3 Zero-Copy Design
- Direct stdout/stdin pipes between processes
- Shared memory ring buffers
- Minimal data copying in critical paths

## 6. Build System

### 6.1 Build Script (`build.sh`)
- **Compiler**: g++/clang++ with C++17 support
- **Optimization**: -O2 for production builds
- **Platform Detection**: Automatic Linux/macOS detection
- **Library Creation**: Static library (libulmd.a)

### 6.2 Platform Abstractions
```cpp
#ifdef ULMD_LINUX
    // Linux-specific implementations
#elif ULMD_MACOS
    // macOS-specific implementations
#else
    // Fallback implementations
#endif
```

## 7. Testing Framework

### 7.1 Test Categories
- **Unit Tests**: Individual component testing
- **Integration Tests**: End-to-end pipeline testing
- **Performance Tests**: Latency and throughput validation
- **Security Tests**: Vulnerability and safety testing

### 7.2 Test Infrastructure
- Custom test framework in `run_tests.sh`
- Automated validation in `validate_production.sh`
- Continuous integration via GitHub Actions

## 8. Deployment Architecture

### 8.1 Service Management
- **Linux**: systemd service files
- **macOS**: Custom startup/shutdown scripts
- **Process Isolation**: Separate processes for each component

### 8.2 Configuration Management
```cpp
struct Config {
    std::string bind_address = "127.0.0.1";
    uint16_t port = 12345;
    uint32_t ring_slots = 1024;
    double tsc_hz = 3000000000.0;
    // ...
};
```

## 9. Monitoring and Observability

### 9.1 Metrics Collection
- **Latency histograms**: Sub-microsecond precision
- **Throughput tracking**: Messages and bytes per second
- **Health monitoring**: Component status and error rates
- **Ring buffer occupancy**: Queue depth monitoring

### 9.2 Output Formats
- **CSV**: Detailed latency measurements
- **Status files**: Health and metrics snapshots
- **Logs**: Structured error and event logging

## 10. Error Handling

### 10.1 Error Categories
- **Network errors**: Socket failures, timeouts
- **Protocol errors**: Invalid messages, CRC failures
- **System errors**: Memory allocation, file I/O
- **Configuration errors**: Invalid parameters

### 10.2 Recovery Strategies
- **Graceful degradation**: Continue processing valid messages
- **Circuit breaker**: Stop processing on critical errors
- **Retry logic**: Automatic recovery for transient failures
- **Health reporting**: Status propagation to monitoring

---

**Document Version**: 1.0  
**Last Updated**: October 2024  
**Status**: Production Implementation Complete