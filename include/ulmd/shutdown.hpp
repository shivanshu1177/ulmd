#pragma once

#include <functional>
#include <vector>

namespace ulmd {

/**
 * @brief Shutdown handler function type
 */
using ShutdownHandler = std::function<void()>;

/**
 * @brief Register shutdown handler
 * @param handler Function to call on shutdown
 */
void register_shutdown_handler(ShutdownHandler handler);

/**
 * @brief Execute all registered shutdown handlers
 */
void execute_shutdown_handlers();

/**
 * @brief Install signal handlers for graceful shutdown
 */
void install_shutdown_signals();

/**
 * @brief Check if shutdown was requested via signal
 * @return true if shutdown signal received
 */
bool shutdown_requested();

} // namespace ulmd