#pragma once
// ============================================================
// ViewCubeGeometry — топология куба навигации.
// 6 граней, 12 рёбер, 8 углов. Чистые данные, никакой логики.
//
// Заполним таблицами kFaces/kEdges/kCorners на шаге 1.3.
// ============================================================

namespace BreezeCAD::UI::ViewCube {

// TODO(step 1.3):
//   struct Face   { int v[4]; int n[3]; const char* label; };
//   struct Edge   { int v1, v2; int f1, f2; };
//   struct Corner { int v; int f1, f2, f3; };
//   inline constexpr Face   kFaces[6]   = { ... };
//   inline constexpr Edge   kEdges[12]  = { ... };
//   inline constexpr Corner kCorners[8] = { ... };
//   inline constexpr double kFaceTextRight[6][3] = { ... };
//   inline constexpr double kFaceTextDown[6][3]  = { ... };

} // namespace BreezeCAD::UI::ViewCube
