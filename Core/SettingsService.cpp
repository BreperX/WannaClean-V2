#include "SettingsService.h"
#include "ProcessEngine.h"
#include "ServiceEngine.h"
#include "StringUtil.h"
#include <algorithm>
#include <windows.h>
#include <utility>

namespace WannaClean::Core
{
    namespace
    {
        bool sameName(const std::string& left, const std::string& right)
        {
            return Utf8EqualsIgnoreCase(left, right);
        }

        bool containsName(const std::vector<std::string>& names, const std::string& name)
        {
            return std::any_of(names.begin(), names.end(), [&](const std::string& value)
                {
                    return sameName(value, name);
                });
        }

        bool isBackground(const std::vector<Preset>& presets, const std::string& executableName)
        {
            for (const auto& preset : presets)
            {
                for (const auto& application : preset.applications)
                {
                    if (application.isBackground && sameName(application.executableName, executableName))
                    {
                        return true;
                    }
                }
            }
            return false;
        }

        std::string currentExecutableName()
        {
            wchar_t path[MAX_PATH]{};
            const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
            if (length == 0 || length >= MAX_PATH)
            {
                return {};
            }

            std::string executable = WideToUtf8(path);
            const size_t separator = executable.find_last_of("\\/");
            return separator == std::string::npos ? executable : executable.substr(separator + 1);
        }

        std::string configuredPath(const std::vector<Preset>& presets, const std::string& executableName)
        {
            for (const auto& preset : presets)
            {
                for (const auto& application : preset.applications)
                {
                    if (sameName(application.executableName, executableName) &&
                        !application.executablePath.empty())
                    {
                        return application.executablePath;
                    }
                }
            }
            return {};
        }

        bool addApplicationEntry(
            std::vector<SettingsApplication>& entries,
            const std::string& executableName,
            const std::string& executablePath,
            const ProcessEngine& processEngine)
        {
            if (executableName.empty() || std::any_of(entries.begin(), entries.end(), [&](const auto& entry)
                {
                    return sameName(entry.executableName, executableName);
                }))
            {
                return false;
            }

            std::string path = executablePath;
            if (path.empty())
            {
                path = processEngine.resolveExecutablePath(executableName);
            }
            entries.push_back({ executableName, std::move(path), processEngine.isProtectedProcess(executableName) });
            return true;
        }

        bool eraseName(std::vector<std::string>& names, const std::string& name)
        {
            const size_t previousSize = names.size();
            names.erase(std::remove_if(names.begin(), names.end(), [&](const std::string& value)
                {
                    return sameName(value, name);
                }), names.end());
            return names.size() != previousSize;
        }

        bool hasSelectedPreset(const std::vector<bool>& selectedPresets)
        {
            return std::any_of(selectedPresets.begin(), selectedPresets.end(), [](bool selected)
                {
                    return selected;
                });
        }
    }

    SettingsCatalog SettingsService::discover(std::vector<Preset>& presets) const
    {
        SettingsCatalog catalog;
        ProcessEngine processEngine;
        ServiceEngine serviceEngine;
        const std::string self = currentExecutableName();
        const std::vector<std::string> running = processEngine.listRunningProcessNames();
        const std::vector<std::string> background = processEngine.listRunningBackgroundApplicationNames();

        for (const auto& preset : presets)
        {
            for (const auto& executable : preset.processKillList)
            {
                if (!isBackground(presets, executable) && !containsName(background, executable))
                {
                    addApplicationEntry(catalog.applications, executable,
                        configuredPath(presets, executable), processEngine);
                }
            }
        }

        for (const auto& executable : running)
        {
            if (!sameName(executable, self) && !isBackground(presets, executable) &&
                !containsName(background, executable))
            {
                addApplicationEntry(catalog.applications, executable, {}, processEngine);
            }
        }

        for (const auto& preset : presets)
        {
            for (const auto& application : preset.applications)
            {
                if (application.isBackground)
                {
                    std::vector<SettingsApplication>& entries = containsName(running, application.executableName)
                        ? catalog.applications
                        : catalog.backgroundApplications;
                    addApplicationEntry(entries,
                        application.executableName, application.executablePath, processEngine);
                }
            }
        }

        for (const auto& executable : background)
        {
            if (!sameName(executable, self) && !isBackground(presets, executable) &&
                !containsName(running, executable) &&
                !std::any_of(catalog.applications.begin(), catalog.applications.end(), [&](const auto& entry)
                    {
                        return sameName(entry.executableName, executable);
                    }))
            {
                addApplicationEntry(catalog.backgroundApplications, executable, {}, processEngine);
            }
        }

        for (const auto& preset : presets)
        {
            for (const auto& service : preset.serviceStopList)
            {
                const bool exists = std::any_of(catalog.services.begin(), catalog.services.end(), [&](const auto& entry)
                    {
                        return sameName(entry.serviceName, service);
                    });
                if (!exists)
                {
                    catalog.services.push_back({ service, serviceEngine.isProtectedService(service) });
                }
            }
        }

        return catalog;
    }

