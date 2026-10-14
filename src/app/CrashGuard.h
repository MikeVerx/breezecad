#pragma once
// SPDX-License-Identifier: MPL-2.0
// When the program crashes or stops responding, the user sees a window
// saying so, with an error code to copy and the report to send: not a
// window that silently vanishes or greys out. That window is another
// process of this program (shell/CrashWindow.h): a crashed or frozen
// process cannot draw.
//
// The report is a text file: a header (program, build, graphics, start
// time), then "Crash: SIGSEGV" or "Hang: …", "Reason: …" (an uncaught
// exception), "Last action: …" and, after a crash, "Stack:" with one frame a
// line. core/CrashCode.h turns it into the code shown to the user. Testers'
// builds keep it in the session folder (crash.txt, hang.txt, packed into the
// report zip); other builds in <settings folder>/reports. Nothing is sent
// anywhere: the user decides.

#include <string>

namespace bz::app::crash {

/// Everything the crash handlers need, made in advance (paths, the command
/// line of the crash window): they may not allocate. Call in main after
/// beta::BeginSession, before installing the handlers.
void Prepare();
/// Adds the graphics card and its driver to the report header.
void NoteGraphics(const std::string& graphics);
/// The cause, when it is known before the program ends (an uncaught
/// exception): written into the report as "Reason: …".
void NoteReason(const char* reason);

// --- For the crash handlers: no allocation, no locks. ---

/// This run's crash report file, UTF-8 / native narrow ("" before Prepare).
const char* ReportPath();
#if defined(_WIN32)
const wchar_t* ReportPathWide();
#endif
/// The report's first lines ("BreezeCAD crash report", version, graphics, start).
const char* ReportHeader();
/// "Reason: …\n" or "".
const char* ReasonLine();
/// Starts the crash window for the written report. Only once per run: false
/// the second time (an uncaught exception reported, then its abort signal).
bool ShowCrashWindow();

// --- The hang window. ---

/// Watches the window's loop (mon::Beat) from a thread of its own: after
/// `seconds` without a beat, writes a hang report and shows the hang
/// window ("not responding": wait, or end the program); closes it again if
/// the loop comes back. Call when the main loop starts.
void StartHangGuard(double seconds);
void StopHangGuard();

} // namespace bz::app::crash
