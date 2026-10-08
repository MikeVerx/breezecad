#pragma once

#include "core/Id.h"
#include "core/Math.h"

#include <optional>
#include <string_view>

namespace bz::doc {

/// Origin elements every part has: they are not features and cannot be
/// deleted. Their ids are reserved (below IdGenerator's first id).
/// The model is Z-up.
namespace origin {

inline constexpr ObjectId kPoint{1};
inline constexpr ObjectId kPlaneXY{2};   ///< top: normal +Z
inline constexpr ObjectId kPlaneXZ{3};   ///< front: normal -Y, sketch Y goes up
inline constexpr ObjectId kPlaneYZ{4};   ///< right: normal +X, sketch Y goes up
inline constexpr ObjectId kAxisX{5};
inline constexpr ObjectId kAxisY{6};
inline constexpr ObjectId kAxisZ{7};

/// First id handed out to user objects.
inline constexpr std::uint64_t kFirstUserId = 16;

/// Frame of an origin plane, or nullopt when `id` is not an origin plane.
inline std::optional<Frame> PlaneFrame(ObjectId id) {
    if (id == kPlaneXY) return Frame{{0, 0, 0}, {1, 0, 0}, {0, 0, 1}};
    if (id == kPlaneXZ) return Frame{{0, 0, 0}, {1, 0, 0}, {0, -1, 0}};
    if (id == kPlaneYZ) return Frame{{0, 0, 0}, {0, 1, 0}, {1, 0, 0}};
    return std::nullopt;
}

inline bool IsOriginElement(ObjectId id) { return id.value >= 1 && id.value <= 7; }

} // namespace origin
} // namespace bz::doc