    SettingsEditResult SettingsService::setApplicationEnabled(
        std::vector<Preset>& presets,
        size_t presetIndex,
        const std::string& executableName,
        bool enabled,
        bool isBackgroundApplication,
        const std::string& displayName,
        const std::string& executablePath) const
    {
        if (presetIndex >= presets.size() || executableName.empty())
        {
            return SettingsEditResult::Invalid;
        }
        if (isProtectedApplication(executableName))
        {
            return SettingsEditResult::Protected;
        }

        Preset& preset = presets[presetIndex];
        const bool wasEnabled = containsName(preset.processKillList, executableName);
        bool changed = false;
        if (enabled && !wasEnabled)
        {
            preset.processKillList.push_back(executableName);
            changed = true;
        }
        else if (!enabled && wasEnabled)
        {
            changed = eraseName(preset.processKillList, executableName);
        }

        if (enabled && isBackgroundApplication)
        {
            auto application = std::find_if(preset.applications.begin(), preset.applications.end(),
                [&](const ApplicationConfig& value)
                {
                    return sameName(value.executableName, executableName);
                });
            if (application == preset.applications.end())
            {
                preset.applications.push_back({ displayName, executableName, executablePath, true });
                changed = true;
            }
            else if (!application->isBackground)
            {
                application->isBackground = true;
                changed = true;
            }
        }

        return changed ? SettingsEditResult::Changed : SettingsEditResult::Unchanged;
    }

    SettingsEditResult SettingsService::addApplication(
        std::vector<Preset>& presets,
        const std::vector<bool>& selectedPresets,
        const std::string& displayName,
        const std::string& executableName) const
    {
        if (displayName.empty() || executableName.empty() || selectedPresets.size() < presets.size() ||
            !hasSelectedPreset(selectedPresets))
        {
            return SettingsEditResult::Invalid;
        }
        if (isProtectedApplication(executableName))
        {
            return SettingsEditResult::Protected;
        }
        const bool alreadyConfigured = std::any_of(presets.begin(), presets.end(), [&](const Preset& preset)
            {
                return containsName(preset.processKillList, executableName) ||
                    std::any_of(preset.applications.begin(), preset.applications.end(), [&](const ApplicationConfig& application)
                        {
                            return sameName(application.executableName, executableName);
                        });
            });
        if (alreadyConfigured)
        {
            return SettingsEditResult::Duplicate;
        }
        ProcessEngine processEngine;
        const std::string path = processEngine.resolveExecutablePath(executableName);
        for (size_t i = 0; i < presets.size(); ++i)
        {
            if (!selectedPresets[i])
            {
                continue;
            }
            if (!containsName(presets[i].processKillList, executableName))
            {
                presets[i].processKillList.push_back(executableName);
            }
            auto application = std::find_if(presets[i].applications.begin(), presets[i].applications.end(),
                [&](const ApplicationConfig& value)
                {
                    return sameName(value.executableName, executableName);
                });
            if (application == presets[i].applications.end())
            {
                presets[i].applications.push_back({ displayName, executableName, path, false });
            }
            else
            {
                application->displayName = displayName;
                if (application->executablePath.empty())
                {
                    application->executablePath = path;
                }
            }
        }
        return SettingsEditResult::Changed;
    }

