#pragma once
// "Внешний вид": colours of bodies and faces. Bodies: pick an appearance in
// the library, then click a body in the view. Faces: click faces to choose
// them (flat or curved; Shift or Ctrl — Settings → Мышь — adds and removes),
// they stay marked; a click on an appearance puts it on the chosen faces.
// A picture's size on the part (one copy, mm) has a slider. The look changes live; Готово makes it one undo step, Отмена
// puts it back. The user's own appearances are made in a small dialog
// (colour with transparency, edge colour, finish) and can be imported or
// exported as a pack.

#include "app/AppearanceLibrary.h"
#include "app/features/OperationEditor.h"
#include "core/I18n.h"
#include "document/Appearance.h"
#include "render/Viewport.h"

#include <imgui.h>

#include <filesystem>
#include <map>
#include <optional>
#include <string>

namespace bz::app {

class AppearanceEditor final : public OperationEditor {
public:
    AppearanceEditor();

    void Start(AppContext& app, Session& session);

    bool        IsOpen() const override { return myOpen; }
    ObjectId    EditedFeature() const override { return {}; }
    bool        CanEdit(const doc::Feature&) const override { return false; }
    void        EditExisting(AppContext&, Session&, ObjectId) override {}
    void        Apply(AppContext& app, Session& session) override { Done(app, session); }
    void        Done(AppContext& app, Session& session) override;
    void        Cancel(AppContext& app, Session& session) override;
    void        DrawPanel(AppContext& app, Session& session) override;
    void        DrawInViewport(AppContext& app, Session& session, const ViewportFrame& vf) override;
    bool        PicksFaces() const override { return true; }
    const char* Title() const override { return Tr("Внешний вид"); }
    std::string Hint() const override;

    /// The appearance picker of the top toolbar (as in Inventor): the part's
    /// look with a sample, a list to choose from; the choice goes on every
    /// body of the part (one undo step).
    void DrawToolbarPicker(AppContext& app, Session* session, ImVec2 pos, float width, float height);

private:
    /// The appearance chosen in the library (nullptr: back to the default look).
    const doc::Appearance* Chosen() const;
    /// Puts `a` (nullptr: the body's own look again) on every chosen face.
    void ApplyToChosenFaces(Session& session, const doc::Appearance* a);
    /// "Размер текстуры": the chosen picture appearance, one copy in mm.
    void DrawPictureSize(AppContext& app, Session& session, float width);
    void ClearChoice(AppContext& app);
    void DrawLibrary(AppContext& app, float width);
    /// A row of a list: sample and name. True when clicked.
    bool Row(AppContext& app, const doc::Appearance* a, bool chosen, float width);
    /// A small sample: a rounded square in the colour, outlined in the edge colour.
    void Sample(AppContext& app, ImDrawList* dl, ImVec2 p, float size, const doc::Appearance* a);
    /// Standard and own appearances as rows; returns the clicked id ("" default), or nullopt.
    std::optional<std::string> List(AppContext& app, std::string_view chosen, float width, bool withOwnMenu);
    void DrawCreateDialog(AppContext& app, Session& session);
    /// A small texture of a library picture (or of the file being chosen), for swatches.
    unsigned Thumbnail(const std::string& key, const std::vector<std::uint8_t>& bytes);
    unsigned ThumbnailOf(const doc::Appearance* a);

    bool        myOpen = false;
    bool        myFaces = false;          ///< a click colours one face, not the whole body
    std::string myChosen = "std.steel";   ///< id; empty = the default look
    doc::Look   myBefore;                 ///< as it was when the panel opened
    std::vector<render::Viewport::ChosenFace> myChosenFaces;   ///< faces mode: the faces clicked
    std::string mySizeText;               ///< the size field while it is typed in
    std::string mySizeFor;                ///< the appearance mySizeText belongs to
    double      mySize = 100.0;           ///< its size as the slider has it now, mm

    AppearanceLibrary myLibrary;

    // The "Создать" dialog.
    bool            myCreating = false;
    doc::Appearance myDraft;
    int             myKind = 0;        ///< 0 a plain colour, 1 a picture
    std::filesystem::path myPictureFile;   ///< a picture just chosen for the draft

    std::map<std::string, unsigned> myThumbnails;   ///< GL textures by picture key
};

} // namespace bz::app
