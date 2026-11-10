#pragma once

#ifdef _WIN32

#include <utility>
#define NOMINMAX
#include <windows.h>
#include <psapi.h>

struct rusage {
    struct {
        long tv_sec{};
        long tv_usec{};
    } ru_utime, ru_stime;
    long ru_maxrss{};
};

#ifndef RUSAGE_SELF
#define RUSAGE_SELF 0
#endif

inline int getrusage(int, rusage* usage) noexcept {
    if (!usage) return -1;
    FILETIME creation{}, exitTime{}, kernelTime{}, userTime{};
    if (!GetProcessTimes(GetCurrentProcess(), &creation, &exitTime, &kernelTime, &userTime)) return -1;
    auto toTimeval = [](const FILETIME& value) {
        const ULARGE_INTEGER ticks{value.dwLowDateTime, value.dwHighDateTime};
        const unsigned long long microseconds = ticks.QuadPart / 10;
        return std::pair<long, long>{static_cast<long>(microseconds / 1'000'000),
                                     static_cast<long>(microseconds % 1'000'000)};
    };
    const auto user = toTimeval(userTime);
    const auto kernel = toTimeval(kernelTime);
    usage->ru_utime.tv_sec = user.first;
    usage->ru_utime.tv_usec = user.second;
    usage->ru_stime.tv_sec = kernel.first;
    usage->ru_stime.tv_usec = kernel.second;

    PROCESS_MEMORY_COUNTERS counters{};
    counters.cb = sizeof(counters);
    if (!GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters))) return -1;
    usage->ru_maxrss = static_cast<long>(counters.PeakWorkingSetSize / 1024);
    return 0;
}

#else

#include <sys/resource.h>

#endif
