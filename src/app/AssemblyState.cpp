#include "app/AssemblyState.h"

#include "app/Session.h"
#include "app/Workspace.h"
#include "assembly/AssemblyFile.h"
#include "kernel/ContextProjection.h"
#include "core/PathText.h"
#include "assembly/Solver.h"
#include "core/Monitor.h"
#include "core/Profile.h"

#include <chrono>
#include <format>

namespace bz::app {
namespace {
constexpr std::size_t kMaxUndo = 200;
}

AssemblyState::AssemblyState() {
    // Part files are looked at for changes about once a second, not every
    // frame: a system call per component per frame added up in large assemblies.
    geometry.SetDiskCheckInterval(1.0);
}

AssemblyState::~AssemblyState() = default;

Session* AssemblyState::InPlaceSession() {
    if (!inPlace) return nullptr;
    const assembly::Component* c = assembly.FindComponent(inPlace);
    if (!c) return nullptr;
    for (PartEdit& e : parts)
        if (e.session && e.session->inPlaceOf == inPlace) return e.session.get();
    return nullptr;
}

const Session* AssemblyState::InPlaceSession() const { return const_cast<AssemblyState*>(this)->InPlaceSession(); }

AssemblyState::PartEdit* AssemblyState::EditOf(const std::filesystem::path& file) {
    for (PartEdit& e : parts)
        if (SameFile(e.file, file) || e.file == file) return &e;
    return nullptr;
}

bool AssemblyState::PartsModified() const {
    for (const PartEdit& e : parts)
        if (e.created || (e.session && e.session->IsModified())) return true;
    return false;
}

const kernel::Model* AssemblyState::InPlaceContext() {
    if (!inPlace || !assembly.FindComponent(inPlace)) {
        myContext.reset();
        myContextKey = {~std::uint64_t{0}, 0, 0};
        return nullptr;
    }
    const std::array<std::uint64_t, 3> key{assembly.Revision(), geometry.PartsGeneration(), inPlace.value};
    if (!myContext || key != myContextKey) {
        myContext = std::make_unique<kernel::Model>(geometry.ContextOf(assembly, inPlace));
        myContextKey = key;
    }
    return myContext.get();
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
    assembly.patterns = std::move(loaded->patterns);   // undoing a pattern's change brings it back too
    // The JSON has the looks but not their pictures: those stay as they are.
    auto pictures = std::move(assembly.look.pictures);
    assembly.look = std::move(loaded->look);
    assembly.look.pictures = std::move(pictures);
    assembly.TouchLook();
    assembly.Ids().Reserve(ObjectId{loaded->Ids().Last()});
    assembly.Touch();
    if (!assembly.FindComponent(selected) && !assembly.FindConstraint(selected) && !assembly.FindPattern(selected))
        selected = {};
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

bool AssemblyState::Adapt(const std::filesystem::path& file, std::string_view name) {
    if (name.empty() || drag.moving) return false;   // a drag: the parts follow on release
    bool changed = false;
    std::vector<std::string> done;
    for (const assembly::Component& c : assembly.components) {
        const doc::Document* part = geometry.PartOf(c.id);
        if (!part) continue;
        const std::filesystem::path partFile = assembly::ResolvePartPath(file, c.path);
        const std::string key = ToUtf8(partFile);
        // Whether it has projections from here: looked at once per version of the part.
        const std::uint64_t generation = geometry.PartGenerationOf(c.id);
        auto& has = myHasContext[key];
        if (has.first != generation || generation == 0) has = {generation, kernel::HasContextProjections(*part, name)};
        if (!has.second || std::find(done.begin(), done.end(), key) != done.end()) continue;
        done.push_back(key);   // one component per part: they all show the same part
        const std::uint64_t now = kernel::ContextKey(geometry, assembly, c.id, *part, name);
        if (const auto known = myAdaptKeys.find(key); known != myAdaptKeys.end() && known->second == now) continue;

        // It follows: edited in memory from now on (it is saved with the assembly).
        PartEdit* edit = EditOf(partFile);
        if (edit && edit->session && edit->session->mode != EditMode::Model) continue;   // its sketch is open: after
        if (!edit) {
            Expected<std::unique_ptr<Session>> loaded = Workspace::LoadSession(partFile, nullptr);
            if (!loaded) continue;
            parts.push_back({partFile, std::move(*loaded), false});
            edit = &parts.back();
            edit->session->host = nullptr;
            geometry.UsePartInMemory(partFile, &edit->session->doc, &edit->session->model);
        }
        const kernel::AdaptResult r = kernel::AdaptContextProjections(geometry, assembly, c.id, edit->session->doc, name);
        myAdaptKeys[key] = now;
        if (r.lost > 0)
            problems.push_back(std::format("{}: {} рёбер других деталей не найдено — их проекции остались на месте",
                                           c.name, r.lost));
        if (r.changed()) {
            edit->session->RebuildNow();
            changed = true;
            mon::Action("assembly", std::format("адаптивность: {} ({} кривых)", c.name, r.moved));
        }
    }
    return changed;
}

bool AssemblyState::Update(const std::filesystem::path& file, std::string_view contextName) {
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
        // ones tied to it follow; otherwise the smallest change wins. The
        // states of the constraints and the freedoms wait for the release:
        // the drag stays smooth in a large assembly.
        assembly::SolveOptions options;
        if (drag.moving) options.keep = drag.component;
        options.analyze = !drag.moving;
        if (assembly::Solve(assembly, options).moved) assembly.Touch();
        if (!drag.moving) {
            freedom = assembly::AnalyzeFreedom(assembly);
            myAnalyzedRevision = assembly.Revision();
        }
        mySolvedRevision = assembly.Revision();
        geometry.Update(assembly, file);   // show them where the solver put them
    } else if (!drag.moving && myAnalyzedRevision != assembly.Revision()) {
        prof::Scope scope("asm.analyze");
        assembly::Analyze(assembly);   // the drag is over: states and freedoms for where it ended
        freedom = assembly::AnalyzeFreedom(assembly);
        myAnalyzedRevision = assembly.Revision();
    }
    {
        prof::Scope scope("asm.adapt");
        // Parts that follow others: rebuilt in memory, shown at the next Update.
        if (Adapt(file, contextName)) geometry.Update(assembly, file);
    }
    return geometry.Shown().Revision() != before;
}

} // namespace bz::app
