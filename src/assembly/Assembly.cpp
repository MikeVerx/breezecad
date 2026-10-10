#include "assembly/Assembly.h"

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
    }
    return "Зависимость";
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
    std::erase_if(constraints, [&](const Constraint& c) { return c.first.component == id || c.second.component == id; });
    std::erase_if(components, [&](const Component& c) { return c.id == id; });
    Touch();
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
    Touch();
}

std::vector<const Constraint*> Assembly::ConstraintsOf(ObjectId id) const {
    std::vector<const Constraint*> out;
    for (const Constraint& c : constraints)
        if (c.first.component == id || c.second.component == id) out.push_back(&c);
    return out;
}

} // namespace bz::assembly
