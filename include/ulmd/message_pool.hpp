#pragma once

#include <cstdint>

namespace ulmd {

/**
 * @brief Opaque fixed-size message pool handle
 * @invariant Lock-free operations, zero allocation in hot path
 * @invariant Fixed capacity set at creation time
 */
struct MessagePool;

/**
 * @brief Create fixed-size message pool
 * @param capacity Maximum number of messages
 * @param msg_size Size of each message in bytes
 * @return Pool handle or nullptr on failure
 * @invariant capacity > 0, msg_size > 0
 * @performance O(1) allocation
 */
MessagePool* pool_create(uint32_t capacity, uint32_t msg_size);

/**
 * @brief Destroy message pool
 * @param pool Pool handle
 * @invariant Pool becomes invalid after call
 * @performance O(1) deallocation
 */
void pool_destroy(MessagePool* pool);

/**
 * @brief Acquire message from pool
 * @param pool Pool handle
 * @return Message pointer or nullptr if pool empty
 * @invariant Returned pointer valid until pool_release() called
 * @performance O(1) lock-free operation, zero allocation
 */
void* pool_acquire(MessagePool* pool);

/**
 * @brief Release message back to pool
 * @param pool Pool handle
 * @param msg Message pointer from pool_acquire()
 * @invariant msg must be from this pool's pool_acquire()
 * @performance O(1) lock-free operation
 */
void pool_release(MessagePool* pool, void* msg);

/**
 * @brief Get pool capacity
 * @param pool Pool handle
 * @return Total capacity of pool
 * @performance O(1) accessor
 */
uint32_t pool_capacity(const MessagePool* pool);

/**
 * @brief Get available messages
 * @param pool Pool handle
 * @return Number of messages available for acquire
 * @performance O(1) accessor
 */
uint32_t pool_available(const MessagePool* pool);

} // namespace ulmd