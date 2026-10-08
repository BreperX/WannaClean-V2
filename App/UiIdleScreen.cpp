#include "UiIdleScreen.h"
#include "imgui.h"

#include <algorithm>
#include <string>
#include <vector>

namespace WannaClean::App
{
    namespace
    {
        constexpr float kPresetButtonWidth = 190.0f;
        constexpr float kPresetButtonHeight = 58.0f;
        constexpr float kFooterButtonWidth = 145.0f;
        constexpr float kFooterButtonHeight = 32.0f;

        void drawIdleLogo(const ImVec2& topLeft, ID3D11ShaderResourceView* logoTexture)
        {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            const ImU32 red = IM_COL32(255, 35, 48, 255);
            const ImVec2 trackMax(topLeft.x + 170.0f, topLeft.y + 126.0f);
            constexpr float bracketLength = 18.0f;

            drawList->AddLine(topLeft, ImVec2(topLeft.x + bracketLength, topLeft.y), red, 3.0f);
            drawList->AddLine(topLeft, ImVec2(topLeft.x, topLeft.y + bracketLength), red, 3.0f);
            drawList->AddLine(ImVec2(trackMax.x, topLeft.y), ImVec2(trackMax.x - bracketLength, topLeft.y), red, 3.0f);
            drawList->AddLine(ImVec2(trackMax.x, topLeft.y), ImVec2(trackMax.x, topLeft.y + bracketLength), red, 3.0f);
            drawList->AddLine(ImVec2(topLeft.x, trackMax.y), ImVec2(topLeft.x + bracketLength, trackMax.y), red, 3.0f);
            drawList->AddLine(ImVec2(topLeft.x, trackMax.y), ImVec2(topLeft.x, trackMax.y - bracketLength), red, 3.0f);
            drawList->AddLine(trackMax, ImVec2(trackMax.x - bracketLength, trackMax.y), red, 3.0f);
            drawList->AddLine(trackMax, ImVec2(trackMax.x, trackMax.y - bracketLength), red, 3.0f);

            if (logoTexture != nullptr)
            {
                const ImVec2 imageMin(topLeft.x + 22.0f, topLeft.y);
                drawList->AddImage(
                    reinterpret_cast<ImTextureID>(logoTexture),
                    imageMin,
                    ImVec2(imageMin.x + 126.0f, imageMin.y + 126.0f));
            }
        }

        void centerText(
            const char* text,
            float width)
        {
            const float textWidth =
                ImGui::CalcTextSize(text).x;

            ImGui::SetCursorPosX(
                ImGui::GetCursorPosX() +
                (width - textWidth) * 0.5f);

            ImGui::TextUnformatted(text);
        }

