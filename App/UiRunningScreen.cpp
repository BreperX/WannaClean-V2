#include "UiRunningScreen.h"
#include "UiTheme.h"
#include "imgui.h"
#include <cstdio>

namespace WannaClean::App
{
    bool RenderRunningScreen(float progress, const std::string& status, bool allowCancellation)
    {
        const ImVec2 available = ImGui::GetContentRegionAvail();
        const float panelWidth = (available.x < 620.0f) ? available.x : 620.0f;
        const float panelHeight = 300.0f;
        const float startY = (available.y - panelHeight) * 0.5f;

        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ((startY > 0.0f) ? startY : 0.0f));
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (available.x - panelWidth) * 0.5f);
        ImGui::BeginChild(
            "RunningCard",
            ImVec2(panelWidth, panelHeight),
            true,
            ImGuiWindowFlags_NoScrollbar);

        UiTheme::sectionHeading("WANNACLEAN V2", "Preparando tu computador para lo que importa.");
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::Text("%s", allowCancellation ? "Optimizando tu equipo" : "Revirtiendo servicios detenidos");
        ImGui::TextDisabled("%s", allowCancellation
            ? "La operacion continua mientras revisamos cada elemento."
            : "Esto puede tardar mientras Windows inicia los servicios.");
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, UiTheme::accent());
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.16f, 0.16f, 0.17f, 1.0f));
        char percentage[16]{};
        std::snprintf(percentage, sizeof(percentage), "%d%%", static_cast<int>(progress * 100.0f));
        ImGui::ProgressBar(progress, ImVec2(-1.0f, 20.0f), percentage);
        ImGui::PopStyleColor(2);
        ImGui::TextColored(UiTheme::accent(), "%s", status.c_str());
        ImGui::Spacing();
        if (allowCancellation)
        {
            ImGui::TextDisabled("El avance incluye aplicaciones, servicios y limpieza de memoria.");
        }
        ImGui::Spacing();

        bool cancelRequested = false;
        if (allowCancellation)
        {
            const float buttonWidth = 190.0f;
            const float contentWidth = ImGui::GetContentRegionAvail().x;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (contentWidth - buttonWidth) * 0.5f);
            cancelRequested = ImGui::Button(
                "Cancelar operacion",
                ImVec2(buttonWidth, 38.0f));
        }

        ImGui::EndChild();
        return cancelRequested;
    }
}
