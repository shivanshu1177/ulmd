#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cinttypes>
#include <signal.h>
#include <errno.h>
#include <chrono>
#include <thread>
#include <ulmd/config.hpp>
#include <ulmd/health.hpp>
#include <ulmd/telemetry.hpp>
#include <ulmd/shutdown.hpp>
#include <atomic>

#ifdef ULMD_LINUX
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <x86intrin.h>
#elif ULMD_MACOS
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <mach/mach_time.h>
#endif

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

static int create_udp_socket(uint16_t port) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return -1;
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr) != 1) {
        close(sock);
        return -1;
    }
    
    if (bind(sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        close(sock);
        return -1;
    }
    
    return sock;
}

static int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static void usage() {
    printf("Usage: ingress [--config <file>] [--port <port>] [--tsc-hz <hz>]\n");
    printf("  --config <file> Configuration file path\n");
    printf("  --port <port>   UDP port to bind to\n");
    printf("  --tsc-hz <hz>   TSC frequency in Hz\n");
}

int main(int argc, char* argv[]) {
    ulmd::Config config;
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
            if (!ulmd::load_config(argv[++i], config)) {
                fprintf(stderr, "Failed to load config file\n");
                return 1;
            }
        } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            char* endptr;
            long port = strtol(argv[++i], &endptr, 10);
            if (*endptr != '\0' || port < 1 || port > 65535) {
                fprintf(stderr, "Invalid port number\n");
                return 1;
            }
            config.port = static_cast<uint16_t>(port);
        } else if (strcmp(argv[i], "--tsc-hz") == 0 && i + 1 < argc) {
            config.tsc_hz = atof(argv[++i]);
        } else if (strcmp(argv[i], "--help") == 0) {
            usage();
            return 0;
        }
    }
    
    if (config.port == 0 || config.tsc_hz <= 0.0) {
        usage();
        return 1;
    }
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    int sock = create_udp_socket(config.port);
    if (sock < 0) {
        perror("create_udp_socket");
        ulmd::log_error("ingress", "Failed to create UDP socket");
        return 1;
    }
    
    if (set_nonblocking(sock) < 0) {
        perror("set_nonblocking");
        close(sock);
        return 1;
    }
    
    ulmd::HealthMetrics health;
    ulmd::health_init(&health);
    
    // Register shutdown handlers
    ulmd::register_shutdown_handler([&]() {
        if (sock >= 0) {
            close(sock);
        }
        ulmd::health_write_status(&health, "/tmp/ingress_health.status");
    });
    
    uint8_t buffer[64];
    auto last_health_check = std::chrono::steady_clock::now();
    
    while (running.load(std::memory_order_relaxed) && !ulmd::shutdown_requested()) {
        uint64_t recv_tsc = get_timestamp();
        ssize_t bytes = recv(sock, buffer, 64, 0);
        
        if (bytes == 64) {
            // Write recv_tsc as 8-byte header, then the 64-byte message
            if (fwrite(&recv_tsc, sizeof(recv_tsc), 1, stdout) != 1 ||
                fwrite(buffer, 1, 64, stdout) != 64) {
                health.errors_count.fetch_add(1);
                ulmd::log_error("ingress", "Failed to write complete data to stdout");
            }
            fflush(stdout);
            
            health.messages_processed.fetch_add(1);
            auto now_ts = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            health.last_activity_ts.store(now_ts);
        } else if (bytes < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            perror("recv");
            health.errors_count.fetch_add(1);
            char error_buf[256];
            const char* error_str = strerror(errno);
            if (error_str) {
                snprintf(error_buf, sizeof(error_buf), "recv() failed: %s", error_str);
            } else {
                snprintf(error_buf, sizeof(error_buf), "recv() failed: errno=%d", errno);
            }
            ulmd::log_error("ingress", error_buf);
            break;
        } else if (bytes < 0) {
            // No data available, sleep briefly to avoid busy waiting
            std::this_thread::sleep_for(std::chrono::microseconds(config.sleep_us));
        }
        
        // Update health status every 5 seconds
        auto now = std::chrono::steady_clock::now();
        if (now - last_health_check >= std::chrono::seconds(5)) {
            auto now_ts = std::chrono::duration_cast<std::chrono::seconds>(
                now.time_since_epoch()).count();
            ulmd::health_update(&health, now_ts);
            ulmd::health_write_status(&health, "/tmp/ingress_health.status");
            last_health_check = now;
        }
    }
    
    // Execute shutdown handlers before exit
    ulmd::execute_shutdown_handlers();
    
    return 0;
}