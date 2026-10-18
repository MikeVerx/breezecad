#pragma once
// Sketch mode: drawing tools, constraints, dimensions, selection and
// dragging, with CAD feedback (snaps, inference, fully defined colours).
//
// Every finished action is one undo step: the sketch feature is edited in
// place for live feedback and then committed with Session::CommitInPlaceEdit.

#include "app/AppContext.h"
#include "app/sketch/SketchPick.h"
#include "document/SketchFeature.h"
#include "sketch/Move.h"
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
    Select, Line, Rectangle, Circle, Arc, Point, Spline, Text, Project, Trim, Extend, Split, Fillet, Offset, Move, Rotate, Copy, Scale, Mirror, PatternRect, PatternCircular, Dimension,
    Coincident, Horizontal, Vertical, Parallel, Perpendicular, Equal, Tangent, Fix, Midpoint, Concentric, Symmetric,
};

/// Ways to draw with one tool, chosen in the tool panel.
enum class RectangleMode : unsigned char {
    Corners, Centre, ThreePoints, ThreePointsCentre, Polygon,
    SlotCentres, SlotOverall, SlotCentre, SlotArc3, SlotArcCentre,
};
inline bool IsSlot(RectangleMode m) { return m >= RectangleMode::SlotCentres; }
inline bool IsArcSlot(RectangleMode m) { return m == RectangleMode::SlotArc3 || m == RectangleMode::SlotArcCentre; }
enum class CircleMode : unsigned char { CentreRadius, ThreePoints, Ellipse, TangentLines };
enum class ArcMode : unsigned char { CentreEnds, ThreePoints };

/// Line type of what is drawn: основная (makes profiles), вспомогательная
/// (helps, makes nothing), осевая (an axis for revolve and coil; lines only).
enum class LineStyle : unsigned char { Normal, Construction, Centerline };

/// The tool panel's settings. Typed sizes lock the cursor while drawing
/// and become dimensions of what is drawn.
struct DrawOptions {
    bool projectLinked = true;   ///< Проецирование: follows the model (зависимое) or a fixed copy (независимое)
    LineStyle     style = LineStyle::Normal;
    RectangleMode rectangle = RectangleMode::Corners;
    CircleMode    circle = CircleMode::CentreRadius;
    ArcMode       arc = ArcMode::CentreEnds;
    int           polygonSides = 6;
    std::string   sides = "6";      ///< polygon side count as typed
    bool          polygonInscribed = true;
    std::string   length, angle;    ///< line
    std::string   width, height;    ///< rectangle
    std::string   size;             ///< circle / polygon diameter, arc radius
    std::string   filletRadius = "5";   ///< Скругление: kept between corners
    std::string   text = "BreezeCAD";     ///< Текст: the words of the next text, or of the one being edited
    std::string   textHeight = "5";
    std::string   textFont;               ///< empty: the program's font
    int           textStyle = 0;          ///< 0 regular, 1 bold, 2 italic, 3 bold italic
    int           textHAlign = 0, textVAlign = 0;
    std::string   offsetDistance = "5"; ///< Эквидистанта
    bool          splineControl = false;  ///< Сплайн: by control vertices (else through the points)
    std::string   moveX, moveY;     ///< move
    std::string   turn;             ///< rotate
    std::string   factor = "2";     ///< scale
    std::string   count1 = "3", count2 = "1";      ///< patterns: copies along each direction / around
    std::string   spacing1 = "20", spacing2 = "20";///< rectangular pattern spacing
    bool          vertical1 = false;               ///< rectangular: first direction along Y
    std::string   sweep = "360";                   ///< circular pattern: total angle
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
    /// The sketch with its dimension labels, in model space (for Показать всё).
    Box3 FitBox(Session& session) const;
    /// Zooms out (keeping the direction) when the sketch left the screen.
    void KeepSketchInView(AppContext& app, Session& session);

