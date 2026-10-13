#pragma once
// AutoDrop: a standard part dropped onto a hole of an assembly picks its
// size by the hole and its length by the stack of parts it holds together,
// and comes with the washer and the nut asked for. The plan says what to
// make, where to put it and which "Вставка" ties it; the application makes
// the part files and adds the components and constraints.

#include "assembly/Assembly.h"
#include "core/Expected.h"
#include "document/StandardParts.h"
#include "kernel/AssemblyModel.h"

#include <string>
#include <vector>

namespace bz::kernel {

struct AutoDropOptions {
    std::string screw;    ///< standard id of the bolt or screw ("gost7798")
    std::string washer;   ///< under the head; empty: none
    std::string nut;      ///< on the far side; empty: none (a screw into a tapped part)
};

struct DropItem {
    doc::parts::Request request;     ///< what to make
    assembly::Placement placement;   ///< where the component goes (already in place)
    /// "Вставка" tying it: `first` is the rim it sits on, `second` its own
    /// seat (its component is the new one, unset here).
    assembly::Constraint insert;
    /// `first` belongs to an earlier item (index), or -1: to a component of
    /// the assembly (`first.component` set).
    int seatOn = -1;
};

struct AutoDropPlan {
    double                holeDiameter = 0;   ///< mm
    double                stack = 0;          ///< mm, thickness of the parts held together
    std::string           size;               ///< "М10"
    double                length = 0;         ///< of the screw, mm
    std::vector<DropItem> items;              ///< washer, screw, nut — in the order to add them
};

/// Plans dropping a screw onto `rim` (a Circle picked on a component with
/// GeometryAt). Errors say why it does not fit, in Russian.
Expected<AutoDropPlan> PlanAutoDrop(const AssemblyModel& model, const assembly::Assembly& assembly,
                                    const assembly::GeometryRef& rim, const AutoDropOptions& options);

/// Placement that puts a part's `seat` (its own coordinates) onto a rim at
/// `centre` whose outward normal is `normal` (assembly coordinates): the
/// seat faces the rim, centres coincide.
assembly::Placement SeatOnRim(const doc::parts::Seat& seat, Vec3 centre, Vec3 normal);

} // namespace bz::kernel
