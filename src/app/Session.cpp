#include "app/Session.h"

#include <imgui.h>

#include "assembly/AssemblyFile.h"
#include "core/Log.h"
#include "core/Monitor.h"
#include "document/BzcFile.h"
#include "document/DocumentTypes.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <format>
#include <thread>

namespace bz::app {
namespace {

std::string NewAutosaveKey() {
    static std::atomic<int> counter{0};   // unique within this process
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    return std::format("{}-{}", std::chrono::duration_cast<std::chrono::milliseconds>(now).count(), ++counter);
}

} // namespace

struct Session::Background {
    doc::Document              doc;     ///< a copy, rebuilt on the other thread
    kernel::Model              model;   ///< a fork of the session's model
    std::size_t                start = 0;
    std::atomic<std::size_t>   done{0}, total{0};
    std::atomic<bool>          finished{false};
    std::thread                thread;
    std::chrono::steady_clock::time_point began = std::chrono::steady_clock::now();
    double                     rebuildMs = 0, meshMs = 0;   ///< for the monitor
};

Session::Session() : autosaveKey(NewAutosaveKey()) {}

Session::~Session() {
    if (myBackground && myBackground->thread.joinable()) myBackground->thread.join();
}

kernel::Model::Progress Session::RebuildProgress() const {
    if (myBackground) return {myBackground->done.load(), myBackground->total.load()};
    return model.RebuildProgress();
}

void Session::StartBackground(std::size_t limit) {
    auto job = std::make_unique<Background>();
    job->doc = doc.Clone();
    job->start = model.FirstToRebuild(doc);
    job->model = model.Fork(job->start);
    const auto shown = myShowAtLimit ? kernel::Model::Stopped::ShowAtStop : kernel::Model::Stopped::KeepShown;
    Background* b = job.get();
    b->thread = std::thread([b, limit, shown, quality = myMeshQuality] {
        // RebuildSlice keeps every OCCT error inside the operation that
        // caused it; anything else must not leave this thread.
        try {
            while (!b->model.RebuildSlice(b->doc, limit, shown, 0.0)) {
                const kernel::Model::Progress p = b->model.RebuildProgress();
                b->total = p.total;
                b->done = p.done;
            }
            const auto rebuilt = std::chrono::steady_clock::now();
            b->rebuildMs = std::chrono::duration<double, std::milli>(rebuilt - b->began).count();
            render::Viewport::PrepareMeshes(b->model, quality);
            b->meshMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - rebuilt).count();
        } catch (const std::exception& e) {
            log::Error("Background rebuild failed: {}", e.what());
        } catch (...) {   // an OCCT failure outside an operation (meshing)
            log::Error("Background rebuild failed");
        }
        b->finished = true;
    });
    myBackground = std::move(job);
}

bool Session::FinishBackground() {
    std::unique_ptr<Background> job = std::move(myBackground);
    job->thread.join();
    if (mon::Enabled())
        mon::Timed("rebuild", std::format("в фоне, операции {}..{}, сетка {:.0f} мс, всего до показа {:.0f} мс", job->start, doc.FeatureCount(),
                                          job->meshMs,
                                          std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - job->began).count()),
                   job->rebuildMs);
    // Made for the document as it is now: take everything. The document
    // changed meanwhile (a value dragged on): still show the shape it got
    // to, the features themselves stay to be rebuilt again.
    const bool current = job->doc.Revision() == doc.Revision();
    if (current) doc.TakeRebuilt(job->doc);
    model.Adopt(std::move(job->model), job->start);
    return current;
}

std::string Session::Title() const {
    if (assembly && !assembly->assembly.info.name.empty()) return assembly->assembly.info.name;
    if (!doc.info.name.empty()) return doc.info.name;
    if (path.empty()) return untitledName;
    const std::u8string stem = path.stem().u8string();
    return {stem.begin(), stem.end()};
}

void Session::Execute(std::unique_ptr<doc::Command> command) {
    mon::Action("command", command->Label());
    history.Execute(doc, std::move(command));
}
void Session::Undo() {
    mon::Action("undo", UndoLabel());
    if (assembly) assembly->Undo();
    else history.Undo(doc);
}
void Session::Redo() {
    mon::Action("redo", "");
    if (assembly) assembly->Redo();
    else history.Redo(doc);
}

AssemblyState& Session::MakeAssembly() {
    assembly = std::make_unique<AssemblyState>();
    assembly->geometry.ShowIn(model);
    return *assembly;
}

std::string_view Session::DocumentType() const { return assembly ? doc::kAssemblyType : doc::kPartType; }

