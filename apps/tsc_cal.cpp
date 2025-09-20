#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <chrono>
#include <cinttypes>

#ifdef ULMD_LINUX
#include <x86intrin.h>
#include <time.h>
#include <sched.h>
#include <unistd.h>
#elif ULMD_MACOS
#include <sys/sysctl.h>
#include <mach/mach_time.h>
#ifdef __x86_64__
#include <x86intrin.h>
#endif
#endif

#ifdef ULMD_LINUX
static uint64_t get_cycles() {
    return __rdtsc();
}

static uint64_t get_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000000000ULL + ts.tv_nsec;
}

static void pin_to_core() {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(0, &cpuset);
    if (sched_setaffinity(0, sizeof(cpuset), &cpuset) != 0) {
        fprintf(stderr, "Warning: Failed to set CPU affinity, measurements may be less accurate\n");
    }
}

static void calibrate_linux() {
    pin_to_core();
    
    // Warm up
    for (int i = 0; i < 1000; i++) {
        get_cycles();
        get_ns();
    }
    
    uint64_t start_cycles = get_cycles();
    uint64_t start_ns = get_ns();
    
    std::this_thread::sleep_for(std::chrono::seconds(3));
    
    uint64_t end_cycles = get_cycles();
    uint64_t end_ns = get_ns();
    
    // Check for counter wraparound
    if (end_cycles < start_cycles || end_ns < start_ns) {
        fprintf(stderr, "Warning: Counter wraparound detected, using fallback values\n");
        printf("cycles_per_second=%" PRIu64 "\n", 1000000000ULL);
        printf("slope_ns_per_cycle=%.9f\n", 1.000000000);
        return;
    }
    
    uint64_t delta_cycles = end_cycles - start_cycles;
    uint64_t delta_ns = end_ns - start_ns;
    
    uint64_t cycles_per_second = (delta_cycles * 1000000000ULL) / delta_ns;
    double slope_ns_per_cycle = static_cast<double>(delta_ns) / static_cast<double>(delta_cycles);
    
    printf("cycles_per_second=%" PRIu64 "\n", cycles_per_second);
    printf("slope_ns_per_cycle=%.9f\n", slope_ns_per_cycle);
}

#elif ULMD_MACOS

static void calibrate_macos() {
#ifdef __x86_64__
    // Intel macOS - use TSC frequency from sysctl
    uint64_t tsc_freq = 0;
    size_t size = sizeof(tsc_freq);
    
    if (sysctlbyname("machdep.tsc.frequency", &tsc_freq, &size, nullptr, 0) == 0) {
        double slope_ns_per_cycle = 1e9 / static_cast<double>(tsc_freq);
        printf("cycles_per_second=%" PRIu64 "\n", tsc_freq);
        printf("slope_ns_per_cycle=%.9f\n", slope_ns_per_cycle);
    } else {
        // Fallback measurement
        mach_timebase_info_data_t timebase;
        mach_timebase_info(&timebase);
        
        uint64_t start_cycles = __rdtsc();
        uint64_t start_mach = mach_absolute_time();
        
        std::this_thread::sleep_for(std::chrono::seconds(3));
        
        uint64_t end_cycles = __rdtsc();
        uint64_t end_mach = mach_absolute_time();
        
        uint64_t delta_cycles = end_cycles - start_cycles;
        uint64_t delta_mach = end_mach - start_mach;
        uint64_t delta_ns = delta_mach * timebase.numer / timebase.denom;
        
        uint64_t cycles_per_second = (delta_cycles * 1000000000ULL) / delta_ns;
        double slope_ns_per_cycle = static_cast<double>(delta_ns) / static_cast<double>(delta_cycles);
        
        printf("cycles_per_second=%" PRIu64 "\n", cycles_per_second);
        printf("slope_ns_per_cycle=%.9f\n", slope_ns_per_cycle);
    }
#else
    // ARM64 macOS - use timebase frequency
    mach_timebase_info_data_t timebase;
    mach_timebase_info(&timebase);
    
    // On ARM64, mach_absolute_time returns timebase ticks
    uint64_t tb_freq = 1000000000ULL * timebase.denom / timebase.numer;
    double slope_ns_per_cycle = static_cast<double>(timebase.numer) / static_cast<double>(timebase.denom);
    
    printf("cycles_per_second=%" PRIu64 "\n", tb_freq);
    printf("slope_ns_per_cycle=%.9f\n", slope_ns_per_cycle);
#endif
}

#endif

int main() {
#ifdef ULMD_LINUX
    calibrate_linux();
#elif ULMD_MACOS
    calibrate_macos();
#else
    printf("cycles_per_second=%" PRIu64 "\n", 1000000000ULL);
    printf("slope_ns_per_cycle=%.9f\n", 1.000000000);
#endif
    return 0;
}