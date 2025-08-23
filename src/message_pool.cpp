#include <ulmd/message_pool.hpp>
#include <atomic>
#include <cstdlib>
#include <new>

namespace ulmd {

struct MessagePool {
    std::atomic<uint32_t> head{0};
    uint32_t capacity;
    uint32_t msg_size;
    void** free_list;
    char* data;
};

MessagePool* pool_create(uint32_t capacity, uint32_t msg_size) {
    if (capacity == 0 || msg_size == 0) return nullptr;
    
    // Check for integer overflow
    if (capacity > UINT32_MAX / sizeof(void*) || capacity > UINT32_MAX / msg_size) {
        return nullptr;
    }
    
    MessagePool* pool = static_cast<MessagePool*>(malloc(sizeof(MessagePool)));
    if (!pool) return nullptr;
    
    // Initialize atomic member immediately
    new (&pool->head) std::atomic<uint32_t>(0);
    
    pool->free_list = static_cast<void**>(malloc(capacity * sizeof(void*)));
    pool->data = static_cast<char*>(malloc(capacity * msg_size));
    
    if (!pool->free_list || !pool->data) {
        free(pool->free_list);
        free(pool->data);
        free(pool);
        return nullptr;
    }
    
    pool->capacity = capacity;
    pool->msg_size = msg_size;
    pool->head.store(capacity);
    
    for (uint32_t i = 0; i < capacity; i++) {
        pool->free_list[i] = pool->data + i * msg_size;
    }
    
    return pool;
}

void pool_destroy(MessagePool* pool) {
    if (pool) {
        // Explicitly call destructor for placement new constructed atomic
        pool->head.~atomic();
        free(pool->free_list);
        free(pool->data);
        free(pool);
    }
}

void* pool_acquire(MessagePool* pool) {
    if (!pool || !pool->free_list) return nullptr;
    
    uint32_t head = pool->head.load(std::memory_order_acquire);
    while (head > 0) {
        if (pool->head.compare_exchange_weak(head, head - 1, std::memory_order_acq_rel)) {
            if (head > 0 && head <= pool->capacity) {
                // Additional bounds check
                uint32_t index = head - 1;
                if (index < pool->capacity && pool->free_list[index]) {
                    return pool->free_list[index];
                }
            }
        }
        head = pool->head.load(std::memory_order_acquire);
    }
    return nullptr;
}

void pool_release(MessagePool* pool, void* msg) {
    if (!pool || !msg || !pool->data || !pool->free_list) return;
    
    // Validate that msg belongs to this pool's memory range
    char* msg_ptr = static_cast<char*>(msg);
    char* pool_start = pool->data;
    
    // Check for integer overflow in size calculation
    if (pool->capacity > UINT32_MAX / pool->msg_size) return;
    
    char* pool_end = pool->data + static_cast<size_t>(pool->capacity) * pool->msg_size;
    if (msg_ptr < pool_start || msg_ptr >= pool_end) return;
    
    // Validate message alignment
    size_t offset = static_cast<size_t>(msg_ptr - pool_start);
    if (pool->msg_size == 0 || offset % pool->msg_size != 0) return;
    
    uint32_t head = pool->head.load(std::memory_order_relaxed);
    while (head < pool->capacity) {
        if (pool->head.compare_exchange_weak(head, head + 1, std::memory_order_release, std::memory_order_relaxed)) {
            if (head < pool->capacity) {
                pool->free_list[head] = msg;
            }
            return;
        }
        // head was updated by compare_exchange_weak, retry
    }
}

uint32_t pool_capacity(const MessagePool* pool) {
    if (!pool) return 0;
    return pool->capacity;
}

uint32_t pool_available(const MessagePool* pool) {
    if (!pool) return 0;
    return pool->head.load(std::memory_order_relaxed);
}

} // namespace ulmd