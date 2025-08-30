#pragma once

#include <cstdint>
#include <cstddef>

namespace ulmd {

/**
 * @brief Create and bind UDP socket for ingress
 * @param port Port to bind to
 * @return Socket file descriptor, or -1 on error
 * @invariant Returned fd is valid for UDP operations or -1
 * @performance O(1) system call
 */
int create_udp_socket(uint16_t port);

/**
 * @brief Set socket to non-blocking mode
 * @param fd Socket file descriptor
 * @return 0 on success, -1 on error
 * @invariant Socket remains valid after call
 * @performance O(1) system call
 */
int set_nonblocking(int fd);

/**
 * @brief Receive UDP datagram into fixed buffer
 * @param fd Socket file descriptor
 * @param buffer Destination buffer (must be >= 64 bytes)
 * @param buffer_size Size of buffer
 * @return Bytes received (0-buffer_size), or -1 on error/EAGAIN
 * @invariant Buffer contents undefined on error
 * @performance O(1) system call, zero-copy when possible
 */
int recv_datagram(int fd, void* buffer, size_t buffer_size);

/**
 * @brief Close socket file descriptor
 * @param fd Socket file descriptor
 * @return 0 on success, -1 on error
 * @invariant fd becomes invalid after successful close
 * @performance O(1) system call
 */
int close_socket(int fd);

} // namespace ulmd
