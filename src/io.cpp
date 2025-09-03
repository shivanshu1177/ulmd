#include <ulmd/io.hpp>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <climits>
#include <cerrno>

namespace ulmd {

int create_udp_socket(uint16_t port) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return -1;
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    
    if (inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr) <= 0) {
        close(sock);
        return -1;
    }
    
    if (bind(sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        close(sock);
        return -1;
    }
    return sock;
}

int set_nonblocking(int fd) {
    if (fd < 0) return -1;
    
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int recv_datagram(int fd, void* buffer, size_t buffer_size) {
    if (fd < 0 || !buffer || buffer_size == 0) return -1;
    
    // Validate buffer size limits
    if (buffer_size > 65536) return -1; // Reasonable UDP packet limit
    
    ssize_t result = recv(fd, buffer, buffer_size, 0);
    if (result < 0) {
        // Log specific error for debugging
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            // Only log unexpected errors, not normal non-blocking behavior
        }
        return -1;
    }
    
    if (result > INT_MAX) return -1;
    return static_cast<int>(result);
}

int send_datagram(int fd, const void* buffer, size_t buffer_size, const struct sockaddr* dest_addr, socklen_t addr_len) {
    if (fd < 0 || !buffer || buffer_size == 0 || !dest_addr) return -1;
    
    // Validate buffer size limits
    if (buffer_size > 65536) return -1;
    
    ssize_t result = sendto(fd, buffer, buffer_size, 0, dest_addr, addr_len);
    if (result < 0) return -1;
    if (result > INT_MAX) return -1;
    return static_cast<int>(result);
}

int close_socket(int fd) {
    return close(fd);
}

} // namespace ulmd