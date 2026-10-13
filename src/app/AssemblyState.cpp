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
    // The JSON has the looks but not their pictures: those stay as they are.
    auto pictures = std::move(assembly.look.pictures);
    assembly.look = std::move(loaded->look);
    assembly.look.pictures = std::move(pictures);
    assembly.TouchLook();
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

const doc::Look& AssemblyState::ShownLook() {
    const std::array<std::uint64_t, 3> key{assembly.Revision(), assembly.LookRevision(), geometry.Shown().Revision()};
    if (key == myShownLookKey) return myShownLook;
    myShownLookKey = key;
    ++myShownLookRevision;

    doc::Look out;
    const doc::Look& own = assembly.look;
    // Bytes of a picture: the assembly's own first, else a part's.
    const auto pictureOf = [&](const std::string& name) -> const std::vector<std::uint8_t>* {
        if (const auto it = own.pictures.find(name); it != own.pictures.end()) return &it->second;
        for (const assembly::Component& c : assembly.components)
            if (const doc::Document* part = geometry.PartOf(c.id))
                if (const auto it = part->look.pictures.find(name); it != part->look.pictures.end()) return &it->second;
        return nullptr;
    };
    for (const assembly::Component& c : assembly.components) {
        const auto placed = [&](doc::FaceReference f) {
            f.point = c.placement.Point(f.point);
            f.normal = c.placement.Direction(f.normal);
            return f;
        };
        // Painted as a whole in this assembly: the part's own looks give way.
        if (const doc::Appearance* whole = own.OfBody(c.id)) {
            out.SetBody(c.id, whole);
        } else if (const doc::Document* part = geometry.PartOf(c.id)) {
            // A component shows its part as one body: the first body look counts.
            for (const doc::Look::BodyLook& b : part->look.bodies)
                if (const doc::Appearance* a = part->look.Find(b.appearance)) {
                    out.SetBody(c.id, a);
                    break;
                }
            for (const doc::Look::FaceLook& f : part->look.faces)
                if (const doc::Appearance* a = part->look.Find(f.appearance)) out.SetFace(c.id, placed(f.face), a);
        }
        for (const doc::Look::FaceLook& f : own.faces)
            if (f.body == c.id) out.SetFace(c.id, placed(f.face), own.Find(f.appearance));
    }
    for (const doc::Appearance& a : out.used)
        if (a.HasPicture())
            if (const auto* bytes = pictureOf(a.picture)) out.pictures[a.picture] = *bytes;
    myShownLook = std::move(out);
    return myShownLook;
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
