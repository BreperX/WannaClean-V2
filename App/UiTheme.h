#pragma once

#include "imgui.h"

namespace WannaClean::App::UiTheme
{
    inline const ImVec4& accent()
    {
        static const ImVec4 value(1.0f, 0.03f, 0.08f, 1.0f);
        return value;
    }

    inline const ImVec4& mutedAccent()
    {
        static const ImVec4 value(0.48f, 0.07f, 0.09f, 1.0f);
        return value;
    }

    inline const ImVec4& success()
    {
        static const ImVec4 value(0.28f, 0.94f, 0.42f, 1.0f);
        return value;
    }

    inline const ImVec4& warning()
    {
        static const ImVec4 value(1.0f, 0.68f, 0.20f, 1.0f);
        return value;
    }

    inline void sectionHeading(const char* title, const char* description = nullptr)
    {
        ImGui::TextColored(accent(), "%s", title);
        if (description != nullptr)
        {
            ImGui::TextDisabled("%s", description);
        }
    }

    inline void metric(const char* label, const char* value, const char* detail = nullptr)
    {
        ImGui::TextDisabled("%s", label);
        ImGui::TextColored(accent(), "%s", value);
        if (detail != nullptr)
        {
            ImGui::TextDisabled("%s", detail);
        }
    }
}
