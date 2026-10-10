#pragma once

#include "app/AssemblyState.h"
#include "core/Expected.h"
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
    ~Session();   // waits for a rebuild still running on another thread
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    doc::Document  doc;
    doc::History   history;
    kernel::Model  model;

    /// Set for an assembly: `doc` then stays empty and `model` shows the
    /// placed components (one body per component, body id = component id).
    std::unique_ptr<AssemblyState> assembly;
    bool IsAssembly() const { return assembly != nullptr; }
    /// Makes this session an assembly (an empty one).
    AssemblyState& MakeAssembly();
    /// The kind of document (doc::DocumentTypes): "part" or "assembly".
    std::string_view DocumentType() const;

    /// Counts every change of the content (autosave).
    std::uint64_t ContentRevision() const;
    /// Writes the document to `target` (part or assembly). An assembly
    /// written elsewhere than its own file keeps pointing at the same
    /// parts (its relative paths are adjusted in the copy).
    Status WriteFile(const std::filesystem::path& target, std::string_view generator) const;

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
    bool IsModified() const { return assembly ? assembly->IsModified() : history.IsModified(); }
    bool CanUndo() const { return assembly ? assembly->CanUndo() : history.CanUndo(); }
    bool CanRedo() const { return assembly ? assembly->CanRedo() : history.CanRedo(); }
    std::string UndoLabel() const { return assembly ? assembly->UndoLabel() : history.UndoLabel(); }
    /// After saving to `path` (the session's file from now on).
    void MarkSaved();

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
    /// The same, to the end at once (before reading the result: a commit,
    /// an export).
    bool RebuildNow();
    /// A rebuild is under way, in slices or on another thread (the window
    /// shows its progress).
    bool Rebuilding() const { return myRebuilding; }
    /// The rebuild under way runs on another thread (the window only has
    /// to show that it is busy).
    bool RebuildingInBackground() const { return myBackground != nullptr; }
    /// How far that rebuild is.
    kernel::Model::Progress RebuildProgress() const;

    /// Display quality (Settings::graphics): a rebuild on another thread
    /// meshes its bodies for it there, not in the window's thread.
    void SetMeshQuality(int level) { myMeshQuality = level; }
    /// Long rebuilds may run on another thread (Settings::backgroundRebuild).
    void AllowBackground(bool allow) { myBackgroundAllowed = allow; }

    /// While a sketch or an operation is edited: rebuild only up to it
    /// (`last`, a feature index); the rest follows when editing ends.
    /// `showAtLast`: show the part right after it (an operation's preview).
    void LimitRebuild(std::optional<std::size_t> last, bool showAtLast) {
        myRebuildLimit = last;
        myShowAtLimit = showAtLast;
    }

private:
    struct Background;
    /// A long rebuild runs on another thread, on copies of the document
    /// and of the shapes it starts from; the window keeps showing the part
    /// as it was until it is done.
    std::unique_ptr<Background> myBackground;
    void StartBackground(std::size_t limit);
    /// Waits for the background rebuild and takes its result. True when
    /// it was made for the document as it is now.
    bool FinishBackground();

    std::optional<std::size_t> myRebuildLimit;
    bool                       myRebuilding = false;
    bool                       Rebuild(double budgetMs);
    bool                       myShowAtLimit = false;
    int                        myMeshQuality = 1;
    bool                       myBackgroundAllowed = true;
    std::uint64_t myBuiltRevision = static_cast<std::uint64_t>(-1);
    double        myLastRebuildSeconds = 0;   ///< how long the last rebuild took
    double        myLastRebuildAt = 0;        ///< when it finished (ImGui time)
};

} // namespace bz::app
