#include "ViewCubeOverlay.h"
#include "occt/OcctView.h"
#include "app/Application.h"  // ThemeTokens

#include "imgui.h"
#include "imgui_internal.h"

#include <gp_Dir.hxx>
#include <gp_Vec.hxx>
#include <gp_Pnt.hxx>

#include <cmath>
#include <cstring>
#include <vector>
#include <algorithm>

namespace BreezeCAD::UI::ViewCube {

void ViewCubeOverlay::Draw(const Context& ctx) {
    OcctView&            view   = ctx.view;
    const ThemeTokens&   theme  = ctx.theme;
    ImGuiIO&             io     = ctx.io;

    const float cubeSize   = 100.0f;
    const float cubeHalf   = 38.0f;
    const float margin     = 15.0f;
    const float topOffset  = 260.0f;
    const float cubeShiftX = ctx.rightPanelShiftX;

    ImVec2 center(io.DisplaySize.x - margin - cubeSize * 0.5f - cubeShiftX,
                  topOffset + cubeSize * 0.5f);

    ImDrawList* draw = ImGui::GetForegroundDrawList();

    draw->AddCircleFilled(center, cubeSize * 0.55f, theme.cubeBgCircle);

    gp_Dir camDir = view.GetCameraDirection();
    gp_Dir camUp  = view.GetCameraUp();

    // --- Плавная анимация камеры: 0.5 сек, smoothstep, очередь ---
    if (myAnimator.myActive) {
        myAnimator.myT += io.DeltaTime / 0.5f;
        if (myAnimator.myT > 1.0f) myAnimator.myT = 1.0f;
    }

    while (myAnimator.myActive && myAnimator.myT >= 1.0f) {
        gp_Dir finDir(myAnimator.myToDir[0], myAnimator.myToDir[1], myAnimator.myToDir[2]);
        gp_Dir finUp (myAnimator.myToUp[0],  myAnimator.myToUp[1],  myAnimator.myToUp[2]);
        view.SetCameraDirectionAndUp(finDir, finUp);
        camDir = finDir;
        camUp  = finUp;

        if (myAnimator.myQueue.empty()) {
            myAnimator.myActive = false;
            break;
        }

        AnimTarget nxt = myAnimator.myQueue.front();
        myAnimator.myQueue.erase(myAnimator.myQueue.begin());

        myAnimator.myFromDir[0] = finDir.X(); myAnimator.myFromDir[1] = finDir.Y(); myAnimator.myFromDir[2] = finDir.Z();
        myAnimator.myFromUp[0]  = finUp.X();  myAnimator.myFromUp[1]  = finUp.Y();  myAnimator.myFromUp[2]  = finUp.Z();

        if (nxt.kind == 0) {
            myAnimator.myToDir[0] = nxt.dir[0]; myAnimator.myToDir[1] = nxt.dir[1]; myAnimator.myToDir[2] = nxt.dir[2];
            myAnimator.myToUp[0]  = nxt.up[0];  myAnimator.myToUp[1]  = nxt.up[1];  myAnimator.myToUp[2]  = nxt.up[2];
        } else {
            double angleDeg = nxt.dir[0];
            double a  = angleDeg * 3.14159265358979323846 / 180.0;
            double ca = std::cos(a), sa = std::sin(a);
            gp_Vec dirV(finDir), upV(finUp);
            gp_Vec crossV = dirV.Crossed(upV);
            gp_Vec newUpV = upV * ca + crossV * sa;
            double len = std::sqrt(newUpV.X()*newUpV.X() + newUpV.Y()*newUpV.Y() + newUpV.Z()*newUpV.Z());
            if (len < 1e-9) { myAnimator.myT = 0.0f; continue; }
            gp_Dir newUp(newUpV.X()/len, newUpV.Y()/len, newUpV.Z()/len);
            myAnimator.myToDir[0] = finDir.X(); myAnimator.myToDir[1] = finDir.Y(); myAnimator.myToDir[2] = finDir.Z();
            myAnimator.myToUp[0]  = newUp.X();  myAnimator.myToUp[1]  = newUp.Y();  myAnimator.myToUp[2]  = newUp.Z();
        }
        myAnimator.myT = 0.0f;
    }

    if (myAnimator.myActive) {
        float s = myAnimator.myT * myAnimator.myT * (3.0f - 2.0f * myAnimator.myT);
        double dx = myAnimator.myFromDir[0] + (myAnimator.myToDir[0] - myAnimator.myFromDir[0]) * s;
        double dy = myAnimator.myFromDir[1] + (myAnimator.myToDir[1] - myAnimator.myFromDir[1]) * s;
        double dz = myAnimator.myFromDir[2] + (myAnimator.myToDir[2] - myAnimator.myFromDir[2]) * s;
        double dlen = std::sqrt(dx*dx + dy*dy + dz*dz);
        if (dlen < 1e-9) dlen = 1.0;
        gp_Dir curDir(dx/dlen, dy/dlen, dz/dlen);
        double ux = myAnimator.myFromUp[0] + (myAnimator.myToUp[0] - myAnimator.myFromUp[0]) * s;
        double uy = myAnimator.myFromUp[1] + (myAnimator.myToUp[1] - myAnimator.myFromUp[1]) * s;
        double uz = myAnimator.myFromUp[2] + (myAnimator.myToUp[2] - myAnimator.myFromUp[2]) * s;
        double dot = ux*curDir.X() + uy*curDir.Y() + uz*curDir.Z();
        ux -= dot * curDir.X(); uy -= dot * curDir.Y(); uz -= dot * curDir.Z();
        double ulen = std::sqrt(ux*ux + uy*uy + uz*uz);
        gp_Dir curUp = (ulen > 1e-6) ? gp_Dir(ux/ulen, uy/ulen, uz/ulen) : gp_Dir(0, 0, 1);
        view.SetCameraDirectionAndUp(curDir, curUp);
        camDir = curDir;
        camUp  = curUp;
    }

    gp_Vec crossVec = camDir.Crossed(camUp);
    gp_Dir camRight;
    if (crossVec.SquareMagnitude() < 1e-9) {
        gp_Dir fallback(1, 0, 0);
        if (std::abs(camDir.Dot(fallback)) > 0.9) fallback = gp_Dir(0, 1, 0);
        camRight = camDir.Crossed(fallback);
    } else {
        camRight = gp_Dir(crossVec);
    }

    gp_Pnt C[8] = {
        {-1,-1,-1}, { 1,-1,-1}, { 1, 1,-1}, {-1, 1,-1},
        {-1,-1, 1}, { 1,-1, 1}, { 1, 1, 1}, {-1, 1, 1}
    };

    auto project = [&](const gp_Pnt& p) -> ImVec2 {
        double x = p.X()*camRight.X() + p.Y()*camRight.Y() + p.Z()*camRight.Z();
        double y = p.X()*camUp.X()    + p.Y()*camUp.Y()    + p.Z()*camUp.Z();
        return ImVec2(center.x + (float)x * cubeHalf,
                      center.y - (float)y * cubeHalf);
    };
    auto depthOf = [&](const gp_Pnt& p) -> double {
        return p.X()*camDir.X() + p.Y()*camDir.Y() + p.Z()*camDir.Z();
    };

    struct Face { int v[4]; gp_Dir n; const char* label; };
    Face faces[6] = {
        {{4,5,6,7}, {0,0, 1}, "ВЕРХ"},
        {{0,3,2,1}, {0,0,-1}, "НИЗ"},
        {{0,1,5,4}, {0,-1,0}, "ПЕРЕД"},
        {{2,3,7,6}, {0, 1,0}, "ЗАД"},
        {{1,2,6,5}, { 1,0,0}, "ПРАВО"},
        {{3,0,4,7}, {-1,0,0}, "ЛЕВО"},
    };

    struct Visible { int idx; double depth; ImVec2 pts[4]; };
    std::vector<Visible> vis;
    bool faceVisible[6] = {false, false, false, false, false, false};

    for (int fi = 0; fi < 6; fi++) {
        if (faces[fi].n.Dot(camDir) >= -0.01) continue;
        Visible v;
        v.idx = fi;
        double d = 0;
        for (int k = 0; k < 4; k++) {
            v.pts[k] = project(C[faces[fi].v[k]]);
            d += depthOf(C[faces[fi].v[k]]);
        }
        v.depth = d / 4.0;
        vis.push_back(v);
        faceVisible[fi] = true;
    }

    std::sort(vis.begin(), vis.end(),
              [](const Visible& a, const Visible& b){ return a.depth < b.depth; });

    struct Edge { int v1, v2; int f1, f2; };
    Edge edges[12] = {
        {0, 1, 1, 2}, {1, 2, 1, 4}, {2, 3, 1, 3}, {3, 0, 1, 5},
        {4, 5, 0, 2}, {5, 6, 0, 4}, {6, 7, 0, 3}, {7, 4, 0, 5},
        {0, 4, 2, 5}, {1, 5, 2, 4}, {2, 6, 3, 4}, {3, 7, 3, 5},
    };

    struct Corner { int v; int f1, f2, f3; };
    Corner corners[8] = {
        {0, 5, 2, 1}, {1, 2, 4, 1}, {2, 4, 3, 1}, {3, 3, 5, 1},
        {4, 5, 2, 0}, {5, 2, 4, 0}, {6, 4, 3, 0}, {7, 3, 5, 0},
    };

    struct VisEdge { int idx; ImVec2 a, b; };
    std::vector<VisEdge> visEdges;
    for (int ei = 0; ei < 12; ei++) {
        if (!faceVisible[edges[ei].f1] && !faceVisible[edges[ei].f2]) continue;
        VisEdge e;
        e.idx = ei;
        e.a = project(C[edges[ei].v1]);
        e.b = project(C[edges[ei].v2]);
        visEdges.push_back(e);
    }

    struct VisCorner { int idx; ImVec2 p; };
    std::vector<VisCorner> visCorners;
    for (int ci = 0; ci < 8; ci++) {
        if (!faceVisible[corners[ci].f1] &&
            !faceVisible[corners[ci].f2] &&
            !faceVisible[corners[ci].f3]) continue;
        VisCorner c;
        c.idx = ci;
        c.p = project(C[corners[ci].v]);
        visCorners.push_back(c);
    }

    auto faceColor = [&](const gp_Dir& n) -> ImU32 {
        if (n.Z() > 0.5)  return theme.cubeFaceTop;
        if (n.Z() < -0.5) return theme.cubeFaceBottom;
        if (n.Y() < -0.5) return theme.cubeFaceFront;
        if (n.Y() > 0.5)  return theme.cubeFaceBack;
        if (n.X() > 0.5)  return theme.cubeFaceRight;
        return theme.cubeFaceLeft;
    };

    ImVec2 mouse = io.MousePos;
    int hoveredCorner = -1;
    int hoveredEdge   = -1;
    int hoveredFace   = -1;

    if (!io.WantCaptureMouse) {
        const float cornerHalf = 10.0f;
        const float edgeTol    = 6.0f;

        for (int k = (int)visCorners.size() - 1; k >= 0; k--) {
            ImVec2 p = visCorners[k].p;
            if (mouse.x >= p.x - cornerHalf && mouse.x <= p.x + cornerHalf &&
                mouse.y >= p.y - cornerHalf && mouse.y <= p.y + cornerHalf) {
                hoveredCorner = k;
                break;
            }
        }

        if (hoveredCorner < 0) {
            for (int k = (int)visEdges.size() - 1; k >= 0; k--) {
                ImVec2 a = visEdges[k].a, b = visEdges[k].b;
                float dx = b.x - a.x, dy = b.y - a.y;
                float len2 = dx*dx + dy*dy;
                if (len2 < 1e-6f) continue;
                float t = ((mouse.x - a.x) * dx + (mouse.y - a.y) * dy) / len2;
                if (t < 0.0f) t = 0.0f;
                if (t > 1.0f) t = 1.0f;
                float px = a.x + t * dx, py = a.y + t * dy;
                float ddx = mouse.x - px, ddy = mouse.y - py;
                if (ddx*ddx + ddy*ddy <= edgeTol * edgeTol) {
                    hoveredEdge = k;
                    break;
                }
            }
        }

        if (hoveredCorner < 0 && hoveredEdge < 0) {
            for (int k = (int)vis.size() - 1; k >= 0; k--) {
                auto& v = vis[k];
                int sign = 0; bool inside = true;
                for (int e = 0; e < 4; e++) {
                    ImVec2 a = v.pts[e], b = v.pts[(e+1)%4];
                    float cr = (b.x-a.x)*(mouse.y-a.y) - (b.y-a.y)*(mouse.x-a.x);
                    int s = (cr > 0) ? 1 : -1;
                    if (sign == 0) sign = s;
                    else if (sign != s) { inside = false; break; }
                }
                if (inside) { hoveredFace = k; break; }
            }
        }
    }

    const ImU32 accentHover    = theme.cubeHover;
    const ImU32 faceHoverColor = theme.cubeFaceHover;
    const ImU32 outlineColor   = theme.cubeOutline;

    for (int k = 0; k < (int)vis.size(); k++) {
        auto& v = vis[k];
        ImU32 col = faceColor(faces[v.idx].n);
        if (k == hoveredFace) col = faceHoverColor;
        draw->AddConvexPolyFilled(v.pts, 4, col);
        draw->AddPolyline(v.pts, 4, outlineColor, ImDrawFlags_Closed, 1.5f);
    }

    if (hoveredEdge >= 0) {
        draw->AddLine(visEdges[hoveredEdge].a, visEdges[hoveredEdge].b,
                      accentHover, 3.0f);
    }

    if (hoveredCorner >= 0) {
        ImVec2 p = visCorners[hoveredCorner].p;
        const float h = 7.0f;
        draw->AddRectFilled(ImVec2(p.x - h, p.y - h),
                            ImVec2(p.x + h, p.y + h),
                            accentHover, 2.0f);
    }

    ImFont* font = ImGui::GetFont();
    ImTextureID atlasTex = io.Fonts->TexID;

    // ===== Подписи: буквы строятся в 3D-координатах грани,
    //       затем проецируются через project() — то же искажение,
    //       что у граней и рёбер. UV из атласа шрифта. =====
    static const double faceTextRight[6][3] = {
        { 1, 0, 0}, {-1, 0, 0}, { 1, 0, 0}, {-1, 0, 0}, { 0, 1, 0}, { 0,-1, 0},
    };
    static const double faceTextDown[6][3] = {
        { 0,-1, 0}, { 0,-1, 0}, { 0, 0,-1}, { 0, 0,-1}, { 0, 0,-1}, { 0, 0,-1},
    };

    for (auto& v : vis) {
        const auto& fc = faces[v.idx];
        int fi = v.idx;

        // Центр грани в 3D-пространстве куба (координаты ±1)
        double cx3 = (C[fc.v[0]].X() + C[fc.v[1]].X() + C[fc.v[2]].X() + C[fc.v[3]].X()) * 0.25;
        double cy3 = (C[fc.v[0]].Y() + C[fc.v[1]].Y() + C[fc.v[2]].Y() + C[fc.v[3]].Y()) * 0.25;
        double cz3 = (C[fc.v[0]].Z() + C[fc.v[1]].Z() + C[fc.v[2]].Z() + C[fc.v[3]].Z()) * 0.25;

        // Экранные длины рёбер грани — только для выбора размера шрифта
        float dxU = v.pts[1].x - v.pts[0].x;
        float dyU = v.pts[1].y - v.pts[0].y;
        float edgeUpx = std::sqrt(dxU*dxU + dyU*dyU);
        float dxV = v.pts[3].x - v.pts[0].x;
        float dyV = v.pts[3].y - v.pts[0].y;
        float edgeVpx = std::sqrt(dxV*dxV + dyV*dyV);
        if (edgeUpx < 6.0f || edgeVpx < 6.0f) continue;

        float fSizePx = (edgeUpx + edgeVpx) * 0.5f * 0.30f;
        if (fSizePx < 8.0f)  fSizePx = 8.0f;
        if (fSizePx > 26.0f) fSizePx = 26.0f;

        // КЛЮЧЕВОЕ: пиксели -> единицы куба (граница куба = [-1..+1] = 2 ед.)
        const double px2cube = 1.0 / (double)cubeHalf;

        const char* s = fc.label;
        const char* s_end = s + std::strlen(s);
        float totalPx = font->CalcTextSizeA(fSizePx, FLT_MAX, 0.0f, s).x;
        if (totalPx < 1.0f) continue;

        double total3D  = (double)totalPx * px2cube;
        double cursor3D = -total3D * 0.5;

        const double* r3 = faceTextRight[fi];
        const double* d3 = faceTextDown[fi];

        auto p3d = [&](double u, double v) -> gp_Pnt {
            return gp_Pnt(cx3 + u * r3[0] + v * d3[0],
                          cy3 + u * r3[1] + v * d3[1],
                          cz3 + u * r3[2] + v * d3[2]);
        };

        while (s < s_end) {
            unsigned char c0 = (unsigned char)s[0];
            int charLen = 1;
            unsigned int cp = c0;
            if      (c0 < 0x80)                       charLen = 1;
            else if ((c0 & 0xE0) == 0xC0 && s+1 < s_end) { cp = ((c0 & 0x1F) << 6) | ((unsigned char)s[1] & 0x3F); charLen = 2; }
            else if ((c0 & 0xF0) == 0xE0 && s+2 < s_end) { cp = ((c0 & 0x0F) << 12) | (((unsigned char)s[1] & 0x3F) << 6) | ((unsigned char)s[2] & 0x3F); charLen = 3; }
            else if ((c0 & 0xF8) == 0xF0 && s+3 < s_end) { cp = ((c0 & 0x07) << 18) | (((unsigned char)s[1] & 0x3F) << 12) | (((unsigned char)s[2] & 0x3F) << 6) | ((unsigned char)s[3] & 0x3F); charLen = 4; }

            const ImFontGlyph* g = font->FindGlyph((ImWchar)cp);
            if (g) {
                float  scale  = fSizePx / font->FontSize;
                float  advPx  = g->AdvanceX * scale;
                double adv3D  = (double)advPx * px2cube;

                float  glyphYCenter = (g->Y0 + g->Y1) * 0.5f;
                double gTop3D = ((double)(g->Y0 - glyphYCenter) * scale) * px2cube;
                double gBot3D = ((double)(g->Y1 - glyphYCenter) * scale) * px2cube;

                double left3D  = cursor3D;
                double right3D = cursor3D + adv3D;

                if (g->X1 > g->X0 && g->Y1 > g->Y0 && advPx > 0.01f) {
                    gp_Pnt q00 = p3d(left3D,  gTop3D);
                    gp_Pnt q10 = p3d(right3D, gTop3D);
                    gp_Pnt q11 = p3d(right3D, gBot3D);
                    gp_Pnt q01 = p3d(left3D,  gBot3D);

                    ImVec2 s00 = project(q00);
                    ImVec2 s10 = project(q10);
                    ImVec2 s11 = project(q11);
                    ImVec2 s01 = project(q01);

                    ImVec2 uv00(g->U0, g->V0);
                    ImVec2 uv10(g->U1, g->V0);
                    ImVec2 uv11(g->U1, g->V1);
                    ImVec2 uv01(g->U0, g->V1);

                    draw->AddImageQuad(atlasTex, s00, s10, s11, s01,
                                       uv00, uv10, uv11, uv01,
                                       theme.cubeText);
                }
                cursor3D += adv3D;
            }
            s += charLen;
        }
    }

    if (hoveredCorner >= 0 || hoveredEdge >= 0 || hoveredFace >= 0) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }

