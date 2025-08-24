#pragma once

#include <cstdint>

namespace ulmd {

/**
 * @brief Opaque SPSC ring buffer handle
 * @invariant Single producer, single consumer only
 * @invariant Power-of-two slot count for fast modulo
 * @invariant Lock-free, wait-free fast path operations
 */
struct Spsc;

/**
 * @brief Create SPSC ring buffer
 * @param slots Number of slots (must be power of 2)
 * @param elem_size Size of each element in bytes
 * @return Ring handle or nullptr on failure
 * @invariant slots must be power of 2, elem_size > 0
 * @performance O(1) allocation
 */
Spsc* spsc_create(uint32_t slots, uint32_t elem_size);

/**
 * @brief Destroy SPSC ring buffer
 * @param ring Ring handle
 * @invariant Ring becomes invalid after call
 * @performance O(1) deallocation
 */
void spsc_destroy(Spsc* ring);

/**
 * @brief Try to push element to ring
 * @param ring Ring handle
 * @param data Element data to copy
 * @return true if pushed, false if full
 * @invariant Single producer only
 * @performance O(1) wait-free operation
 */
bool spsc_try_push(Spsc* ring, const void* data);

/**
 * @brief Try to pop element from ring
 * @param ring Ring handle
 * @param data Buffer to copy element into
 * @return true if popped, false if empty
 * @invariant Single consumer only
 * @performance O(1) wait-free operation
 */
bool spsc_try_pop(Spsc* ring, void* data);

/**
 * @brief Get ring capacity
 * @param ring Ring handle
 * @return Total number of slots
 * @performance O(1) accessor
 */
uint32_t spsc_size(const Spsc* ring);

/**
 * @brief Get current occupancy
 * @param ring Ring handle
 * @return Number of elements currently in ring
 * @performance O(1) accessor
 */
uint32_t spsc_occupancy(const Spsc* ring);

/**
 * @brief Reset ring to empty state
 * @param ring Ring handle
 * @invariant Must not be called concurrently with push/pop
 * @performance O(1) reset
 */
void spsc_reset(Spsc* ring);

/**
 * @brief Create SPSC ring in shared memory
 * @param name Shared memory name/path
 * @param slots Number of slots (must be power of 2)
 * @param elem_size Size of each element in bytes
 * @return Ring handle or nullptr on failure
 * @performance O(1) shared memory allocation
 */
Spsc* spsc_create_shared(const char* name, uint32_t slots, uint32_t elem_size);

/**
 * @brief Attach to existing shared memory ring
 * @param name Shared memory name/path
 * @return Ring handle or nullptr on failure
 * @performance O(1) shared memory attach
 */
Spsc* spsc_attach_shared(const char* name);

/**
 * @brief Destroy shared memory ring
 * @param name Shared memory name/path
 * @return 0 on success, -1 on error
 * @performance O(1) shared memory cleanup
 */
int spsc_destroy_shared(const char* name);

} // namespace ulmd