        void prepareHelpPopup()
        {
            ImGuiViewport* viewport = ImGui::GetMainViewport();
            const ImVec2 workSize = viewport->WorkSize;
            const float maxWidth = (std::max)(1.0f, workSize.x - 32.0f);
            const float minWidth = (std::min)(420.0f, maxWidth);
            const float maxHeight = (std::max)(120.0f, workSize.y - 32.0f);
            ImGui::SetNextWindowPos(
                viewport->GetCenter(),
                ImGuiCond_Appearing,
                ImVec2(0.5f, 0.5f));
            ImGui::SetNextWindowSizeConstraints(
                ImVec2(minWidth, 0.0f),
                ImVec2(maxWidth, maxHeight));
        }
    }
    int RenderIdleScreen(
        const std::vector<WannaClean::Core::Preset>& presets,
        ID3D11ShaderResourceView* logoTexture,
        IdleScreenState& state,
        size_t pendingCount,
        bool& settingsRequested,
        bool& revertPendingServicesRequested)
    {
        int clickedPresetIndex = -1;
        settingsRequested = false;
        revertPendingServicesRequested = false;

        constexpr float footerHeight = 56.0f;

        const bool hasPendingServices =
            pendingCount > 0;

        ImGuiStyle& style = ImGui::GetStyle();

        const ImVec2 headerCursor =
            ImGui::GetCursorScreenPos();

        const ImVec2 windowPos =
            ImGui::GetWindowPos();
        ImGui::GetWindowDrawList()->AddRectFilled(
            ImVec2(
                windowPos.x + style.WindowPadding.x,
                headerCursor.y),
            ImVec2(
                windowPos.x + style.WindowPadding.x + 4.0f,
                headerCursor.y + 48.0f),
            IM_COL32(220, 25, 35, 255));

        ImGui::SetCursorPosX(
            ImGui::GetCursorPosX() + 16.0f);

        ImGui::SetWindowFontScale(1.25f);

        ImGui::Text("WANNACLEAN v2");

        ImGui::SetWindowFontScale(1.0f);

        ImGui::SetCursorPosX(
            ImGui::GetCursorPosX() + 16.0f);

        ImGui::TextDisabled(
            "Prepara tu computadora para lo que importa.");

        ImGui::Dummy(
            ImVec2(0.0f, 8.0f));

        float mainHeight =
            ImGui::GetContentRegionAvail().y -
            footerHeight;

        mainHeight =
            (std::max)(220.0f, mainHeight);

        ImGui::BeginChild(
            "IdleMainArea",
            ImVec2(0.0f, mainHeight),
            false,
            ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoScrollWithMouse);

        const ImVec2 available =
            ImGui::GetContentRegionAvail();

        const float logoWidth = 170.0f;
        const float logoHeight = 126.0f;

        const float gap =
            ImGui::GetStyle().ItemSpacing.x;

        const size_t count =
            (std::min)(presets.size(), size_t(3));

        const float buttonsWidth =
            kPresetButtonWidth *
            static_cast<float>(count) +
            gap *
            static_cast<float>(
                count > 0 ? count - 1 : 0);

        const float itemSpacing = style.ItemSpacing.y;
        float contentHeight =
            logoHeight + itemSpacing +
            8.0f + itemSpacing + ImGui::GetTextLineHeight() + itemSpacing +
            10.0f + itemSpacing + kPresetButtonHeight + itemSpacing;
        if (hasPendingServices)
        {
            contentHeight += 8.0f + itemSpacing +
                (std::max)(kFooterButtonHeight, ImGui::GetTextLineHeight()) + itemSpacing;
        }

        float startY =
            (available.y - contentHeight) * 0.5f;

        startY = hasPendingServices
            ? 0.0f
            : (std::max)(8.0f, startY);

        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + startY);

        ImGui::SetCursorPosX(
            ImGui::GetCursorPosX() + (available.x - logoWidth) * 0.5f);

        drawIdleLogo(ImGui::GetCursorScreenPos(), logoTexture);

        ImGui::Dummy(
            ImVec2(
                logoWidth,
                logoHeight));

        ImGui::Dummy(
            ImVec2(
                0.0f,
                8.0f));

        centerText(
            "Un clic. Menos cosas estorbando.",
            available.x);

        ImGui::Dummy(
            ImVec2(
                0.0f,
                10.0f));

        if (count > 0)
        {
            ImGui::SetCursorPosX(
                ImGui::GetCursorPosX() + (available.x - buttonsWidth) * 0.5f);

            for (size_t i = 0; i < count; ++i)
            {
                ImGui::PushID(
                    static_cast<int>(i));
                if (i != 2)
                {
                    ImGui::PushStyleColor(
                        ImGuiCol_Button,
                        ImVec4(
                            0.11f,
                            0.11f,
                            0.13f,
                            1.0f));

                    ImGui::PushStyleColor(
                        ImGuiCol_ButtonHovered,
                        ImVec4(
                            0.24f,
                            0.035f,
                            0.04f,
                            1.0f));

                    ImGui::PushStyleColor(
                        ImGuiCol_ButtonActive,
                        ImVec4(
                            0.34f,
                            0.04f,
                            0.045f,
                            1.0f));
                }
                else
                {
                    ImGui::PushStyleColor(
                        ImGuiCol_Button,
                        ImVec4(
                            0.55f,
                            0.015f,
                            0.015f,
                            1.0f));

                    ImGui::PushStyleColor(
                        ImGuiCol_ButtonHovered,
                        ImVec4(
                            0.82f,
                            0.03f,
                            0.03f,
                            1.0f));

                    ImGui::PushStyleColor(
                        ImGuiCol_ButtonActive,
                        ImVec4(
                            0.68f,
                            0.02f,
                            0.02f,
                            1.0f));
                }

                if (ImGui::Button(
                    presets[i].name.c_str(),
                    ImVec2(
                        kPresetButtonWidth,
                        kPresetButtonHeight)))
                {
                    clickedPresetIndex =
                        static_cast<int>(i);
                }

                ImGui::PopStyleColor(3);
                ImGui::PopID();

                if (i + 1 < count)
                {
                    ImGui::SameLine();
                }
            }
        }

