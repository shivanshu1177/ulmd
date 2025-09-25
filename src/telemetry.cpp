#include <ulmd/telemetry.hpp>
#include <cstdio>
#include <cinttypes>
#include <chrono>

namespace ulmd {
    void sanitize_for_log(const char* input, char* output, size_t output_size) {
        if (!input || !output || output_size == 0) return;
        
        size_t i = 0, j = 0;
        while (input[i] && j < output_size - 1) {
            char c = input[i];
            // Strict sanitization - only allow safe printable ASCII
            if (c >= 32 && c <= 126 && c != '\\' && c != '"' && c != '\'' && 
                c != '<' && c != '>' && c != '&' && c != '%' && c != '$') {
                output[j++] = c;
            } else {
                output[j++] = '_';
            }
            i++;
        }
        output[j] = '\0';
    }

    static bool is_safe_path(const char* path) {
        if (!path || strlen(path) == 0 || strlen(path) > 255) return false;
        
        // Reject paths with traversal sequences or dangerous patterns
        if (strstr(path, "../") || strstr(path, "..\\")
            || strstr(path, "/etc/") || strstr(path, "/root/")
            || strstr(path, "/home/") || strstr(path, "/usr/")
            || strstr(path, "/var/") || strstr(path, "/sys/")
            || strstr(path, "/proc/") || strstr(path, "/dev/")
            || path[0] != '/' || strstr(path, "//")) {
            return false;
        }
        
        // Only allow /tmp/ paths with safe characters
        if (strncmp(path, "/tmp/", 5) != 0) return false;
        
        // Validate characters in path
        for (const char* p = path; *p; p++) {
            if (!isalnum(*p) && *p != '/' && *p != '_' && *p != '-' && *p != '.') {
                return false;
            }
        }
        
        return true;
    }
    
    int log_info(const char* component, const char* info_msg) {
        if (!component || !info_msg) return -1;
        
        char safe_component[128], safe_msg[512];
        sanitize_for_log(component, safe_component, sizeof(safe_component));
        sanitize_for_log(info_msg, safe_msg, sizeof(safe_msg));
        
        FILE* f = fopen("/tmp/ulmd_info.log", "a");
        if (!f) return -1;
        
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        
        int result = fprintf(f, "[%" PRId64 "] INFO %.100s: %.400s\n", static_cast<int64_t>(time_t), safe_component, safe_msg);
        if (result < 0) {
            fclose(f);
            return -1;
        }
        
        if (fclose(f) != 0) return -1;
        return 0;
    }
    
    int log_warning(const char* component, const char* warning_msg) {
        if (!component || !warning_msg) return -1;
        
        char safe_component[128], safe_msg[512];
        sanitize_for_log(component, safe_component, sizeof(safe_component));
        sanitize_for_log(warning_msg, safe_msg, sizeof(safe_msg));
        
        FILE* f = fopen("/tmp/ulmd_warnings.log", "a");
        if (!f) return -1;
        
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        
        int result = fprintf(f, "[%" PRId64 "] WARN %.100s: %.400s\n", static_cast<int64_t>(time_t), safe_component, safe_msg);
        if (result < 0) {
            fclose(f);
            return -1;
        }
        
        if (fclose(f) != 0) return -1;
        return 0;
    }
    
    int write_line(const char* path, const char* line) {
        if (!is_safe_path(path) || !line) return -1;
        
        FILE* f = fopen(path, "a");
        if (!f) return -1;
        
        char safe_line[1024];
        sanitize_for_log(line, safe_line, sizeof(safe_line));
        
        int result = fprintf(f, "%s\n", safe_line);
        if (result < 0) {
            fclose(f);
            return -1;
        }
        
        if (fclose(f) != 0) return -1;
        return 0;
    }
    
    const char* get_csv_header() {
        return "seq_no,symbol,recv_tsc,enq_tsc,deq_tsc,recv_ns,egress_ns,lat_ingress_to_worker_ns,lat_parse_to_worker_ns,ingress_core,worker_core,ring_slots,ring_occupancy_max,dropped,crc_ok";
    }
    
    int log_error(const char* component, const char* error_msg) {
        if (!component || !error_msg) return -1;
        
        char safe_component[128], safe_msg[512];
        sanitize_for_log(component, safe_component, sizeof(safe_component));
        sanitize_for_log(error_msg, safe_msg, sizeof(safe_msg));
        
        FILE* f = fopen("/tmp/ulmd_errors.log", "a");
        if (!f) return -1;
        
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        
        int result = fprintf(f, "[%" PRId64 "] %.100s: %.400s\n", static_cast<int64_t>(time_t), safe_component, safe_msg);
        if (result < 0) {
            fclose(f);
            return -1;
        }
        
        if (fclose(f) != 0) return -1;
        return 0;
    }
}
