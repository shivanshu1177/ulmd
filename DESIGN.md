# ULMD High-Level Design Document

## 1. Executive Summary

### 1.1 System Overview
ULMD (Ultra Low Latency Market Data) is a production-ready, high-performance market data processing system designed for financial institutions requiring sub-microsecond latency and million+ message-per-second throughput. The system processes real-time market data feeds with precise latency measurement and risk processing capabilities.

### 1.2 Key Performance Indicators
- **Latency**: 200-800 ns average end-to-end processing
- **Throughput**: 1M+ messages/second sustained
- **Availability**: 99.99% uptime target
- **Memory Footprint**: <100MB resident set
- **CPU Utilization**: <50% single core

### 1.3 Business Value
- Enables high-frequency trading strategies
- Provides competitive advantage through ultra-low latency
- Reduces infrastructure costs through efficient resource utilization
- Ensures regulatory compliance through comprehensive audit trails

## 2. System Architecture

### 2.1 High-Level Architecture

```
┌─────────────┐    ┌─────────────┐    ┌─────────────┐    ┌─────────────┐
│   Market    │    │   Ingress   │    │   Parser    │    │   Worker    │
│   Data      │───▶│   Service   │───▶│   Service   │───▶│   Risk      │
│   Feeds     │    │             │    │             │    │   Service   │
└─────────────┘    └─────────────┘    └─────────────┘    └─────────────┘
                           │                   │                   │
                           ▼                   ▼                   ▼
                   ┌─────────────┐    ┌─────────────┐    ┌─────────────┐
                   │ Timestamp   │    │ Ring Buffer │    │ CSV Output  │
                   │ Capture     │    │ SPSC Queue  │    │ & Metrics   │
                   └─────────────┘    └─────────────┘    └─────────────┘
```

### 2.2 Component Architecture

#### 2.2.1 Ingress Service
- **Purpose**: UDP packet reception and timestamp capture
- **Technology**: C++17, POSIX sockets, mach_absolute_time()
- **Performance**: Zero-copy packet processing
- **Scalability**: Single-threaded, event-driven

#### 2.2.2 Parser Service  
- **Purpose**: Message validation and protocol parsing
- **Technology**: Lock-free algorithms, CRC32 validation
- **Protocol**: ULMD binary format (64-byte frames)
- **Throughput**: 1M+ messages/second parsing rate

#### 2.2.3 Worker Risk Service
- **Purpose**: Risk processing and latency measurement
- **Technology**: High-resolution timing, CSV output
- **Metrics**: End-to-end latency tracking
- **Output**: Real-time performance analytics

#### 2.2.4 Ring Buffer Communication
- **Type**: Single Producer Single Consumer (SPSC)
- **Implementation**: Lock-free, wait-free operations
- **Capacity**: Configurable (default 4096 slots)
- **Memory**: Shared memory for inter-process communication

## 3. Data Flow Design

### 3.1 Message Processing Pipeline

```
UDP Packet (64B) → Timestamp → Parse → Validate → Queue → Process → Output
     ↓               ↓          ↓        ↓         ↓       ↓        ↓
   Network        Ingress    Parser   Protocol   Ring   Worker   CSV/Metrics
   Interface      Service    Service  Validation Buffer Service
```

### 3.2 Data Structures

#### 3.2.1 ULMD Message Format
```cpp
struct UlmdFrame {
    uint32_t magic;        // 0x554C4D44 ("ULMD")
    uint8_t version;       // Protocol version
    uint8_t msg_type;      // Message type
    uint16_t flags;        // Control flags
    uint64_t seq_no;       // Sequence number
    uint64_t send_ts_ns;   // Send timestamp
    char symbol[8];        // Trading symbol
    int64_t bid_px;        // Bid price
    uint32_t bid_sz;       // Bid size
    int64_t ask_px;        // Ask price
    uint32_t ask_sz;       // Ask size
    uint32_t reserved;     // Reserved field
    uint32_t crc32;        // CRC checksum
} __attribute__((packed));
```

#### 3.2.2 Internal Message Structure
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

## 4. Performance Design

### 4.1 Latency Optimization Strategies

#### 4.1.1 Zero-Copy Operations
- Direct memory mapping for shared buffers
- Elimination of unnecessary data copying
- Efficient memory layout for cache optimization

