#pragma once
// Geometry of a document. This header is free of OCCT types so that the UI
// can use it; the renderer reaches the shapes through occt/ModelAccess.h.

#include "core/Expected.h"
#include "core/Id.h"
#include "core/Math.h"
#include "document/Document.h"

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

    struct Impl;
    const Impl& GetImpl() const { return *myImpl; }

private:
    std::unique_ptr<Impl> myImpl;
};

} // namespace bz::kernel
