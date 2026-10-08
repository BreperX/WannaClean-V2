#pragma once

#include "imgui.h"
#include <algorithm>

namespace WannaClean::App
{
    namespace UiWindow
    {
        inline void prepareMainWindow(
            const ImVec2& preferredSize = ImVec2(900.0f, 600.0f))
        {
            ImGuiViewport* viewport =
                ImGui::GetMainViewport();

            const ImVec2 workPos =
                viewport->WorkPos;

            const ImVec2 workSize =
                viewport->WorkSize;

            constexpr float screenMargin = 20.0f;

            const float maxWidth =
                (std::max)(
                    1.0f,
                    workSize.x - screenMargin * 2.0f);

            const float maxHeight =
                (std::max)(
                    1.0f,
                    workSize.y - screenMargin * 2.0f);

            ImGui::SetNextWindowSizeConstraints(
                ImVec2(1.0f, 1.0f),
                ImVec2(maxWidth, maxHeight));

            ImGui::SetNextWindowSize(
                preferredSize,
                ImGuiCond_Appearing);

            ImGui::SetNextWindowPos(
                ImVec2(
                    workPos.x + workSize.x * 0.5f,
                    workPos.y + workSize.y * 0.5f),
                ImGuiCond_Appearing,
                ImVec2(0.5f, 0.5f));
        }
    }
}