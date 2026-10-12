// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 MikeVerx
#include "app/shell/TouchpadSettings.h"

#include "core/I18n.h"
#include "ui/GestureArt.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace bz::app {
namespace {

// The order the choices are shown in, and the values behind them.
constexpr std::array<const char*, 4> kMoveLabels{N_("Сдвиг"), N_("Вращение"), N_("Масштаб"), N_("Нет")};
constexpr std::array<TouchMove, 4>   kMoves{TouchMove::Pan, TouchMove::Orbit, TouchMove::Zoom, TouchMove::None};
constexpr std::array<const char*, 3> kTurnLabels{N_("Крен вида"), N_("Вращение"), N_("Нет")};
constexpr std::array<TouchTurn, 3>   kTurns{TouchTurn::Roll, TouchTurn::Orbit, TouchTurn::None};

template <class T, std::size_t N>
int IndexOf(const std::array<T, N>& values, T value) {
    for (std::size_t i = 0; i < N; ++i)
        if (values[i] == value) return static_cast<int>(i);
    return 0;
}

std::string CommandLabel(const AppContext& app, std::string_view id) {
    if (id.empty()) return Tr("Ничего");
    if (id == kTouchMenu) return Tr("Контекстное меню");
    if (const Action* a = app.actions.Find(id)) return a->Label();
    return std::string(id);
}

} // namespace

void DrawTouchpadSettings(AppContext& app, float width) {
    ui::Ui& ui = app.ui;
    const ui::ThemeColors& c = ui.C();
    TouchNavigation& t = app.settings.touch;
    const platform::TouchSupport can = platform::TouchpadSupport();

    const float rows = 8;
    const float artW = ui.S(124), artH = ui.S(52), gap = ui.S(14), rowGap = ui.S(10);
    // The rest of the dialog takes about 480 px: the list scrolls on small screens.
    const float listH = std::clamp(ImGui::GetIO().DisplaySize.y - ui.S(480), ui.S(200), rows * (artH + rowGap));
    ImGui::BeginChild("##touchpad-gestures", {width, listH}, ImGuiChildFlags_None);
    const float w = ImGui::GetContentRegionAvail().x;
    const float textW = ui.S(180);
    const float control = std::max(ui.S(200), w - artW - textW - gap * 2);

    // One gesture: its picture, its name, and on the right what it does.
    const auto row = [&](const char* id, const std::string& title, ui::GestureArt art, const char* key, bool available, auto&& choose) {
        ImGui::PushID(id);
        const ImVec2 at = ImGui::GetCursorScreenPos();
        ui::DrawGestureArt(ui, ImGui::GetWindowDrawList(), at, {artW, artH}, art, app.now, key, !available);
        if (available) app.shell.animateUi = true;   // the pictures move: keep drawing frames

        ImGui::SetCursorScreenPos({at.x + artW + gap, at.y + ui.S(available ? 8 : 4)});
        ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + textW);
        {
            ui::FontScope font(ui.fonts.medium);
            ImGui::PushStyleColor(ImGuiCol_Text, ui::ToVec4(available ? c.text : c.textDisabled));
            ImGui::TextUnformatted(title.c_str());
            ImGui::PopStyleColor();
        }
        if (!available) {
            ImGui::SetCursorScreenPos({at.x + artW + gap, ImGui::GetCursorScreenPos().y});
            ui::FontScope font(ui.fonts.caption);
            ImGui::PushStyleColor(ImGuiCol_Text, ui::ToVec4(c.textMuted));
            ImGui::TextUnformatted(Tr("недоступно в этой системе"));
            ImGui::PopStyleColor();
        }
        ImGui::PopTextWrapPos();

        ImGui::SetCursorScreenPos({at.x + w - control, at.y + (artH - ui.S(34)) / 2});
        ImGui::BeginDisabled(!available);
        choose();
        ImGui::EndDisabled();
        ImGui::SetCursorScreenPos({at.x, at.y + artH + rowGap});
        ImGui::Dummy({0, 0});
        ImGui::PopID();
    };
    const auto moveChoice = [&ui, control](TouchMove& setting) {
        return [&ui, control, m = &setting] {
            const int current = IndexOf(kMoves, *m);
            if (const int n = ui::Segmented(ui, "move", kMoveLabels, current, 4, control); n != current) *m = kMoves[static_cast<std::size_t>(n)];
        };
    };
    const auto commandChoice = [&app, &ui, control](std::string& setting) {
        return [&app, &ui, control, command = &setting] {
            ImGui::SetNextItemWidth(control);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ui.S(3));
            if (ImGui::BeginCombo("##command", CommandLabel(app, *command).c_str())) {
                for (const std::string_view id : TouchCommands())
                    if (ImGui::Selectable(CommandLabel(app, id).c_str(), *command == id)) *command = std::string(id);
                ImGui::EndCombo();
            }
        };
    };
    const char* ctrl = ModifierName(ModifierChoice::Ctrl);
    const char* alt = ModifierName(ModifierChoice::Alt);
    row("two", Tr("Два пальца"), ui::GestureArt::TwoFingers, nullptr, can.scroll, moveChoice(t.twoFingers));
    row("shift", Trf("{} + два пальца", "Shift"), ui::GestureArt::TwoFingers, "Shift", can.scroll, moveChoice(t.withShift));
    row("ctrl", Trf("{} + два пальца", ctrl), ui::GestureArt::TwoFingers, ctrl, can.scroll, moveChoice(t.withCtrl));
    row("alt", Trf("{} + два пальца", alt), ui::GestureArt::TwoFingers, alt, can.scroll, moveChoice(t.withAlt));
    row("pinch", Tr("Щипок"), ui::GestureArt::Pinch, nullptr, can.pinch, [&] {
        static constexpr std::array<const char*, 2> labels{N_("Масштаб"), N_("Нет")};
        const int current = t.pinchZooms ? 0 : 1;
        if (const int n = ui::Segmented(ui, "pinch", labels, current, 2, control); n != current) t.pinchZooms = n == 0;
    });
    row("turn", Tr("Поворот двумя пальцами"), ui::GestureArt::Rotate, nullptr, can.rotate, [&] {
        const int current = IndexOf(kTurns, t.turn);
        if (const int n = ui::Segmented(ui, "turn", kTurnLabels, current, 3, control); n != current) t.turn = kTurns[static_cast<std::size_t>(n)];
    });
    row("tap", Tr("Двойное касание двумя пальцами"), ui::GestureArt::DoubleTap, nullptr, can.smartZoom, commandChoice(t.doubleTap));
    row("force", Tr("Сильное нажатие"), ui::GestureArt::ForceClick, nullptr, can.forceClick, commandChoice(t.forceClick));
    ImGui::EndChild();

    ImGui::Dummy({0, ui.S(4)});
    {
        static constexpr std::array<const char*, 4> speeds{N_("Медленно"), N_("Обычно"), N_("Быстро"), N_("Очень быстро")};
        static constexpr std::array<double, 4> values{0.5, 1.0, 1.5, 2.0};
        int current = 1;
        for (std::size_t i = 0; i < values.size(); ++i)
            if (std::abs(values[i] - t.speed) < 1e-6) current = static_cast<int>(i);
        ui::FieldLabel(ui, Tr("Скорость"));
        if (const int n = ui::Segmented(ui, "touch-speed", speeds, current, 4, width); n != current) t.speed = values[static_cast<std::size_t>(n)];
        static constexpr std::array<const char*, 2> direction{N_("Как пальцы"), N_("Наоборот")};
        ui::FieldLabel(ui, Tr("Вид движется"));
        const int inv = t.invert ? 1 : 0;
        if (const int n = ui::Segmented(ui, "touch-invert", direction, inv, 2, width); n != inv) t.invert = n == 1;
    }