std::uint64_t Session::ContentRevision() const {
    return assembly ? assembly->assembly.Revision() + assembly->assembly.LookRevision() : doc.Revision();
}

Status Session::WriteFile(const std::filesystem::path& target, std::string_view generator) const {
    if (!assembly) return doc::SaveBzc(doc, target, generator);
    if (path == target) return assembly::SaveAssembly(assembly->assembly, target, generator);
    Expected<assembly::Assembly> copy = assembly::AssemblyFromJson(assembly::AssemblyToJson(assembly->assembly));
    if (!copy) return copy.GetError();
    copy->look.pictures = assembly->assembly.look.pictures;   // not in the JSON
    assembly::RebasePaths(*copy, path, target);
    return assembly::SaveAssembly(*copy, target, generator);
}

void Session::MarkSaved() {
    if (assembly) assembly->MarkSaved();
    else history.MarkSaved();
}

void Session::CommitInPlaceEdit(std::unique_ptr<doc::Feature> before, std::string label) {
    mon::Action("edit", label);
    // Put the old version back and take the edited one, then run the edit
    // as a normal command so undo/redo and the saved-state marker work.
    std::unique_ptr<doc::Feature> edited = doc.Replace(std::move(before));
    if (!edited) return;
    history.Execute(doc, doc::ReplaceFeature(std::move(edited), std::move(label)));
}

bool Session::RebuildIfNeeded() { return Rebuild(40.0); }

bool Session::RebuildNow() { return Rebuild(-1.0); }

bool Session::Rebuild(double budgetMs) {
    if (assembly) {
        // Parts are rebuilt as they load; what is left is cheap (placing).
        prof::Scope scope("assembly");
        const bool changed = assembly->Update(path);
        myRebuilding = false;
        return changed;
    }
    bool adopted = false;
    if (myBackground) {
        if (budgetMs >= 0 && !myBackground->finished) return false;   // still at it; the window goes on
        if (FinishBackground()) myBuiltRevision = doc.Revision();
        myRebuilding = false;
        adopted = true;
    }
    // Something left over from a partial rebuild that is no longer limited.
    const std::size_t limit = myRebuildLimit ? *myRebuildLimit : static_cast<std::size_t>(-1);
    const bool leftOver = doc.DirtyFrom() < doc.FeatureCount() && doc.DirtyFrom() <= limit;
    if (doc.Revision() == myBuiltRevision && !leftOver) return adopted;
    // A change that needs no rebuild (the look of the part).
    if (doc.DirtyFrom() >= doc.FeatureCount() && !leftOver && model.Revision() > 0) {
        myBuiltRevision = doc.Revision();
        return adopted;
    }
    // While a handle or a value is being dragged, a part that takes long to
    // rebuild is rebuilt at most every so often, and once more on release:
    // the drag stays smooth instead of waiting for every step.
    const double now = ImGui::GetTime();
    if (budgetMs >= 0 && !myRebuilding && ImGui::IsMouseDown(ImGuiMouseButton_Left) && myLastRebuildSeconds > 0.04 &&
        now - myLastRebuildAt < std::max(0.25, 3 * myLastRebuildSeconds))
        return false;
    // Long work (a heavy boolean, a part just opened) goes to another
    // thread: one operation alone can take seconds and cannot be sliced.
    if (budgetMs >= 0 && myBackgroundAllowed && model.EstimateRebuildMs(doc, limit) > 60.0) {
        StartBackground(limit);
        myRebuilding = true;
        return adopted;
    }
    const auto start = std::chrono::steady_clock::now();
    if (!myRebuilding) {
        myRebuildBeganAt = ImGui::GetTime();
        myRebuildWorkMs = 0;
    }
    // A slice at a time: the window keeps answering while a large part
    // rebuilds; the part shown changes when the rebuild is done.
    const bool done = model.RebuildSlice(doc, limit, myShowAtLimit ? kernel::Model::Stopped::ShowAtStop : kernel::Model::Stopped::KeepShown,
                                         budgetMs);
    myLastRebuildSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    myLastRebuildAt = ImGui::GetTime();
    myBuiltRevision = doc.Revision();
    myRebuilding = !done;
    myRebuildWorkMs += myLastRebuildSeconds * 1000.0;
    if (done && mon::Enabled())
        mon::Timed("rebuild", std::format("в окне{}, от начала до конца {:.0f} мс", budgetMs < 0 ? " сразу" : " кусками",
                                          (myLastRebuildAt - myRebuildBeganAt) * 1000.0),
                   myRebuildWorkMs);
    return done;
}

} // namespace bz::app
