#include "UiDoneScreen.h"
#include "UiTheme.h"
#include "imgui.h"
#include <string>
#include <cstdio>

namespace WannaClean::App
{
    namespace
    {
        std::string formatMegabytes(uint64_t bytes)
        {
            char value[64]{};
            std::snprintf(value, sizeof(value), "%.2f MB", static_cast<double>(bytes) / 1048576.0);
            return value;
        }

        void drawMemoryCard(const char* id, const char* heading, const char* firstLabel,
            const std::string& firstValue, const char* secondLabel, const std::string& secondValue,
            float width)
        {
            ImGui::BeginChild(
                id,
                ImVec2(width, 126.0f),
                true,
                ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
            UiTheme::sectionHeading(heading);
            ImGui::Separator();
            ImGui::TextDisabled("%s", firstLabel);
            ImGui::SameLine();
            ImGui::TextColored(UiTheme::success(), "%s", firstValue.c_str());
            ImGui::TextDisabled("%s", secondLabel);
            ImGui::SameLine();
            ImGui::TextColored(UiTheme::success(), "%s", secondValue.c_str());
            ImGui::EndChild();
        }
    }

    void RenderDoneScreen(const WannaClean::Core::OperationResult& result, bool& detailsRequested, bool detailsVisible)
    {
        detailsRequested = false;
        const ImVec2 available = ImGui::GetContentRegionAvail();
        const float panelWidth = (available.x < 900.0f) ? available.x : 900.0f;
        const float panelHeight = 340.0f;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (available.x - panelWidth) * 0.5f);
        ImGui::BeginChild(
            "DoneCard",
            ImVec2(panelWidth, panelHeight),
            true,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        UiTheme::sectionHeading("RESULTADO DE LA OPERACION");
        const char* resultTitle = result.cancelled
            ? "Operacion cancelada"
            : (result.errors.empty() ? "LISTO" : "TERMINADO CON AVISOS");
        const ImVec4& resultColor = result.cancelled || !result.errors.empty()
            ? UiTheme::warning()
            : UiTheme::success();
        ImGui::SetWindowFontScale(1.6f);
        ImGui::TextColored(resultColor, "%s", resultTitle);
        ImGui::SetWindowFontScale(1.0f);
        const char* resultDescription = result.cancelled
            ? "La operacion se detuvo; estos son los resultados parciales."
            : (result.errors.empty()
                ? "Tu equipo termino de procesar las tareas seleccionadas."
                : "La operacion termino con errores; consulta el detalle.");
        ImGui::TextWrapped("%s", resultDescription);
        ImGui::Separator();
        ImGui::Spacing();

        const float gap = ImGui::GetStyle().ItemSpacing.x;
        const float cardWidth = (ImGui::GetContentRegionAvail().x - gap) * 0.5f;
        const std::string processMemory = formatMegabytes(result.processWorkingSetEstimateBytes);
        const std::string standbyMemory = result.standbyReductionEstimateValid
            ? formatMegabytes(result.standbyReductionEstimateBytes)
            : "No disponible";
        drawMemoryCard(
            "MemorySummary",
            "MEMORIA",
            "Working set de apps",
            processMemory,
            "Estimación standby",
            standbyMemory,
            cardWidth);
        ImGui::SameLine();

        ImGui::BeginChild(
            "OperationSummary",
            ImVec2(cardWidth, 126.0f),
            true,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        UiTheme::sectionHeading("OPERACIONES");
        ImGui::Separator();
        ImGui::Text("Aplicaciones cerradas: %d", result.processesClosed);
        ImGui::Text("Servicios detenidos: %d", result.servicesStopped);
        ImGui::TextColored(
            result.errors.empty() ? UiTheme::success() : UiTheme::warning(),
            "Errores: %zu",
            result.errors.size());
        ImGui::EndChild();

        ImGui::Spacing();
        ImGui::TextDisabled("Estimaciones independientes; pueden superponerse y no deben sumarse.");
        ImGui::Spacing();
        const float buttonWidth = 170.0f;
        const float contentWidth = ImGui::GetContentRegionAvail().x;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (contentWidth - buttonWidth) * 0.5f);
        detailsRequested = ImGui::Button(
            detailsVisible ? "Ocultar detalles" : "Ver detalles",
            ImVec2(buttonWidth, 32.0f));
        ImGui::EndChild();
    }
}
