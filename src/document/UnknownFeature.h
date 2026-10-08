#pragma once

#include "document/Feature.h"

#include <memory>

namespace bz::doc {

/// A feature whose type this build does not know (made by a newer version
/// or by a module that is not installed). Its data is kept verbatim and
/// written back on save, so opening and saving never loses it.
class UnknownFeature final : public FeatureBase<UnknownFeature> {
public:
    UnknownFeature();
    UnknownFeature(const UnknownFeature& other);
    UnknownFeature& operator=(const UnknownFeature& other);
    ~UnknownFeature() override;

    std::string typeId;   ///< the type found in the file

    std::string_view Type() const override { return typeId; }
    void SaveFields(nlohmann::json& out) const override;
    Status LoadFields(const nlohmann::json& in) override;

private:
    std::unique_ptr<nlohmann::json> myFields;
};

} // namespace bz::doc