    auto computeUp = [](const gp_Dir& dir) -> gp_Dir {
        gp_Dir zAxis(0, 0, 1);
        if (std::abs(dir.Dot(zAxis)) > 0.99) {
            return gp_Dir(0, 1, 0);
        }
        gp_Vec upProj = gp_Vec(zAxis) - gp_Vec(dir) * dir.Dot(zAxis);
        return gp_Dir(upProj);
    };

    auto startVCAnim = [&](const gp_Dir& targetDir, const gp_Dir& targetUp) {
        if (myAnimator.myActive || !myAnimator.myQueue.empty()) {
            if (myAnimator.myQueue.size() < 8) {
                AnimTarget t;
                t.kind = 0;
                t.dir[0] = targetDir.X(); t.dir[1] = targetDir.Y(); t.dir[2] = targetDir.Z();
                t.up[0]  = targetUp.X();  t.up[1]  = targetUp.Y();  t.up[2]  = targetUp.Z();
                myAnimator.myQueue.push_back(t);
            }
            return;
        }
        myAnimator.myFromDir[0] = camDir.X(); myAnimator.myFromDir[1] = camDir.Y(); myAnimator.myFromDir[2] = camDir.Z();
        myAnimator.myFromUp[0]  = camUp.X();  myAnimator.myFromUp[1]  = camUp.Y();  myAnimator.myFromUp[2]  = camUp.Z();
        myAnimator.myToDir[0]   = targetDir.X(); myAnimator.myToDir[1] = targetDir.Y(); myAnimator.myToDir[2] = targetDir.Z();
        myAnimator.myToUp[0]    = targetUp.X();  myAnimator.myToUp[1]  = targetUp.Y();  myAnimator.myToUp[2]  = targetUp.Z();
        myAnimator.myT = 0.0f;
        myAnimator.myActive = true;
    };

