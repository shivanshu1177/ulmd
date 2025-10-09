#include <ulmd/shutdown.hpp>
#include <signal.h>
#include <atomic>
#include <mutex>

namespace ulmd {

static std::vector<ShutdownHandler> shutdown_handlers;
static std::mutex shutdown_mutex;
static std::atomic<bool> shutdown_in_progress{false};
static std::atomic<bool> shutdown_flag{false};

static void signal_handler(int sig) {
    (void)sig;
    shutdown_flag.store(true, std::memory_order_relaxed);
}

void register_shutdown_handler(ShutdownHandler handler) {
    std::lock_guard<std::mutex> lock(shutdown_mutex);
    shutdown_handlers.push_back(handler);
}

void execute_shutdown_handlers() {
    // Prevent multiple simultaneous shutdown executions
    bool expected = false;
    if (!shutdown_in_progress.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        return; // Shutdown already in progress
    }
    
    std::lock_guard<std::mutex> lock(shutdown_mutex);
    for (auto it = shutdown_handlers.rbegin(); it != shutdown_handlers.rend(); ++it) {
        try {
            (*it)();
        } catch (...) {
            // Ignore exceptions during shutdown
        }
    }
    
    shutdown_in_progress.store(false, std::memory_order_release);
}

void install_shutdown_signals() {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
}

bool shutdown_requested() {
    return shutdown_flag.load(std::memory_order_relaxed);
}

} // namespace ulmd