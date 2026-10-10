#pragma once
// Geometry of a document. This header is free of OCCT types so that the UI
// can use it; the renderer reaches the shapes through occt/ModelAccess.h.

#include "core/Expected.h"
#include "core/Id.h"
#include "core/Math.h"
#include "document/Document.h"
#include "document/EdgeReference.h"
#include "kernel/Projection.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace bz::kernel {

/// One closed region of a sketch, for highlighting and profile picking.
struct SketchRegion {
    Vec2                           sample;        ///< a point inside the region
    std::vector<std::vector<Vec2>> loops;         ///< loops[0] outer, the rest holes
    bool                           automatic = false;   ///< part of the automatic profile
};

struct BodyInfo {
    ObjectId    id;      ///< id of the feature that created the body
    std::string name;    ///< "Тело1"
};

struct Bounds {
    Vec3 min;
    Vec3 max;
};

/// What "МЦХ" shows. Mass is volume times the density the user picks.
struct MassProperties {
    double volume = 0.0;   ///< mm^3
    double area = 0.0;     ///< surface, mm^2
    Vec3   centre;         ///< centre of volume (of mass for a uniform material), mm
    Bounds bounds;         ///< tight box around the bodies
};

/// Where two bodies overlap ("Коллизии").
struct Interference {
    std::size_t first = 0, second = 0;   ///< indices into Bodies()
    double      volume = 0.0;            ///< of the shared material, mm^3
    Vec3        centre;                  ///< its centre, mm
};

/// Mesh quality for STL and 3MF export.
struct MeshQuality {
    double chordal = 0.02;   ///< max distance between mesh and surface, mm
    double angular = 0.25;   ///< max angle between mesh normals, rad
};

class Model {
public:
    Model();
    ~Model();
    Model(Model&&) noexcept;
    Model& operator=(Model&&) noexcept;

    /// Brings geometry up to date with the document: evaluates parameters,
    /// solves sketches and rebuilds solids from the first changed feature.
    /// Sets every feature's status. Never throws.
    void Rebuild(doc::Document& doc);

    /// What a partial rebuild shows when it stops early.
    enum class Stopped { KeepShown, ShowAtStop };
    /// Rebuilds only up to feature number `last` (included); the features
    /// after it stay to be rebuilt later (Document::DirtyFrom). While a
    /// sketch is edited the part shown is kept as it was (KeepShown); while
    /// an operation is edited it is shown as it is right after it
    /// (ShowAtStop). Editing then waits for nothing that comes after.
    void RebuildUpTo(doc::Document& doc, std::size_t last, Stopped shown);

    /// As RebuildUpTo, but returns after about `budgetMs` (at least one
    /// operation): the window stays alive while a large part rebuilds, a
    /// slice per frame. True when it got to the end (or to `last`); the
    /// part shown changes only then.
    bool RebuildSlice(doc::Document& doc, std::size_t last, Stopped shown, double budgetMs);

    /// How far the rebuild in progress is: operations done and to do.
    struct Progress {
        std::size_t done = 0, total = 0;
    };
    Progress RebuildProgress() const;

    /// Bumped whenever the geometry changes (the viewport re-syncs on it).
    std::uint64_t Revision() const;

    const std::vector<BodyInfo>& Bodies() const;
    std::optional<Bounds>        BoundingBox() const;

    /// Regions of a sketch feature, or nullptr when it has none yet.
    const std::vector<SketchRegion>* Regions(ObjectId sketchFeature) const;

    /// Exports all visible bodies.
    Status ExportStep(const std::filesystem::path& path) const;
    Status ExportStl(const std::filesystem::path& path, MeshQuality quality = {}) const;
    Status Export3mf(const std::filesystem::path& path, MeshQuality quality = {}) const;

    /// Volume of all bodies, mm^3 (status bar, tests).
    double Volume() const;

    /// Every pair of bodies that share material (touching does not count).
    /// Computed on demand and kept until the geometry changes.
    const std::vector<Interference>& Interferences() const;

    /// The operation (before feature number `beforeIndex`) whose added or
    /// cut material has its surface at `point`: what a click on the model
    /// picked, for choosing what a pattern or mirror copies. Latest first.
    ObjectId FeatureAt(Vec3 point, const doc::Document& doc, std::size_t beforeIndex) const;
    /// A model edge as it would appear on `plane`, taken from the bodies as
    /// they were before feature number `featureIndex` (a sketch projecting it).
    Expected<ProjectedCurve> ProjectEdge(const doc::EdgeReference& edge, const Frame& plane, std::size_t featureIndex) const;

    /// Volume, area, centre and exact size of all bodies; nullopt without bodies.
    std::optional<MassProperties> Properties() const;

    struct Impl;
    const Impl& GetImpl() const { return *myImpl; }

private:
    std::unique_ptr<Impl> myImpl;
};

} // namespace bz::kernel
