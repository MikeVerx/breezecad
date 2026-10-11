#pragma once
// The testers' build ("BreezeCAD for beta testers"): the same program, with a
// file `beta-tester.txt` next to it. Then every run keeps a session folder —
// the log, monitoring events (actions, slow frames, hangs, memory), the crash
// dump, a description of the computer — and after a crash the next start asks
// what the tester was doing and packs it all into one zip to send by hand.
// Nothing leaves the computer on its own.

#include "core/Expected.h"

#include <filesystem>
#include <string>
#include <vector>

namespace bz::platform {
class Window;
}

namespace bz::app::beta {

/// The testers' build (beta-tester.txt next to the program).
bool Enabled();
/// "Отчёты для разработчика" next to the program, or in Documents when the
/// program's folder is read-only.
std::filesystem::path ReportsDir();

/// Starts this run's session folder and points the monitoring at it (sets
/// BZ_MONITOR): call in main before mon::StartFromEnvironment. Nothing when
/// not the testers' build.
void BeginSession();
/// This run's folder, empty when not the testers' build.
const std::filesystem::path& SessionDir();
/// The computer, its graphics and the program's components → system.json.
/// Once the window (and OpenGL) exists.
void WriteSystemInfo(const platform::Window& window, const std::string& language);
/// A normal end: the session is not a crash.
void EndSession(int exitCode);

struct PastSession {
    std::filesystem::path dir;
    std::string           started;   ///< "2026-10-11 16:28"
};
/// Earlier sessions that ended without EndSession (a crash, a kill) and were
/// not reported or dismissed yet, newest first.
std::vector<PastSession> UnreportedCrashes();
/// Do not ask about it again.
void Dismiss(const std::filesystem::path& sessionDir);
/// One zip with the session and the tester's words, in ReportsDir().
Expected<std::filesystem::path> Pack(const std::filesystem::path& sessionDir, const std::string& description, bool crash);

} // namespace bz::app::beta
