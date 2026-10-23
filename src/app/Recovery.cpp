#include "app/Recovery.h"

#include "app/AppContext.h"
#include "app/Autosave.h"
#include "app/CrashGuard.h"
#include "app/Notifications.h"
#include "app/Session.h"
#include "app/Workspace.h"
#include "app/features/OperationEditors.h"
#include "app/sketch/SketchEditor.h"
#include "core/FaultGuard.h"
#include "core/I18n.h"
#include "core/Log.h"
#include "core/Monitor.h"
#include "platform/Window.h"
#include "render/Viewport.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cstdlib>
#include <format>
#include <stdexcept>

namespace bz::app {
namespace {

/// Failures this close together count as one failure repeating.
constexpr double kRepeatWindow = 5.0;
/// That many in the window: the home page is shown (the document's
/// interface or view keeps failing). Twice as many: the program gives up.
constexpr std::size_t kShowHomeAfter = 4;
constexpr std::size_t kGiveUpAfter = 2 * kShowHomeAfter;

/// Runs `work` with C++ exceptions and crashes both caught. True when it
/// ended normally; otherwise `reason` says why and `crashed` whether it was
/// a crash (an access violation...) rather than an exception.
bool Guarded(const std::function<void()>& work, std::string& reason, bool& crashed) {
    bool threw = false;
    std::string thrown;
    std::string fault;
    const bool survived = guard::RunGuarded(
        [&] {
            try {
                work();
            } catch (const std::exception& e) {
                threw = true;
                thrown = e.what();
            } catch (...) {   // OCCT's Standard_Failure and anything else
                threw = true;
                thrown = "unknown exception";
            }
        },
        &fault);
    crashed = !survived;
    if (!survived) reason = fault;
    else if (threw) reason = thrown;
    return survived && !threw;
}

/// The same, when only "did it work" matters (closing a tool during recovery).
bool Try(const std::function<void()>& work) {
    std::string reason;
    bool crashed = false;
    return Guarded(work, reason, crashed);
}

/// The sessions whose documents a failure in `tab` may touch: a part tab
/// itself, or an assembly's parts edited in it (the assembly's own state
/// is kept apart, see AssemblyState::Mark).
std::vector<Session*> DocumentsOf(Session* tab) {
    std::vector<Session*> out;
    if (!tab) return out;
    if (!tab->assembly) {
        out.push_back(tab);
        return out;
    }
    for (AssemblyState::PartEdit& e : tab->assembly->parts)
        if (e.session) out.push_back(e.session.get());
    return out;
}

bool TabOpen(const Workspace& workspace, const Session* tab) {
    return std::ranges::any_of(workspace.sessions, [&](const auto& s) { return s.get() == tab; });
}

} // namespace

void Recovery::Remember(AppContext& app) {
    if (myRecovering) return;
    Session* tab = app.workspace.ActiveTab();
    if (tab != myState.tab) {
        myState = {};
        myState.tab = tab;
    }
    if (!tab) return;

    const std::vector<Session*> sessions = DocumentsOf(tab);
    // Forget the ones gone (a part let go by its assembly).
    std::erase_if(myState.docs, [&](const DocState& d) { return std::ranges::find(sessions, d.session) == sessions.end(); });
    for (Session* s : sessions) {
        auto it = std::ranges::find_if(myState.docs, [&](const DocState& d) { return d.session == s; });
        const doc::History::Mark history = s->history.GetMark();
        if (it != myState.docs.end() && it->revision == s->doc.Revision() && it->history == history) continue;   // as it was
        DocState fresh{s, s->doc.Clone(), history, s->doc.Revision()};
        if (it != myState.docs.end()) *it = std::move(fresh);
        else myState.docs.push_back(std::move(fresh));
    }
    if (tab->assembly && (!myState.assembly || !tab->assembly->Matches(*myState.assembly)))
        myState.assembly = tab->assembly->GetMark(myState.assembly ? &*myState.assembly : nullptr);
    if (!tab->assembly) myState.assembly.reset();
}

bool Recovery::Run(AppContext& app, const char* stage, const std::function<void()>& work) {
    std::string reason;
    bool crashed = false;
    if (Guarded(work, reason, crashed)) return true;
    Recover(app, stage, reason, crashed);
    return false;
}

bool Recovery::RunInterface(AppContext& app, const std::function<void()>& work) {
    ImGuiErrorRecoveryState imgui;
    ImGui::ErrorRecoveryStoreState(&imgui);
    std::string reason;
    bool crashed = false;
    if (Guarded(work, reason, crashed)) return true;
    // Close what the interface left open (windows, groups, pushed styles),
    // quietly: these are not mistakes of the interface code to report.
    ImGuiIO& io = ImGui::GetIO();
    const bool asserts = io.ConfigErrorRecoveryEnableAssert, logs = io.ConfigErrorRecoveryEnableDebugLog,
               tips = io.ConfigErrorRecoveryEnableTooltip;
    io.ConfigErrorRecoveryEnableAssert = io.ConfigErrorRecoveryEnableDebugLog = io.ConfigErrorRecoveryEnableTooltip = false;
    const bool imguiBack = Try([&] { ImGui::ErrorRecoveryTryToRecoverState(&imgui); });
    io.ConfigErrorRecoveryEnableAssert = asserts;
    io.ConfigErrorRecoveryEnableDebugLog = logs;
    io.ConfigErrorRecoveryEnableTooltip = tips;
    if (!imguiBack) log::Error("Recovery: the interface state could not be put back");
    Recover(app, "интерфейс", reason, crashed);
    return false;
}

void Recovery::CloseTools(AppContext& app) {
    // An operation panel: cancelled as usual; if even that fails, dropped
    // without touching the document (it is put back below anyway).
    if (OperationEditor* open = app.ops.Active()) {
        Session* s = app.Doc();
        if (!s || !Try([&] { open->Cancel(app, *s); }) || open->IsOpen())
            for (OperationEditor* e : app.ops.All())
                if (e->IsOpen()) Try([&] { e->Drop(app); });
    }
    // A sketch being edited, a plane being picked: back to the model.
    for (Session* s : {app.Doc(), app.workspace.ActiveTab()}) {
        if (!s) continue;
        if (s->mode == EditMode::Sketch && !Try([&] { app.sketch.Finish(app, *s); })) {
            Try([&] { app.sketch.Reset(); });
            s->mode = EditMode::Model;
            s->activeSketch = {};
        }
        if (s->mode != EditMode::Model) s->mode = EditMode::Model;
    }
    // Picking in the view, measuring.
    app.shell.measuring = false;
    Try([&] {
        app.viewport.EndPickingOriginal();
        app.viewport.SetContextPicking(false);
        app.viewport.SetMeasuring(false);
    });
}

void Recovery::PutBack(AppContext& app) {
    Session* tab = myState.tab;
    if (!tab || !TabOpen(app.workspace, tab)) return;   // closed by the failing work: nothing to put back
    if (tab->assembly && myState.assembly && !tab->assembly->Matches(*myState.assembly))
        if (!Try([&] { tab->assembly->RollBack(*myState.assembly); }))
            log::Error("Recovery: the assembly could not be put back");
    for (DocState& d : myState.docs) {
        if (!app.workspace.IsOpen(d.session)) continue;
        Session& s = *d.session;
        if (s.doc.Revision() == d.revision && s.history.GetMark() == d.history) continue;   // untouched
        s.doc = d.doc.Clone();
        s.history.RollBack(d.history);
        // What the model holds was built for the changed document: built again.
        s.doc.MarkChanged(0);
        s.selectedFeature = {};
        s.selectedFaces.clear();
    }
}

void Recovery::Recover(AppContext& app, const char* stage, const std::string& reason, bool crashed) {
    const std::string last(mon::LastAction());
    if (myRecovering) {
        // Failing again while putting things back: nothing safe is left to try.
        log::Error("Recovery failed: {} in {} while recovering (last action: {})", reason, stage, last);
        crash::NoteReason(std::format("failure while recovering from a failure: {} in {}", reason, stage).c_str());
        std::abort();
    }
    myRecovering = true;
    ++myRecovered;
    log::Error("Recovered: {} in {} ({}; last action: {})", reason, stage, crashed ? "crash" : "exception", last);
    mon::Action("recovery", std::format("{}: {} ({})", stage, reason, last));

    CloseTools(app);
    PutBack(app);
    if (crashed) {
        // A crash may have left the view or the parts' geometry half-way
        // through a change: shown again from scratch.
        Try([&] { app.viewport.ForgetSynced(); });
        if (Session* tab = myState.tab; tab && TabOpen(app.workspace, tab) && tab->assembly)
            Try([&] { tab->assembly->geometry.ReloadParts(); });
    }
    app.Later([] {});   // a few more frames: the document is rebuilt and shown

    // The same thing failing again and again (every frame): the home page
    // takes the document's interface and view off the screen.
    const double now = platform::Window::Time();
    if (stage != myLastStage) myRecent.clear();   // repeating: the same stage failing again
    myLastStage = stage;
    myRecent.push_back(now);
    while (!myRecent.empty() && now - myRecent.front() > kRepeatWindow) myRecent.pop_front();
    if (myRecent.size() >= kGiveUpAfter) {
        log::Error("Recovery: failing again and again ({} times in {} s): giving up", myRecent.size(), kRepeatWindow);
        for (auto& s : app.workspace.sessions) Try([&] { app.autosave.WriteNow(*s, now); });
        crash::NoteReason(std::format("repeated failures: {} in {}", reason, stage).c_str());
        std::abort();
    }
    if (myRecent.size() >= kShowHomeAfter && !app.workspace.ShowsHome()) {
        app.workspace.ShowHome(true);
        app.notes.Error(Tr("Повторяющийся сбой: показана главная страница. Документы открыты во вкладках, работа сохранена в автокопии"));
        for (auto& s : app.workspace.sessions) Try([&] { app.autosave.WriteNow(*s, now); });
    } else {
        app.notes.Error(Tr("Внутренняя ошибка: последнее действие отменено, документ возвращён к состоянию до него. "
                           "Работу можно продолжать"));
    }
    myState.tab = nullptr;   // remembered afresh before the next piece of work
    myRecovering = false;
}

namespace fault_test {
namespace {
std::function<void()> gInterface;
}
void RequestInInterface(std::function<void()> work) { gInterface = std::move(work); }
void HitInInterface() {
    if (!gInterface) return;
    const std::function<void()> work = std::move(gInterface);
    gInterface = nullptr;
    work();
}
void Crash() {
    volatile int* nowhere = nullptr;
    *nowhere = 1;
    std::abort();   // not reached
}
} // namespace fault_test

} // namespace bz::app
