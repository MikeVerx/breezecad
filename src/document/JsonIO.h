#pragma once
// JSON helpers shared by the .bzc format code. Kept out of public headers
// so that only serialization code pays for including nlohmann/json.

#include "core/Expected.h"
#include "core/Id.h"
#include "core/Math.h"
#include "core/Parameter.h"
#include "document/FaceReference.h"

#include <nlohmann/json.hpp>

#include <string>

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
