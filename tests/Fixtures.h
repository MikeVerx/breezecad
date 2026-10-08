#pragma once
// Small builders for test documents.

#include "document/Document.h"
#include "document/ExtrudeFeature.h"
#include "document/Origin.h"
#include "document/SketchFeature.h"

#include <string>

namespace bz::test {

/// New empty sketch on an origin plane. Returns the feature (owned by doc).
inline doc::SketchFeature& AddSketch(doc::Document& d, ObjectId plane = doc::origin::kPlaneXY) {
    auto sk = std::make_unique<doc::SketchFeature>();
    sk->id = d.Ids().Next();
    sk->name = d.UniqueFeatureName("Эскиз");
    sk->placement = {plane, *doc::origin::PlaneFrame(plane), false};
    sk->sketch.EnsureOrigin(d.Ids());
    auto* raw = sk.get();
    d.Insert(std::move(sk), d.FeatureCount());
    return *raw;
}

/// Axis-aligned rectangle made of four connected lines (no constraints).
inline void AddRect(doc::Document& d, doc::SketchFeature& sk, Vec2 lo, Vec2 hi) {
    auto& s = sk.sketch;
    IdGenerator& ids = d.Ids();
    const ObjectId p0 = s.AddPoint(ids, lo), p1 = s.AddPoint(ids, {hi.x, lo.y});
    const ObjectId p2 = s.AddPoint(ids, hi), p3 = s.AddPoint(ids, {lo.x, hi.y});
    s.AddLine(ids, p0, p1);
    s.AddLine(ids, p1, p2);
    s.AddLine(ids, p2, p3);
    s.AddLine(ids, p3, p0);
}

inline void AddCircle(doc::Document& d, doc::SketchFeature& sk, Vec2 center, double radius) {
    sk.sketch.AddCircle(d.Ids(), sk.sketch.AddPoint(d.Ids(), center), radius);
}

inline doc::ExtrudeFeature& AddExtrude(doc::Document& d, ObjectId sketch, const std::string& distance,
                                       doc::BooleanOp op = doc::BooleanOp::NewBody) {
    auto ex = std::make_unique<doc::ExtrudeFeature>();
    ex->id = d.Ids().Next();
    ex->name = d.UniqueFeatureName("Выдавливание");
    ex->sketch = sketch;
    ex->operation = op;
    ex->distance.name = d.NextParameterName();
    ex->distance.expression = distance;
    auto* raw = ex.get();
    d.Insert(std::move(ex), d.FeatureCount());
    return *raw;
}

} // namespace bz::test
