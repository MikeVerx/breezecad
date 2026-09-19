#include "app/Application.h"
#include "app/Localization.h"
#include "doc/OrdoApplication.h"
#include "platform/FileDialog.h"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <GLFW/glfw3.h>

#include <TDF_ChildIterator.hxx>
#include <TDataStd_Name.hxx>
#include <TDataStd_Integer.hxx>
#include <TCollection_AsciiString.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Vertex.hxx>
#include <TopExp_Explorer.hxx>
#include <BRep_Tool.hxx>
#include <TNaming_NamedShape.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>
#include <gp_Dir.hxx>
#include <gp_Trsf.hxx>
#include <gp_Ax3.hxx>
#include <gp_Pln.hxx>

#define _USE_MATH_DEFINES
#include <cmath>
#include <cstring>
#include <sstream>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace OrdoCAD {

static std::string LabelKey(const TDF_Label& label) {
    std::ostringstream oss;
    label.EntryDump(oss);
    return oss.str();
}

static void LoadCyrillicFont() {
    ImGuiIO& io = ImGui::GetIO();
    const std::vector<std::string> candidates = {
        "assets/fonts/Roboto-Regular.ttf",
        "../assets/fonts/Roboto-Regular.ttf",
        "../../assets/fonts/Roboto-Regular.ttf",
        "C:/Windows/Fonts/segoeui.ttf",
        "C:/Windows/Fonts/arial.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    };
    for (const auto& path : candidates) {
        std::error_code ec;
        if (!std::filesystem::exists(path, ec)) continue;
        ImFont* font = io.Fonts->AddFontFromFileTTF(
            path.c_str(), 18.0f, nullptr,
            io.Fonts->GetGlyphRangesCyrillic());
        if (font) { std::cout << "OrdoCAD: font '" << path << "'\n"; return; }
    }
    io.Fonts->AddFontDefault();
}

Application::Application() {
    myWindow = std::make_unique<GlfwWindow>(1280, 720, "OrdoCAD - Prototype");
    if (!myWindow->GetNativeWindow()) return;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    LoadCyrillicFont();
    ImGui_ImplGlfw_InitForOpenGL(myWindow->GetNativeWindow(), true);
    ImGui_ImplOpenGL3_Init("#version 330");

    int fbW = 0, fbH = 0;
    glfwGetFramebufferSize(myWindow->GetNativeWindow(), &fbW, &fbH);

    myOcctView = std::make_unique<OcctView>();
    try {
        if (!myOcctView->Init(myWindow->GetNativeWindow(), fbW, fbH)) {
            myOcctView.reset(); return;
        }
    } catch (...) { myOcctView.reset(); return; }

    myDocument = std::make_unique<OrdoDocument>();
    myDocument->CreateBaseGeometry();

    RefreshView();
    if (myOcctView) myOcctView->FitAll();

    myWindow->SetMouseButtonCallback([this](int b, int a, int m) {
        if (ImGui::GetIO().WantCaptureMouse) return;
        if (myOcctView) myOcctView->OnMouseButton(b, a, m);
    });
    myWindow->SetCursorPosCallback([this](double x, double y) {
        if (ImGui::GetIO().WantCaptureMouse) return;
        if (myOcctView) myOcctView->OnMouseMove(x, y);
    });
    myWindow->SetScrollCallback([this](double x, double y) {
        if (ImGui::GetIO().WantCaptureMouse) return;
        if (myOcctView) myOcctView->OnMouseScroll(x, y);
    });
    myWindow->SetResizeCallback([this](int w, int h) {
        if (myOcctView) myOcctView->Resize(w, h);
    });
}

Application::~Application() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void Application::RefreshView() {
    if (!myOcctView || !myDocument) return;
    myOcctView->ClearAllShapes();

    // Рекурсивный обход всего дерева
    std::function<void(const TDF_Label&)> walk = [&](const TDF_Label& l) {
        TopoDS_Shape s = myDocument->GetShape(l);
        if (!s.IsNull()) {
            myOcctView->DisplayShape(s, l, OrdoDocument::IsSystem(l));
            // Применяем сохранённую видимость
            std::string key = LabelKey(l);
            auto it = myVisibility.find(key);
            if (it != myVisibility.end() && !it->second) {
                myOcctView->SetShapeVisible(l, false);
            }
        }
        TDF_ChildIterator it(l, false);
        for (; it.More(); it.Next()) walk(it.Value());
    };
    walk(myDocument->MainLabel());

    myOcctView->Redraw();
}

void Application::OnNewDocument() {
    if (!myDocument) return;
    OrdoApplication::Instance().CloseDocument(myDocument->GetDoc());
    myDocument = std::make_unique<OrdoDocument>();
    myDocument->CreateBaseGeometry();
    myCurrentPath.clear();
    myVisibility.clear();
    RefreshView();
    if (myOcctView) myOcctView->FitAll();
}

void Application::OnSave() {
    if (!myDocument) return;
    std::string path = myCurrentPath;
    if (path.empty()) {
        if (!ShowFileDialog(true, path, "Сохранить как")) return;
    }
    if (OrdoApplication::Instance().SaveDocument(myDocument->GetDoc(), path)) {
        myCurrentPath = path;
        std::strncpy(myFilePathBuffer, path.c_str(), sizeof(myFilePathBuffer) - 1);
        myFilePathBuffer[sizeof(myFilePathBuffer) - 1] = '\0';
        std::cout << "Saved to: " << myCurrentPath << "\n";
    }
}

void Application::OnOpen() {
    std::string path = myCurrentPath;
    if (!ShowFileDialog(false, path, "Открыть файл")) return;

    Handle(TDocStd_Document) loaded =
        OrdoApplication::Instance().OpenDocument(path);
    if (loaded.IsNull()) { std::cerr << "Open failed\n"; return; }

    myDocument = std::make_unique<OrdoDocument>();
    myDocument->SetDocument(loaded);
    myCurrentPath = path;
    myVisibility.clear();
    std::strncpy(myFilePathBuffer, path.c_str(), sizeof(myFilePathBuffer) - 1);
    myFilePathBuffer[sizeof(myFilePathBuffer) - 1] = '\0';
    RefreshView();
    if (myOcctView) myOcctView->FitAll();
}

// === Дерево ===

bool Application::DrawLabelTree(const TDF_Label& label,
                                 const TDF_Label& selectedLabel) {
    std::string labelName = "<unnamed>";
    Handle(TDataStd_Name) nameAttr;
    if (label.FindAttribute(TDataStd_Name::GetID(), nameAttr)) {
        TCollection_AsciiString ascii(nameAttr->Get());
        labelName = ascii.ToCString();
    }

    int type = OrdoDocument::GetType(label);
    bool isSystem = (type < 0);

    TDF_ChildIterator childIt(label, false);
    bool hasChildren = childIt.More();

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                                ImGuiTreeNodeFlags_SpanAvailWidth |
                                ImGuiTreeNodeFlags_DefaultOpen;
    if (!hasChildren)
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    if (!selectedLabel.IsNull() && selectedLabel.IsEqual(label))
        flags |= ImGuiTreeNodeFlags_Selected;

    ImGui::PushID(LabelKey(label).c_str());

    // Чекбокс видимости для системных элементов с геометрией
    if (isSystem && (type == FT_Plane || type == FT_Axis)) {
        std::string key = LabelKey(label);
        bool visible = true;
        auto it = myVisibility.find(key);
        if (it != myVisibility.end()) visible = it->second;

        if (ImGui::Checkbox("##vis", &visible)) {
            myVisibility[key] = visible;
            if (myOcctView) myOcctView->SetShapeVisible(label, visible);
        }
        ImGui::SameLine();
    }

    if (isSystem) ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(160, 160, 160, 255));
    bool opened = ImGui::TreeNodeEx(labelName.c_str(), flags);
    if (isSystem) ImGui::PopStyleColor();

    // Двойной клик по эскизу → войти в режим 2D
    if (type == FT_Sketch && ImGui::IsItemHovered()
        && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        myPendingEnterSketchLabel = label;
    }

    // Контекстное меню
    if (ImGui::BeginPopupContextItem("##ctx")) {
        ImGui::TextDisabled("%s", labelName.c_str());
        ImGui::Separator();

        if (type == FT_Plane) {
            if (ImGui::MenuItem(Tr("btn.createSketch").c_str())) {
                myPendingCreateSketchLabel = label;
            }
        }
        if (!isSystem) {
            if (ImGui::MenuItem(Tr("btn.delete").c_str())) {
                myPendingDeleteLabel = label;
            }
        }
        ImGui::EndPopup();
    }

    ImGui::SameLine();
    ImGui::TextDisabled("[type=%d, tag=%d]", type, label.Tag());

    bool requested = false;
    if (opened && hasChildren) {
        TDF_ChildIterator it(label, false);
        for (; it.More(); it.Next()) {
            if (DrawLabelTree(it.Value(), selectedLabel)) requested = true;
        }
        ImGui::TreePop();
    }

    ImGui::PopID();
    return requested || !myPendingDeleteLabel.IsNull()
                     || !myPendingCreateSketchLabel.IsNull();
}

