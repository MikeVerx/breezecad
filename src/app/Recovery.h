#pragma once
// SPDX-License-Identifier: MPL-2.0
// One tool failing must cost that tool, not the program (the author,
// 23.10.2026: «должен просто падать инструмент и всё быстро возвращается на
// круги своя»). Every piece of work the window loop runs — a shortcut, the
// interface of a frame, the update of the document in front, work put off
// to the end of the frame, a command from a module — runs through Run().
// When it throws, or crashes outright (an access violation, caught by
// core/FaultGuard.h), the program does not end:
//   - the open tool is closed (an operation panel, a sketch, picking);
//   - the documents in front are put back as they were before that piece
//     of work (Remember): the part's features and undo history, the
//     assembly with its parts edited in it;
//   - the user is told in one line; the log says where and why.
// A failure repeating over and over (the same thing failing every frame)
// shows the home page; only when even that keeps failing does the program
// give up (crash window, with the autosave keeping the work).
//
// What a crash had on its stack is not destroyed (core/FaultGuard.h): a
// leak, accepted for keeping the user's work and the program running.

#include "app/AssemblyState.h"
#include "document/Commands.h"
#include "document/Document.h"

#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace bz::app {

struct AppContext;
class Session;

class Recovery {
public:
    /// Remembers the documents in front (the tab, the parts edited in it)
    /// as they are now: what a failure goes back to. Cheap when nothing
    /// changed since the last call (revisions are compared).
    void Remember(AppContext& app);

    /// Runs `work`. True when it ended normally; false when it failed and
    /// was recovered from (see above). `stage` names it for the log.
    bool Run(AppContext& app, const char* stage, const std::function<void()>& work);

    /// Run() for drawing the interface: ImGui's windows, groups and styles
    /// left open by the failure are closed again, so the frame can end.
    bool RunInterface(AppContext& app, const std::function<void()>& work);

    /// Failures recovered from in this run.
    int Recovered() const { return myRecovered; }

private:
    struct DocState {
        Session*              session = nullptr;
        doc::Document         doc;
        doc::History::Mark    history;
        std::uint64_t         revision = 0;
    };
    struct State {
        Session*                          tab = nullptr;
        std::vector<DocState>             docs;
        std::optional<AssemblyState::Mark> assembly;
    };

    void Recover(AppContext& app, const char* stage, const std::string& reason, bool crashed);
    void CloseTools(AppContext& app);
    void PutBack(AppContext& app);

    State              myState;
    std::deque<double> myRecent;   ///< when the last failures (of the same stage) happened
    std::string        myLastStage;
    int                myRecovered = 0;
    bool               myRecovering = false;
};

namespace fault_test {
/// Test hooks (BZ_FAULT_INJECTION, the debug.fault command): `work` runs
/// in the next interface frame, halfway through drawing (it fails there).
void RequestInInterface(std::function<void()> work);
/// Called by the interface each frame; does what was requested, once.
void HitInInterface();
/// A crash (a write through a null pointer the compiler cannot see).
[[noreturn]] void Crash();
} // namespace fault_test

} // namespace bz::app
