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

    /// Ensures future ids are greater than an id loaded from a file. Ids
    /// beyond kLargest (damage, or ids the program keeps for itself such as
    /// the assembly origin's 2^64-1) are not counted on: they would make the
    /// next id overflow to 0, the "no id".
    void Reserve(ObjectId used) {
        if (used.value > myLast && used.value <= kLargest) myLast = used.value;
    }
    static constexpr std::uint64_t kLargest = std::uint64_t{1} << 62;

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
