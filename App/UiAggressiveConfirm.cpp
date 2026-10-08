#include "UiAggressiveConfirm.h"
#include "UiTheme.h"
#include "imgui.h"

namespace WannaClean::App
{
    void RenderAggressiveConfirmDialog(
        const WannaClean::Core::OperationPlan& plan,
        bool& confirmed,
        bool& cancelled)
    {
        confirmed = false;
        cancelled = false;

        const ImVec2 available = ImGui::GetContentRegionAvail();
        const float panelWidth = (available.x < 620.0f) ? available.x : 620.0f;
        const float panelHeight = (available.y < 460.0f) ? available.y : 460.0f;
        const float startY = (available.y - panelHeight) * 0.5f;
        ImGui::SetCursorPosY((startY > 0.0f) ? startY : 0.0f);
        ImGui::SetCursorPosX((available.x - panelWidth) * 0.5f);
        ImGui::BeginChild("AggressiveConfirmCard", ImVec2(panelWidth, panelHeight), true);

        const float buttonWidth = 160.0f;
        const float footerHeight = 36.0f + ImGui::GetStyle().ItemSpacing.y * 2.0f + 2.0f;
        ImGui::BeginChild("AggressiveConfirmContent", ImVec2(0.0f, -footerHeight), false);
        ImGui::TextColored(UiTheme::accent(), "AGRESIVO");
        ImGui::TextDisabled("Revisa el alcance antes de continuar.");
        ImGui::Separator();
        ImGui::Text("ESTO VA A CERRAR");
        ImGui::BulletText("%zu procesos activos de la lista", plan.activeProcesses.size());
        ImGui::BulletText("%zu aplicaciones seleccionadas", plan.processNames.size());
        ImGui::Spacing();
        ImGui::Text("ESTO VA A DETENER");
        ImGui::BulletText("%zu servicios seleccionados", plan.serviceNames.size());
        ImGui::Spacing();
        ImGui::TextColored(UiTheme::success(), "NO VA A TOCAR");
        ImGui::BulletText("Procesos y servicios protegidos");
        ImGui::BulletText("Procesos fuera de la lista");
        ImGui::BulletText("Tus archivos");

        if (ImGui::CollapsingHeader("Ver objetivos detectados"))
        {
            ImGui::BeginChild("AggressiveTargets", ImVec2(0.0f, 100.0f), true);
            for (const auto& process : plan.activeProcesses)
            {
                ImGui::BulletText("%s (PID %u)%s",
                    process.processName.c_str(),
                    process.processId,
                    process.protectedProcess ? " [PROTEGIDO]" : "");
            }
            for (const auto& service : plan.services)
            {
                ImGui::BulletText("Servicio: %s%s",
                    service.serviceName.c_str(),
                    service.protectedService ? " [PROTEGIDO]" : "");
            }
            ImGui::EndChild();
        }

        ImGui::EndChild();
        ImGui::Separator();
        const float buttonsWidth = buttonWidth * 2.0f + ImGui::GetStyle().ItemSpacing.x;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - buttonsWidth);
        cancelled = ImGui::Button("Cancelar", ImVec2(buttonWidth, 36.0f));
        ImGui::SameLine();
        confirmed = ImGui::Button("Proceder", ImVec2(buttonWidth, 36.0f));
        ImGui::EndChild();
    }
}