    // --- tools ------------------------------------------------------------
    SketchTool Tool() const { return myTool; }
    /// What the point under the cursor snaps to now (the phone's cursor shows it).
    SnapKind CurrentSnap() const { return mySnap.kind; }
    /// Projection linked to the model (it follows it) or a fixed copy.
    bool ProjectsLinked() const { return myOptions.projectLinked; }
    void SetProjectLinked(bool linked) { myOptions.projectLinked = linked; }
    void SetTool(AppContext& app, Session& session, SketchTool tool);
    /// Esc: drops the current step, then the tool, then leaves the sketch.
    /// Esc: one step back — the field, the clicks of the tool, the tool, the
    /// selection, then (mayFinish) the sketch itself.
    void Escape(AppContext& app, Session& session, bool mayFinish = true);
    void DeleteSelection(Session& session);
    void ToggleConstruction(Session& session);
    /// Makes the selected lines осевые (or normal again).
    void ToggleCenterline(Session& session);
    bool HasSelection() const { return !mySelection.empty(); }
    /// The phone: each new dimension's text is placed clear of the others
    /// (a finger cannot nudge it the way a mouse can). Set every frame.
    void SetAutoArrange(bool on) { myAutoArrange = on; }
    /// The active sketch has a dimension to lay out.
    bool HasDimensions(Session& session) const;
    /// "Упорядочить размеры": every dimension of the sketch laid out anew, one undo step.
    void ArrangeDimensions(AppContext& app, Session& session);

    /// A drawing tool is active: the left area shows its panel.
    bool HasToolPanel() const;
    /// Ways to draw and typed sizes of the active drawing tool.
    void DrawToolPanel(AppContext& app, Session& session, float width);

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
    /// "Проецировать": a click on a model edge adds its projection.
    void HandleProject(AppContext& app, Session& session, const SketchView& view);
    /// "Обрезка": the piece under the cursor is shown, a click removes it.
    void HandleTrim(AppContext& app, Session& session, const SketchView& view);
    void HandleExtend(AppContext& app, Session& session, const SketchView& view);
    void HandleSplit(AppContext& app, Session& session, const SketchView& view);
    void HandleOffset(AppContext& app, Session& session, const SketchView& view);
    void HandleSpline(AppContext& app, Session& session, const SketchView& view);
    void HandleText(AppContext& app, Session& session, const SketchView& view);
    /// Text being edited (double click, or a click with the Текст tool): the
    /// panel changes it live; one undo step when the editing ends.
    ObjectId                      myEditedText;
    std::unique_ptr<doc::Feature> myTextBefore;
    std::optional<Vec2>           myTextPress;   ///< where a new text's box starts (mouse down)
    void BeginTextEdit(Session& session, ObjectId text);
    void EndTextEdit(Session& session);
    /// Puts the panel's format into the edited text (when it differs).
    void ApplyTextOptions(Session& session);

public:
    /// A tool is in the middle of something (a chain of clicks): a short
    /// right click ends it instead of opening the menu.
    bool Busy() const { return !myClicks.empty() || myOffsetCurve; }

private:
    /// Makes the spline from the clicks so far (closed: back to the first point).
    void FinishSpline(AppContext& app, Session& session, bool closed);
    ObjectId myOffsetCurve;   ///< Эквидистанта: a curve of the chosen contour (then the side is picked)
    void HandleFillet(AppContext& app, Session& session, const SketchView& view);
    /// "Перенос" / "Поворот": the selection follows two (three) clicks.
    void HandleMotion(AppContext& app, Session& session, const SketchView& view);
    /// "Зеркало": clicks pick the elements, then (step two) the mirror line.
    void HandleMirror(AppContext& app, Session& session, const SketchView& view);
    /// The motion the clicks so far and the cursor describe; nullopt when not ready.
    std::optional<sketch::Motion> PendingMotion(const UnitSystem& units, bool withCursor) const;
    /// Moves (or for Копировать copies) the selection as one undo step.
    void ApplyMotion(AppContext& app, Session& session, const sketch::Motion& motion);
    /// Масштаб about the first click, by the typed factor.
    void ApplyScale(AppContext& app, Session& session);
    /// Массив: copies of the selection by the panel's settings.
    void ApplyPattern(AppContext& app, Session& session);
    /// The placements a pattern makes (none when the settings are not valid).
    std::vector<sketch::Motion> PatternMotions(const UnitSystem& units) const;
    void HandleDimension(AppContext& app, Session& session, const SketchView& view);
    void HandleConstraint(AppContext& app, Session& session, const SketchView& view);
    /// Constraint tools' panel: the picked elements, each can be dropped.
    void DrawPicks(AppContext& app, Session& session, float width);
    void HandleInlineEdit(AppContext& app, Session& session, const SketchView& view);

