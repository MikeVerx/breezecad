#pragma once
// "Внешний вид": colours of bodies and faces. Pick an appearance in the
// library (standard or the user's own), then click a body (or a face) in
// the view. The look changes live; Готово makes it one undo step, Отмена
// puts it back. The user's own appearances are made in a small dialog
// (colour with transparency, edge colour, finish) and can be imported or
// exported as a pack.

#include "app/AppearanceLibrary.h"
#include "app/features/OperationEditor.h"
#include "core/I18n.h"
#include "document/Appearance.h"

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

private:
    /// The appearance chosen in the library (nullptr: back to the default look).
    const doc::Appearance* Chosen() const;
    void DrawLibrary(AppContext& app, float width);
    bool Swatch(AppContext& app, const doc::Appearance* a, const std::string& label, float size);
    void DrawCreateDialog(AppContext& app, Session& session);
    /// A small texture of a library picture (or of the file being chosen), for swatches.
    unsigned Thumbnail(const std::string& key, const std::vector<std::uint8_t>& bytes);
    unsigned ThumbnailOf(const doc::Appearance* a);

    bool        myOpen = false;
    bool        myFaces = false;          ///< a click colours one face, not the whole body
    std::string myChosen = "std.steel";   ///< id; empty = the default look
    doc::Look   myBefore;                 ///< as it was when the panel opened

    AppearanceLibrary myLibrary;

    // The "Создать" dialog.
    bool            myCreating = false;
    doc::Appearance myDraft;
    int             myKind = 0;        ///< 0 a plain colour, 1 a picture
    std::filesystem::path myPictureFile;   ///< a picture just chosen for the draft

    std::map<std::string, unsigned> myThumbnails;   ///< GL textures by picture key
};

} // namespace bz::app