void Application::DrawMenuBar() {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New")) OnNewDocument();
            if (ImGui::MenuItem("Open...  (Ctrl+O)")) OnOpen();
            ImGui::Separator();
            if (ImGui::MenuItem("Save  (Ctrl+S)")) OnSave();
            if (ImGui::MenuItem("Save As...")) { myCurrentPath.clear(); OnSave(); }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit"))
                glfwSetWindowShouldClose(myWindow->GetNativeWindow(), GLFW_TRUE);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            if (ImGui::MenuItem("Fit All")) {
                if (myOcctView) myOcctView->FitAll();
            }
            if (ImGui::MenuItem("Fullscreen (F11)")) myWindow->ToggleFullscreen();
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Language")) {
            Language cur = Localization::Instance().GetLanguage();
            if (ImGui::MenuItem("English", nullptr, cur == Language::EN))
                Localization::Instance().SetLanguage(Language::EN);
            if (ImGui::MenuItem("Русский", nullptr, cur == Language::RU))
                Localization::Instance().SetLanguage(Language::RU);
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }
}

void Application::DrawDocumentTree() {
    ImGuiIO& io = ImGui::GetIO();
    const float panelWidth = 340.0f;

    ImGui::SetNextWindowPos(ImVec2(0, 20), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(panelWidth, io.DisplaySize.y - 20), ImGuiCond_Always);

    ImGui::Begin("##ocaf_tree", nullptr,
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoTitleBar);

    ImGui::Text("%s", Tr("panel.title").c_str());
    ImGui::TextDisabled("%s", Tr("panel.hint.delete").c_str());
    ImGui::Separator();

    if (ImGui::Button(Tr("btn.addBox").c_str())) {
        myShowAddBoxDialog = true;
        std::strncpy(myNewBoxName, Tr("default.boxName").c_str(), sizeof(myNewBoxName) - 1);
        myNewBoxName[sizeof(myNewBoxName) - 1] = '\0';
        myNewBoxPos[0] = myNewBoxPos[1] = myNewBoxPos[2] = 0.0f;
        myNewBoxSize[0] = myNewBoxSize[1] = myNewBoxSize[2] = 100.0f;
    }
    ImGui::SameLine();
    if (ImGui::Button("Save")) OnSave();
    ImGui::SameLine();
    if (ImGui::Button("Open")) OnOpen();
    ImGui::Separator();

    if (!myDocument || myDocument->GetDoc().IsNull()) {
        ImGui::Text("%s", Tr("panel.document.null").c_str());
        ImGui::End();
        return;
    }

    TDF_Label selectedLabel;
    if (myOcctView) selectedLabel = myOcctView->GetSelectedLabel();

    TDF_Label root = myDocument->MainLabel();

    if (ImGui::TreeNodeEx(Tr("panel.document").c_str(),
            ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth)) {
        TDF_Label baseLabel = myDocument->GetBaseGeometryLabel();

        if (!baseLabel.IsNull()) {
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(160, 160, 160, 255));
            bool opened = ImGui::TreeNodeEx("Origin",
                ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth);
            ImGui::PopStyleColor();
            if (opened) {
                TDF_ChildIterator it(baseLabel, false);
                for (; it.More(); it.Next())
                    DrawLabelTree(it.Value(), selectedLabel);
                ImGui::TreePop();
            }
        }

        TDF_ChildIterator it(root, false);
        for (; it.More(); it.Next()) {
            TDF_Label child = it.Value();
            if (child.IsEqual(baseLabel)) continue;
            DrawLabelTree(child, selectedLabel);
        }
        ImGui::TreePop();
    }

    ImGui::End();

    // Обработка отложенных операций
    if (!myPendingCreateSketchLabel.IsNull() && myDocument) {
        myDocument->AddSketchFeature(myPendingCreateSketchLabel);
        myPendingCreateSketchLabel = TDF_Label();
    }
    if (!myPendingEnterSketchLabel.IsNull() && myDocument) {
        EnterSketch(myPendingEnterSketchLabel);
        myPendingEnterSketchLabel = TDF_Label();
    }
    if (!myPendingDeleteLabel.IsNull() && myDocument) {
        myDocument->RemoveFeature(myPendingDeleteLabel);
        myPendingDeleteLabel = TDF_Label();
        RefreshView();
        if (myOcctView) myOcctView->FitAll();
    }
}

