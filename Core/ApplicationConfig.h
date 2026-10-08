#pragma once

#include <string>

namespace WannaClean::Core
{
    struct ApplicationConfig
    {
        std::string displayName;
        std::string executableName;
        std::string executablePath;
        bool isBackground = false;
    };
}
