#pragma once

#include <compare>
#include <cstdint>
#include <functional>

namespace bz {

/// Stable identifier of an object inside one document (feature, sketch
/// entity, constraint, parameter). Never reused, survives save/load, so
/// references between objects stay valid when the tree is edited.
struct ObjectId {
    std::uint64_t value = 0;

    constexpr bool IsValid() const { return value != 0; }
    constexpr explicit operator bool() const { return IsValid(); }
    constexpr auto operator<=>(const ObjectId&) const = default;
};

/// Hands out fresh ids. One generator per document; its state is saved
/// with the document so ids stay unique across sessions.
class IdGenerator {
public:
    ObjectId Next() { return ObjectId{++myLast}; }

    /// Ensures future ids are greater than an id loaded from a file.
    void Reserve(ObjectId used) {
        if (used.value > myLast) myLast = used.value;
    }

    std::uint64_t Last() const { return myLast; }

private:
    std::uint64_t myLast = 0;
};

} // namespace bz

template <>
struct std::hash<bz::ObjectId> {
    std::size_t operator()(const bz::ObjectId& id) const noexcept {
        return std::hash<std::uint64_t>{}(id.value);
    }
};
