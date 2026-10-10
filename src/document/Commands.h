#pragma once

#include "core/Parameter.h"
#include "document/Document.h"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace bz::doc {

/// One undoable change of a document.
class Command {
public:
    virtual ~Command() = default;
    /// Shown in the undo/redo tooltips: "Отменить: Выдавливание1".
    virtual std::string Label() const = 0;
    virtual void Do(Document& doc) = 0;
    virtual void Undo(Document& doc) = 0;
};

/// Undo/redo stack of one document. Also knows whether the document has
/// unsaved changes: the position of the last save is remembered.
class History {
public:
    /// Runs the command and puts it on the stack; drops the redo tail.
    void Execute(Document& doc, std::unique_ptr<Command> command);

    bool CanUndo() const { return myPosition > 0; }
    bool CanRedo() const { return myPosition < myCommands.size(); }
    std::string UndoLabel() const;
    std::string RedoLabel() const;
    void Undo(Document& doc);
    void Redo(Document& doc);

    /// Changes since the last save (or since the document was opened).
    bool IsModified() const { return myPosition != mySavedPosition; }
    void MarkSaved() { mySavedPosition = myPosition; }
    /// The document differs from any saved file (e.g. recovered from autosave).
    void MarkUnsaved() { mySavedPosition = kNever; }
    void Clear();

private:
    static constexpr std::size_t kLimit = 500;   // oldest steps are forgotten
    static constexpr std::size_t kNever = static_cast<std::size_t>(-1);

    std::vector<std::unique_ptr<Command>> myCommands;
    std::size_t                           myPosition      = 0;
    std::size_t                           mySavedPosition = 0;
};

// --- commands ---------------------------------------------------------------
// Each factory captures what it needs from the current document so that the
// command can later undo itself exactly.

/// Inserts a new feature at `index` (end of the active part by default).
std::unique_ptr<Command> AddFeature(std::unique_ptr<Feature> feature, std::size_t index);

/// Deletes a feature. Features that used it show an error until fixed.
std::unique_ptr<Command> RemoveFeature(const Document& doc, ObjectId id);

/// Replaces a feature with an edited copy (same id): any edit of inputs,
/// renaming, suppressing, every step of sketch editing.
std::unique_ptr<Command> ReplaceFeature(std::unique_ptr<Feature> edited, std::string label);

/// Moves a feature in the tree.
std::unique_ptr<Command> MoveFeature(const Document& doc, ObjectId id, std::size_t newIndex);

/// Moves the end-of-part marker.
std::unique_ptr<Command> SetEndMarker(const Document& doc, std::size_t index);

/// Replaces the whole variables table.
std::unique_ptr<Command> SetVariables(std::vector<Parameter> variables);

/// Changes the document's display units (values stay the same physically).
std::unique_ptr<Command> SetUnits(UnitSystem units);

/// Changes the part's name, designation or material.
std::unique_ptr<Command> SetPartInfo(PartInfo info);

/// Several commands as one undo step.
/// Replaces the look of the part (colours of bodies and faces).
std::unique_ptr<Command> SetLook(Look look, std::string label);

std::unique_ptr<Command> Group(std::string label, std::vector<std::unique_ptr<Command>> commands);

} // namespace bz::doc
