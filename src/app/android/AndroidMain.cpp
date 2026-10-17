// BreezeCAD on Android: the entry point. BreezeActivity (a NativeActivity,
// resources/android/java) loads libbreezecad.so; the glue calls android_main
// on a thread of its own, and the program runs there as on the desktop
// (app/Application) — window, input and drawing on that thread.
//
// Crashes (ADR-0004, "Сбои и зависания на Android", layer 1): the signal
// handler only writes the report (crash::ReportPath), as on Linux; the
// watchdog process shows it, and the next start checks it again.
#include "app/Application.h"
#include "app/BetaTesting.h"
#include "app/CrashGuard.h"
#include "core/Log.h"
#include "core/Monitor.h"
#include "platform/Android.h"

#include <android_native_app_glue.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>
#include <unwind.h>

#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>

namespace {

void Say(int fd, const char* text) { (void)!::write(fd, text, std::strlen(text)); }

struct Frames {
    void** at;
    int    count, max;
};

_Unwind_Reason_Code OneFrame(_Unwind_Context* context, void* arg) {
    auto* frames = static_cast<Frames*>(arg);
    const std::uintptr_t pc = _Unwind_GetIP(context);
    if (pc && frames->count < frames->max) frames->at[frames->count++] = reinterpret_cast<void*>(pc);
    return frames->count < frames->max ? _URC_NO_REASON : _URC_END_OF_STACK;
}

/// "  #3 libTKBO.so+0x1a2b3c (Name)" per frame: the library and the offset
/// in it (what the code is made from); dladdr is not strictly safe in a
/// signal handler, but the process is lost anyway and it is what bionic's
/// own tombstones do.
void Trace(int fd, const char* name) {
    Say(fd, "Crash: ");
    Say(fd, name);
    Say(fd, "\n");
    Say(fd, bz::app::crash::ReasonLine());
    Say(fd, "Last action: ");
    Say(fd, bz::mon::LastAction().data());
    Say(fd, "\nStack:\n");
    void* at[48];
    Frames frames{at, 0, 48};
    _Unwind_Backtrace(OneFrame, &frames);
    for (int i = 0; i < frames.count; ++i) {
        char line[512];
        Dl_info info{};
        if (dladdr(at[i], &info) && info.dli_fname) {
            const char* file = std::strrchr(info.dli_fname, '/');
            std::snprintf(line, sizeof line, "  #%d %s+0x%zx%s%s%s\n", i, file ? file + 1 : info.dli_fname,
                          static_cast<std::size_t>(reinterpret_cast<std::uintptr_t>(at[i]) - reinterpret_cast<std::uintptr_t>(info.dli_fbase)),
                          info.dli_sname ? " (" : "", info.dli_sname ? info.dli_sname : "", info.dli_sname ? ")" : "");
        } else {
            std::snprintf(line, sizeof line, "  #%d %p\n", i, at[i]);
        }
        Say(fd, line);
    }
}

void OnSignal(int signal) {
    const char* name = signal == SIGSEGV ? "SIGSEGV" : signal == SIGABRT ? "SIGABRT" : signal == SIGFPE ? "SIGFPE"
                     : signal == SIGBUS  ? "SIGBUS"  : signal == SIGILL  ? "SIGILL"  : "signal";
    bz::mon::CrashNote(name);
    if (const char* report = bz::app::crash::ReportPath(); report[0]) {
        const int fd = ::open(report, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0) {
            Say(fd, bz::app::crash::ReportHeader());
            Trace(fd, name);
            ::close(fd);
        }
    }
    // The system's own handler next: its tombstone and the exit reason
    // the next start reads (layer 3).
    std::signal(signal, SIG_DFL);
    std::raise(signal);
}

void CatchCrashes() {
    static char altStack[64 * 1024];   // a stack overflow is reported too
    stack_t ss{};
    ss.ss_sp = altStack;
    ss.ss_size = sizeof altStack;
    sigaltstack(&ss, nullptr);
    struct sigaction sa{};
    sa.sa_handler = OnSignal;
    sa.sa_flags = static_cast<int>(SA_ONSTACK | SA_RESETHAND);
    for (const int s : {SIGSEGV, SIGABRT, SIGFPE, SIGBUS, SIGILL}) sigaction(s, &sa, nullptr);
}

[[noreturn]] void OnTerminate() {
    std::string what = "unknown";
    if (const std::exception_ptr e = std::current_exception()) {
        try {
            std::rethrow_exception(e);
        } catch (const std::exception& x) {
            what = x.what();
        } catch (...) {
        }
    }
    bz::log::Error("Crash: uncaught exception: {} (last action: {})", what, bz::mon::LastAction());
    bz::mon::CrashNote("uncaught exception");
    bz::app::crash::NoteReason(("uncaught exception: " + what).c_str());
    std::abort();   // SIGABRT, reported by OnSignal with the reason
}

} // namespace

void android_main(android_app* state) {
    namespace android = bz::platform::android;
    android::SetApp(state);
    const std::filesystem::path data = android::DataDir();
    bz::log::OpenFile(data / "logs" / "breezecad.log");   // the app's log until Application opens its own
    android::UnpackAssets();
    android::SetWatchdogState("fg");

    bz::app::beta::BeginSession();
    bz::mon::StartFromEnvironment();
    bz::app::crash::Prepare();
    std::set_terminate(OnTerminate);
    CatchCrashes();

    int code = 1;
    {
        bz::app::Application app;
        if (app.Init({})) code = app.Run();
    }
    bz::mon::Stop(code);
    bz::app::beta::EndSession(code);
    android::SetWatchdogState("exit");
    bz::log::Info("BreezeCAD for Android: exit ({})", code);
    // The program ended by itself (not the system closing it): the activity goes too.
    if (!state->destroyRequested) {
        ANativeActivity_finish(state->activity);
        while (!state->destroyRequested) {
            android_poll_source* source = nullptr;
            if (ALooper_pollOnce(-1, nullptr, nullptr, reinterpret_cast<void**>(&source)) == ALOOPER_POLL_ERROR) break;
            if (source) source->process(state, source);
        }
    }
}