void Application::RefreshSketchLines() {
    if (!myDocument || !myOcctView || myActiveSketchLabel.IsNull()) return;

    // Обходим дочерние метки эскиза и показываем их линии
    TDF_ChildIterator it(myActiveSketchLabel, false);
    for (; it.More(); it.Next()) {
        TDF_Label lineLabel = it.Value();
        TopoDS_Shape s = myDocument->GetShape(lineLabel);
        if (!s.IsNull()) {
            LineStyle style = OrdoDocument::GetLineStyle(lineLabel);
            myOcctView->DisplaySketchLine(s, lineLabel, static_cast<int>(style));
        }
    }
}

// Вычисление длины и угла в плоскости эскиза
static void ComputeLengthAngle(const gp_Pln& plane, const gp_Pnt& from, const gp_Pnt& to,
                               double& length, double& angleDeg) {
    gp_Ax3 ax = plane.Position();
    gp_Pnt origin = ax.Location();
    gp_Dir xDir = ax.XDirection();
    gp_Dir yDir = ax.YDirection();

    gp_Vec v(from, to);
    double u = v.Dot(gp_Vec(xDir));
    double w = v.Dot(gp_Vec(yDir));

    length = std::sqrt(u*u + w*w);
    angleDeg = std::atan2(w, u) * 180.0 / M_PI;
}

