#pragma once
// JSON helpers shared by the .bzc format code. Kept out of public headers
// so that only serialization code pays for including nlohmann/json.

#include "core/Expected.h"
#include "core/Id.h"
#include "core/Math.h"
#include "core/Parameter.h"
#include "document/EdgeReference.h"
#include "document/FaceReference.h"

#include <nlohmann/json.hpp>

#include <array>
#include <string>
#include <string_view>
#include <utility>

namespace bz::doc::json_io {

using Json = nlohmann::json;

inline Json ToJson(Vec2 v) { return Json::array({v.x, v.y}); }
inline Json ToJson(Vec3 v) { return Json::array({v.x, v.y, v.z}); }
inline Json ToJson(ObjectId id) { return id.value; }

inline Json ToJson(const Frame& f) {
    return Json{{"origin", ToJson(f.origin)}, {"xDir", ToJson(f.xDir)}, {"normal", ToJson(f.normal)}};
}

std::string_view QuantityId(Quantity q);
Quantity         QuantityFromId(std::string_view id);

Json ToJson(const Parameter& p);
inline Json ToJson(const FaceReference& f) { return Json{{"normal", ToJson(f.normal)}, {"point", ToJson(f.point)}}; }

// Readers return false (and leave `out` unchanged) on malformed input.
bool Read(const Json& j, Vec2& out);
bool Read(const Json& j, Vec3& out);
bool Read(const Json& j, ObjectId& out);
bool Read(const Json& j, Frame& out);
bool Read(const Json& j, Parameter& out);
bool Read(const Json& j, FaceReference& out);
Json ToJson(const EdgeReference& e);
bool Read(const Json& j, EdgeReference& out);

/// Enum values in files are written by name: `table` lists {value, name}.
template <class E, std::size_t N>
std::string_view IdOf(const std::array<std::pair<E, std::string_view>, N>& table, E value) {
    for (const auto& [v, id] : table)
        if (v == value) return id;
    return table.front().second;
}

/// The value named `id` in `table`; false when the name is unknown.
template <class E, std::size_t N>
bool FromId(const std::array<std::pair<E, std::string_view>, N>& table, std::string_view id, E& out) {
    for (const auto& [v, name] : table)
        if (name == id) {
            out = v;
            return true;
        }
    return false;
}

/// Reads an optional member; missing keeps the default, malformed fails.
template <class T>
bool ReadMember(const Json& obj, const char* key, T& out) {
    const auto it = obj.find(key);
    if (it == obj.end()) return true;
    if constexpr (std::is_same_v<T, bool> || std::is_same_v<T, std::string> || std::is_arithmetic_v<T>) {
        try {
            out = it->template get<T>();
            return true;
        } catch (const nlohmann::json::exception&) {
            return false;
        }
    } else {
        return Read(*it, out);
    }
}

} // namespace bz::doc::json_io
