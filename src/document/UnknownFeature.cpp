#include "document/UnknownFeature.h"

#include <nlohmann/json.hpp>

namespace bz::doc {

UnknownFeature::UnknownFeature() : myFields(std::make_unique<nlohmann::json>(nlohmann::json::object())) {}

UnknownFeature::UnknownFeature(const UnknownFeature& other)
    : FeatureBase(other), typeId(other.typeId), myFields(std::make_unique<nlohmann::json>(*other.myFields)) {}

UnknownFeature& UnknownFeature::operator=(const UnknownFeature& other) {
    if (this != &other) {
        FeatureBase::operator=(other);
        typeId   = other.typeId;
        myFields = std::make_unique<nlohmann::json>(*other.myFields);
    }
    return *this;
}

UnknownFeature::~UnknownFeature() = default;

void UnknownFeature::SaveFields(nlohmann::json& out) const {
    for (const auto& [key, value] : myFields->items()) out[key] = value;
}

Status UnknownFeature::LoadFields(const nlohmann::json& in) {
    *myFields = in;
    for (const char* common : {"id", "type", "name", "suppressed"}) myFields->erase(common);
    return Status::Ok();
}

} // namespace bz::doc