        if (hasPendingServices)
        {
            ImGui::Dummy(
                ImVec2(
                    0.0f,
                    8.0f));

            const float textWidth = 280.0f;

            ImGui::SetCursorPosX(
                ImGui::GetCursorPosX() + (available.x - textWidth) * 0.5f);

            ImGui::TextColored(
                ImVec4(
                    1.0f,
                    0.68f,
                    0.20f,
                    1.0f),
                "%zu servicio(s) siguen detenidos",
                pendingCount);

            ImGui::SameLine();

            if (ImGui::Button(
                "Revertir##pending-services"))
            {
                revertPendingServicesRequested = true;
            }
        }

        ImGui::EndChild();
        const float footerLineY =
            ImGui::GetCursorScreenPos().y;

        ImGui::GetWindowDrawList()->AddLine(
            ImVec2(
                windowPos.x,
                footerLineY),
            ImVec2(
                windowPos.x + ImGui::GetWindowWidth(),
                footerLineY),
            IM_COL32(220, 25, 35, 255),
            1.0f);

        ImGui::Dummy(
            ImVec2(
                0.0f,
                8.0f));

        ImGui::PushStyleVar(
            ImGuiStyleVar_FrameBorderSize,
            1.0f);

        ImGui::PushStyleColor(
            ImGuiCol_Button,
            ImVec4(
                0.035f,
                0.012f,
                0.012f,
                1.0f));

        ImGui::PushStyleColor(
            ImGuiCol_ButtonHovered,
            ImVec4(
                0.12f,
                0.025f,
                0.025f,
                1.0f));

        ImGui::PushStyleColor(
            ImGuiCol_ButtonActive,
            ImVec4(
                0.18f,
                0.03f,
                0.03f,
                1.0f));

        if (ImGui::Button(
            "Ajustes##idle-settings",
            ImVec2(
                kFooterButtonWidth,
                kFooterButtonHeight)))
        {
            settingsRequested = true;
        }

        ImGui::SameLine();

        ImGui::SetCursorPosX(
            ImGui::GetWindowWidth() -
            ImGui::GetStyle().WindowPadding.x -
            kFooterButtonWidth);

        if (ImGui::Button(
            "? Ayuda##idle-help",
            ImVec2(
                kFooterButtonWidth,
                kFooterButtonHeight)))
        {
            state.showHelp = true;
        }

        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar();

        if (state.showHelp)
        {
            ImGui::OpenPopup(
                "Ayuda de WannaClean");

            state.showHelp = false;
        }

        prepareHelpPopup();

        if (ImGui::BeginPopupModal(
            "Ayuda de WannaClean", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextWrapped(
                "WannaClean prepara tu computadora "
                "cerrando aplicaciones y deteniendo "
                "servicios seleccionados para el preset "
                "que elijas.");

            ImGui::Spacing();

            ImGui::Bullet();
            ImGui::SameLine();
            ImGui::TextWrapped("Jugar: prepara la PC para una sesion de juego.");

            ImGui::Bullet();
            ImGui::SameLine();
            ImGui::TextWrapped("Trabajar: prepara la PC para trabajar.");

            ImGui::Bullet();
            ImGui::SameLine();
            ImGui::TextWrapped(
                "Agresivo: realiza una limpieza mas profunda "
                "y requiere confirmacion.");

            ImGui::Spacing();

            ImGui::TextWrapped(
                "WannaClean solo actua sobre elementos "
                "incluidos explicitamente en los ajustes. "
                "Los procesos y servicios protegidos no se modifican.");

            ImGui::Spacing();
            const float closeButtonWidth = 100.0f;

            ImGui::SetCursorPosX(
                ImGui::GetWindowWidth() -
                ImGui::GetStyle().WindowPadding.x -
                closeButtonWidth);

            if (ImGui::Button(
                "Entendido##idle-help-close",
                ImVec2(
                    closeButtonWidth,
                    30.0f)))
            {
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }

        return clickedPresetIndex;
    }
}
