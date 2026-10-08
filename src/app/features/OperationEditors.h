#pragma once
// Every operation panel of the app. At most one is open at a time.

#include "app/features/EdgeOperationEditor.h"
#include "app/features/ExtrudeEditor.h"
#include "app/features/RevolveEditor.h"

#include <array>

namespace bz::app {

struct OperationEditors {
    ExtrudeEditor extrude;
    RevolveEditor revolve;
    FilletEditor  fillet;
    ChamferEditor chamfer;

    std::array<OperationEditor*, 4> All() { return {&extrude, &revolve, &fillet, &chamfer}; }

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
