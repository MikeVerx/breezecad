#pragma once

#include "document/Feature.h"

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <string_view>

namespace bz::doc {

/// Creates features by their file type id. Built-in types are registered
/// in Default(); modules add their own types at start-up.
class FeatureRegistry {
public:
    using Factory = std::function<std::unique_ptr<Feature>()>;

    /// Registry with every built-in feature type.
    static FeatureRegistry& Default();

    void Register(std::string typeId, Factory factory);

    /// A new feature of `typeId`, or nullptr when the type is unknown.
    std::unique_ptr<Feature> Create(std::string_view typeId) const;
    /// Every registered type id, sorted.
    std::vector<std::string> Types() const;

private:
    std::map<std::string, Factory, std::less<>> myFactories;
};

} // namespace bz::doc
