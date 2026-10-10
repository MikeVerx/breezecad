#include "app/Session.h"

#include <imgui.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <format>

namespace bz::app {
namespace {

std::string NewAutosaveKey() {
    static std::atomic<int> counter{0};   // unique within this process
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    return std::format("{}-{}", std::chrono::duration_cast<std::chrono::milliseconds>(now).count(), ++counter);
}

} // namespace

Session::Session() : autosaveKey(NewAutosaveKey()) {}

std::string Session::Title() const {
    if (!doc.info.name.empty()) return doc.info.name;
    if (path.empty()) return untitledName;
    const std::u8string stem = path.stem().u8string();
    return {stem.begin(), stem.end()};
}

void Session::Execute(std::unique_ptr<doc::Command> command) { history.Execute(doc, std::move(command)); }
void Session::Undo() { history.Undo(doc); }
void Session::Redo() { history.Redo(doc); }

void Session::CommitInPlaceEdit(std::unique_ptr<doc::Feature> before, std::string label) {
    // Put the old version back and take the edited one, then run the edit
    // as a normal command so undo/redo and the saved-state marker work.
    std::unique_ptr<doc::Feature> edited = doc.Replace(std::move(before));
    if (!edited) return;
    history.Execute(doc, doc::ReplaceFeature(std::move(edited), std::move(label)));
}

bool Session::RebuildIfNeeded() {
    if (doc.Revision() == myBuiltRevision) return false;
    // While a handle or a value is being dragged, a part that takes long to
    // rebuild is rebuilt at most every so often, and once more on release:
    // the drag stays smooth instead of waiting for every step.
    const double now = ImGui::GetTime();
    if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && myLastRebuildSeconds > 0.04 &&
        now - myLastRebuildAt < std::max(0.25, 3 * myLastRebuildSeconds))
        return false;
    const auto start = std::chrono::steady_clock::now();
    model.Rebuild(doc);
    myLastRebuildSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    myLastRebuildAt = ImGui::GetTime();
    myBuiltRevision = doc.Revision();
    return true;
}

} // namespace bz::app
