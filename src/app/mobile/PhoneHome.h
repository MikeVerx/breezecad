#pragma once
// The home page on the phone (the author's approved mockup, 17.10.2026):
// on the left the brand, big "Деталь" / "Сборка" and "Создать…" /
// "Открыть…"; on the right the projects as cards with pictures (the
// projects folder on Android, the recent files elsewhere), the work to
// recover above them.
//
// The pictures: a small snapshot of the view taken when a document is saved
// on the phone, kept by file name in the config folder (thumbs/).

#include "app/AppContext.h"

#include <string>

namespace bz::app::mobile {

void DrawPhoneHome(AppContext& app, ImVec2 pos, ImVec2 size);

/// After a save on the phone: the picture of `session` for its card.
void SaveThumbnail(AppContext& app, const Session& session);

} // namespace bz::app::mobile