    auto startVCRoll = [&](double angleDeg) {
        if (myAnimator.myActive || !myAnimator.myQueue.empty()) {
            if (myAnimator.myQueue.size() < 8) {
                AnimTarget t;
                t.kind = 1;
                t.dir[0] = angleDeg; t.dir[1] = 0; t.dir[2] = 0;
                t.up[0] = 0; t.up[1] = 0; t.up[2] = 0;
                myAnimator.myQueue.push_back(t);
            }
            return;
        }
        gp_Dir curDir = camDir;
        gp_Dir curUp  = camUp;
        double a  = angleDeg * 3.14159265358979323846 / 180.0;
        double ca = std::cos(a), sa = std::sin(a);
        gp_Vec dirV(curDir), upV(curUp);
        gp_Vec crossV = dirV.Crossed(upV);
        gp_Vec newUpV = upV * ca + crossV * sa;
        double len = std::sqrt(newUpV.X()*newUpV.X() + newUpV.Y()*newUpV.Y() + newUpV.Z()*newUpV.Z());
        if (len < 1e-9) return;
        gp_Dir newUp(newUpV.X()/len, newUpV.Y()/len, newUpV.Z()/len);
        myAnimator.myFromDir[0] = curDir.X(); myAnimator.myFromDir[1] = curDir.Y(); myAnimator.myFromDir[2] = curDir.Z();
        myAnimator.myFromUp[0]  = curUp.X();  myAnimator.myFromUp[1]  = curUp.Y();  myAnimator.myFromUp[2]  = curUp.Z();
        myAnimator.myToDir[0]   = curDir.X(); myAnimator.myToDir[1]   = curDir.Y(); myAnimator.myToDir[2]   = curDir.Z();
        myAnimator.myToUp[0]    = newUp.X();  myAnimator.myToUp[1]    = newUp.Y();  myAnimator.myToUp[2]    = newUp.Z();
        myAnimator.myT = 0.0f;
        myAnimator.myActive = true;
    };

    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        if (hoveredCorner >= 0) {
            auto& c = corners[visCorners[hoveredCorner].idx];
            gp_Vec sum = gp_Vec(faces[c.f1].n) + gp_Vec(faces[c.f2].n) + gp_Vec(faces[c.f3].n);
            gp_Dir newDir = gp_Dir(sum).Reversed();
            gp_Dir newUp = computeUp(newDir);
            startVCAnim(newDir, newUp);
        } else if (hoveredEdge >= 0) {
            auto& e = edges[visEdges[hoveredEdge].idx];
            gp_Vec sum = gp_Vec(faces[e.f1].n) + gp_Vec(faces[e.f2].n);
            gp_Dir newDir = gp_Dir(sum).Reversed();
            gp_Dir newUp = computeUp(newDir);
            startVCAnim(newDir, newUp);
        } else if (hoveredFace >= 0) {
            gp_Dir n = faces[vis[hoveredFace].idx].n;
            gp_Dir newDir = n.Reversed();
            gp_Dir newUp = computeUp(newDir);
            startVCAnim(newDir, newUp);
        }
    }

    float arrowY = center.y + cubeSize * 0.5f + 10.0f;

    bool cubeAligned = (vis.size() == 1);

    const float stickyL = center.x - 80.0f;
    const float stickyR = center.x + 80.0f;
    const float stickyT = center.y - 70.0f;
    const float stickyB = center.y + cubeSize * 0.5f + 100.0f;
    bool mouseInBtnZone =
        (mouse.x >= stickyL && mouse.x <= stickyR &&
         mouse.y >= stickyT && mouse.y <= stickyB);

    if (cubeAligned && hoveredFace >= 0) {
        myButtonsTimer = 0.5f;
    } else if (cubeAligned && mouseInBtnZone && myButtonsTimer > 0.0f) {
        myButtonsTimer = 0.5f;
    } else {
        myButtonsTimer -= io.DeltaTime;
        if (myButtonsTimer < 0.0f) myButtonsTimer = 0.0f;
    }

    if (myButtonsTimer > 0.0f) {
        auto rollCameraAnim = [&](double angleDeg) {
            startVCRoll(angleDeg);
        };

        const float btnW = 40.0f;
        const float btnH = 28.0f;
        const float gap  = 6.0f;
        const float winW = btnW * 2.0f + gap + 16.0f;
        const float winH = btnH + 16.0f;

        ImGui::SetNextWindowPos(ImVec2(center.x - winW * 0.5f, arrowY), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(winW, winH), ImGuiCond_Always);
        ImGui::Begin("##viewcube_arrows", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoBringToFrontOnFocus |
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(gap, 0));
        ImGui::PushStyleColor(ImGuiCol_Button, theme.homeBtnBg);
        if (ImGui::Button("<<", ImVec2(btnW, btnH))) {
            rollCameraAnim( 90.0);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Повернуть по часовой на 90");
        ImGui::SameLine();
        if (ImGui::Button(">>", ImVec2(btnW, btnH))) {
            rollCameraAnim(-90.0);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Повернуть против часовой на 90");
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();

        ImGui::End();
    }
}

} // namespace BreezeCAD::UI::ViewCube
