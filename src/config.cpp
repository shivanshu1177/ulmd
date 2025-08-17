#include <ulmd/config.hpp>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <functional>

namespace ulmd {

bool load_config(const char* path, Config& config) {
    if (!path) return false;
    
    // Validate path safety - prevent path traversal
    if (strlen(path) > 256 || strstr(path, "..") || strstr(path, "//") || path[0] == '/') {
        return false;
    }
    
    // Only allow relative paths in current directory or config subdirectory
    if (strncmp(path, "config/", 7) != 0 && strchr(path, '/') != nullptr) {
        return false;
    }
    
    FILE* f = fopen(path, "r");
    if (!f) return false;
    
    // Ensure file is closed on all exit paths
    auto cleanup = [&f]() { if (f) fclose(f); };
    struct FileGuard { std::function<void()> fn; ~FileGuard() { fn(); } } guard{cleanup};
    
    char line[256];
    int line_number = 0;
    
    while (fgets(line, sizeof(line), f)) {
        line_number++;
        
        // Skip comments and empty lines
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;
        
        // Remove trailing newline safely
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) {
            line[--len] = '\0';
        }
        
        char key[128], value[128];
        if (sscanf(line, "%127[^=]=%127s", key, value) != 2) {
            // Log invalid configuration line but continue
            continue;
        }
        
        // Trim whitespace from key
        char* key_end = key + strlen(key) - 1;
        while (key_end > key && (*key_end == ' ' || *key_end == '\t')) {
            *key_end-- = '\0';
        }
        
        // Network settings
        if (strcmp(key, "bind_address") == 0) {
            config.bind_address = value;
        } else if (strcmp(key, "port") == 0) {
            char* endptr;
            long port = strtol(value, &endptr, 10);
            if (*endptr == '\0' && port >= 1 && port <= 65535) {
                config.port = static_cast<uint16_t>(port);
            }
        }
        // Performance settings
        else if (strcmp(key, "ring_slots") == 0) {
            char* endptr;
            unsigned long slots = strtoul(value, &endptr, 10);
            if (*endptr == '\0' && slots > 0 && slots <= UINT32_MAX) {
                config.ring_slots = static_cast<uint32_t>(slots);
            }
        } else if (strcmp(key, "ring_elem_size") == 0) {
            char* endptr;
            unsigned long size = strtoul(value, &endptr, 10);
            if (*endptr == '\0' && size > 0 && size <= UINT32_MAX) {
                config.ring_elem_size = static_cast<uint32_t>(size);
            }
        } else if (strcmp(key, "flush_interval_ms") == 0) {
            char* endptr;
            unsigned long interval = strtoul(value, &endptr, 10);
            if (*endptr == '\0' && interval <= UINT32_MAX) {
                config.flush_interval_ms = static_cast<uint32_t>(interval);
            }
        }
        // System settings
        else if (strcmp(key, "tsc_hz") == 0) {
            double hz = atof(value);
            if (hz > 0.0) {
                config.tsc_hz = hz;
            }
        } else if (strcmp(key, "sleep_us") == 0) {
            int us = atoi(value);
            if (us >= 0) {
                config.sleep_us = static_cast<uint32_t>(us);
            }
        }
        // File paths
        else if (strcmp(key, "csv_output") == 0) {
            config.csv_output = value;
        } else if (strcmp(key, "ring_name") == 0) {
            config.ring_name = value;
        }
    }
    
    f = nullptr; // Prevent double close
    return true;
}

} // namespace ulmd