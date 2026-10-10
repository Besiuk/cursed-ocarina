#include <cmath>
#include <memory>
#include <imgui.h>
#include <ship/Context.h>
#include <ship/window/Window.h>
#include <ship/window/gui/GuiWindow.h>

#include "CursedScript_Effects.h"
#include "CursedScript_Debug.h"
#include "CursedScript_DebugItems.h"
#include "soh/ShipInit.hpp"

namespace {

class CursedWindow final : public Ship::GuiWindow {
  public:
    using GuiWindow::GuiWindow;

    void InitElement() override {
    }
    void DrawElement() override {
    }
    void UpdateElement() override {
    }

    // Delete toggles the window, End rolls the dice
    void Draw() override {
        if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
            mOpen = !mOpen;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_End, false)) {
            Cursed::RollDice();
        }
        if (!mOpen) {
            return;
        }

        ImGui::SetNextWindowSize(ImVec2(380, 440), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Cursed Ocarina", &mOpen)) {
            if (ImGui::Button("Roll dice")) {
                Cursed::RollDice();
            }
            ImGui::SameLine();
            if (ImGui::Button("Stop all")) {
                Cursed::StopAll();
            }
            ImGui::Separator();
            ImGui::Checkbox("Infinite nuts", &Cursed::gDebugInfiniteNuts);
            ImGui::Checkbox("No damage for Link", &Cursed::gDebugNoDamage);
            ImGui::Separator();
            ImGui::TextUnformatted("Equipment");
            if (ImGui::Button("All equipment")) {
                Cursed::GiveAllEquipment();
            }
            ImGui::SameLine();
            if (ImGui::Button("Reset item states")) {
                Cursed::ResetItemStates();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Back to the first time: Stick Saber unlit, pick-up and ignition texts again,\nKokiri sword intact");
            }
            ImGui::SameLine();
            if (ImGui::Button(Cursed::DebugIsAdult() ? "Switch to Child" : "Switch to Adult")) {
                Cursed::DebugSwitchAge();
            }
            for (const Cursed::DebugUpgrade& upgrade : Cursed::GetDebugUpgrades()) {
                ImGui::PushID(upgrade.name);
                if (ImGui::SmallButton("-")) {
                    Cursed::ChangeUpgradeLevel(upgrade, -1);
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("+")) {
                    Cursed::ChangeUpgradeLevel(upgrade, 1);
                }
                ImGui::SameLine();
                const int level = Cursed::GetUpgradeLevel(upgrade);
                ImGui::Text("%s: %s", upgrade.name,
                            level < static_cast<int>(upgrade.levelNames.size()) ? upgrade.levelNames[level] : "?");
                ImGui::PopID();
            }
            ImGui::Separator();
            ImGui::TextUnformatted("Dice pool (checked = can be rolled)");
            if (ImGui::Button("All on")) {
                Cursed::SetAllEnabled(true);
            }
            ImGui::SameLine();
            if (ImGui::Button("All off")) {
                Cursed::SetAllEnabled(false);
            }
            ImGui::Separator();

            for (const Cursed::Effect& effect : Cursed::GetEffects()) {
                ImGui::PushID(effect.id.c_str());
                bool enabled = Cursed::IsEnabled(effect.id);
                if (ImGui::Checkbox("##enabled", &enabled)) {
                    Cursed::SetEnabled(effect.id, enabled);
                }
                ImGui::SameLine();
                if (ImGui::Button("Trigger")) {
                    Cursed::Trigger(effect.id);
                }
                ImGui::SameLine();
                if (Cursed::IsActive(effect.id)) {
                    ImGui::Text("%s (%.0fs)", effect.name.c_str(), Cursed::RemainingSeconds(effect.id));
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Stop")) {
                        Cursed::Stop(effect.id);
                    }
                } else {
                    ImGui::TextUnformatted(effect.name.c_str());
                }
                ImGui::PopID();
            }
        }
        ImGui::End();
    }

  private:
    bool mOpen = false;
};

// Always-on list of running timers (top right). Empty list = nothing is drawn.
class TimerHud final : public Ship::GuiWindow {
  public:
    using GuiWindow::GuiWindow;

    void InitElement() override {
    }
    void DrawElement() override {
    }
    void UpdateElement() override {
    }

    void Draw() override {
        const std::vector<Cursed::TimerInfo> timers = Cursed::GetActiveTimers();
        if (timers.empty()) {
            return;
        }
        ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowViewport(vp->ID);
        ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x - 30.0f, vp->Pos.y + 30.0f), ImGuiCond_Always,
                                ImVec2(1.0f, 0.0f));
        ImGui::SetNextWindowBgAlpha(0.5f);
        ImGui::Begin("cursed_timers", nullptr,
                     ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing |
                         ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
                         ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings);
        ImGui::SetWindowFontScale(1.6f);
        for (const Cursed::TimerInfo& timer : timers) {
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 1.0f, 1.0f), "%s", timer.name.c_str());
            ImGui::SameLine();
            if (timer.infinite) {
                // Infinity sign drawn by hand, the UI font has no glyph for it
                const float h = ImGui::GetTextLineHeight();
                const float w = h * 1.6f;
                const ImVec2 pos = ImGui::GetCursorScreenPos();
                ImGui::Dummy(ImVec2(w, h));
                const ImVec2 c(pos.x + w * 0.5f, pos.y + h * 0.5f);
                const float a = w * 0.5f;
                ImVec2 points[33];
                for (int i = 0; i < 33; i++) {
                    const float t = 6.2831853f * static_cast<float>(i) / 32.0f;
                    const float d = 1.0f + std::sin(t) * std::sin(t);
                    points[i] = ImVec2(c.x + a * std::cos(t) / d, c.y + a * std::sin(t) * std::cos(t) / d * 1.4f);
                }
                ImGui::GetWindowDrawList()->AddPolyline(points, 33, IM_COL32(255, 204, 77, 255), ImDrawFlags_None,
                                                        2.0f);
            } else {
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "%ds", static_cast<int>(timer.seconds + 0.99f));
            }
        }
        ImGui::End();
    }
};

void Register() {
    auto gui = Ship::Context::GetRawInstance()->GetWindow()->GetGui();
    auto window = std::make_shared<CursedWindow>("gWindows.CursedOcarina", "Cursed Ocarina", ImVec2(380, 440));
    gui->AddGuiWindow(window);
    window->Show();

    auto hud = std::make_shared<TimerHud>("gWindows.CursedOcarinaTimers", "Cursed Ocarina Timers", ImVec2(10, 10));
    gui->AddGuiWindow(hud);
    hud->Show();
}

} // namespace

static RegisterShipInitFunc cursedWindowInit(Register);
