#include "app/Session.h"

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
    model.Rebuild(doc);
    myBuiltRevision = doc.Revision();
    return true;
}

} // namespace bz::app