// Принудительное выравнивание угла к ближайшему кратному 90° (Компас-стиль)
static gp_Pnt ApplyOrtho(const gp_Pln& plane, const gp_Pnt& from, const gp_Pnt& to) {
    gp_Ax3 ax = plane.Position();
    gp_Pnt origin = ax.Location();
    gp_Dir xDir = ax.XDirection();
    gp_Dir yDir = ax.YDirection();

    gp_Vec v(from, to);
    double u = v.Dot(gp_Vec(xDir));
    double w = v.Dot(gp_Vec(yDir));

    double len = std::sqrt(u*u + w*w);
    if (len < 1e-6) return to;

    // Выбираем ближайшее направление: 0 / 90 / 180 / 270
    double u2 = 0.0, w2 = 0.0;
    if (std::abs(u) >= std::abs(w)) {
        u2 = (u >= 0) ? len : -len;
        w2 = 0.0;
    } else {
        u2 = 0.0;
        w2 = (w >= 0) ? len : -len;
    }

    gp_Pnt result = from;  // орто работает от стартовой точки
    result.Translate(gp_Vec(xDir) * u2);
    result.Translate(gp_Vec(yDir) * w2);
    return result;
}

// Проверка: попадает ли точка p в существующую вершину эскиза
// (Origin или конец уже нарисованной линии).
// skip — точка, которую нужно игнорировать (старт новой линии,
//        чтобы случайно не «замкнуться» на только что начатой линии).
static bool IsPointOnExistingVertex(const gp_Pnt& p, const TDF_Label& sketchLabel,
                                     const gp_Pnt& skip,
                                     double threshold) {
    auto dist2 = [](const gp_Pnt& a, const gp_Pnt& b) {
        double dx = a.X()-b.X(), dy = a.Y()-b.Y(), dz = a.Z()-b.Z();
        return dx*dx + dy*dy + dz*dz;
    };

    double th2 = threshold * threshold;

    // Origin
    gp_Pnt origin(0, 0, 0);
    if (dist2(p, origin) < th2 && dist2(skip, origin) > th2)
        return true;

    // Концы уже нарисованных линий эскиза
    TDF_ChildIterator it(sketchLabel, false);
    for (; it.More(); it.Next()) {
        TDF_Label lineLabel = it.Value();
        Handle(TNaming_NamedShape) ns;
        if (lineLabel.FindAttribute(TNaming_NamedShape::GetID(), ns) && !ns.IsNull()) {
            TopoDS_Shape s = ns->Get();
            if (!s.IsNull() && s.ShapeType() == TopAbs_EDGE) {
                TopoDS_Edge e = TopoDS::Edge(s);
                TopExp_Explorer exp(e, TopAbs_VERTEX);
                for (; exp.More(); exp.Next()) {
                    gp_Pnt vp = BRep_Tool::Pnt(TopoDS::Vertex(exp.Current()));
                    if (dist2(skip, vp) > th2 && dist2(p, vp) < th2) {
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

// Округление точки до сетки (шаг 10 мм) в плоскости эскиза.
static gp_Pnt SnapToGridPoint(const gp_Pnt& p, const gp_Pln& plane) {
    gp_Ax3 ax = plane.Position();
    gp_Pnt origin = ax.Location();
    gp_Dir xDir   = ax.XDirection();
    gp_Dir yDir   = ax.YDirection();

    gp_Vec v(origin, p);
    double u = v.Dot(gp_Vec(xDir));
    double w = v.Dot(gp_Vec(yDir));

    const double step = 10.0;
    u = std::round(u / step) * step;
    w = std::round(w / step) * step;

    gp_Pnt snapped = origin;
    snapped.Translate(gp_Vec(xDir) * u);
    snapped.Translate(gp_Vec(yDir) * w);
    return snapped;
}

// Привязка к известным точкам: Origin (0,0,0) + концы существующих линий эскиза
static bool SnapToKnownPoint(const gp_Pnt& p, const TDF_Label& sketchLabel,
                              const Handle(TDocStd_Document)& doc,
                              double threshold, gp_Pnt& out) {
    if (doc.IsNull() || sketchLabel.IsNull()) return false;

    std::vector<gp_Pnt> candidates;
    candidates.push_back(gp_Pnt(0, 0, 0));  // Origin

    // Концы линий эскиза
    TDF_ChildIterator it(sketchLabel, false);
    for (; it.More(); it.Next()) {
        TDF_Label lineLabel = it.Value();
        Handle(TNaming_NamedShape) ns;
        if (lineLabel.FindAttribute(TNaming_NamedShape::GetID(), ns) && !ns.IsNull()) {
            TopoDS_Shape s = ns->Get();
            if (!s.IsNull() && s.ShapeType() == TopAbs_EDGE) {
                TopoDS_Edge e = TopoDS::Edge(s);
                TopExp_Explorer exp(e, TopAbs_VERTEX);
                for (; exp.More(); exp.Next()) {
                    TopoDS_Vertex v = TopoDS::Vertex(exp.Current());
                    gp_Pnt vp = BRep_Tool::Pnt(v);
                    candidates.push_back(vp);
                }
            }
        }
    }

    double bestDist = threshold * threshold;
    bool found = false;
    for (const auto& c : candidates) {
        double dx = c.X() - p.X();
        double dy = c.Y() - p.Y();
        double dz = c.Z() - p.Z();
        double d2 = dx*dx + dy*dy + dz*dz;
        if (d2 < bestDist) {
            bestDist = d2;
            out = c;
            found = true;
        }
    }
    return found;
}

void Application::OnSketchClick(int px, int py) {
    if (!myOcctView || !myDocument || myActiveSketchLabel.IsNull()) return;

    gp_Pnt p;
    if (!myOcctView->ScreenToPlane(px, py, p)) return;

    // 1. Shift → ortho mode (45°)
    bool shiftHeld = glfwGetKey(myWindow->GetNativeWindow(), GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS
                  || glfwGetKey(myWindow->GetNativeWindow(), GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;

    if (myHasSketchStart && shiftHeld) {
        p = ApplyOrtho(myOcctView->GetSketchPlane(), mySketchStartPoint, p);
    } else {
        // 2. Привязка к известным точкам
        bool snapped = false;
        if (mySnapToPoints) {
            gp_Pnt snapP;
            if (SnapToKnownPoint(p, myActiveSketchLabel, myDocument->GetDoc(),
                                  3.0, snapP)) {
                p = snapP;
                snapped = true;
            }
        }
        // 3. Привязка к сетке
        if (!snapped && mySnapToGrid) {
            p = SnapToGridPoint(p, myOcctView->GetSketchPlane());
        }
    }

    if (!myHasSketchStart) {
        mySketchStartPoint = p;
        myHasSketchStart = true;
        std::cout << "Sketch: start point set\n";
    } else {
        // Проверяем, что точки не совпадают
        double dx = p.X() - mySketchStartPoint.X();
        double dy = p.Y() - mySketchStartPoint.Y();
        double dz = p.Z() - mySketchStartPoint.Z();
        double dist2 = dx*dx + dy*dy + dz*dz;

        if (dist2 < 1e-4) {
            std::cout << "Sketch: points coincide, ignoring\n";
            return;
        }

        // 1. Проверка замыкания — ДО создания линии.
        //    Если конечная точка p совпадает с существующей вершиной эскиза
        //    (Origin или концом другой линии, кроме старта этой линии) — замыкание.
        bool loopClosed = IsPointOnExistingVertex(p, myActiveSketchLabel,
                                                   mySketchStartPoint,
                                                   0.5);  // порог 0.5 мм

        std::cout << "Sketch: loop check = " << (loopClosed ? "CLOSED" : "open")
                  << " (start was at " << mySketchStartPoint.X() << ","
                  << mySketchStartPoint.Y() << "," << mySketchStartPoint.Z()
                  << " end at " << p.X() << "," << p.Y() << "," << p.Z() << ")\n";

        std::cout << "Sketch: creating line with style="
                  << static_cast<int>(myCurrentLineStyle) << "\n";

        try {
            myDocument->AddSketchLine(myActiveSketchLabel, mySketchStartPoint, p, myCurrentLineStyle);
            myOcctView->ClearPreviewLine();

            TopoDS_Shape line = BRepBuilderAPI_MakeEdge(mySketchStartPoint, p).Shape();

            int maxTag = 0;
            TDF_ChildIterator it(myActiveSketchLabel, false);
            for (; it.More(); it.Next()) {
                if (it.Value().Tag() > maxTag) maxTag = it.Value().Tag();
            }
            TDF_Label lineLabel = myActiveSketchLabel.FindChild(maxTag, false);
            myOcctView->DisplaySketchLine(line, lineLabel, static_cast<int>(myCurrentLineStyle));

            if (myAutoContinue && !loopClosed) {
                mySketchStartPoint = p;
                myHasSketchStart = true;
                myOcctView->ShowCursorMarker(p);
                std::cout << "Sketch: line added, continuing\n";
            } else {
                myHasSketchStart = false;
                myOcctView->ClearPreviewLine();
                if (loopClosed) {
                    std::cout << "Sketch: line added, loop closed\n";
                } else {
                    std::cout << "Sketch: line added, stopped\n";
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "Sketch: exception creating line: " << e.what() << "\n";
        } catch (...) {
            std::cerr << "Sketch: unknown exception creating line\n";
        }
    }
}

void Application::EnterSketch(const TDF_Label& sketchLabel) {
    if (!myDocument || !myOcctView || sketchLabel.IsNull()) return;

    // Родитель эскиза — плоскость. Берём её геометрию и вычисляем нормаль.
    TDF_Label parentPlane = sketchLabel.Father();
    if (parentPlane.IsNull()) {
        std::cerr << "EnterSketch: no parent plane\n";
        return;
    }

    TopoDS_Shape planeShape = myDocument->GetShape(parentPlane);
    if (planeShape.IsNull() || planeShape.ShapeType() != TopAbs_FACE) {
        std::cerr << "EnterSketch: parent is not a face\n";
        return;
    }

    TopoDS_Face face = TopoDS::Face(planeShape);
    BRepAdaptor_Surface surf(face);
    Standard_Real u = (surf.FirstUParameter() + surf.LastUParameter()) * 0.5;
    Standard_Real v = (surf.FirstVParameter() + surf.LastVParameter()) * 0.5;
    gp_Pnt p;
    gp_Vec du, dv;
    surf.D1(u, v, p, du, dv);
    gp_Dir normal = du.Crossed(dv);

    myOcctView->EnterSketchMode(normal, p);
    myActiveSketchLabel = sketchLabel;
    myHasSketchStart = false;

    // Показать уже существующие линии эскиза
    RefreshSketchLines();

    std::cout << "Application: entered sketch mode\n";
}

void Application::ExitSketch() {
    myHasSketchStart = false;
    if (myOcctView) {
        myOcctView->ClearPreviewLine();
        myOcctView->ExitSketchMode();
    }
    myActiveSketchLabel = TDF_Label();
    std::cout << "Application: exited sketch mode\n";
}

void Application::DrawPreviewInfo() {
    if (!myHasPreview || myActiveSketchLabel.IsNull()) return;

    // Позиция — рядом с курсором
    ImVec2 pos = ImVec2((float)myMouseX + 20, (float)myMouseY + 20);

    ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.75f);
    ImGui::Begin("##preview_info", nullptr,
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoNav);

    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.3f, 1.0f), "%.2f mm", myPreviewLength);
    ImGui::TextColored(ImVec4(0.7f, 0.9f, 1.0f, 1.0f), "%.1f°", myPreviewAngle);

    ImGui::End();
}

void Application::DrawSketchOverlay() {
    if (myActiveSketchLabel.IsNull()) return;

    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f - 100, 30), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(200, 0), ImGuiCond_Always);
    ImGui::Begin("##sketch_overlay", nullptr,
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_AlwaysAutoResize);

    ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "SKETCH MODE");
    ImGui::Separator();
    if (ImGui::Button("Finish Sketch", ImVec2(180, 30))) {
        ExitSketch();
    }
    if (ImGui::Button("Undo Last Line", ImVec2(180, 25))) {
        if (myDocument && !myActiveSketchLabel.IsNull()) {
            myDocument->RemoveLastSketchLine(myActiveSketchLabel);
            // Перерисовываем
            if (myOcctView) myOcctView->ClearAllShapes();
            RefreshView();
        }
    }
    if (ImGui::Button("Clear Sketch", ImVec2(180, 25))) {
        if (myDocument && !myActiveSketchLabel.IsNull()) {
            myDocument->ClearSketch(myActiveSketchLabel);
            if (myOcctView) myOcctView->ClearAllShapes();
            RefreshView();
        }
    }
    ImGui::Separator();
    ImGui::Text("Line Style:");

    // Normal — фиолетовая (основная геометрия)
    if (ImGui::RadioButton("Normal (purple)", myCurrentLineStyle == LS_Normal))
        myCurrentLineStyle = LS_Normal;

    // Construction — серая пунктирная (вспомогательная)
    if (ImGui::RadioButton("Construction (dashed)", myCurrentLineStyle == LS_Construction))
        myCurrentLineStyle = LS_Construction;

    // Centerline — коричневая штрих-пунктирная (осевая)
    if (ImGui::RadioButton("Centerline (dot-dash)", myCurrentLineStyle == LS_Centerline))
        myCurrentLineStyle = LS_Centerline;

    ImGui::Separator();
    ImGui::Text("Snapping:");
    ImGui::Checkbox("To Grid (10mm)", &mySnapToGrid);
    ImGui::Checkbox("To Points (Origin, Ends)", &mySnapToPoints);
    ImGui::Checkbox("Auto-Continue", &myAutoContinue);
    ImGui::Separator();
    ImGui::TextDisabled("LMB: click start, click end");
    ImGui::TextDisabled("Shift: ortho (0/90 deg)");
    ImGui::TextDisabled("Esc: cancel line / exit sketch");
    ImGui::End();
}

void Application::DrawAddBoxDialog() {
    if (myShowAddBoxDialog) {
        ImGui::OpenPopup(Tr("dlg.addBox.title").c_str());
        myShowAddBoxDialog = false;
    }
    if (ImGui::BeginPopupModal(Tr("dlg.addBox.title").c_str(), nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::InputText(Tr("dlg.name").c_str(), myNewBoxName, sizeof(myNewBoxName));
        ImGui::Separator();
        ImGui::Text("%s", Tr("dlg.position").c_str());
        ImGui::InputFloat("X##pos", &myNewBoxPos[0]);
        ImGui::InputFloat("Y##pos", &myNewBoxPos[1]);
        ImGui::InputFloat("Z##pos", &myNewBoxPos[2]);
        ImGui::Separator();
        ImGui::Text("%s", Tr("dlg.size").c_str());
        ImGui::InputFloat("DX", &myNewBoxSize[0]);
        ImGui::InputFloat("DY", &myNewBoxSize[1]);
        ImGui::InputFloat("DZ", &myNewBoxSize[2]);
        ImGui::Separator();
        if (ImGui::Button(Tr("btn.ok").c_str(), ImVec2(120, 0))) {
            if (myDocument) {
                myDocument->AddBoxFeature(myNewBoxName,
                    myNewBoxSize[0], myNewBoxSize[1], myNewBoxSize[2],
                    myNewBoxPos[0], myNewBoxPos[1], myNewBoxPos[2]);
                RefreshView();
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(Tr("btn.cancel").c_str(), ImVec2(120, 0)))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void Application::Run() {
    bool prevF11 = false;
    bool prevDelete = false;
    bool prevCtrlS = false;
    bool prevCtrlO = false;

    while (!myWindow->ShouldClose()) {
      try {
        myWindow->PollEvents();

        bool curF11 = glfwGetKey(myWindow->GetNativeWindow(), GLFW_KEY_F11) == GLFW_PRESS;
        if (curF11 && !prevF11) myWindow->ToggleFullscreen();
        prevF11 = curF11;

        // В режиме эскиза: всегда показываем маркер под курсором,
        // а если есть старт — ещё и линию-превью
        if (!myActiveSketchLabel.IsNull() && myOcctView) {
            if (!ImGui::GetIO().WantCaptureMouse) {
                static double lastPreviewX = -1e9, lastPreviewY = -1e9;

                double mx, my;
                glfwGetCursorPos(myWindow->GetNativeWindow(), &mx, &my);

                double dxp = mx - lastPreviewX;
                double dyp = my - lastPreviewY;
                if (dxp*dxp + dyp*dyp > 9.0) {  // сдвиг > 3 пикселей
                    lastPreviewX = mx;
                    lastPreviewY = my;

                    int winW = 1, winH = 1, fbW = 1, fbH = 1;
                    glfwGetWindowSize(myWindow->GetNativeWindow(), &winW, &winH);
                    glfwGetFramebufferSize(myWindow->GetNativeWindow(), &fbW, &fbH);
                    int px = (int)(mx * fbW / winW);
                    int py = (int)(my * fbH / winH);

                    gp_Pnt cur;
                    if (myOcctView->ScreenToPlane(px, py, cur)) {
                        bool shiftHeld = glfwGetKey(myWindow->GetNativeWindow(), GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS
                                      || glfwGetKey(myWindow->GetNativeWindow(), GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;

                        if (myHasSketchStart && shiftHeld) {
                            cur = ApplyOrtho(myOcctView->GetSketchPlane(), mySketchStartPoint, cur);
                        } else {
                            bool curSnapped = false;
                            if (mySnapToPoints) {
                                gp_Pnt snapP;
                                if (SnapToKnownPoint(cur, myActiveSketchLabel,
                                                      myDocument->GetDoc(),
                                                      3.0, snapP)) {
                                    cur = snapP;
                                    curSnapped = true;
                                }
                            }
                            if (!curSnapped && mySnapToGrid) {
                                cur = SnapToGridPoint(cur, myOcctView->GetSketchPlane());
                            }
                        }

                        if (myHasSketchStart) {
                            myOcctView->ShowPreviewLine(mySketchStartPoint, cur);

                            // Считаем длину и угол для отображения
                            ComputeLengthAngle(myOcctView->GetSketchPlane(),
                                                mySketchStartPoint, cur,
                                                myPreviewLength, myPreviewAngle);
                            myHasPreview = true;
                        } else {
                            // Проверяем: подсветить зелёным, если точка попадает
                            // в существующую вершину (снап сработает при клике)
                            bool willSnap = IsPointOnExistingVertex(cur,
                                              myActiveSketchLabel,
                                              gp_Pnt(1e9, 1e9, 1e9),  // skip невозможно
                                              3.0);
                            myOcctView->ShowCursorMarkerHighlighted(cur, willSnap);
                            myHasPreview = false;
                        }
                    }
                }
            }

            static bool prevLMB = false;
            bool curLMB = glfwGetMouseButton(myWindow->GetNativeWindow(), GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
            if (curLMB && !prevLMB) {
                // Получаем позицию курсора в координатах framebuffer
                double mx, my;
                glfwGetCursorPos(myWindow->GetNativeWindow(), &mx, &my);
                int winW = 1, winH = 1, fbW = 1, fbH = 1;
                glfwGetWindowSize(myWindow->GetNativeWindow(), &winW, &winH);
                glfwGetFramebufferSize(myWindow->GetNativeWindow(), &fbW, &fbH);
                int px = (int)(mx * fbW / winW);
                int py = (int)(my * fbH / winH);
                // Не обрабатывать клики по панелям ImGui
                if (!ImGui::GetIO().WantCaptureMouse) {
                    OnSketchClick(px, py);
                }
            }
            prevLMB = curLMB;
        }

        bool curDelete = glfwGetKey(myWindow->GetNativeWindow(), GLFW_KEY_DELETE) == GLFW_PRESS;
        if (curDelete && !prevDelete && myOcctView && myDocument) {
            TDF_Label sel = myOcctView->GetSelectedLabel();
            if (!sel.IsNull() && !OrdoDocument::IsSystem(sel)) {
                myDocument->RemoveFeature(sel);
                myOcctView->ClearSelection();
                RefreshView();
                myOcctView->FitAll();
            }
        }
        prevDelete = curDelete;

        bool ctrl = glfwGetKey(myWindow->GetNativeWindow(), GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS
                 || glfwGetKey(myWindow->GetNativeWindow(), GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;
        bool curCtrlS = ctrl && glfwGetKey(myWindow->GetNativeWindow(), GLFW_KEY_S) == GLFW_PRESS;
        if (curCtrlS && !prevCtrlS) OnSave();
        prevCtrlS = curCtrlS;

        bool curCtrlO = ctrl && glfwGetKey(myWindow->GetNativeWindow(), GLFW_KEY_O) == GLFW_PRESS;
        if (curCtrlO && !prevCtrlO) OnOpen();
        prevCtrlO = curCtrlO;

        // Esc — сначала прерывает текущую линию, потом выходит из эскиза
        static bool prevEsc = false;
        bool curEsc = glfwGetKey(myWindow->GetNativeWindow(), GLFW_KEY_ESCAPE) == GLFW_PRESS;
        if (curEsc && !prevEsc && !myActiveSketchLabel.IsNull()) {
            if (myHasSketchStart) {
                // Прервать текущую цепочку линий
                myHasSketchStart = false;
                if (myOcctView) myOcctView->ClearPreviewLine();
                myHasPreview = false;
                std::cout << "Sketch: line chain cancelled\n";
            } else {
                // Нечего прерывать — выходим из эскиза
                ExitSketch();
            }
        }
        prevEsc = curEsc;

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Запоминаем позицию мыши для превью
        {
            double mx, my;
            glfwGetCursorPos(myWindow->GetNativeWindow(), &mx, &my);
            myMouseX = mx;
            myMouseY = my;
        }

        DrawMenuBar();
        DrawDocumentTree();
        DrawAddBoxDialog();
        DrawSketchOverlay();
        DrawPreviewInfo();

        ImGuiIO& io = ImGui::GetIO();
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 360, 30), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(350, 0), ImGuiCond_Always);
        ImGui::Begin(Tr("debug.title").c_str(), nullptr,
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
        if (myOcctView) {
            TDF_Label sel = myOcctView->GetSelectedLabel();
            if (!sel.IsNull()) {
                char buf[128];
                std::snprintf(buf, sizeof(buf), Tr("debug.selected.tag").c_str(), sel.Tag());
                ImGui::Text("%s", buf);
            } else {
                ImGui::TextDisabled("%s", Tr("debug.selected.none").c_str());
            }
        }
        if (!myCurrentPath.empty()) ImGui::Text("File: %s", myCurrentPath.c_str());
        else ImGui::TextDisabled("File: <unsaved>");
        ImGui::Separator();
        ImGui::Text("Ctrl+S save, Ctrl+O open");
        ImGui::Text("RMB on plane - create sketch");
        ImGui::End();

        ImGui::Render();

        int fbW = 0, fbH = 0;
        glfwGetFramebufferSize(myWindow->GetNativeWindow(), &fbW, &fbH);
        glViewport(0, 0, fbW, fbH);
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        if (myOcctView) myOcctView->Redraw();

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        myWindow->SwapBuffers();
      } catch (const std::exception& e) {
        std::cerr << "Runtime exception in main loop: " << e.what() << "\n";
      } catch (...) {
        std::cerr << "Unknown exception in main loop (ignored)\n";
      }
    }
}

} // namespace OrdoCAD
