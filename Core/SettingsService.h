#pragma once

#include <cstddef>
#include <string>
#include <vector>
#include "Preset.h"
#include "SettingsCatalog.h"

namespace WannaClean::Core
{
    enum class SettingsEditResult
    {
        Changed,
        Unchanged,
        Protected,
        Duplicate,
        Invalid
    };

    class SettingsService
    {
    public:
        SettingsCatalog discover(std::vector<Preset>& presets) const;

        SettingsEditResult setApplicationEnabled(
            std::vector<Preset>& presets,
            std::size_t presetIndex,
            const std::string& executableName,
            bool enabled,
            bool isBackground,
            const std::string& displayName,
            const std::string& executablePath) const;

        SettingsEditResult addApplication(
            std::vector<Preset>& presets,
            const std::vector<bool>& selectedPresets,
            const std::string& displayName,
            const std::string& executableName) const;

        bool renameApplication(
            std::vector<Preset>& presets,
            const std::string& executableName,
            const std::string& displayName) const;

        SettingsEditResult removeApplication(
            std::vector<Preset>& presets,
            const std::string& executableName) const;

        SettingsEditResult setServiceEnabled(
            std::vector<Preset>& presets,
            std::size_t presetIndex,
            const std::string& serviceName,
            bool enabled) const;

        SettingsEditResult addService(
            std::vector<Preset>& presets,
            const std::vector<bool>& selectedPresets,
            const std::string& serviceName) const;

        SettingsEditResult removeService(
            std::vector<Preset>& presets,
            const std::string& serviceName) const;

        bool isProtectedApplication(const std::string& executableName) const;
        bool isProtectedService(const std::string& serviceName) const;
    };
}
