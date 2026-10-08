#include "platform/ProcessInfo.h"

#if defined(_WIN32)
#include <windows.h>
#include <psapi.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#else
#include <fstream>
#include <unistd.h>
#endif

namespace bz::platform {

double ResidentMemoryMb() {
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS counters{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)))
        return static_cast<double>(counters.WorkingSetSize) / (1024.0 * 1024.0);
    return 0.0;
#elif defined(__APPLE__)
    mach_task_basic_info info{};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &count) == KERN_SUCCESS)
        return static_cast<double>(info.resident_size) / (1024.0 * 1024.0);
    return 0.0;
#else
    std::ifstream statm("/proc/self/statm");
    long pages = 0, resident = 0;
    statm >> pages >> resident;
    return static_cast<double>(resident) * static_cast<double>(sysconf(_SC_PAGESIZE)) / (1024.0 * 1024.0);
#endif
}

} // namespace bz::platform
