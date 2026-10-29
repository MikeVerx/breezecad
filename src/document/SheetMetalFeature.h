#pragma once
// Sheet metal (docs/ADR/0007-sheet-metal.md).
//
// «Лист» turns one closed sketch region into a flat plate of the sheet's
// thickness and carries the bend rules of the part: only the thickness has to
// be typed, the bend radius and the K-factor follow from it (R = s, K by
// DIN 6935) until the user types their own.
// «Отбортовка» bends a flange up or down on a straight edge of that plate.
//
// Both keep only what the user chose; the folded body and the flat pattern
// are computed (kernel::Model, src/sheetmetal).

#include "document/SketchSolidFeature.h"
#include "sheetmetal/SheetPart.h"
#include "sheetmetal/SheetRules.h"

namespace bz::doc {

class SheetMetalFeature final : public FeatureBase<SheetMetalFeature, SketchSolidFeature> {
public:
    static constexpr std::string_view kType = "sheetMetal";

    Parameter thickness{.name = "", .expression = "2", .quantity = Quantity::Length, .value = 2.0, .error = {}};
    /// Inner bend radius; follows the thickness while `radiusAuto`.
    Parameter radius{.name = "", .expression = "2", .quantity = Quantity::Length, .value = 2.0, .error = {}};
    bool      radiusAuto = true;
    /// K-factor (0..1); computed by DIN 6935 from R / s while `kAuto`.
    Parameter kFactor{.name = "", .expression = "0.325", .quantity = Quantity::Scalar, .value = 0.325, .error = {}};
    bool      kAuto = true;
    /// The plate goes below the sketch plane instead of above it.
    bool      reversed = false;

    std::string_view Type() const override { return kType; }
    void CollectParameters(std::vector<Parameter*>& out) override;
    void SaveFields(nlohmann::json& out) const override;
    Status LoadFields(const nlohmann::json& in) override;

    /// The rules with the automatic values worked out from the thickness.
    sheet::Rules Rules() const;
    /// Writes the automatic values into `radius` and `kFactor` (value and
    /// expression), so the panel and the variables table show what is used.
    void ApplyAuto();
};

/// How much of a plate edge a flange takes.
enum class FlangeWidth : unsigned char {
    Edge,       ///< the whole edge
    Offsets,    ///< `offset1` from the edge's start, `offset2` before its end
    Centered,   ///< `width` wide in the middle of the edge
};

class FlangeFeature final : public FeatureBase<FlangeFeature> {
public:
    static constexpr std::string_view kType = "sheetFlange";

    ObjectId  sheet;   ///< the «Лист» it bends
    /// Set: stands on the far edge of this «Отбортовка» (a flange on a
    /// flange) instead of an edge of the plate; `edge` is then only where it
    /// was shown.
    ObjectId  onFlange;
    /// With `onFlange`: which edge of that flange (far: on top of it; start /
    /// end: its side edges, the flange bends from the edge itself).
    sheet::FlangeEdge onEdge = sheet::FlangeEdge::Far;
    /// A point on the chosen edge of the plate, model space; the nearest
    /// straight edge of the plate's outline is used. Rebuilds move it to the
    /// middle of that edge, so it follows the edge when sizes change.
    Vec3      edge;
    bool      edgeSet = false;
    /// Outer height of the flange, from the virtual sharp of the bend.
    Parameter height{.name = "", .expression = "20", .quantity = Quantity::Length, .value = 20.0, .error = {}};
    Parameter angle{.name = "", .expression = "90", .quantity = Quantity::Angle, .value = kPi / 2, .error = {}};
    bool      down = false;   ///< bent to the other side of the plate
    /// On a plate edge: how much of it (on a flange: always all of its edge).
    FlangeWidth widthMode = FlangeWidth::Edge;
    Parameter offset1{.name = "", .expression = "0", .quantity = Quantity::Length, .value = 0.0, .error = {}};
    Parameter offset2{.name = "", .expression = "0", .quantity = Quantity::Length, .value = 0.0, .error = {}};
    Parameter width{.name = "", .expression = "50", .quantity = Quantity::Length, .value = 50.0, .error = {}};

    std::string_view Type() const override { return kType; }
    std::vector<ObjectId> Dependencies() const override {
        if (onFlange) return {sheet, onFlange};
        return {sheet};
    }
    void CollectParameters(std::vector<Parameter*>& out) override;
    void SaveFields(nlohmann::json& out) const override;
    Status LoadFields(const nlohmann::json& in) override;
};

/// «Вырез»: closed contours of a sketch lying on a flat face of the sheet
/// (the plate's or a flange's) cut through it; the flat pattern has them too.
class SheetCutFeature final : public FeatureBase<SheetCutFeature, SketchSolidFeature> {
public:
    static constexpr std::string_view kType = "sheetCut";

    ObjectId sheet;   ///< the «Лист»

    std::string_view Type() const override { return kType; }
    std::vector<ObjectId> Dependencies() const override { return {sketch, sheet}; }
    void SaveFields(nlohmann::json& out) const override;
    Status LoadFields(const nlohmann::json& in) override;
};

/// "2.5" for 2.5, "0.33" for 0.3251: how automatic values are written.
std::string SheetNumber(double v, int decimals = 3);

} // namespace bz::doc
