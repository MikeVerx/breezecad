#include "kernel/ComponentPattern.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <format>

namespace bz::kernel {

using assembly::Component;
using assembly::ComponentPattern;
using assembly::Constraint;
using assembly::GeometryKind;
using assembly::GeometryRef;
using assembly::PatternKind;
using assembly::Placement;
using assembly::Rotation;

namespace {

bool In(const std::vector<ObjectId>& ids, ObjectId id) { return std::find(ids.begin(), ids.end(), id) != ids.end(); }

/// "Болт:3" -> "Болт".
std::string BaseName(const std::string& name) {
    const auto colon = name.rfind(':');
    if (colon == std::string::npos || colon + 1 == name.size()) return name;
    const bool digits = std::all_of(name.begin() + std::ptrdiff_t(colon + 1), name.end(), [](char ch) { return ch >= '0' && ch <= '9'; });
    return digits ? name.substr(0, colon) : name;
}

/// `g` (on component `owner`) after the assembly is moved by `move`.
GeometryRef Moved(const GeometryRef& g, const Placement& owner, const Placement& move) {
    const Placement toLocal = owner.Inverse();
    GeometryRef out = g;
    out.origin = toLocal.Point(move.Point(owner.Point(g.origin)));
    out.pick = toLocal.Point(move.Point(owner.Point(g.pick)));
    if (g.direction.Length() > 0) out.direction = toLocal.Direction(move.Direction(owner.Direction(g.direction))).Normalized();
    return out;
}

/// Whether the part really has `g` there (the next hole of a plate...).
/// Parts without geometry cannot tell: kept.
bool FoundThere(const AssemblyModel& model, const assembly::Assembly& a, const GeometryRef& g) {
    if (!model.PartModelOf(g.component)) return true;
    GeometryRef f = g;
    if (!model.Refresh(a, f)) return false;
    constexpr double tol = 1e-3;
    const Vec3 n = g.direction.Normalized();
    const bool parallel = g.direction.Length() == 0 || std::abs(f.direction.Normalized().Dot(n)) > 0.999;
    const bool sameRadius = std::abs(f.radius - g.radius) < tol;
    switch (g.kind) {
        case GeometryKind::Plane: return parallel && std::abs((f.origin - g.origin).Dot(n)) < tol;
        case GeometryKind::Axis:
        case GeometryKind::Cylinder: {
            const Vec3 d = g.origin - f.origin;
            return parallel && sameRadius && (d - n * d.Dot(n)).Length() < tol;
        }
        case GeometryKind::Circle:
        case GeometryKind::Point:
        case GeometryKind::Sphere: return sameRadius && (f.origin - g.origin).Length() < tol;
    }
    return false;
}

Placement Shift(Vec3 by) { return {Rotation{}, by}; }

} // namespace

std::vector<ObjectId> PatternGroup(const assembly::Assembly& a, ObjectId component,
                                   const std::function<bool(const Component&)>& joins) {
    if (!a.FindComponent(component)) return {};
    std::vector<ObjectId> found{component};
    std::deque<ObjectId> queue{component};
    const auto take = [&](ObjectId id) {
        const Component* o = a.FindComponent(id);
        if (!o || o->grounded || In(found, id) || (joins && !joins(*o))) return;
        found.push_back(id);
        queue.push_back(id);
    };
    // A "Вставка" into a hole, as a line in the assembly.
    struct Line {
        Vec3 o, d;
    };
    const auto holeLine = [&](const Constraint& c, ObjectId member) -> std::optional<Line> {
        if (c.type != assembly::ConstraintType::Insert || c.suppressed) return std::nullopt;
        const GeometryRef& hole = c.first.component == member ? c.second : c.first;
        const Component* owner = a.FindComponent(hole.component);
        if (!owner || hole.kind != GeometryKind::Circle) return std::nullopt;
        return Line{owner->placement.Point(hole.origin), owner->placement.Direction(hole.direction).Normalized()};
    };
    for (;;) {
        while (!queue.empty()) {
            const ObjectId cur = queue.front();
            queue.pop_front();
            for (const Constraint* c : a.ConstraintsOf(cur)) take(c->first.component == cur ? c->second.component : c->first.component);
        }
        // A nut is tied to the far side of the hole, not to the bolt: whatever
        // sits in the same hole (on the same axis) comes along.
        std::vector<Line> lines;
        for (const Constraint& c : a.constraints)
            for (ObjectId m : {c.first.component, c.second.component})
                if (In(found, m) && !In(found, m == c.first.component ? c.second.component : c.first.component))
                    if (auto l = holeLine(c, m)) lines.push_back(*l);
        for (const Constraint& c : a.constraints)
            for (ObjectId m : {c.first.component, c.second.component}) {
                if (In(found, m)) continue;
                const auto l = holeLine(c, m);
                if (!l) continue;
                for (const Line& g : lines) {
                    const Vec3 v = l->o - g.o;
                    if (std::abs(l->d.Dot(g.d)) > 0.999 && (v - g.d * v.Dot(g.d)).Length() < 1e-3) {
                        take(m);
                        break;
                    }
                }
            }
        if (queue.empty()) break;
    }
    std::vector<ObjectId> out{component};
    for (const Component& c : a.components)
        if (c.id != component && In(found, c.id)) out.push_back(c.id);
    return out;
}

std::optional<GeometryRef> SeatHoleOf(const assembly::Assembly& a, const std::vector<ObjectId>& group) {
    for (const Constraint& c : a.constraints) {
        if (c.type != assembly::ConstraintType::Insert || c.suppressed) continue;
        const bool firstIn = In(group, c.first.component), secondIn = In(group, c.second.component);
        if (firstIn == secondIn) continue;
        const GeometryRef& outside = firstIn ? c.second : c.first;
        if (outside.kind == GeometryKind::Circle) return outside;
    }
    return std::nullopt;
}

Expected<std::vector<PatternSpot>> PatternSpots(const AssemblyModel& model, const assembly::Assembly& a,
                                                const ComponentPattern& p) {
    std::vector<PatternSpot> spots;
    switch (p.kind) {
        case PatternKind::Holes: {
            const Component* holder = a.FindComponent(p.rim.component);
            if (!holder || p.rim.kind != GeometryKind::Circle) return Error{"Не указано отверстие, в котором стоит образец"};
            const std::vector<GeometryRef> holes = model.SameHoles(a, p.rim);
            if (holes.empty() || (holes.front().origin - p.rim.origin).Length() > 1e-3)
                return Error{std::format("Отверстие образца не найдено на «{}»", holder->name)};
            if (holes.size() < 2) return Error{std::format("На «{}» нет других таких отверстий", holder->name)};
            for (std::size_t i = 1; i < holes.size(); ++i)
                spots.push_back({Shift(holder->placement.Direction(holes[i].origin - p.rim.origin)), holes[i]});
            break;
        }
        case PatternKind::Rectangular: {
            const Vec3 d1 = p.direction1.Normalized(), d2 = p.direction2.Normalized();
            if (p.count1 < 1 || p.count2 < 1 || d1.Length() < 0.5 || (p.count2 > 1 && d2.Length() < 0.5))
                return Error{"Неверные направления или количество массива"};
            for (int j = 0; j < p.count2; ++j)
                for (int i = 0; i < p.count1; ++i)
                    if (i || j) spots.push_back({Shift(d1 * (i * p.spacing1) + d2 * (j * p.spacing2)), {}});
            break;
        }
        case PatternKind::Circular: {
            Vec3 o = p.axisOrigin, d = p.axisDirection;
            if (p.axis.IsSet()) {
                const Component* c = a.FindComponent(p.axis.component);
                if (!c || !p.axis.HasAxis()) return Error{"Ось массива не найдена"};
                o = c->placement.Point(p.axis.origin);
                d = c->placement.Direction(p.axis.direction);
            }
            if (d.Length() < 1e-9 || p.count < 2) return Error{"Неверная ось или количество массива"};
            const bool round = std::abs(p.angle) >= 2 * kPi - 1e-9;
            const double step = round ? 2 * kPi / p.count : p.angle / (p.count - 1);
            for (int k = 1; k < p.count; ++k) {
                const Rotation r = Rotation::AxisAngle(d, k * step);
                spots.push_back({{r, o - r * o}, {}});
            }
            break;
        }
    }
    if (spots.empty()) return Error{"В массиве нет экземпляров кроме образца"};
    return spots;
}

Expected<int> ApplyPattern(const AssemblyModel& model, assembly::Assembly& a, ObjectId id) {
    const ComponentPattern* found = a.FindPattern(id);
    if (!found) return Error{"Массив не найден"};
    if (found->seed.empty() || !std::all_of(found->seed.begin(), found->seed.end(), [&](ObjectId c) { return a.FindComponent(c); }))
        return Error{std::format("{}: образец удалён", found->name)};
    Expected<std::vector<PatternSpot>> spots = PatternSpots(model, a, *found);
    if (!spots) return spots.GetError();

    a.ClearInstances(id);
    const std::vector<ObjectId> seed = a.FindPattern(id)->seed;
    // Copies: adding components and constraints moves the vectors.
    std::vector<Component> seedParts;
    for (ObjectId c : seed) seedParts.push_back(*a.FindComponent(c));
    std::vector<Constraint> seedTies;
    for (const Constraint& c : a.constraints)
        if (In(seed, c.first.component) || In(seed, c.second.component)) seedTies.push_back(c);

    std::vector<std::vector<ObjectId>> instances;
    for (const PatternSpot& spot : *spots) {
        std::vector<ObjectId> made;
        for (const Component& s : seedParts) {
            Component& c = a.AddComponent(s.path, BaseName(s.name), spot.move * s.placement);
            c.properties = s.properties;
            c.visible = s.visible;
            c.grounded = false;
            made.push_back(c.id);
        }
        const auto map = [&](GeometryRef& g) {
            const auto at = std::find(seed.begin(), seed.end(), g.component);
            if (at != seed.end()) {
                g.component = made[std::size_t(at - seed.begin())];   // the same geometry of the copy
                return true;
            }
            const Component* owner = a.FindComponent(g.component);
            if (!owner) return false;
            g = Moved(g, owner->placement, spot.move);
            return FoundThere(model, a, g);
        };
        for (Constraint k : seedTies) {
            if (!map(k.first) || !map(k.second)) continue;
            k.state = assembly::ConstraintState::Unknown;
            k.error = 0;
            a.AddConstraint(k);
        }
        instances.push_back(std::move(made));
    }
    a.FindPattern(id)->instances = std::move(instances);
    a.Touch();
    return int(spots->size());
}

} // namespace bz::kernel
