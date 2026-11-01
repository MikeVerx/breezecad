#pragma once
// A sketch whose projections broke (their edge lost, or the sketch no longer
// agreeing with them after a change above it): the "!" at the sketch, in the
// tree and in the status bar while it is edited, offers to bring the sketch
// back as it was before and unlink its projections from the model.

#include "core/Id.h"

namespace bz::app {

struct AppContext;
class Session;

/// "Восстановить эскиз как до изменения": one undoable step in the part.
/// False when the sketch is not there or has no projections.
bool RestoreSketchBeforeBreak(Session& session, ObjectId sketch);

/// The menu the "!" opens (ImGui::OpenPopup(kSketchRepairPopup) in the same
/// ID scope first). Draws nothing while it is closed.
inline constexpr const char* kSketchRepairPopup = "##fix-sketch";
void SketchRepairMenu(AppContext& app, Session& session, ObjectId sketch);

} // namespace bz::app
