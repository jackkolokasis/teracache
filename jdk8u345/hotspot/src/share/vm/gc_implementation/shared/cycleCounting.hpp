/*
 * cycle_counting.hpp
 *
 *  Created on: Nov 27, 2023
 *      Author: perpap
 */

#ifndef _SHARE_GC_TERAHEAP_CYCLE_COUNTING_HPP_
#define _SHARE_GC_TERAHEAP_CYCLE_COUNTING_HPP_

#ifndef __GNUC__
# error "Works only on GCC"
#endif

#include <stdint.h>

#if defined(__x86_64__)

static inline uint64_t get_cycles()
{
    uint32_t low, high;
    asm volatile("rdtsc" : "=a" (low), "=d" (high));
    return ((uint64_t)high << 32) | low;
}

#elif defined(__arm__) || defined(__aarch64__)

static inline uint64_t get_cycles()
{
    uint64_t val;
    asm volatile("mrs %0, cntvct_el0" : "=r" (val));
    return val;
}

#else
#error "Unsupported architecture, x86-64 and AArch64 only."
#endif

#if defined(_WIN32)
#include <windows.h>

static inline uint64_t get_cpu_frequency() {
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    return frequency.QuadPart;
}

#elif defined(__linux__)
#include <stdio.h>

static inline uint64_t get_cpu_frequency() {
    FILE *fp = fopen("/proc/cpuinfo", "r");
    if (!fp) return 0;

    char buffer[1024];
    uint64_t frequency = 0;

    while (fgets(buffer, sizeof(buffer), fp)) {
        if (sscanf(buffer, "cpu MHz : %lu", &frequency)) {
            frequency *= 1000000; // Convert MHz to Hz
            break;
        }
    }

    fclose(fp);
    return frequency;
}

#else
#error "Unsupported platform"
#endif

static inline uint64_t get_cycles_per_second() {
    static uint64_t cpu_frequency = 0;
    if (cpu_frequency == 0) {
        cpu_frequency = get_cpu_frequency();
        if (cpu_frequency == 0) {
            // Fallback to a default value if detection fails
            cpu_frequency = 3000000000; // 3 GHz, ampere max clock via cat /sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq
        }
    }
    return cpu_frequency;
}

#endif /* _SHARE_GC_TERAHEAP_CYCLE_COUNTING_HPP_ */