    bool SettingsService::renameApplication(
        std::vector<Preset>& presets,
        const std::string& executableName,
        const std::string& displayName) const
    {
        if (executableName.empty() || displayName.empty() || isProtectedApplication(executableName))
        {
            return false;
        }

        bool found = false;
        for (auto& preset : presets)
        {
            for (auto& application : preset.applications)
            {
                if (sameName(application.executableName, executableName))
                {
                    application.displayName = displayName;
                    found = true;
                }
            }
        }
        if (!found && !presets.empty())
        {
            ProcessEngine processEngine;
            presets.front().applications.push_back({
                displayName,
                executableName,
                processEngine.resolveExecutablePath(executableName),
                false
            });
        }
        return true;
    }

    SettingsEditResult SettingsService::removeApplication(
        std::vector<Preset>& presets,
        const std::string& executableName) const
    {
        if (executableName.empty())
        {
            return SettingsEditResult::Invalid;
        }
        if (isProtectedApplication(executableName))
        {
            return SettingsEditResult::Protected;
        }

        bool changed = false;
        for (auto& preset : presets)
        {
            changed = eraseName(preset.processKillList, executableName) || changed;
            const size_t previousSize = preset.applications.size();
            preset.applications.erase(std::remove_if(
                preset.applications.begin(), preset.applications.end(), [&](const ApplicationConfig& application)
                {
                    return sameName(application.executableName, executableName);
                }), preset.applications.end());
            changed = preset.applications.size() != previousSize || changed;
        }
        return changed ? SettingsEditResult::Changed : SettingsEditResult::Unchanged;
    }

    SettingsEditResult SettingsService::setServiceEnabled(
        std::vector<Preset>& presets,
        size_t presetIndex,
        const std::string& serviceName,
        bool enabled) const
    {
        if (presetIndex >= presets.size() || serviceName.empty())
        {
            return SettingsEditResult::Invalid;
        }
        if (isProtectedService(serviceName))
        {
            return SettingsEditResult::Protected;
        }

        auto& services = presets[presetIndex].serviceStopList;
        const bool wasEnabled = containsName(services, serviceName);
        if (enabled && !wasEnabled)
        {
            services.push_back(serviceName);
            return SettingsEditResult::Changed;
        }
        if (!enabled && wasEnabled)
        {
            eraseName(services, serviceName);
            return SettingsEditResult::Changed;
        }
        return SettingsEditResult::Unchanged;
    }

    SettingsEditResult SettingsService::addService(
        std::vector<Preset>& presets,
        const std::vector<bool>& selectedPresets,
        const std::string& serviceName) const
    {
        if (serviceName.empty() || selectedPresets.size() < presets.size() || !hasSelectedPreset(selectedPresets))
        {
            return SettingsEditResult::Invalid;
        }
        if (isProtectedService(serviceName))
        {
            return SettingsEditResult::Protected;
        }
        if (std::any_of(presets.begin(), presets.end(), [&](const Preset& preset)
            {
                return containsName(preset.serviceStopList, serviceName);
            }))
        {
            return SettingsEditResult::Duplicate;
        }

        for (size_t i = 0; i < presets.size(); ++i)
        {
            if (selectedPresets[i] && !containsName(presets[i].serviceStopList, serviceName))
            {
                presets[i].serviceStopList.push_back(serviceName);
            }
        }
        return SettingsEditResult::Changed;
    }

    SettingsEditResult SettingsService::removeService(
        std::vector<Preset>& presets,
        const std::string& serviceName) const
    {
        if (serviceName.empty())
        {
            return SettingsEditResult::Invalid;
        }
        if (isProtectedService(serviceName))
        {
            return SettingsEditResult::Protected;
        }

        bool changed = false;
        for (auto& preset : presets)
        {
            changed = eraseName(preset.serviceStopList, serviceName) || changed;
        }
        return changed ? SettingsEditResult::Changed : SettingsEditResult::Unchanged;
    }

    bool SettingsService::isProtectedApplication(const std::string& executableName) const
    {
        ProcessEngine processEngine;
        return processEngine.isProtectedProcess(executableName);
    }

    bool SettingsService::isProtectedService(const std::string& serviceName) const
    {
        ServiceEngine serviceEngine;
        return serviceEngine.isProtectedService(serviceName);
    }
}
