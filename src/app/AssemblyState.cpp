#include "app/AssemblyState.h"

#include "assembly/AssemblyFile.h"

namespace bz::app {
namespace {
constexpr std::size_t kMaxUndo = 200;
}

void AssemblyState::Checkpoint(std::string label) {
    myUndo.push_back({assembly::AssemblyToJson(assembly), std::move(label)});
    if (myUndo.size() > kMaxUndo) myUndo.erase(myUndo.begin());
    myRedo.clear();
}

void AssemblyState::Restore(const std::string& json) {
    Expected<assembly::Assembly> loaded = assembly::AssemblyFromJson(json);
    if (!loaded) return;
    // Keep this object (its revision keeps counting up, so everything that
    // watches it notices) and take the contents.
    assembly.info = loaded->info;
    assembly.components = std::move(loaded->components);
    assembly.constraints = std::move(loaded->constraints);
    assembly.Ids().Reserve(ObjectId{loaded->Ids().Last()});
    assembly.Touch();
    if (!assembly.FindComponent(selected) && !assembly.FindConstraint(selected)) selected = {};
}

bool AssemblyState::Undo() {
    if (myUndo.empty()) return false;
    myRedo.push_back({assembly::AssemblyToJson(assembly), myUndo.back().label});
    Restore(myUndo.back().json);
    myUndo.pop_back();
    return true;
}

bool AssemblyState::Redo() {
    if (myRedo.empty()) return false;
    myUndo.push_back({assembly::AssemblyToJson(assembly), myRedo.back().label});
    Restore(myRedo.back().json);
    myRedo.pop_back();
    return true;
}

bool AssemblyState::Update(const std::filesystem::path& file) {
    const std::uint64_t before = geometry.Shown().Revision();
    geometry.Update(assembly, file, &problems);
    if (assembly.Revision() != mySolvedRevision) {
        mySolvedRevision = assembly.Revision();
        geometry.Update(assembly, file);   // the solver moved components: show them there
    }
    return geometry.Shown().Revision() != before;
}

} // namespace bz::app
