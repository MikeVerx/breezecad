#pragma once
// Every operation panel of the app. At most one is open at a time.

#include "app/features/CoilEditor.h"
#include "app/features/CopyEditors.h"
#include "app/features/DraftEditor.h"
#include "app/features/EdgeOperationEditor.h"
#include "app/features/ExtrudeEditor.h"
#include "app/features/HoleEditor.h"
#include "app/features/LoftEditor.h"
#include "app/features/RevolveEditor.h"
#include "app/features/SectionEditor.h"
#include "app/features/ShellEditor.h"
#include "app/features/SweepEditor.h"
#include "app/features/WorkPlaneEditor.h"

#include <array>

namespace bz::app {

struct OperationEditors {
    ExtrudeEditor extrude;
    RevolveEditor revolve;
    FilletEditor  fillet;
    ChamferEditor chamfer;
    HoleEditor    hole;
    ShellEditor     shell;
    WorkPlaneEditor workPlane;
    PatternEditor   pattern;
    MirrorEditor    mirror;
    LoftEditor      loft;
    SweepEditor     sweep;
    DraftEditor     draft;
    CoilEditor      coil;
    SectionEditor   section;

    std::array<OperationEditor*, 14> All() {
        return {&extrude, &revolve, &fillet, &chamfer, &hole, &shell, &workPlane, &pattern, &mirror, &loft, &sweep, &draft, &coil, &section};
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

    /// Finishes the open operation before something else starts; an
    /// operation that cannot be committed (it has an error) is cancelled.
    void Close(AppContext& app, Session& session) {
        OperationEditor* open = Active();
        if (!open) return;
        open->Done(app, session);
        if (open->IsOpen()) open->Cancel(app, session);
    }
};

} // namespace bz::app