#if !defined(__APPLE__)
    // macOS tells fingers from a wheel itself; here a smooth wheel is a guess.
    ImGui::Dummy({0, ui.S(2)});
    ui::FieldLabel(ui, Tr("Плавная прокрутка (не по щелчкам колеса)"));
    {
        static constexpr std::array<const char*, 2> smooth{N_("Это тачпад"), N_("Это мышь с плавным колесом")};
        const int s = t.smoothIsTouchpad ? 0 : 1;
        if (const int n = ui::Segmented(ui, "touch-smooth", smooth, s, 2, width); n != s) t.smoothIsTouchpad = n == 0;
    }
#endif
    ImGui::Dummy({0, ui.S(2)});
    {
        ui::FontScope font(ui.fonts.caption);
        ImGui::PushStyleColor(ImGuiCol_Text, ui::ToVec4(c.textMuted));
        ImGui::PushTextWrapPos(0);
        if (!can.scroll)
            ImGui::TextUnformatted(Tr("Система не сообщает программе жесты тачпада: два пальца работают как колесо мыши."));
        ImGui::TextUnformatted(Tr("Щелчок двумя пальцами — это правая кнопка мыши: меню и вращение, как на вкладке «Мышь». "
                                  "Жесты тремя и четырьмя пальцами остаются за системой (рабочие столы, переключение окон)."));
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    }
    if (ui::LinkButton(ui, "##touch-reset", Tr("Вернуть как было"))) t = TouchNavigation{};
}

} // namespace bz::app
