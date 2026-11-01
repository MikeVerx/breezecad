#pragma once
// Every operation panel of the app. At most one is open at a time.

#include "app/features/AppearanceEditor.h"
#include "app/features/CoilEditor.h"
#include "app/features/CopyEditors.h"
#include "app/features/EdgeOperationEditor.h"
#include "app/features/ExtrudeEditor.h"
#include "app/features/HoleEditor.h"
#include "app/features/LoftEditor.h"
#include "app/features/RevolveEditor.h"
#include "app/features/SectionEditor.h"
#include "app/features/SheetMetalEditors.h"
#include "app/features/ShellEditor.h"
#include "app/features/SweepEditor.h"
#include "app/features/ThreadEditor.h"
#include "app/features/WorkAxisEditor.h"
#include "app/features/WorkPlaneEditor.h"
#include "app/features/WorkPointEditor.h"

#include <array>

namespace bz::app {

struct OperationEditors {
    ExtrudeEditor extrude;
    RevolveEditor revolve;
    FilletEditor  fillet;
    ChamferEditor chamfer;
    HoleEditor    hole;
    WorkPlaneEditor workPlane;
    WorkAxisEditor  workAxis;
    WorkPointEditor workPoint;
    PatternEditor   pattern;
    MirrorEditor    mirror;
    LoftEditor      loft;
    SweepEditor     sweep;
    CoilEditor      coil;
    SectionEditor   section;
    ThreadEditor    thread;
    ShellEditor     shell;
    SheetEditor     sheet;
    FlangeEditor    flange;
    SheetCutEditor  sheetCut;
    AppearanceEditor appearance;

    std::array<OperationEditor*, 20> All() {
        return {&extrude, &revolve, &fillet,  &chamfer, &hole,  &workPlane, &workAxis, &workPoint, &pattern,  &mirror,
                &loft,    &sweep,   &coil,    &section, &thread, &shell,    &sheet,    &flange,    &sheetCut, &appearance};
    }

    /// The open operation, or nullptr.
    OperationEditor* Active() {
        for (OperationEditor* e : All())
            if (e->IsOpen()) return e;
        return nullptr;
    }

    /// The editor for features like `f`, or nullptr (sketches have their own editor).
    OperationEditor* EditorFor(const doc::Feature& f) {
        for (OperationEditor* e : All())
            if (e->CanEdit(f)) return e;
        return nullptr;
    }

    /// Drops the open operation before another tool starts: what was not
    /// confirmed (✓, «Применить», «Готово») is reset, never committed behind
    /// the user's back (the author, 18.10.2026: "везде").
    void Close(AppContext& app, Session& session) {
        if (OperationEditor* open = Active()) open->Cancel(app, session);
    }

    /// Keeps the open operation as it is (Готово) before something that is not
    /// another tool — saving the document, the tree's panel: these must not
    /// throw the user's work away (18.10.2026: «Сохранить» dropped an extrude).
    /// One that cannot be made (an error) is dropped.
    void Commit(AppContext& app, Session& session) {
        OperationEditor* open = Active();
        if (!open) return;
        open->Done(app, session);
        if (open->IsOpen()) open->Cancel(app, session);
    }
};

} // namespace bz::app
