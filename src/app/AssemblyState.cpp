#include "app/AssemblyState.h"

#include "assembly/AssemblyFile.h"
#include "assembly/Solver.h"
#include "core/Monitor.h"
#include "core/Profile.h"

#include <chrono>
#include <format>

namespace bz::app {
namespace {
constexpr std::size_t kMaxUndo = 200;
}

void AssemblyState::Checkpoint(std::string label) {
    mon::Action("assembly", label);
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
    {
        prof::Scope scope("asm.parts");
        const auto start = std::chrono::steady_clock::now();
        geometry.Update(assembly, file, &problems);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        if (ms > 5.0) mon::Timed("assembly.parts", std::format("компонентов {}", assembly.components.size()), ms);
    }
    if (assembly.Revision() != mySolvedRevision) {
        prof::Scope scope("asm.solve");
        // While a component is dragged it stays near the cursor and the
        // ones tied to it follow; otherwise the smallest change wins.
        assembly::SolveOptions options;
        if (drag.moving) options.keep = drag.component;
        if (assembly::Solve(assembly, options).moved) assembly.Touch();
        freedom = assembly::AnalyzeFreedom(assembly);
        mySolvedRevision = assembly.Revision();
        geometry.Update(assembly, file);   // show them where the solver put them
    }
    return geometry.Shown().Revision() != before;
}

} // namespace bz::app
