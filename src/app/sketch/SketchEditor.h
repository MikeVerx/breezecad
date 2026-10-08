#pragma once
// Sketch mode: drawing tools, constraints, dimensions, selection and
// dragging, with CAD feedback (snaps, inference, fully defined colours).
//
// Every finished action is one undo step: the sketch feature is edited in
// place for live feedback and then committed with Session::CommitInPlaceEdit.

#include "app/AppContext.h"
#include "app/sketch/SketchPick.h"
#include "document/SketchFeature.h"
#include "sketch/Solver.h"

#include <memory>
#include "render/Viewport.h"

#include <optional>
#include <span>
#include <string>
#include <unordered_set>
#include <vector>

namespace bz::app {

enum class SketchTool : unsigned char {
    Select, Line, Rectangle, Circle, Arc, Point, Dimension,
    Coincident, Horizontal, Vertical, Parallel, Perpendicular, Equal, Tangent, Fix, Midpoint, Concentric,
};

struct SketchToolInfo {
    SketchTool  tool;
    const char* label;     ///< "Отрезок"
    const char* icon;      ///< "sk-line"
    const char* hint;      ///< first step hint for the status bar
};

/// All tools in ribbon order.
std::span<const SketchToolInfo> SketchToolList();
const SketchToolInfo& InfoOf(SketchTool tool);

class SketchEditor {
public:
    // --- entering and leaving --------------------------------------------
    /// Creates a sketch on `placement` (undoable) and starts editing it.
    void CreateAndEdit(AppContext& app, Session& session, const doc::SketchPlacement& placement);
    void Edit(AppContext& app, Session& session, ObjectId sketch);
    void Finish(AppContext& app, Session& session);
    /// Turns the camera to look straight at the sketch plane (F6).
    void LookAtSketch(AppContext& app, Session& session);
    /// Zooms out (keeping the direction) when the sketch left the screen.
    void KeepSketchInView(AppContext& app, Session& session);

    // --- tools ------------------------------------------------------------
    SketchTool Tool() const { return myTool; }
    void SetTool(AppContext& app, Session& session, SketchTool tool);
    /// Esc: drops the current step, then the tool, then leaves the sketch.
    void Escape(AppContext& app, Session& session);
    void DeleteSelection(Session& session);
    void ToggleConstruction(Session& session);
    bool HasSelection() const { return !mySelection.empty(); }

    /// Input and drawing inside the viewport, once per frame.
    void Frame(AppContext& app, Session& session, const ViewportFrame& vf);

    /// Status bar: what to do next.
    std::string Hint(const Session& session) const;
    /// Cursor position in sketch coordinates, when over the plane.
    std::optional<Vec2> Cursor() const { return myCursor; }

    /// Forgets transient state (when switching documents).
    void Reset();

private:
    doc::SketchFeature* Active(Session& session) const;

    // Tool handlers, called from Frame().
    void HandleSelect(AppContext& app, Session& session, const SketchView& view);
    void HandleDrawing(AppContext& app, Session& session, const SketchView& view);
    void HandleDimension(AppContext& app, Session& session, const SketchView& view);
    void HandleConstraint(AppContext& app, Session& session, const SketchView& view);
    void HandleInlineEdit(AppContext& app, Session& session, const SketchView& view);

    // Building blocks.
    /// A clicked position: an existing point to reuse or a new one.
    struct PointRef {
        ObjectId id;
        Vec2     pos;
        ObjectId onCurve;
    };
    PointRef RefFromSnap(const Snap& snap) const;
    ObjectId Resolve(doc::Document& doc, sketch::Sketch& sk, const PointRef& ref);
    void FinishDrawingStep(AppContext& app, Session& session, const Snap& snap);

    /// Applies a constraint as one undo step if it is valid, not redundant
    /// and solvable. Returns false and explains why otherwise.
    bool AddConstraint(AppContext& app, Session& session, sketch::Constraint constraint, const char* label);
    void TryApplyToSelection(AppContext& app, Session& session);
    void StartInlineEdit(const sketch::Constraint& c);
    void DrawPreview(AppContext& app, const SketchView& view, const sketch::Sketch& sk, const UnitSystem& units);

    SketchTool myTool = SketchTool::Select;

    // drawing tools
    std::vector<PointRef> myClicks;
    Snap                  mySnap;
    std::optional<Vec2>   myCursor;

    // selection, picking
    std::unordered_set<ObjectId> mySelection;
    std::vector<ObjectId>        myPicks;
    SketchHit                    myHover;

    // dragging in the select tool
    std::unique_ptr<doc::Feature> myDragBefore;
    ObjectId                      myDragEntity;
    Vec2                          myDragStart;
    Vec2                          myDragLabelStart;   ///< dimension label position at drag start
    std::vector<sketch::Drag>     myDragOrigins;   ///< initial positions
    bool                          myDragging = false;
    bool                          myPressedOnEntity = false;

    /// Camera before entering the sketch; Finish returns to its direction.
    std::optional<render::CameraState> myCameraBeforeSketch;

    // inline dimension value editor
    ObjectId    myEditDimension;
    std::string myEditText;
    bool        myEditFocus = false;
};

} // namespace bz::app
