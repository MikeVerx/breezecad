// Assemblies: components (part files, standard parts), where they are,
// constraints and joints between them — what the «Сборка» tab does with
// clicks. Geometry of a component is named by a point in the assembly
// (on a face, near a round edge) or by a number from assembly.faces.

#include "app/AppContext.h"
#include "app/Session.h"
#include "app/Workspace.h"
#include "app/assembly/AssemblyUi.h"
#include "app/assembly/InPlace.h"
#include "document/Origin.h"
#include "app/scripting/Commands.h"
#include "assembly/Solver.h"
#include "core/I18n.h"
#include "core/PathText.h"
#include "document/BzcFile.h"
#include "document/DocumentTypes.h"
#include "document/SketchFeature.h"
#include "kernel/AssemblyReadApi.h"
#include "kernel/ContextProjection.h"
#include "kernel/Topology.h"

#include <cmath>
#include <format>

namespace bz::app::scripting {

namespace {

using assembly::ConstraintType;

// In the order of assembly::ConstraintType.
const std::vector<std::string> kTypes = {"mate",   "insert",      "angle",  "tangent", "axis_mate", "rotational", "slider",
                                         "cylindrical", "planar", "ball", "rigid", "symmetry", "gear", "screw"};
// Origin elements by name, in the order of doc::origin ids 1..7.
const std::vector<std::string> kOrigin = {"centre", "xy", "xz", "yz", "x", "y", "z"};

Session& ActiveAssembly(AppContext& app) {
    Session& s = Active(app);
    if (!s.IsAssembly()) Fail("Активный документ — не сборка (новая — assembly.new)");
    return s;
}

assembly::Component& ComponentOf(Session& s, const Json& args, const char* key) {
    assembly::Component* c = s.assembly->assembly.FindComponent(Id(args, key));
    if (!c) Fail(std::format("«{}»: нет такого компонента (список — assembly.components)", key));
    return *c;
}

Json PlacementJson(const assembly::Placement& p) {
    return {{"origin", ToJson3(p.translation)}, {"x_axis", ToJson3(p.rotation.col[0])}, {"z_axis", ToJson3(p.rotation.col[2])}};
}

const char* StateName(assembly::ConstraintState st) {
    switch (st) {
        case assembly::ConstraintState::Satisfied: return "satisfied";
        case assembly::ConstraintState::Failed: return "failed";
        case assembly::ConstraintState::Redundant: return "redundant";
        default: return "unknown";
    }
}

bool IsAngle(ConstraintType t) { return t == ConstraintType::Angle; }

Json ConstraintJson(const assembly::Constraint& c) {
    Json j = {{"id", c.id.value},
              {"name", c.name},
              {"type", kTypes[static_cast<std::size_t>(c.type)]},
              {"components", {c.first.component.value, c.second.component.value}},
              {"state", StateName(c.state)},
              {"suppressed", c.suppressed},
              {"flipped", c.flipped}};
    if (IsAngle(c.type)) j["angle_deg"] = c.value * 180.0 / kPi;
    else if (!assembly::IsJoint(c.type) || c.type == ConstraintType::Rotational || c.type == ConstraintType::Planar) j["offset_mm"] = c.value;
    if (c.state == assembly::ConstraintState::Failed) j["error"] = c.error;
    if (assembly::IsTransmission(c.type)) {
        j["components"] = Json::array();
        j["drive"] = c.drive.value;
        j["driven"] = c.driven.value;
        j["ratio"] = c.ratio;
    }
    if (c.type == assembly::ConstraintType::Symmetry) j["plane_component"] = c.third.component.value;
    if (assembly::HasRange(c.type) && c.limits.enabled)
        j["range"] = IsAngle(c.type) ? Json::array({c.limits.lower * 180.0 / kPi, c.limits.upper * 180.0 / kPi})
                                     : Json::array({c.limits.lower, c.limits.upper});
    if (assembly::MotionOf(c.type) != assembly::JointMotion::None) {
        j["locked"] = c.locked;
        j["position"] = assembly::MotionOf(c.type) == assembly::JointMotion::Rotation ? c.position * 180.0 / kPi : c.position;
    }
    return j;
}

Json ComponentJson(Session& s, const kernel::ComponentRecord& r) {
    AssemblyState& st = *s.assembly;
    Json j = {{"id", r.id.value},
              {"name", r.treeName},
              {"file", r.path},
              {"designation", r.designation},
              {"part_name", r.name},
              {"grounded", r.grounded},
              {"visible", r.visible},
              {"placement", PlacementJson(r.placement)}};
    if (!r.grounded)
        if (const auto f = st.freedom.find(r.id); f != st.freedom.end())
            j["freedom"] = {{"translations", f->second.translations}, {"rotations", f->second.rotations}};
    if (const auto box = st.geometry.ComponentBounds(r.id)) j["box"] = {{"min", ToJson3(box->min)}, {"max", ToJson3(box->max)}};
    if (!r.problem.empty()) j["problem"] = Tr(r.problem);
    return j;
}

/// The assembly brought up to date (parts loaded, constraints solved).
void Solved(Session& s) { s.RebuildNow(); }

Json Overview(Session& s) {
    Solved(s);
    AssemblyState& st = *s.assembly;
    const kernel::AssemblyReader reader(st.assembly, st.geometry);
    Json components = Json::array(), constraints = Json::array();
    for (const kernel::ComponentRecord& r : reader.Components()) components.push_back(ComponentJson(s, r));
    for (const assembly::Constraint& c : st.assembly.constraints) constraints.push_back(ConstraintJson(c));
    Json out = {{"components", std::move(components)}, {"constraints", std::move(constraints)}};
    if (!st.problems.empty()) {
        for (const std::string& p : st.problems) out["problems"].push_back(Tr(p));
        st.problems.clear();
    }
    return out;
}

Json New(AppContext& app, const Json&) {
    Session& s = app.workspace.NewAssembly();
    return {{"title", s.Title()}, {"type", "assembly"},
            {"note", "Пустая сборка активна. Компоненты — assembly.place (файл детали .bzc или стандартное изделие)."}};
}

Json Components(AppContext& app, const Json&) { return Overview(ActiveAssembly(app)); }

/// The rotation of `rotate`: {"axis": [x, y, z], "angle": degrees}.
assembly::Rotation RotationArg(const Json& v) {
    if (!v.is_object()) Fail("«rotate» — {\"axis\": [x, y, z], \"angle\": градусы}");
    const Vec3 axis = Point3(Required(v, "axis"), "rotate.axis");
    if (axis.Length() < 1e-9) Fail("«rotate.axis» — нулевой вектор");
    return assembly::Rotation::AxisAngle(axis, Number(v, "angle") * kPi / 180.0);
}

Json Place(AppContext& app, const Json& args) {
    Session&        s  = ActiveAssembly(app);
    AssemblyState&  st = *s.assembly;
    std::filesystem::path file;
    std::string designation;
    if (Has(args, "standard")) {
        doc::parts::Request request;
        request.standard = Text(args, "standard");
        const doc::parts::StandardInfo* info = doc::parts::FindStandard(request.standard);
        if (!info) Fail(std::format("Нет стандарта «{}» (список — library.list)", request.standard));
        request.size = doc::parts::MatchSize(*info, Text(args, "size"));
        request.length = Number(args, "length", 0.0);
        if (const Expected<doc::parts::Made> check = doc::parts::MakePart(request); !check) Fail(Tr(check.GetError().message));
        const auto made = StandardPartFileOf(app, s, request);
        if (!made) Fail("Не удалось сохранить файл стандартного изделия");
        file = *made;
        designation = doc::parts::Designation(request);
    } else if (Has(args, "document")) {   // an open part, by its title or its file's name
        const std::string title = Text(args, "document");
        const auto named = [&](const Session& other) {
            if (other.Title() == title) return true;
            const std::u8string stem = other.path.stem().u8string();
            return !other.path.empty() && std::string(stem.begin(), stem.end()) == title;
        };
        for (const auto& other : app.workspace.sessions)
            if (named(*other) && !other->IsAssembly()) {
                if (other->path.empty()) Fail(std::format("«{}» не сохранена — сначала doc.save в файл (активируйте её app.activate)", title));
                if (other->IsModified()) Fail(std::format("В «{}» несохранённые изменения — сначала doc.save", title));
                file = other->path;
            }
        if (file.empty()) Fail(std::format("Нет открытой детали «{}» (список — app.documents)", title));
    } else {
        file = PathFromUtf8(Text(args, "file"));
    }
    std::error_code ec;
    if (!std::filesystem::exists(file, ec)) Fail(std::format("Нет файла «{}»", ToUtf8(file)));
    if (const Expected<std::string> type = doc::ReadDocumentType(file); !type || *type != doc::kPartType)
        Fail(std::format("«{}» — не деталь BreezeCAD (.bzc)", ToUtf8(file.filename())));

    const ObjectId id = PlaceFile(app, s, file);
    if (!id) Fail("Компонент не вставлен (см. уведомление в программе)");
    assembly::Component& c = *st.assembly.FindComponent(id);
    if (!designation.empty() && c.properties.designation.empty()) c.properties.designation = designation;
    if (Has(args, "rotate")) c.placement.rotation = RotationArg(args["rotate"]);
    if (Has(args, "at")) c.placement.translation = Point3(args["at"], "at");
    c.grounded = Flag(args, "grounded", st.assembly.components.size() == 1);   // the first one holds the rest
    st.assembly.Touch();
    Solved(s);
    const kernel::AssemblyReader reader(st.assembly, st.geometry);
    Json out = ComponentJson(s, *reader.Component(id));
    if (!st.problems.empty()) {
        for (const std::string& p : st.problems) out["problems"].push_back(Tr(p));
        st.problems.clear();
    }
    return out;
}

/// Faces and round edges of a component, numbered, in assembly coordinates.
Json Faces(AppContext& app, const Json& args) {
    Session&        s  = ActiveAssembly(app);
    Solved(s);
    AssemblyState&  st = *s.assembly;
    const assembly::Component& c = ComponentOf(s, args, "component");
    const auto placed = st.geometry.PlacedModelOf(c.id, c.placement);
    if (!placed) Fail("У компонента нет геометрии (файл детали не прочитан?)");
    const kernel::Topology t = kernel::DescribeTopology(*placed);
    const std::string kind = Text(args, "kind", "");
    const int limit = Integer(args, "limit", 60);
    const std::optional<Vec3> closeTo = Has(args, "near") ? std::optional<Vec3>(Point3(args["near"], "near")) : std::nullopt;
    std::vector<const kernel::FaceInfo*> faces;
    for (const kernel::FaceInfo& f : t.faces) {
        const char* name = f.kind == doc::SurfaceKind::Plane ? "plane" : f.kind == doc::SurfaceKind::Cylinder ? "cylinder" : "other";
        if (!kind.empty() && kind != name) continue;
        if (Has(args, "normal") && (f.kind != doc::SurfaceKind::Plane || f.normal.Dot(Point3(args["normal"], "normal").Normalized()) < 0.9998)) continue;
        faces.push_back(&f);
    }
    const auto faceMiss = [&](const kernel::FaceInfo* f) {
        return kernel::BoxDistance(f->boxMin, f->boxMax, *closeTo) * 1e6 + (f->point - *closeTo).Length();
    };
    if (closeTo) std::stable_sort(faces.begin(), faces.end(), [&](auto* a, auto* b) { return faceMiss(a) < faceMiss(b); });
    if (limit > 0 && static_cast<int>(faces.size()) > limit) faces.resize(static_cast<std::size_t>(limit));
    Json fj = Json::array(), cj = Json::array();
    for (const kernel::FaceInfo* f : faces) {
        Json j = {{"face", f->index}, {"point", ToJson3(f->point)}, {"normal", ToJson3(f->normal)},
                  {"kind", f->kind == doc::SurfaceKind::Plane ? "plane" : f->kind == doc::SurfaceKind::Cylinder ? "cylinder" : "other"}};
        if (f->axis) {
            j["axis_point"] = ToJson3(f->axis->axisPoint);
            j["axis_dir"] = ToJson3(f->axis->axisDir);
            j["radius"] = f->axis->radius;
            if (f->kind == doc::SurfaceKind::Cylinder) j["hole"] = f->hole;
        }
        fj.push_back(std::move(j));
    }
    // Round edges go by the same filters: with kind/normal, only the rims of
    // the faces listed; nearest first with near; no more than limit.
    const bool filtered = !kind.empty() || Has(args, "normal");
    std::vector<int> listed;
    for (const kernel::FaceInfo* f : faces) listed.push_back(f->index);
    std::vector<const kernel::EdgeInfo*> rims;
    for (const kernel::EdgeInfo& e : t.edges) {
        if (!e.centre || !e.closed) continue;
        if (filtered && std::ranges::none_of(e.faces, [&](int fi) { return std::ranges::find(listed, fi) != listed.end(); })) continue;
        rims.push_back(&e);
    }
    if (closeTo) std::stable_sort(rims.begin(), rims.end(), [&](auto* a, auto* b) { return kernel::EdgeDistance(*a, *closeTo) < kernel::EdgeDistance(*b, *closeTo); });
    if (limit > 0 && static_cast<int>(rims.size()) > limit) rims.resize(static_cast<std::size_t>(limit));
    for (const kernel::EdgeInfo* e : rims)
        cj.push_back({{"edge", e->index}, {"point", ToJson3(e->middle)}, {"centre", ToJson3(*e->centre)},
                      {"axis", ToJson3(e->direction)}, {"radius", e->radius}});
    return {{"component", c.id.value}, {"faces", std::move(fj)}, {"round_edges", std::move(cj)},
            {"note", "Точки — в координатах сборки. Для assembly.constrain: {component, face: N} (грань, ось цилиндра) или {component, edge: N} (круглая кромка для insert/rotational), или {component, point: [x,y,z]}."}};
}

/// One side of a constraint: what a click on the component at a point gives,
/// or a named element (an origin plane of the assembly or of a part, a work
/// plane, axis or point of a part).
assembly::GeometryRef GeometryArg(Session& s, const Json& v, const char* what, ConstraintType type) {
    AssemblyState& st = *s.assembly;
    if (!v.is_object()) Fail(std::format("«{}» — {{\"component\": id, \"face\": N | \"edge\": N | \"point\": [x, y, z]}}", what));
    // The assembly's origin: {"origin": "xy"}; a part's: {"component": id, "element": "xy" | id}.
    if (v.contains("origin")) {
        const std::size_t k = static_cast<std::size_t>(Choice(v, "origin", kOrigin, 1));
        return *assembly::OriginGeometry(assembly::kAssemblyOrigin, ObjectId{k + 1});
    }
    const assembly::Component& c = ComponentOf(s, v, "component");
    if (v.contains("element")) {
        ObjectId element;
        if (v["element"].is_string()) {
            const std::string name = v["element"].get<std::string>();
            const auto it = std::find(kOrigin.begin(), kOrigin.end(), name);
            if (it != kOrigin.end()) element = ObjectId{static_cast<std::uint64_t>(it - kOrigin.begin()) + 1};
            else
                for (const auto& e : st.geometry.ElementsOf(c.id))
                    if (e.name == name) element = e.id;
        } else {
            element = Id(v, "element");
        }
        const std::optional<assembly::GeometryRef> g = st.geometry.ElementGeometry(c.id, element);
        if (!g) Fail(std::format("«{}»: у детали нет такого элемента (xy, xz, yz, x, y, z, centre или имя рабочей плоскости/оси/точки)", what));
        return *g;
    }
    Vec3 at;
    if (v.contains("point")) {
        at = Point3(v["point"], what);
    } else {
        const auto placed = st.geometry.PlacedModelOf(c.id, c.placement);
        if (!placed) Fail(std::format("«{}»: у компонента нет геометрии", what));
        const kernel::Topology t = kernel::DescribeTopology(*placed);
        if (v.contains("face")) {
            const int i = Integer(v, "face");
            if (i < 0 || static_cast<std::size_t>(i) >= t.faces.size()) Fail(std::format("«{}»: нет грани {} (assembly.faces)", what, i));
            at = t.faces[static_cast<std::size_t>(i)].point;
        } else if (v.contains("edge")) {
            const int i = Integer(v, "edge");
            if (i < 0 || static_cast<std::size_t>(i) >= t.edges.size()) Fail(std::format("«{}»: нет ребра {} (assembly.faces)", what, i));
            at = t.edges[static_cast<std::size_t>(i)].middle;
        } else {
            Fail(std::format("«{}»: задайте face, edge или point", what));
        }
    }
    const kernel::AssemblyPick want = ConstraintPick(type);
    // A point given by numbers: a vertex or an edge right there (half a millimetre) counts.
    std::optional<assembly::GeometryRef> g = st.geometry.GeometryAt(st.assembly, c.id, at, want, v.contains("point") ? 0.5 : 0.0);
    if (!g)
        Fail(std::format("«{}»: {}", what,
                         want == kernel::AssemblyPick::Circle ? "здесь нет круглой кромки (round_edges из assembly.faces)"
                         : want == kernel::AssemblyPick::Axis ? "у этой грани нет оси — нужен цилиндр или конус"
                                                              : "в этой точке нет грани компонента"));
    return *g;
}

Json Constrain(AppContext& app, const Json& args) {
    Session&       s  = ActiveAssembly(app);
    Solved(s);
    AssemblyState& st = *s.assembly;
    assembly::Constraint c;
    c.type = static_cast<ConstraintType>(Choice(args, "type", kTypes, 0));
    if (assembly::IsTransmission(c.type)) {
        // Two joints: gear — rotational to rotational; screw — rotational to slider.
        c.drive = Id(args, "drive");
        c.driven = Id(args, "driven");
        const assembly::Constraint* d = st.assembly.FindConstraint(c.drive);
        const assembly::Constraint* n = st.assembly.FindConstraint(c.driven);
        if (!d || d->type != ConstraintType::Rotational) Fail("drive — id соединения rotational");
        const ConstraintType want = c.type == ConstraintType::Gear ? ConstraintType::Rotational : ConstraintType::Slider;
        if (!n || n->type != want || n->id == d->id)
            Fail(c.type == ConstraintType::Gear ? "driven — id другого соединения rotational" : "driven — id соединения slider");
        c.ratio = Number(args, "ratio", c.type == ConstraintType::Gear ? 1.0 : 2.0);
        if (std::abs(c.ratio) < 1e-9) Fail("ratio не может быть нулём");
        c.flipped = Flag(args, "flipped", c.type == ConstraintType::Gear);
        assembly::FitTransmission(st.assembly, c);
    } else {
        c.first  = GeometryArg(s, Required(args, "first"), "first", c.type);
        if (const char* why = ConstraintUnfit(c.type, c.first, nullptr)) Fail(Tr(why));
        c.second = GeometryArg(s, Required(args, "second"), "second", c.type);
        if (const char* why = ConstraintUnfit(c.type, c.second, &c.first)) Fail(Tr(why));
        if (c.first.component == c.second.component) Fail("Зависимость связывает два разных компонента");
        if (c.type == ConstraintType::Symmetry) {
            c.third = GeometryArg(s, Required(args, "plane"), "plane", ConstraintType::Mate);
            if (c.third.kind != assembly::GeometryKind::Plane) Fail("plane — плоскость симметрии (грань или {\"origin\": \"yz\"})");
        }
        if (IsAngle(c.type)) c.value = Number(args, "angle", 90.0) * kPi / 180.0;
        else c.value = Number(args, "offset", 0.0);
        c.flipped = Flag(args, "flipped", false);
        if (Has(args, "range") && assembly::HasRange(c.type)) {
            const Json& r = args["range"];
            if (!r.is_array() || r.size() != 2 || !r[0].is_number() || !r[1].is_number()) Fail("range — [от, до]");
            const double k = IsAngle(c.type) ? kPi / 180.0 : 1.0;
            c.limits = {true, r[0].get<double>() * k, r[1].get<double>() * k};
        }
    }
    st.Checkpoint(assembly::IsJoint(c.type) ? N_("Соединение") : N_("Зависимость"));
    if (assembly::IsJoint(c.type)) assembly::FitJoint(st.assembly, c);
    const ObjectId id = st.assembly.AddConstraint(c).id;
    st.assembly.Touch();
    st.selected = id;
    Solved(s);
    Json out = ConstraintJson(*st.assembly.FindConstraint(id));
    for (const ObjectId comp : st.assembly.ComponentsOf(c))
        if (const auto f = st.freedom.find(comp); f != st.freedom.end())
            out["freedom"][std::to_string(comp.value)] = {{"translations", f->second.translations}, {"rotations", f->second.rotations}};
    if (out["state"] == "failed")
        out["hint"] = "Не выполняется: проверьте грани (assembly.faces), flipped или закрепление; отменить — doc.undo.";
    return out;
}

Json EditConstraint(AppContext& app, const Json& args) {
    Session&       s  = ActiveAssembly(app);
    AssemblyState& st = *s.assembly;
    assembly::Constraint* c = st.assembly.FindConstraint(Id(args, "id"));
    if (!c) Fail("Нет такой зависимости (assembly.components → constraints)");
    st.Checkpoint(N_("Изменить зависимость"));
    if (Has(args, "offset")) c->value = Number(args, "offset");
    if (Has(args, "angle")) c->value = Number(args, "angle") * kPi / 180.0;
    if (Has(args, "flipped")) c->flipped = Flag(args, "flipped", false);
    if (Has(args, "suppressed")) c->suppressed = Flag(args, "suppressed", false);
    if (Has(args, "locked")) c->locked = Flag(args, "locked", false);
    if (Has(args, "position"))
        c->position = assembly::MotionOf(c->type) == assembly::JointMotion::Rotation ? Number(args, "position") * kPi / 180.0 : Number(args, "position");
    if (Has(args, "name")) c->name = Text(args, "name");
    const ObjectId id = c->id;
    st.assembly.Touch();
    Solved(s);
    return ConstraintJson(*st.assembly.FindConstraint(id));
}

Json Move(AppContext& app, const Json& args) {
    Session&             s  = ActiveAssembly(app);
    AssemblyState&       st = *s.assembly;
    assembly::Component& c  = ComponentOf(s, args, "component");
    if (c.grounded) Fail("Компонент закреплён — сначала assembly.ground с grounded: false");
    st.Checkpoint(N_("Перемещение"));
    if (Has(args, "rotate")) c.placement.rotation = RotationArg(args["rotate"]) * c.placement.rotation;
    if (Has(args, "to")) c.placement.translation = Point3(args["to"], "to");
    if (Has(args, "by")) c.placement.translation = c.placement.translation + Point3(args["by"], "by");
    const ObjectId id = c.id;
    st.assembly.Touch();
    Solved(s);
    Json out = {{"placement", PlacementJson(st.assembly.FindComponent(id)->placement)}};
    if (!st.assembly.ConstraintsOf(id).empty()) out["note"] = "Зависимости компонента пересчитаны: он мог встать обратно туда, куда они велят.";
    return out;
}

Json Ground(AppContext& app, const Json& args) {
    Session&             s  = ActiveAssembly(app);
    AssemblyState&       st = *s.assembly;
    assembly::Component& c  = ComponentOf(s, args, "component");
    const bool g = Flag(args, "grounded", true);
    if (c.grounded != g) {
        st.Checkpoint(g ? N_("Закрепить") : N_("Освободить"));
        c.grounded = g;
        st.assembly.Touch();
    }
    Solved(s);
    return {{"component", c.id.value}, {"grounded", g}};
}

Json Remove(AppContext& app, const Json& args) {
    Session&       s  = ActiveAssembly(app);
    AssemblyState& st = *s.assembly;
    if (Has(args, "constraint")) {
        const ObjectId id = Id(args, "constraint");
        if (!st.assembly.FindConstraint(id)) Fail("Нет такой зависимости");
        st.Checkpoint(N_("Удалить"));
        st.assembly.RemoveConstraint(id);
    } else {
        const ObjectId id = ComponentOf(s, args, "component").id;
        st.Checkpoint(N_("Удалить"));
        st.assembly.RemoveComponent(id);
    }
    st.selected = {};
    st.assembly.Touch();
    return Overview(s);
}

Json Clashes(AppContext& app, const Json&) {
    Session& s = ActiveAssembly(app);
    Solved(s);
    Json out = Json::array();
    for (const kernel::ComponentClash& c : s.assembly->geometry.Clashes())
        out.push_back({{"components", {c.first.value, c.second.value}}, {"volume_mm3", c.volume}, {"centre", ToJson3(c.centre)}});
    return {{"clashes", std::move(out)}, {"note", "Пересечения тел компонентов (касание не считается)."}};
}

Json GeomSchema(const char* what) {
    return {{"type", "object"},
            {"description", std::string(what) + ": {\"component\": id, \"face\": N} — грань или её ось; {\"component\": id, \"edge\": N} — круглая кромка "
                            "(insert, rotational); {\"component\": id, \"point\": [x, y, z]} — точка на грани, ребре или в вершине "
                            "(в координатах сборки); {\"origin\": \"xy|xz|yz|x|y|z|centre\"} — начало координат сборки; "
                            "{\"component\": id, \"element\": \"xy\" | имя рабочей плоскости/оси/точки} — элемент детали."}};
}

Json EditInPlaceCommand(AppContext& app, const Json& args) {
    Session& s = ActiveAssembly(app);
    const assembly::Component& c = ComponentOf(s, args, "component");
    std::string why;
    if (!EditInPlace(app, s, c.id, &why)) Fail(why.empty() ? std::string("Деталь не открылась для правки на месте") : why);
    Session* part = app.Doc();
    return {{"editing", c.name}, {"part", part ? part->Title() : std::string{}},
            {"note", "Теперь команды деталей (sketch.*, feature.*) работают с этой деталью; вернуться — assembly.return_to_assembly."}};
}

Json ReturnCommand(AppContext& app, const Json&) {
    if (!EditingInPlace(app)) Fail("Сейчас не правится деталь на месте");
    ReturnToAssembly(app);
    return {{"ok", true}};
}

Json ProjectEdgeCommand(AppContext& app, const Json& args) {
    Session* part = app.Doc();
    if (!part || !part->host || !part->host->assembly) Fail("Сначала откройте деталь на месте: assembly.edit_in_place или assembly.create_part");
    auto* f = part->doc.FindAs<doc::SketchFeature>(Id(args, "sketch"));
    if (!f) Fail("«sketch» — id эскиза детали, открытой на месте");
    AssemblyState& st = *part->host->assembly;
    const ObjectId target = part->inPlaceOf;
    const ObjectId source = Id(args, "component");
    if (source == target || !st.assembly.FindComponent(source)) Fail("«component» — id другого компонента сборки");
    const std::string name = part->host->ContextName();
    if (kernel::WouldCycle(st.geometry, st.assembly, source, target, name))
        Fail("Тот компонент сам следует за этой деталью: проекция замкнула бы их друг на друга");
    const std::optional<doc::EdgeReference> edge =
        st.geometry.EdgeNear(st.assembly, source, Point3(Required(args, "point"), "point"), Number(args, "tolerance", 1.0));
    if (!edge) Fail("Около этой точки нет ребра компонента (точка — в координатах сборки)");
    doc::ContextProjection link{name, source, *edge, {}, Flag(args, "adaptive", true)};
    const Expected<kernel::ProjectedCurve> pc = st.geometry.ProjectSourceEdge(st.assembly, target, link, f->placement.frame);
    if (!pc) Fail(Tr(pc.GetError().message));
    std::unique_ptr<doc::Feature> before = f->Clone();
    sketch::Sketch& sk = f->sketch;
    IdGenerator& ids = part->doc.Ids();
    const auto point = [&](Vec2 at) {
        const ObjectId id = sk.AddPoint(ids, at);
        sk.FindPoint(id)->projected = true;
        return id;
    };
    ObjectId curve;
    switch (pc->kind) {
        case kernel::ProjectedCurve::Kind::Line:
            if ((pc->end - pc->start).Length() < 1e-6) Fail("Ребро видно с торца — проецируется в точку");
            curve = sk.AddLine(ids, point(pc->start), point(pc->end));
            sk.FindLine(curve)->projected = true;
            break;
        case kernel::ProjectedCurve::Kind::Circle:
            curve = sk.AddCircle(ids, point(pc->centre), pc->radius);
            sk.FindCircle(curve)->projected = true;
            break;
        case kernel::ProjectedCurve::Kind::Arc:
            curve = sk.AddArc(ids, point(pc->centre), point(pc->start), point(pc->end));
            for (auto& a : sk.arcs)
                if (a.id == curve) a.projected = true;
            break;
        case kernel::ProjectedCurve::Kind::Ellipse:
            curve = sk.AddEllipse(ids, point(pc->centre), point(pc->start), point(pc->end));
            break;
    }
    link.curve = curve;
    f->contextProjections.push_back(link);
    part->doc.MarkChanged(static_cast<std::size_t>(std::max(0, part->doc.IndexOf(f->id))));
    part->CommitInPlaceEdit(std::move(before), Tr("Проецирование из другой детали"));
    static const char* kKinds[] = {"line", "circle", "arc", "ellipse"};
    return {{"curve", curve.value}, {"kind", kKinds[static_cast<int>(pc->kind)]}, {"adaptive", link.adaptive},
            {"note", "Кривая следует за ребром компонента-источника, пока деталь в этой сборке."}};
}

Json CreatePartCommand(AppContext& app, const Json& args) {
    Session& s = ActiveAssembly(app);
    const assembly::GeometryRef plane = GeometryArg(s, Required(args, "plane"), "plane", ConstraintType::Mate);
    if (plane.kind != assembly::GeometryKind::Plane) Fail("plane — плоская грань или плоскость начала координат");
    const ObjectId id = CreatePartAt(app, s, Has(args, "name") ? Text(args, "name") : std::string{}, plane, false);
    if (!id) Fail("Деталь не создана (см. сообщения программы)");
    const assembly::Component* c = s.assembly->assembly.FindComponent(id);
    return {{"component", id.value}, {"name", c ? c->name : std::string{}},
            {"note", "Деталь открыта на месте (пустая; её XY лежит на plane): sketch.create plane XY; вернуться — "
                     "assembly.return_to_assembly."}};
}

} // namespace

void AddAssemblyCommands(Commands& out) {
    out.push_back({"assembly.new", "Новая пустая сборка (становится активной).", Schema(), true, &New});
    out.push_back({"assembly.components",
                   "Компоненты активной сборки (id, файл, положение, закреплён, оставшиеся свободы, габарит) и зависимости "
                   "(тип, состояние satisfied/failed, значение).",
                   Schema(), false, &Components});
    out.push_back({"assembly.place",
                   "Вставляет компонент: файл детали .bzc (file), открытую сохранённую деталь (document — заголовок из "
                   "app.documents) или стандартное изделие (standard/size/length из library.*; файл кладётся в папку "
                   "«Стандартные изделия» рядом со сборкой). Первый компонент закрепляется. at — куда поставить начало "
                   "координат детали, rotate — поворот.",
                   Schema({{"file", StringArg("Путь к .bzc.")},
                           {"document", StringArg("Или заголовок открытой детали.")},
                           {"standard", StringArg("Или id стандарта (gost7798…).")},
                           {"size", StringArg("Размер стандартного изделия (M10).")},
                           {"length", NumberArg("Длина стандартного изделия, мм.")},
                           {"at", Point3Arg("Положение начала координат детали в сборке, мм.")},
                           {"rotate", {{"type", "object"}, {"description", "{\"axis\": [x, y, z], \"angle\": градусы}."}}},
                           {"grounded", BoolArg("Закрепить (по умолчанию — только первый).")}}),
                   true, &Place});
    out.push_back({"assembly.faces",
                   "Грани и круглые кромки компонента, пронумерованные, в координатах сборки — чтобы указать их в "
                   "assembly.constrain. Номера — до изменения детали.",
                   Schema({{"component", IdArg("id компонента.")},
                           {"kind", EnumArg("Только plane / cylinder / other.", {"plane", "cylinder", "other"})},
                           {"normal", Point3Arg("Только плоские грани с этой нормалью.")},
                           {"near", Point3Arg("Сначала ближайшие к точке.")},
                           {"limit", IntegerArg("Сколько граней (по умолчанию 60; 0 — все).")}},
                          {"component"}),
                   false, &Faces});
    out.push_back({"assembly.constrain",
                   "Зависимость или соединение двух компонентов. mate — грани лицом к лицу (flipped — заподлицо), offset — "
                   "зазор; insert — круглые кромки: оси совпадают, плоскости кромок сходятся (болт в отверстие); angle — угол "
                   "между гранями/осями; tangent — касание; axis_mate — оси на одной прямой; symmetry — first и second "
                   "симметричны относительно plane. Соединения: rotational (по кромкам), slider, cylindrical, planar, ball, "
                   "rigid. Передачи между соединениями: gear (drive, driven, ratio), screw (drive — rotational, driven — "
                   "slider, ratio — мм на оборот). Сборка сразу пересчитывается; в ответе — состояние и свободы компонентов.",
                   Schema({{"type", EnumArg("Тип.", kTypes)},
                           {"first", GeomSchema("Первая геометрия")},
                           {"second", GeomSchema("Вторая (другой компонент)")},
                           {"plane", GeomSchema("symmetry: плоскость симметрии")},
                           {"offset", NumberArg("Зазор / смещение, мм.")},
                           {"angle", NumberArg("angle: угол, градусы (по умолчанию 90).")},
                           {"range", {{"type", "array"}, {"description", "mate/angle: пределы [от, до] (мм или градусы) вместо значения."}}},
                           {"drive", IdArg("gear/screw: id ведущего соединения rotational.")},
                           {"driven", IdArg("gear: id ведомого rotational; screw: id slider.")},
                           {"ratio", NumberArg("gear: оборотов ведомого на оборот ведущего; screw: мм на оборот.")},
                           {"flipped", BoolArg("mate: заподлицо; insert: в одну сторону; tangent: изнутри; symmetry: грани навстречу; "
                                               "gear: в разные стороны (по умолчанию да); screw: обратное направление.")}},
                          {"type"}),
                   true, &Constrain});
    out.push_back({"assembly.edit_constraint",
                   "Меняет зависимость: offset, angle, flipped, suppressed; у соединений — locked и position (градусы или мм).",
                   Schema({{"id", IdArg("id зависимости.")},
                           {"offset", NumberArg("мм.")},
                           {"angle", NumberArg("градусы.")},
                           {"flipped", BoolArg("Перевернуть.")},
                           {"suppressed", BoolArg("Подавить.")},
                           {"locked", BoolArg("Соединение: держать в position.")},
                           {"position", NumberArg("Соединение: угол (градусы) или сдвиг (мм).")},
                           {"name", StringArg("Имя.")}},
                          {"id"}),
                   true, &EditConstraint});
    out.push_back({"assembly.move",
                   "Двигает незакреплённый компонент: to — новое положение начала координат детали, by — сдвиг, rotate — "
                   "поворот {axis, angle}. Зависимости затем пересчитываются.",
                   Schema({{"component", IdArg("id компонента.")},
                           {"to", Point3Arg("Новое положение, мм.")},
                           {"by", Point3Arg("Сдвиг, мм.")},
                           {"rotate", {{"type", "object"}, {"description", "{\"axis\": [x, y, z], \"angle\": градусы}."}}}},
                          {"component"}),
                   true, &Move});
    out.push_back({"assembly.ground", "Закрепить (grounded: true, по умолчанию) или освободить компонент.",
                   Schema({{"component", IdArg("id компонента.")}, {"grounded", BoolArg("Закрепить.")}}, {"component"}), true, &Ground});
    out.push_back({"assembly.remove", "Удаляет компонент (с его зависимостями) или одну зависимость.",
                   Schema({{"component", IdArg("id компонента.")}, {"constraint", IdArg("Или id зависимости.")}}), true, &Remove});
    out.push_back({"assembly.clashes", "Пересечения компонентов: пары и объём общего материала.", Schema(), false, &Clashes});
    out.push_back({"assembly.edit_in_place",
                   "Правка детали компонента на месте, в сборке (как двойной щелчок): дальше команды деталей работают с ней, "
                   "остальные компоненты видны вокруг. Изменения сохраняются вместе со сборкой.",
                   Schema({{"component", IdArg("id компонента.")}}, {"component"}), true, &EditInPlaceCommand});
    out.push_back({"assembly.return_to_assembly", "Закончить правку детали на месте и вернуться в сборку.", Schema(), true, &ReturnCommand});
    out.push_back({"assembly.project_edge",
                   "В эскиз детали, открытой на месте, проецирует ребро другого компонента (ближайшее к point — в "
                   "координатах сборки). Связанная проекция следует за тем компонентом (адаптивность), пока деталь в "
                   "этой сборке; adaptive: false — останется на месте.",
                   Schema({{"sketch", IdArg("id эскиза детали.")},
                           {"component", IdArg("id компонента-источника.")},
                           {"point", Point3Arg("Точка у ребра, мм (координаты сборки).")},
                           {"tolerance", NumberArg("Насколько близко к ребру, мм (по умолчанию 1).")},
                           {"adaptive", BoolArg("Следовать за источником (по умолчанию да).")}},
                          {"sketch", "component", "point"}),
                   true, &ProjectEdgeCommand});
    out.push_back({"assembly.create_part",
                   "Новая деталь прямо в сборке: её плоскость XY — на plane (грань компонента или {\"origin\": \"xy\"}), с "
                   "совмещением заподлицо; файл «<имя> - <сборка>.bzc» рядом со сборкой (при сохранении). Деталь сразу "
                   "открывается на месте.",
                   Schema({{"plane", GeomSchema("Плоскость")}, {"name", StringArg("Имя детали.")}}, {"plane"}), true,
                   &CreatePartCommand});
}

} // namespace bz::app::scripting
