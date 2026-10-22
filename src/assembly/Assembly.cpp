#include "assembly/Assembly.h"

#include "document/Origin.h"

#include <algorithm>
#include <format>

namespace bz::assembly {

std::string_view ConstraintTitle(ConstraintType type) {
    switch (type) {
        case ConstraintType::Mate:     return "Совмещение";
        case ConstraintType::Insert:   return "Вставка";
        case ConstraintType::Angle:    return "Угол";
        case ConstraintType::Tangent:  return "Касание";
        case ConstraintType::AxisMate: return "Совпадение осей";
        case ConstraintType::Rotational:  return "Вращение";
        case ConstraintType::Slider:      return "Ползун";
        case ConstraintType::Cylindrical: return "Цилиндрическое";
        case ConstraintType::Planar:      return "Плоское";
        case ConstraintType::Ball:        return "Шаровое";
        case ConstraintType::Rigid:       return "Жёсткое";
        case ConstraintType::Symmetry:    return "Симметрия";
        case ConstraintType::Gear:        return "Передача";
        case ConstraintType::Screw:       return "Винт-рейка";
    }
    return "Зависимость";
}

std::optional<GeometryRef> OriginGeometry(ObjectId component, ObjectId element) {
    namespace o = doc::origin;
    GeometryRef g;
    g.component = component;
    g.source = PickSource::Element;
    g.element = element;
    if (const std::optional<Frame> f = o::PlaneFrame(element)) {
        g.kind = GeometryKind::Plane;
        g.origin = f->origin;
        g.direction = f->normal;
    } else if (element == o::kAxisX || element == o::kAxisY || element == o::kAxisZ) {
        g.kind = GeometryKind::Axis;
        g.direction = element == o::kAxisX ? Vec3{1, 0, 0} : element == o::kAxisY ? Vec3{0, 1, 0} : Vec3{0, 0, 1};
    } else if (element == o::kPoint) {
        g.kind = GeometryKind::Point;
    } else {
        return std::nullopt;
    }
    return g;
}

std::string_view OriginElementTitle(ObjectId element) {
    namespace o = doc::origin;
    if (element == o::kPlaneXY) return "Плоскость XY";
    if (element == o::kPlaneXZ) return "Плоскость XZ";
    if (element == o::kPlaneYZ) return "Плоскость YZ";
    if (element == o::kAxisX) return "Ось X";
    if (element == o::kAxisY) return "Ось Y";
    if (element == o::kAxisZ) return "Ось Z";
    if (element == o::kPoint) return "Центр";
    return "";
}

std::vector<ObjectId> Assembly::ComponentsOf(const Constraint& c) const {
    std::vector<ObjectId> out;
    const auto add = [&](ObjectId id) {
        if (id && id != kAssemblyOrigin && FindComponent(id) && std::find(out.begin(), out.end(), id) == out.end())
            out.push_back(id);
    };
    if (IsTransmission(c.type)) {
        for (ObjectId j : {c.drive, c.driven})
            if (const Constraint* joint = FindConstraint(j); joint && !IsTransmission(joint->type)) {
                add(joint->first.component);
                add(joint->second.component);
            }
        return out;
    }
    add(c.first.component);
    add(c.second.component);
    if (c.type == ConstraintType::Symmetry) add(c.third.component);
    return out;
}

Placement Assembly::PlacementOf(ObjectId component) const {
    const Component* c = FindComponent(component);
    return c ? c->placement : Placement{};
}

bool Assembly::IsFixed(ObjectId component) const {
    if (component == kAssemblyOrigin) return true;
    const Component* c = FindComponent(component);
    return !c || c->grounded;
}

std::string Assembly::NameOf(ObjectId component) const {
    if (component == kAssemblyOrigin) return "Начало координат";
    const Component* c = FindComponent(component);
    return c ? c->name : std::string("?");
}

Component* Assembly::FindComponent(ObjectId id) {
    for (Component& c : components)
        if (c.id == id) return &c;
    return nullptr;
}
const Component* Assembly::FindComponent(ObjectId id) const {
    return const_cast<Assembly*>(this)->FindComponent(id);
}
int Assembly::ComponentIndex(ObjectId id) const {
    for (std::size_t i = 0; i < components.size(); ++i)
        if (components[i].id == id) return static_cast<int>(i);
    return -1;
}
Constraint* Assembly::FindConstraint(ObjectId id) {
    for (Constraint& c : constraints)
        if (c.id == id) return &c;
    return nullptr;
}
const Constraint* Assembly::FindConstraint(ObjectId id) const {
    return const_cast<Assembly*>(this)->FindConstraint(id);
}

namespace {
/// Smallest N such that "<base>:N" is not taken.
template <class Range>
std::string NextName(const Range& items, std::string_view base) {
    for (int n = 1;; ++n) {
        std::string name = std::format("{}:{}", base, n);
        if (std::none_of(items.begin(), items.end(), [&](const auto& i) { return i.name == name; })) return name;
    }
}
} // namespace

Component& Assembly::AddComponent(std::string path, std::string_view baseName, const Placement& at) {
    Component c;
    c.id        = myIds.Next();
    c.name      = NextName(components, baseName);
    c.path      = std::move(path);
    c.placement = at;
    // The first part is fixed in place, as in other CAD programs: the rest
    // are put against it.
    c.grounded = components.empty();
    components.push_back(std::move(c));
    Touch();
    return components.back();
}

void Assembly::RemoveComponent(ObjectId id) {
    for (std::size_t i = patterns.size(); i-- > 0;) {
        ComponentPattern& p = patterns[i];
        if (std::find(p.seed.begin(), p.seed.end(), id) != p.seed.end()) {
            RemovePattern(p.id);
        } else {
            for (auto& instance : p.instances) std::erase(instance, id);
            std::erase_if(p.instances, [](const auto& instance) { return instance.empty(); });
        }
    }
    std::erase_if(constraints, [&](const Constraint& c) {
        return c.first.component == id || c.second.component == id ||
               (c.type == ConstraintType::Symmetry && c.third.component == id);
    });
    std::erase_if(components, [&](const Component& c) { return c.id == id; });
    DropOrphanTransmissions();
    Touch();
}

void Assembly::DropOrphanTransmissions() {
    std::erase_if(constraints, [&](const Constraint& c) {
        if (!IsTransmission(c.type)) return false;
        const Constraint* a = FindConstraint(c.drive);
        const Constraint* b = FindConstraint(c.driven);
        return !a || !b || IsTransmission(a->type) || IsTransmission(b->type);
    });
}

Constraint& Assembly::AddConstraint(Constraint c) {
    c.id   = myIds.Next();
    c.name = NextName(constraints, ConstraintTitle(c.type));
    constraints.push_back(std::move(c));
    Touch();
    return constraints.back();
}

void Assembly::RemoveConstraint(ObjectId id) {
    std::erase_if(constraints, [&](const Constraint& c) { return c.id == id; });
    DropOrphanTransmissions();   // a transmission without its joint means nothing
    Touch();
}

bool ComponentPattern::IsInstance(ObjectId component) const {
    for (const auto& instance : instances)
        if (std::find(instance.begin(), instance.end(), component) != instance.end()) return true;
    return false;
}

bool ComponentPattern::Contains(ObjectId component) const {
    return std::find(seed.begin(), seed.end(), component) != seed.end() || IsInstance(component);
}

ComponentPattern& Assembly::AddPattern(ComponentPattern p) {
    p.id   = myIds.Next();
    p.name = NextName(patterns, "Массив");
    patterns.push_back(std::move(p));
    Touch();
    return patterns.back();
}

ComponentPattern* Assembly::FindPattern(ObjectId id) {
    for (ComponentPattern& p : patterns)
        if (p.id == id) return &p;
    return nullptr;
}
const ComponentPattern* Assembly::FindPattern(ObjectId id) const { return const_cast<Assembly*>(this)->FindPattern(id); }

const ComponentPattern* Assembly::PatternOfInstance(ObjectId component) const {
    for (const ComponentPattern& p : patterns)
        if (p.IsInstance(component)) return &p;
    return nullptr;
}

void Assembly::ClearInstances(ObjectId id) {
    ComponentPattern* p = FindPattern(id);
    if (!p) return;
    std::vector<ObjectId> gone;
    for (const auto& instance : p->instances) gone.insert(gone.end(), instance.begin(), instance.end());
    p->instances.clear();
    const auto removed = [&](ObjectId c) { return std::find(gone.begin(), gone.end(), c) != gone.end(); };
    std::erase_if(constraints, [&](const Constraint& c) {
        return removed(c.first.component) || removed(c.second.component) ||
               (c.type == ConstraintType::Symmetry && removed(c.third.component));
    });
    std::erase_if(components, [&](const Component& c) { return removed(c.id); });
    DropOrphanTransmissions();
    Touch();
}

void Assembly::RemovePattern(ObjectId id) {
    ClearInstances(id);
    std::erase_if(patterns, [&](const ComponentPattern& p) { return p.id == id; });
    Touch();
}

std::vector<const Constraint*> Assembly::ConstraintsOf(ObjectId id) const {
    std::vector<const Constraint*> out;
    for (const Constraint& c : constraints) {
        const std::vector<ObjectId> ids = ComponentsOf(c);
        if (std::find(ids.begin(), ids.end(), id) != ids.end()) out.push_back(&c);
    }
    return out;
}

} // namespace bz::assembly
