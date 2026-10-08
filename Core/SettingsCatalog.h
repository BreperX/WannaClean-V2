#pragma once

#include <string>
#include <vector>

namespace WannaClean::Core
{
    struct SettingsApplication
    {
        std::string executableName;
        std::string executablePath;
        bool isProtected = false;
    };

    struct SettingsServiceEntry
    {
        std::string serviceName;
        bool isProtected = false;
    };

    struct SettingsCatalog
    {
        std::vector<SettingsApplication> applications;
        std::vector<SettingsApplication> backgroundApplications;
        std::vector<SettingsServiceEntry> services;
    };
}
