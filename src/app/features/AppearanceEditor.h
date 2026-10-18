#pragma once
// "Внешний вид": colours of bodies and faces. Pick an appearance in the
// library (standard or the user's own), then click a body (or a face) in
// the view. A picture's size on the part (one copy, mm) has a slider.
// The look changes live; Готово makes it one undo step, Отмена
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
#include <vector>

namespace bz::app {

class AppearanceEditor final : public OperationEditor {
public:
    AppearanceEditor();

    void Start(AppContext& app, Session& session);
    /// Opens on faces chosen in the view: the appearance picked in the
    /// library goes on all of them at once (and can still be clicked onto
    /// other faces). One undo step on Готово, as usual.
    void StartOnFaces(AppContext& app, Session& session, std::vector<render::SelectedFace> faces);
    /// Assembly: where Готово puts the looks — this assembly only (false) or the parts' files.
    bool ToPart() const { return myToPart; }
    /// Removes the own looks of `faces` (they take the body's look again), one undo step.
    static void ResetFaces(AppContext& app, Session& session, const std::vector<render::SelectedFace>& faces);

    bool        IsOpen() const override { return myOpen; }
    ObjectId    EditedFeature() const override { return {}; }
    bool        CanEdit(const doc::Feature&) const override { return false; }
    void        EditExisting(AppContext&, Session&, ObjectId) override {}
    void        Apply(AppContext& app, Session& session) override { Done(app, session); }
    bool        HasApply() const override { return false; }
    void        Done(AppContext& app, Session& session) override;
    void        Cancel(AppContext& app, Session& session) override;
    void        Drop(AppContext& app) override {
        Close(app);
        myEdits.clear();
    }
    void        DrawPanel(AppContext& app, Session& session) override;
    void        DrawInViewport(AppContext& app, Session& session, const ViewportFrame& vf) override;
    bool        PicksFaces() const override { return true; }
    const char* Title() const override { return Tr("Внешний вид"); }
    const char* Icon() const override { return "appearance"; }
    std::string Hint() const override;

    /// The appearance picker of the top toolbar (as in Inventor): the part's
    /// look with a sample, a list to choose from; the choice goes on every
    /// body of the part (one undo step).
    void DrawToolbarPicker(AppContext& app, Session* session, ImVec2 pos, float width, float height);

private:
    /// The appearance chosen in the library (nullptr: back to the default look).
    const doc::Appearance* Chosen() const;
    /// "Размер текстуры": the chosen picture appearance, one copy in mm.
    void DrawPictureSize(AppContext& app, Session& session, float width);
    void DrawLibrary(AppContext& app, float width);
    /// A row of a list: sample and name. True when clicked.
    bool Row(AppContext& app, const doc::Appearance* a, bool chosen, float width);
    /// A small sample: a rounded square in the colour, outlined in the edge colour.
    void Sample(AppContext& app, ImDrawList* dl, ImVec2 p, float size, const doc::Appearance* a);
    /// Standard and own appearances as rows; returns the clicked id ("" default), or nullopt.
    std::optional<std::string> List(AppContext& app, std::string_view chosen, float width, bool withOwnMenu);
    void DrawCreateDialog(AppContext& app, Session& session);
    /// The new look's fields; `myInline`: one column in the phone's block 1, not a window.
    void DrawCreateBody(AppContext& app, float width);
    /// A new look of one's own, starting from the chosen colour.
    void StartCreate();
    /// The phone (mockups 9a–9c, 18.10.2026): what is on now, three buttons —
    /// the library, my colours, a new colour — each opening over block 1 with
    /// ✕ in its corner; the recent looks a tap away.
    void DrawPhonePanel(AppContext& app, Session& session, float width);
    /// Puts the chosen appearance on what it was opened on: the faces
    /// (Поверхность) or their objects and the chosen component (Объект).
    void ApplyToTargets(AppContext& app, Session& session);
    std::size_t TargetCount() const;
    /// The look being edited: the part's, or the assembly's overrides.
    static doc::Look& LookOf(Session& session);
    static void       Touch(Session& session);
    /// A face as the view shows it, as stored (an assembly keeps faces in
    /// its part's coordinates), and back.
    static doc::FaceReference Stored(Session& session, ObjectId body, doc::FaceReference shown);
    static doc::FaceReference Shown(Session& session, ObjectId body, doc::FaceReference stored);
    void KeepPicture(Session& session);
    void PaintBody(Session& session, ObjectId body);
    void PaintFace(AppContext& app, Session& session, ObjectId body, const doc::FaceReference& shown);
    void Close(AppContext& app);
    /// An assembly's change made while open, for writing it into a part.
    struct Edit {
        ObjectId                          component;
        std::optional<doc::FaceReference> face;         ///< in the part's coordinates; none: the whole object
        std::string                       appearance;   ///< empty: back to the default
    };
    /// "В детали": the edits go into the parts' files (or their open tabs).
    void WriteIntoParts(AppContext& app, Session& session, const doc::Look& painted, const std::vector<Edit>& edits);
    /// A small texture of a library picture (or of the file being chosen), for swatches.
    unsigned Thumbnail(const std::string& key, const std::vector<std::uint8_t>& bytes);
    unsigned ThumbnailOf(const doc::Appearance* a);

    bool        myOpen = false;
    bool        myInline = false;            ///< the create form is drawn in block 1 (the phone)
    int         myPhonePage = 0;             ///< 0 the main one, 1 the library, 2 my colours, 3 a new colour
    std::string myRecentSeen;
    bool        myFaces = false;          ///< Поверхность: a click colours one face (default: the whole object)
    bool        myToPart = false;         ///< assembly: Готово writes into the parts' files
    std::vector<ObjectId> myTargetBodies;   ///< objects it was opened on (the assembly's chosen component)
    std::vector<Edit>     myEdits;          ///< assembly: what was painted, in order
    std::string myChosen = "std.steel";   ///< id; empty = the default look
    doc::Look   myBefore;                 ///< as it was when the panel opened
    std::vector<render::SelectedFace> myTargets;   ///< faces it was opened on (StartOnFaces)
    std::string myTargetsChosen;          ///< what the targets got last (a new choice goes on them)
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
