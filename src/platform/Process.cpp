// SPDX-License-Identifier: MPL-2.0
#include "platform/Process.h"

#if defined(_WIN32)
#include <windows.h>
#else
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace bz::platform {

#if defined(_WIN32)

namespace {

/// One argument as the C runtime reads it back (CommandLineToArgvW rules).
std::wstring Quoted(const std::wstring& arg) {
    std::wstring out = L"\"";
    std::size_t slashes = 0;
    for (const wchar_t c : arg) {
        if (c == L'\\') {
            ++slashes;
            continue;
        }
        if (c == L'"') out.append(slashes * 2 + 1, L'\\');
        else out.append(slashes, L'\\');
        slashes = 0;
        out += c;
    }
    out.append(slashes * 2, L'\\');
    return out + L'"';
}

std::wstring Wide(const std::string& utf8) {
    const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    std::wstring out(static_cast<std::size_t>(n > 0 ? n - 1 : 0), L'\0');
    if (n > 1) MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, out.data(), n);
    return out;
}

} // namespace

ProcessId CurrentProcessId() { return static_cast<ProcessId>(GetCurrentProcessId()); }

bool StartProcess(const std::filesystem::path& program, const std::vector<std::string>& args, ProcessId* id) {
    std::wstring line = Quoted(program.wstring());
    for (const std::string& a : args) line += L" " + Quoted(Wide(a));
    STARTUPINFOW si{};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(program.c_str(), line.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) return false;
    if (id) *id = static_cast<ProcessId>(pi.dwProcessId);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

bool ProcessRunning(ProcessId id) {
    HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(id));
    if (!process) return false;
    const bool running = WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
    CloseHandle(process);
    return running;
}

void StopProcess(ProcessId id) {
    if (HANDLE process = OpenProcess(PROCESS_TERMINATE, FALSE, static_cast<DWORD>(id))) {
        TerminateProcess(process, 1);
        CloseHandle(process);
    }
}

#else

ProcessId CurrentProcessId() { return static_cast<ProcessId>(::getpid()); }

bool StartProcess(const std::filesystem::path& program, const std::vector<std::string>& args, ProcessId* id) {
    // Everything execv needs, made before fork: after it only execv and _exit.
    const std::string path = program.string();
    std::vector<char*> argv;
    argv.push_back(const_cast<char*>(path.c_str()));
    for (const std::string& a : args) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);
    const pid_t child = ::fork();
    if (child < 0) return false;
    if (child == 0) {
        ::setsid();   // its own session: a terminal closing or Ctrl+C there does not end it
        ::execv(path.c_str(), argv.data());
        ::_exit(127);
    }
    if (id) *id = static_cast<ProcessId>(child);
    return true;
}

bool ProcessRunning(ProcessId id) {
    const auto pid = static_cast<pid_t>(id);
    int status = 0;
    if (::waitpid(pid, &status, WNOHANG) == pid) return false;   // our child, just finished (collected now)
    return ::kill(pid, 0) == 0;
}

void StopProcess(ProcessId id) {
    const auto pid = static_cast<pid_t>(id);
    if (::kill(pid, SIGKILL) == 0) {
        int status = 0;
        ::waitpid(pid, &status, WNOHANG);   // ours: no zombie left; another's: nothing happens
    }
}

#endif

} // namespace bz::platform
