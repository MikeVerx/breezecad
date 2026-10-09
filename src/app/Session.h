#pragma once

#include "document/Commands.h"
#include "document/Document.h"
#include "kernel/Model.h"
#include "render/Viewport.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace bz::app {

enum class EditMode : unsigned char {
    Model,       ///< the part with its tree
    PickPlane,   ///< "Начать эскиз": waiting for a plane or a flat face
    Sketch,      ///< editing a sketch
};

/// One open document with everything that belongs to it: the recipe,
/// undo history, computed geometry, file, camera and editing state.
class Session {
public:
    Session();

    doc::Document  doc;
    doc::History   history;
    kernel::Model  model;

    std::filesystem::path path;        ///< empty until saved once
    std::string           untitledName; ///< "Деталь1" for documents never saved
    std::string           autosaveKey;  ///< folder name for autosave copies

    render::CameraState camera;
    bool                hasCamera = false;

    EditMode mode = EditMode::Model;
    ObjectId activeSketch;      ///< sketch being edited in EditMode::Sketch
    ObjectId selectedFeature;   ///< selection in the tree

    // --- autosave bookkeeping ---
    double        unsavedSince      = -1.0;   ///< time of the first unsaved change, -1 = clean
    std::uint64_t autosavedRevision = 0;

    /// File name without extension, or the untitled name.
    std::string Title() const;
    bool IsModified() const { return history.IsModified(); }

    /// Runs an undoable command.
    void Execute(std::unique_ptr<doc::Command> command);
    void Undo();
    void Redo();

    /// Commits an edit that was already applied to feature `before->id` in
    /// place (live preview, dragging): the edit becomes one undo step that
    /// restores `before`.
    void CommitInPlaceEdit(std::unique_ptr<doc::Feature> before, std::string label);

    /// Rebuilds geometry if the document changed since the last rebuild.
    /// Returns true when it did.
    bool RebuildIfNeeded();

private:
    std::uint64_t myBuiltRevision = static_cast<std::uint64_t>(-1);
};

} // namespace bz::app
