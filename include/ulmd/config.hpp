#pragma once

#include <cstdint>
#include <string>

namespace ulmd {

struct Config {
    // Network settings
    std::string bind_address = "127.0.0.1";
    uint16_t port = 12345;
    
    // Performance settings
    uint32_t ring_slots = 1024;
    uint32_t ring_elem_size = 128;
    uint32_t flush_interval_ms = 1000;
    
    // System settings
    double tsc_hz = 3000000000.0;
    uint32_t sleep_us = 10;
    
    // File paths
    std::string csv_output = "output.csv";
    std::string ring_name = "ulmd_ring";
};

/**
 * @brief Load configuration from file
 * @param path Configuration file path
 * @param config Configuration struct to populate
 * @return true on success, false on failure
 */
bool load_config(const char* path, Config& config);

} // namespace ulmd