#### 4.1.2 Lock-Free Algorithms
- SPSC ring buffer implementation
- Atomic operations for thread safety
- Wait-free data structures

#### 4.1.3 CPU Optimization
- Single-threaded design to avoid context switching
- CPU affinity for consistent performance
- Branch prediction optimization

### 4.2 Throughput Optimization

#### 4.2.1 Batching Strategy
- Configurable flush intervals
- Bulk processing capabilities
- Adaptive batching based on load

#### 4.2.2 Memory Management
- Pre-allocated message pools
- Aligned memory allocation
- NUMA-aware memory placement

## 5. Security Architecture

### 5.1 Security Principles
- Defense in depth
- Principle of least privilege
- Input validation and sanitization
- Memory safety guarantees

### 5.2 Security Controls

#### 5.2.1 Input Validation
- Protocol format validation
- CRC32 checksum verification
- Bounds checking on all inputs
- String sanitization for logging

#### 5.2.2 Memory Safety
- Buffer overflow protection
- Integer overflow prevention
- Safe memory allocation patterns
- Proper resource cleanup

#### 5.2.3 Access Control
- File path validation
- Shared memory name restrictions
- Process isolation
- Secure logging practices

## 6. Operational Design

### 6.1 Deployment Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    Production Environment                    │
├─────────────────────────────────────────────────────────────┤
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐         │
│  │   Ingress   │  │   Parser    │  │   Worker    │         │
│  │   Process   │  │   Process   │  │   Process   │         │
│  └─────────────┘  └─────────────┘  └─────────────┘         │
│         │                 │                 │              │
│         └─────────────────┼─────────────────┘              │
│                           │                                │
│  ┌─────────────────────────────────────────────────────────┤
│  │              Shared Memory Ring Buffer                  │
│  └─────────────────────────────────────────────────────────┤
│                                                            │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐         │
│  │ Monitoring  │  │   Logging   │  │   Backup    │         │
│  │   Service   │  │   Service   │  │   Service   │         │
│  └─────────────┘  └─────────────┘  └─────────────┘         │
└─────────────────────────────────────────────────────────────┘
```

### 6.2 Monitoring and Observability

#### 6.2.1 Key Metrics
- Message processing rate
- End-to-end latency percentiles
- Ring buffer occupancy
- Error rates and dropped messages
- System resource utilization

#### 6.2.2 Health Checks
- Process health monitoring
- Memory leak detection
- Performance degradation alerts
- Automatic failover capabilities

### 6.3 Configuration Management

#### 6.3.1 Configuration Parameters
```ini
# Network Configuration
port=12346
bind_address=127.0.0.1

# Performance Tuning
sleep_us=1
ring_slots=4096
flush_ms=100

# Monitoring
health_check_interval=5
metrics_update_interval=1
```

## 7. Scalability and Reliability

### 7.1 Horizontal Scaling
- Multiple ingress instances for different feeds
- Partitioned processing by symbol or market
- Load balancing across worker processes

### 7.2 Fault Tolerance
- Graceful degradation under load
- Automatic recovery from transient failures
- Circuit breaker patterns for external dependencies

### 7.3 Disaster Recovery
- Automated backup procedures
- Point-in-time recovery capabilities
- Cross-region replication support

## 8. Technology Stack

### 8.1 Core Technologies
- **Language**: C++17
- **Build System**: GNU Make
- **Platforms**: Linux, macOS
- **IPC**: POSIX shared memory
- **Networking**: POSIX sockets

### 8.2 Development Tools
- **Compiler**: GCC/Clang
- **Testing**: Custom test framework
- **Profiling**: Valgrind, perf
- **CI/CD**: GitHub Actions

## 9. Quality Assurance

### 9.1 Testing Strategy
- Unit tests for individual components
- Integration tests for end-to-end flows
- Performance benchmarking
- Security vulnerability scanning
- Load testing and stress testing

### 9.2 Code Quality
- Static analysis tools
- Memory safety validation
- Performance profiling
- Code review processes

## 10. Future Enhancements

### 10.1 Planned Features
- GPU acceleration for complex calculations
- Machine learning-based anomaly detection
- Real-time risk analytics
- Multi-protocol support

### 10.2 Scalability Improvements
- Distributed processing architecture
- Cloud-native deployment options
- Kubernetes orchestration
- Auto-scaling capabilities

---

**Document Version**: 1.0  
**Last Updated**: October 2025 
**Status**: Production Ready