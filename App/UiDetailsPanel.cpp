#include "UiDetailsPanel.h"
#include "UiTheme.h"
#include "imgui.h"

namespace WannaClean::App
{
    namespace
    {
        const char* processOutcomeName(WannaClean::Core::ProcessCloseOutcome outcome)
        {
            using WannaClean::Core::ProcessCloseOutcome;
            switch (outcome)
            {
            case ProcessCloseOutcome::ClosedGracefully: return "CERRADO";
            case ProcessCloseOutcome::ForcedTermination: return "FORZADO";
            case ProcessCloseOutcome::StillRunning: return "SIGUE ACTIVO";
            case ProcessCloseOutcome::ForceTerminationFailed: return "ERROR";
            case ProcessCloseOutcome::ErrorOpeningProcess: return "ERROR";
            case ProcessCloseOutcome::EnumerationFailed: return "ERROR";
            case ProcessCloseOutcome::Protected: return "PROTEGIDO";
            case ProcessCloseOutcome::Ignored: return "OMITIDO";
            case ProcessCloseOutcome::AlreadyExited: return "YA CERRADO";
            }
            return "UNKNOWN";
        }

        const char* serviceOutcomeName(WannaClean::Core::ServiceStopOutcome outcome)
        {
            using WannaClean::Core::ServiceStopOutcome;
            switch (outcome)
            {
            case ServiceStopOutcome::Stopped: return "DETENIDO";
            case ServiceStopOutcome::FailedToStop: return "ERROR";
            case ServiceStopOutcome::Protected: return "PROTEGIDO";
            case ServiceStopOutcome::Ignored: return "OMITIDO";
            }
            return "UNKNOWN";
        }
    }

    void RenderDetailsPanel(const WannaClean::Core::OperationResult& result)
    {
        UiTheme::sectionHeading("DETALLES DE LA OPERACION");
        ImGui::Spacing();

        if (ImGui::CollapsingHeader("Aplicaciones", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (result.processDetails.empty())
            {
                ImGui::TextDisabled("No hay detalles de procesos.");
            }
            for (const auto& process : result.processDetails)
            {
                const bool closed = process.outcome == WannaClean::Core::ProcessCloseOutcome::ClosedGracefully ||
                    process.outcome == WannaClean::Core::ProcessCloseOutcome::ForcedTermination;
                if (closed)
                {
                    const double estimatedMegabytes =
                        static_cast<double>(process.estimatedWorkingSetBytes) / 1048576.0;
                    ImGui::BulletText("%s  |  %s  |  %.2f MB estimados",
                        process.processName.c_str(),
                        processOutcomeName(process.outcome),
                        estimatedMegabytes);
                }
                else
                {
                    ImGui::BulletText("%s  |  %s",
                        process.processName.c_str(),
                        processOutcomeName(process.outcome));
                }
            }
        }

        if (ImGui::CollapsingHeader("Servicios", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (result.serviceDetails.empty())
            {
                ImGui::TextDisabled("No hay detalles de servicios.");
            }
            for (const auto& service : result.serviceDetails)
            {
                ImGui::BulletText("%s  |  %s",
                    service.serviceName.c_str(),
                    serviceOutcomeName(service.outcome));
            }
        }

        if (ImGui::CollapsingHeader("Errores", result.errors.empty() ? 0 : ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (result.errors.empty())
            {
                ImGui::TextColored(UiTheme::success(), "No hubo errores.");
            }
            for (const auto& error : result.errors)
            {
                ImGui::TextColored(UiTheme::warning(), "%s: %s",
                    error.target.c_str(), error.reason.c_str());
            }
        }
    }
}
