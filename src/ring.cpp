#include <ulmd/ring.hpp>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <new>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

namespace ulmd {
    struct Spsc {
        std::atomic<uint32_t> head{0};
        char pad1[64 - sizeof(std::atomic<uint32_t>)];
        std::atomic<uint32_t> tail{0};
        char pad2[64 - sizeof(std::atomic<uint32_t>)];
        uint32_t mask;
        uint32_t elem_size;
        bool is_shared; // Track allocation type
        char data[];
    };
    
    Spsc* spsc_create(uint32_t slots, uint32_t elem_size) {
        if ((slots & (slots - 1)) != 0 || slots == 0 || elem_size == 0) return nullptr;
        
        size_t size = sizeof(Spsc) + static_cast<size_t>(slots) * elem_size;
        
        // Use posix_memalign for better compatibility
        void* ptr = nullptr;
        if (posix_memalign(&ptr, 64, size) != 0) {
            return nullptr;
        }
        
        Spsc* ring = new (ptr) Spsc();  // properly constructs atomics via their constructors
        ring->mask = slots - 1;
        ring->elem_size = elem_size;
        ring->is_shared = false;
        memset(ring->data, 0, static_cast<size_t>(slots) * elem_size);
        return ring;
    }
    
    void spsc_destroy(Spsc* ring) {
        if (!ring) return;
        if (ring->is_shared) {
            // For shared memory, calculate size and unmap
            size_t size = sizeof(Spsc) + (ring->mask + 1) * ring->elem_size;
            munmap(ring, size);
        } else {
            free(ring);
        }
    }
    
    bool spsc_try_push(Spsc* ring, const void* data) {
        if (!ring || !data || ring->elem_size == 0) return false;
        
        uint32_t head = ring->head.load(std::memory_order_relaxed);
        uint32_t tail = ring->tail.load(std::memory_order_acquire);
        
        if (((head + 1) & ring->mask) == (tail & ring->mask)) {
            return false; // Ring is full
        }
        
        // Bounds check to prevent buffer overflow
        uint32_t index = head & ring->mask;
        if (index > ring->mask) return false; // Safety check
        
        size_t offset = static_cast<size_t>(index) * ring->elem_size;
        size_t total_size = static_cast<size_t>(ring->mask + 1) * ring->elem_size;
        if (offset + ring->elem_size > total_size) return false;
        
        memcpy(ring->data + offset, data, ring->elem_size);
        ring->head.store(head + 1, std::memory_order_release);
        return true;
    }

    
    bool spsc_try_pop(Spsc* ring, void* data) {
        if (!ring || !data || ring->elem_size == 0) return false;
        
        uint32_t tail = ring->tail.load(std::memory_order_relaxed);
        uint32_t head = ring->head.load(std::memory_order_acquire);
        
        if (tail == head) return false;
        
        // Bounds check to prevent buffer overflow
        uint32_t index = tail & ring->mask;
        if (index > ring->mask) return false; // Safety check
        
        size_t offset = static_cast<size_t>(index) * ring->elem_size;
        size_t total_size = static_cast<size_t>(ring->mask + 1) * ring->elem_size;
        if (offset + ring->elem_size > total_size) return false;
        
        memcpy(data, ring->data + offset, ring->elem_size);
        ring->tail.store(tail + 1, std::memory_order_release);
        return true;
    }
    
    uint32_t spsc_size(const Spsc* ring) {
        if (!ring) return 0;
        return ring->mask + 1;
    }
    
    uint32_t spsc_occupancy(const Spsc* ring) {
        if (!ring) return 0;
        uint32_t head = ring->head.load(std::memory_order_relaxed);
        uint32_t tail = ring->tail.load(std::memory_order_relaxed);
        return (head - tail) & ring->mask;
    }
    
    void spsc_reset(Spsc* ring) {
        ring->head.store(0);
        ring->tail.store(0);
    }
    
    static bool is_safe_shm_name(const char* name) {
        if (!name || strlen(name) == 0 || strlen(name) > 64) return false;
        
        // Reject path traversal, absolute paths, and dangerous patterns
        if (strstr(name, "/") || strstr(name, "..") || name[0] == '.' ||
            strstr(name, "../") || strstr(name, "..\\")
            || strstr(name, "/etc/") || strstr(name, "/root/")
            || strstr(name, "/home/") || strstr(name, "/usr/")
            || strstr(name, "/var/") || strstr(name, "/sys/")
            || strstr(name, "/proc/") || strstr(name, "/dev/")
            || strstr(name, "//")) {
            return false;
        }
        
        // Only allow alphanumeric, underscore, hyphen
        for (const char* p = name; *p; p++) {
            if (!isalnum(*p) && *p != '_' && *p != '-') return false;
        }
        return true;
    }

    Spsc* spsc_create_shared(const char* name, uint32_t slots, uint32_t elem_size) {
        if (!is_safe_shm_name(name) || (slots & (slots - 1)) != 0 || slots == 0 || elem_size == 0) return nullptr;

        size_t size = sizeof(Spsc) + slots * elem_size;

        char shm_name[67]; // '/' + up to 64 chars + '\0'
        snprintf(shm_name, sizeof(shm_name), "/%s", name);
        int fd = shm_open(shm_name, O_CREAT | O_EXCL | O_RDWR, 0666);
        if (fd < 0) return nullptr;
        
        if (ftruncate(fd, size) < 0) {
            int saved_errno = errno;
            close(fd);
            shm_unlink(shm_name);
            errno = saved_errno;
            return nullptr;
        }

        Spsc* ring = (Spsc*)mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        close(fd);

        if (ring == MAP_FAILED) {
            shm_unlink(shm_name);
            return nullptr;
        }
        
        ring->head.store(0);
        ring->tail.store(0);
        ring->mask = slots - 1;
        ring->elem_size = elem_size;
        ring->is_shared = true;
        return ring;
    }
    
    Spsc* spsc_attach_shared(const char* name) {
        if (!is_safe_shm_name(name)) return nullptr;

        char shm_name[67];
        snprintf(shm_name, sizeof(shm_name), "/%s", name);
        int fd = shm_open(shm_name, O_RDWR, 0);
        if (fd < 0) return nullptr;
        
        struct stat st;
        if (fstat(fd, &st) < 0) {
            close(fd);
            return nullptr;
        }
        
        Spsc* ring = (Spsc*)mmap(nullptr, st.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        close(fd);
        
        if (ring == MAP_FAILED) return nullptr;
        
        ring->is_shared = true;
        return ring;
    }
    
    int spsc_destroy_shared(const char* name) {
        if (!is_safe_shm_name(name)) return -1;
        char shm_name[67];
        snprintf(shm_name, sizeof(shm_name), "/%s", name);
        return shm_unlink(shm_name);
    }
}
