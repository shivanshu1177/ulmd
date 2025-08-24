#include <ulmd/ring.hpp>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cinttypes>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

static bool is_power_of_two(uint32_t n) {
    return n > 0 && (n & (n - 1)) == 0;
}

static int cmd_create(const char* name, uint32_t slots, uint32_t elem_size) {
    if (!name) {
        fprintf(stderr, "Error: name cannot be null\n");
        return 1;
    }
    
    if (!is_power_of_two(slots)) {
        fprintf(stderr, "Error: slots must be power of 2\n");
        return 1;
    }
    
    if (elem_size == 0) {
        fprintf(stderr, "Error: elem_size must be > 0\n");
        return 1;
    }
    
    ulmd::Spsc* ring = ulmd::spsc_create_shared(name, slots, elem_size);
    if (!ring) {
        fprintf(stderr, "Error: failed to create shared ring\n");
        return 1;
    }
    
    printf("created name=%.64s slots=%" PRIu32 " elem=%" PRIu32 "\n", name, slots, elem_size);
    
    // Clean up the ring pointer (shared memory remains for other processes)
    // Calculate size based on ring structure layout
    size_t ring_size = 128 + slots * elem_size; // Spsc struct is ~128 bytes
    munmap(ring, ring_size);
    
    return 0;
}

static int cmd_destroy(const char* name) {
    if (!name) {
        fprintf(stderr, "Error: name cannot be null\n");
        return 1;
    }
    
    if (ulmd::spsc_destroy_shared(name) < 0) {
        perror("shm_unlink");
        return 1;
    }
    
    printf("destroyed name=%.64s\n", name);
    return 0;
}

static int cmd_show(const char* name) {
    if (!name) {
        fprintf(stderr, "Error: name cannot be null\n");
        return 1;
    }
    
    // Get shared memory size for proper cleanup
    int fd = shm_open(name, O_RDWR, 0);
    if (fd < 0) {
        perror("shm_open");
        return 1;
    }
    
    struct stat st;
    if (fstat(fd, &st) < 0) {
        perror("fstat");
        close(fd);
        return 1;
    }
    close(fd);
    
    ulmd::Spsc* ring = ulmd::spsc_attach_shared(name);
    if (!ring) {
        perror("attach_shared");
        return 1;
    }
    
    uint32_t slots = ulmd::spsc_size(ring);
    uint32_t occupancy = ulmd::spsc_occupancy(ring);
    
    printf("name=%.64s slots=%" PRIu32 " occupancy=%" PRIu32 "\n", name, slots, occupancy);
    
    // Detach from shared memory to prevent leak
    if (munmap(ring, static_cast<size_t>(st.st_size)) < 0) {
        perror("munmap");
        return 1;
    }
    
    return 0;
}

static void usage() {
    printf("Usage:\n");
    printf("  ringctl create --name <path> --slots <pow2> --elem <bytes>\n");
    printf("  ringctl destroy --name <path>\n");
    printf("  ringctl show --name <path>\n");
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        usage();
        return 1;
    }
    
    const char* cmd = argv[1];
    
    if (strcmp(cmd, "create") == 0) {
        const char* name = nullptr;
        uint32_t slots = 0;
        uint32_t elem_size = 0;
        
        for (int i = 2; i < argc; i++) {
            if (strcmp(argv[i], "--name") == 0 && i + 1 < argc) {
                name = argv[++i];
            } else if (strcmp(argv[i], "--slots") == 0 && i + 1 < argc) {
                slots = static_cast<uint32_t>(atoi(argv[++i]));
            } else if (strcmp(argv[i], "--elem") == 0 && i + 1 < argc) {
                elem_size = static_cast<uint32_t>(atoi(argv[++i]));
            }
        }
        
        if (!name || slots == 0 || elem_size == 0) {
            usage();
            return 1;
        }
        
        return cmd_create(name, slots, elem_size);
        
    } else if (strcmp(cmd, "destroy") == 0) {
        const char* name = nullptr;
        
        for (int i = 2; i < argc; i++) {
            if (strcmp(argv[i], "--name") == 0 && i + 1 < argc) {
                name = argv[++i];
            }
        }
        
        if (!name) {
            usage();
            return 1;
        }
        
        return cmd_destroy(name);
        
    } else if (strcmp(cmd, "show") == 0) {
        const char* name = nullptr;
        
        for (int i = 2; i < argc; i++) {
            if (strcmp(argv[i], "--name") == 0 && i + 1 < argc) {
                name = argv[++i];
            }
        }
        
        if (!name) {
            usage();
            return 1;
        }
        
        return cmd_show(name);
        
    } else {
        usage();
        return 1;
    }
}