    // Building blocks.
    /// A clicked position: an existing point to reuse or a new one.
    struct PointRef {
        ObjectId id;
        Vec2     pos;
        ObjectId onCurve;
        ObjectId midOf;   ///< a line whose midpoint this is
    };
    PointRef RefFromSnap(const Snap& snap) const;
    ObjectId Resolve(doc::Document& doc, sketch::Sketch& sk, const PointRef& ref);
    void FinishDrawingStep(AppContext& app, Session& session, const Snap& snap);
    /// Clicks the active tool needs, in its current mode.
    std::size_t ClicksNeeded() const;
    /// Moves the cursor position to honour typed sizes; true when it moved.
    bool ApplyTypedSizes(const UnitSystem& units, Vec2& pos) const;
    /// A typed size to turn into a dimension; its label goes away from `inside`.
    struct TypedDimension {
        sketch::Constraint  constraint;
        std::string         text;
        std::optional<Vec2> inside;
    };
    /// Adds dimensions for typed sizes to the shape just drawn.
    void AddTypedDimensions(doc::Document& doc, sketch::Sketch& sk, const std::vector<TypedDimension>& dims);

    /// Applies a constraint as one undo step if it is valid, not redundant
    /// and solvable. Returns false and explains why otherwise.
    bool AddConstraint(AppContext& app, Session& session, sketch::Constraint constraint, const char* label);
    void TryApplyToSelection(AppContext& app, Session& session);
    void StartInlineEdit(const sketch::Constraint& c);
    void DrawPreview(AppContext& app, const SketchView& view, const sketch::Sketch& sk, const UnitSystem& units);
    /// The shape being drawn, from the clicks so far to the cursor.
    void DrawShapePreview(AppContext& app, const SketchView& view, const UnitSystem& units) const;

    SketchTool myTool = SketchTool::Select;

    // drawing tools
    DrawOptions           myOptions;
    bool                  myMirrorAxisStep = false;   ///< Зеркало: the next click picks the mirror line
    std::string           mySidesError;          ///< polygon side count that is not a whole number
    double                myMmPerPixel = 0.1;   ///< of the last frame, to place automatic dimension labels
    bool                  myAutoArrange = false;   ///< SetAutoArrange
    std::vector<PointRef> myClicks;
    /// A chain of lines: where it began and where the next line starts. A
    /// line that ends where the chain began closes it, and the chain stops.
    ObjectId myChainStart, myChainEnd;
    Snap                  mySnap;
    std::optional<Vec2>   myCursor;

    /// Constraints whose glyphs show when not all are shown: those of the
    /// selected and hovered elements (rebuilt every frame).
    std::unordered_set<ObjectId> myShownGlyphs;
    void UpdateShownGlyphs(const sketch::Sketch& sk, bool all);

    // selection, picking
    // Typing a size while drawing: the first field of the tool panel takes
    // the keys; Shift+Up/Down (or Shift+W/S) moves between the fields.
    int         myFocusField = -1;   ///< the field to focus on the next panel frame (Shift+Up/Down)
    std::string myFieldSeed;         ///< keys typed in the view, not yet added to the field
    int         myFieldCount = 0;    ///< fields shown on the last panel frame
    int         mySeedField = -1;    ///< the field the keys typed in the view go to
    bool        myClearOnSeed = false;   ///< the next typed key replaces the field's text
    int         myBackspaces = 0;        ///< Backspaces typed in the view, not applied yet
    std::unordered_set<ObjectId> mySelection;
    std::optional<ImVec2>        myBoxStart;   ///< where a selection box was started (screen)
    bool                         myBoxing = false;
    std::vector<Vec2>            myFence;   ///< Обрезка: the path dragged so far (sketch coordinates)
    std::vector<ObjectId>        myPicks;
    SketchHit                    myHover;

    // dragging in the select tool
    std::unique_ptr<doc::Feature> myDragBefore;
    ObjectId                      myLabelGrab;          ///< "Размер" tool: the dimension whose text is pressed
    bool                          myLabelMoved = false; ///< …and dragged (a click without a drag edits the value)
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
    bool        myEditTyped = false;   ///< the value was changed (a clicked name is then added, not put instead)
    int         myCaretFrames = 0;     ///< frames to keep the caret at the end after inserting a name
    bool        myClickUsed = false;   ///< this frame's click went into the formula, tools must not see it
};

} // namespace bz::app
