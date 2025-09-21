#pragma once

#include <cstdint>
#include <cstddef>

namespace ulmd {

/**
 * @brief Write line to file with buffered I/O
 * @param path File path to write to
 * @param line Line content to write (newline added automatically)
 * @return 0 on success, -1 on error
 * @invariant File created if not exists, line appended with newline
 * @performance O(1) buffered write, periodic flush
 */
int write_line(const char* path, const char* line);

/**
 * @brief Convert TSC cycles to nanoseconds
 * @param cycles TSC cycle count
 * @param tsc_hz TSC frequency in Hz
 * @return Nanoseconds as double
 * @invariant tsc_hz > 0 for valid conversion
 * @performance O(1) floating point operation
 */
inline double cycles_to_ns(uint64_t cycles, double tsc_hz) {
    return static_cast<double>(cycles) * 1e9 / tsc_hz;
}

/**
 * @brief Get CSV header string for latency logging
 * @return Pointer to static CSV header string
 * @invariant Returns exact header per test harness specification
 * @performance O(1) constant access
 */
const char* get_csv_header();

/**
 * @brief Sanitize string for safe logging
 * @param input Input string to sanitize
 * @param output Output buffer for sanitized string
 * @param output_size Size of output buffer
 */
void sanitize_for_log(const char* input, char* output, size_t output_size);

/**
 * @brief Log error message with timestamp
 * @param component Component name (e.g., "parser", "ingress")
 * @param error_msg Error message
 * @return 0 on success, -1 on failure
 */
int log_error(const char* component, const char* error_msg);

} // namespace ulmd