#pragma once

#include <string>

namespace WannaClean::App
{
    bool RenderRunningScreen(float progress, const std::string& status, bool allowCancellation = true);
}
