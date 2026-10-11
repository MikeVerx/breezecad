#include "core/FaultGuard.h"

#include <format>

#if defined(_WIN32)
#include <windows.h>
#else
#include <csetjmp>
#include <csignal>
#include <iterator>
#include <mutex>
#endif

namespace bz::guard {
namespace {

#if defined(_WIN32)

// MinGW: GCC's own non-local jump, which restores the registers and the
// stack of RunGuarded without unwinding (nothing to unwind through: the
// fault is not a C++ exception).
struct Landing {
    void* buffer[5];
};
thread_local Landing* tlLanding = nullptr;
thread_local unsigned long tlCode = 0;

/// Where a guarded fault resumes: a normal frame, outside the exception
/// dispatcher, that jumps back into RunGuarded.
[[noreturn]] __attribute__((noinline)) void JumpHome(Landing* landing) { __builtin_longjmp(landing->buffer, 1); }

LONG CALLBACK OnFault(EXCEPTION_POINTERS* info) {
    if (!tlLanding) return EXCEPTION_CONTINUE_SEARCH;
    const DWORD code = info->ExceptionRecord->ExceptionCode;
    switch (code) {
        case EXCEPTION_ACCESS_VIOLATION:
        case EXCEPTION_IN_PAGE_ERROR:
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
        case EXCEPTION_DATATYPE_MISALIGNMENT:
        case EXCEPTION_INT_DIVIDE_BY_ZERO:
        case EXCEPTION_ILLEGAL_INSTRUCTION:
        case EXCEPTION_PRIV_INSTRUCTION: break;
        default: return EXCEPTION_CONTINUE_SEARCH;   // C++ exceptions, stack overflow, …
    }
    Landing* landing = tlLanding;
    tlLanding = nullptr;
    tlCode = code;
    // Not a jump from inside the handler: the system resumes the thread
    // (the documented way out of a vectored handler) in JumpHome, on the
    // faulting stack, below the red zone, aligned as right after a call.
    CONTEXT* c = info->ContextRecord;
    c->Rcx = reinterpret_cast<DWORD64>(landing);
    c->Rsp = ((c->Rsp - 512) & ~static_cast<DWORD64>(15)) - 8;
    c->Rip = reinterpret_cast<DWORD64>(&JumpHome);
    return EXCEPTION_CONTINUE_EXECUTION;
}

void Install() {
    static const PVOID handler = AddVectoredExceptionHandler(1, OnFault);
    (void)handler;
}

std::string Describe() { return std::format("исключение 0x{:08x}", tlCode); }

#else

struct Landing {
    sigjmp_buf buffer;
};
thread_local Landing* tlLanding = nullptr;
thread_local int tlSignal = 0;

constexpr int kSignals[] = {SIGSEGV, SIGBUS, SIGFPE, SIGILL};
struct sigaction gPrevious[sizeof kSignals / sizeof kSignals[0]];

void OnFault(int signal, siginfo_t*, void*) {
    if (Landing* landing = tlLanding) {
        tlLanding = nullptr;
        tlSignal = signal;
        siglongjmp(landing->buffer, 1);
    }
    // Not ours: hand the signal to whoever had it before (the crash report
    // in main); returning re-runs the faulting instruction under it.
    for (std::size_t i = 0; i < std::size(kSignals); ++i)
        if (kSignals[i] == signal) sigaction(signal, &gPrevious[i], nullptr);
}

/// Ours again if someone set their own handler since (a test runner does
/// for every test); theirs is kept to hand faults outside RunGuarded to.
void Install() {
    static std::mutex mutex;
    const std::lock_guard lock(mutex);
    for (std::size_t i = 0; i < std::size(kSignals); ++i) {
        struct sigaction now{};
        sigaction(kSignals[i], nullptr, &now);
        if ((now.sa_flags & SA_SIGINFO) && now.sa_sigaction == OnFault) continue;
        struct sigaction sa{};
        sa.sa_sigaction = OnFault;
        sa.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_NODEFER;
        sigemptyset(&sa.sa_mask);
        sigaction(kSignals[i], &sa, &gPrevious[i]);
    }
}

std::string Describe() {
    switch (tlSignal) {
        case SIGSEGV: return "SIGSEGV";
        case SIGBUS:  return "SIGBUS";
        case SIGFPE:  return "SIGFPE";
        case SIGILL:  return "SIGILL";
        default:      return std::format("сигнал {}", tlSignal);
    }
}

#endif

/// Puts the previous landing back however RunGuarded is left.
struct Restore {
    Landing* previous;
    ~Restore() { tlLanding = previous; }
};

} // namespace

__attribute__((noinline)) bool RunGuarded(const std::function<void()>& work, std::string* what) {
    Install();
    Landing landing;
    const Restore restore{tlLanding};
#if defined(_WIN32)
    if (__builtin_setjmp(landing.buffer)) {
#else
    if (sigsetjmp(landing.buffer, 1)) {
#endif
        if (what) *what = Describe();
        return false;
    }
    tlLanding = &landing;
    work();
    return true;
}

} // namespace bz::guard
