#include "kernel/AutoDrop.h"

#include <cmath>
#include <format>

namespace bz::kernel {

using assembly::GeometryKind;
using assembly::GeometryRef;
using assembly::Placement;
using assembly::Rotation;

Placement SeatOnRim(const doc::parts::Seat& seat, Vec3 centre, Vec3 normal) {
    // The seat's normal goes against the rim's.
    const Vec3 n = normal.Normalized();
    const Vec3 zWorld = seat.normal.z < 0 ? n : -n;
    const Vec3 x = std::abs(zWorld.x) < 0.9 ? Vec3{1, 0, 0} : Vec3{0, 1, 0};
    Placement p;
    p.rotation = Rotation::FromXZ(x, zWorld);
    p.translation = centre - p.rotation * seat.centre;
    return p;
}

namespace {

GeometryRef SeatRef(const doc::parts::Seat& seat) {
    GeometryRef g;
    g.kind = GeometryKind::Circle;
    g.origin = seat.centre;
    g.direction = seat.normal;
    g.radius = seat.radius;
    g.pick = seat.centre + Vec3{seat.radius, 0, 0};
    return g;
}

assembly::Constraint InsertOn(const GeometryRef& rim, const doc::parts::Seat& seat) {
    assembly::Constraint c;
    c.type = assembly::ConstraintType::Insert;
    c.first = rim;
    c.second = SeatRef(seat);
    return c;
}

} // namespace

Expected<AutoDropPlan> PlanAutoDrop(const AssemblyModel& model, const assembly::Assembly& a, const GeometryRef& rim,
                                    const AutoDropOptions& options) {
    namespace parts = doc::parts;
    const assembly::Component* holder = a.FindComponent(rim.component);
    if (!holder || rim.kind != GeometryKind::Circle || rim.radius <= 0) return Error{"Укажите кромку отверстия"};
    const parts::StandardInfo* screw = parts::FindStandard(options.screw);
    if (!screw) return Error{std::format("Неизвестный стандарт «{}»", options.screw)};

    AutoDropPlan plan;
    plan.holeDiameter = 2 * rim.radius;
    plan.size = parts::SizeForHole(*screw, plan.holeDiameter);
    if (plan.size.empty())
        return Error{std::format("{}: нет размера для отверстия ⌀{:.1f} мм", screw->title, plan.holeDiameter)};
    const std::optional<HoleStack> stack = model.StackAt(a, rim);
    if (!stack) return Error{"Под отверстием нет материала"};
    plan.stack = stack->thickness;

    // Sizes of the washer and the nut: the same thread.
    const auto sized = [&](const std::string& id) -> std::optional<parts::Request> {
        if (id.empty()) return std::nullopt;
        const parts::StandardInfo* s = parts::FindStandard(id);
        if (!s) return std::nullopt;
        parts::Request q{id, parts::MatchingSize(*screw, plan.size, *s)};
        return q;
    };
    const std::optional<parts::Request> washer = sized(options.washer), nut = sized(options.nut);
    const double washerH = washer ? parts::HeightOf(*parts::FindStandard(washer->standard), washer->size) : 0;
    const double nutH = nut ? parts::HeightOf(*parts::FindStandard(nut->standard), nut->size) : 0;
    const double dn = 2 * parts::SeatOf({std::string(screw->id), plan.size})->radius;
    if (nut && !stack->farRim.IsSet()) return Error{"Не найдена кромка отверстия с другой стороны — гайку некуда поставить"};

    // Through the stack and the washer; with a nut also through it and two pitches beyond.
    const double needed = plan.stack + washerH + (nut ? nutH + 2 * parts::ThreadPitch(dn) : 0);
    plan.length = parts::LengthFor(*screw, plan.size, needed);
    if (plan.length <= 0)
        return Error{std::format("{} {}: нет длины не меньше {:.1f} мм", screw->title, plan.size, needed)};

    const Vec3 o = holder->placement.Point(rim.origin);
    const Vec3 n = holder->placement.Direction(rim.direction).Normalized();

    // Under the head: a washer on the rim, the screw on the washer (or the rim).
    GeometryRef screwRim = rim;
    int screwOn = -1;
    Vec3 screwAt = o;
    if (washer) {
        const auto seat = parts::SeatOf(*washer);
        const auto top = parts::TopOf(*washer);
        if (!seat || !top) return Error{"Шайба этого стандарта не подходит"};
        DropItem w{*washer, SeatOnRim(*seat, o, n), InsertOn(rim, *seat), -1};
        screwRim = SeatRef(*top);
        screwOn = 0;
        screwAt = w.placement.Point(top->centre);
        plan.items.push_back(std::move(w));
    }
    parts::Request screwRequest{std::string(screw->id), plan.size, plan.length};
    const auto screwSeat = parts::SeatOf(screwRequest);
    if (!screwSeat) return Error{std::format("{}: такие изделия не ставятся по отверстию", screw->title)};
    plan.items.push_back({screwRequest, SeatOnRim(*screwSeat, screwAt, n), InsertOn(screwRim, *screwSeat), screwOn});

    if (nut) {
        const auto seat = parts::SeatOf(*nut);
        if (!seat) return Error{"Гайка этого стандарта не подходит"};
        const assembly::Component* farPart = a.FindComponent(stack->last);
        const Vec3 farAt = farPart->placement.Point(stack->farRim.origin);
        const Vec3 farN = farPart->placement.Direction(stack->farRim.direction);
        plan.items.push_back({*nut, SeatOnRim(*seat, farAt, farN), InsertOn(stack->farRim, *seat), -1});
    }
    return plan;
}

} // namespace bz::kernel
