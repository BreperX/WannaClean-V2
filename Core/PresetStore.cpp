#include "PresetStore.h"
#include "AtomicFile.h"
#include "StringUtil.h"
#include "ProcessEngine.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <shlobj.h>
#include <algorithm>
#include <windows.h>

using json = nlohmann::json;

namespace WannaClean::Core
{
    std::filesystem::path PresetStore::configFilePath() const
    {
        PWSTR localAppData = nullptr;
        HRESULT result = SHGetKnownFolderPath(
            FOLDERID_LocalAppData,
            KF_FLAG_DEFAULT,
            nullptr,
            &localAppData);

        if (FAILED(result) || localAppData == nullptr)
        {
            return {};
        }

        std::filesystem::path path(localAppData);
        CoTaskMemFree(localAppData);
        path /= "WannaClean";
        path /= "config.json";
        return path;
    }

    namespace
    {
        // Convierte un MemoryCleanLevel a/desde string, para que el JSON sea legible.
        std::string cleanLevelToString(MemoryCleanLevel level)
        {
            return level == MemoryCleanLevel::Deep ? "Deep" : "Light";
        }

        MemoryCleanLevel cleanLevelFromString(const std::string& str)
        {
            return str == "Deep" ? MemoryCleanLevel::Deep : MemoryCleanLevel::Light;
        }

        ApplicationConfig applicationFromExecutable(const std::string& executableName)
        {
            ApplicationConfig application;
            application.executableName = executableName;

            ProcessEngine processEngine;
            application.executablePath = processEngine.resolveExecutablePath(executableName);

            if (Utf8EqualsIgnoreCase(executableName, "chrome.exe")) application.displayName = "Google Chrome";
            else if (Utf8EqualsIgnoreCase(executableName, "msedge.exe")) application.displayName = "Microsoft Edge";
            else if (Utf8EqualsIgnoreCase(executableName, "firefox.exe")) application.displayName = "Mozilla Firefox";
            else if (Utf8EqualsIgnoreCase(executableName, "winword.exe")) application.displayName = "Microsoft Word";
            else if (Utf8EqualsIgnoreCase(executableName, "excel.exe")) application.displayName = "Microsoft Excel";
            else if (Utf8EqualsIgnoreCase(executableName, "powerpnt.exe")) application.displayName = "Microsoft PowerPoint";
            else if (Utf8EqualsIgnoreCase(executableName, "outlook.exe")) application.displayName = "Microsoft Outlook";
            else if (Utf8EqualsIgnoreCase(executableName, "onedrive.exe")) application.displayName = "Microsoft OneDrive";
            else if (Utf8EqualsIgnoreCase(executableName, "dropbox.exe")) application.displayName = "Dropbox";
            else if (Utf8EqualsIgnoreCase(executableName, "googledrivesync.exe")) application.displayName = "Google Drive";
            else if (Utf8EqualsIgnoreCase(executableName, "teams.exe")) application.displayName = "Microsoft Teams";
            else if (Utf8EqualsIgnoreCase(executableName, "slack.exe")) application.displayName = "Slack";
            else if (Utf8EqualsIgnoreCase(executableName, "discord.exe")) application.displayName = "Discord";
            else if (Utf8EqualsIgnoreCase(executableName, "steam.exe")) application.displayName = "Steam";
            else if (Utf8EqualsIgnoreCase(executableName, "epicgameslauncher.exe")) application.displayName = "Epic Games Launcher";
            else if (Utf8EqualsIgnoreCase(executableName, "battle.net.exe")) application.displayName = "Battle.net";
            else if (Utf8EqualsIgnoreCase(executableName, "geforcenow.exe")) application.displayName = "GeForce NOW";
            else if (Utf8EqualsIgnoreCase(executableName, "overwolf.exe")) application.displayName = "Overwolf";
            else if (Utf8EqualsIgnoreCase(executableName, "galaxyclient.exe")) application.displayName = "GOG Galaxy";
            else if (Utf8EqualsIgnoreCase(executableName, "upc.exe")) application.displayName = "Ubisoft Connect";
            else if (Utf8EqualsIgnoreCase(executableName, "riotclientservices.exe")) application.displayName = "Riot Client";
            else if (Utf8EqualsIgnoreCase(executableName, "nvidia geforce experience.exe")) application.displayName = "NVIDIA GeForce Experience";
            else if (Utf8EqualsIgnoreCase(executableName, "msiafterburner.exe")) application.displayName = "MSI Afterburner";
            else if (Utf8EqualsIgnoreCase(executableName, "rtss.exe")) application.displayName = "RivaTuner Statistics Server";
            else application.displayName = executableName;

            return application;
        }

        void ensureApplicationMetadata(Preset& preset)
        {
            for (const auto& application : preset.applications)
            {
                bool inKillList = std::any_of(
                    preset.processKillList.begin(),
                    preset.processKillList.end(),
                    [&](const std::string& executable)
                    {
                        return Utf8EqualsIgnoreCase(executable, application.executableName);
                    });

                if (!inKillList && !application.isBackground && !application.executableName.empty())
                {
                    preset.processKillList.push_back(application.executableName);
                }
            }

            for (const auto& executable : preset.processKillList)
            {
                bool alreadyPresent = std::any_of(
                    preset.applications.begin(),
                    preset.applications.end(),
                    [&](const ApplicationConfig& application)
                    {
                        return Utf8EqualsIgnoreCase(application.executableName, executable);
                    });

                if (!alreadyPresent)
                {
                    preset.applications.push_back(applicationFromExecutable(executable));
                }
            }
        }

        bool preserveInvalidConfig(const std::filesystem::path& path)
        {
            std::filesystem::path backup = path;
            backup += ".corrupt";
            std::error_code error;
            unsigned int suffix = 1;
            while (std::filesystem::exists(backup, error))
            {
                error.clear();
                backup = path;
                backup += ".corrupt." + std::to_string(suffix++);
            }

            error.clear();
            std::filesystem::copy_file(path, backup, std::filesystem::copy_options::none, error);
            return !error;
        }

        json applicationToJson(const ApplicationConfig& application)
        {
            return {
                { "displayName", application.displayName },
                { "executableName", application.executableName },
                { "executablePath", application.executablePath },
                { "isBackground", application.isBackground }
            };
        }

        json presetToJson(const Preset& preset)
        {
            json j;
            j["name"] = preset.name;
            j["processKillList"] = preset.processKillList;
            j["serviceStopList"] = preset.serviceStopList;
            j["applications"] = json::array();
            for (const auto& application : preset.applications)
            {
                j["applications"].push_back(applicationToJson(application));
            }
            j["cleanLevel"] = cleanLevelToString(preset.cleanLevel);
            j["requiresConfirmation"] = preset.requiresConfirmation;
            j["allowsForceTermination"] = preset.allowsForceTermination;
            return j;
        }

        Preset presetFromJson(const json& j)
        {
            Preset preset;
            preset.name = j.value("name", "");
            preset.processKillList = j.value("processKillList", std::vector<std::string>{});
            preset.serviceStopList = j.value("serviceStopList", std::vector<std::string>{});
            if (j.contains("applications") && j["applications"].is_array())
            {
                for (const auto& item : j["applications"])
                {
                    ApplicationConfig application;
                    application.displayName = item.value("displayName", "");
                    application.executableName = item.value("executableName", "");
                    application.executablePath = item.value("executablePath", "");
                    application.isBackground = item.value("isBackground", false);
                    if (!application.executableName.empty())
                    {
                        preset.applications.push_back(application);
                    }
                }
            }

            ensureApplicationMetadata(preset);
            preset.cleanLevel = cleanLevelFromString(j.value("cleanLevel", "Light"));
            preset.requiresConfirmation = j.value("requiresConfirmation", false);
            preset.allowsForceTermination = j.value("allowsForceTermination", false);
            return preset;
        }
    }

    std::vector<Preset> PresetStore::buildDefaults() const
    {
        Preset jugar;
        jugar.name = "Jugar";
        jugar.processKillList = {
            "chrome.exe", "msedge.exe", "firefox.exe",
            "winword.exe", "excel.exe", "powerpnt.exe", "outlook.exe",
            "onedrive.exe", "dropbox.exe", "googledrivesync.exe",
            "teams.exe", "slack.exe"
        };
        jugar.cleanLevel = MemoryCleanLevel::Light;
        jugar.requiresConfirmation = false;
        jugar.allowsForceTermination = false;

        Preset trabajar;
        trabajar.name = "Trabajar";
        trabajar.processKillList = {
            "discord.exe", "steam.exe", "epicgameslauncher.exe",
            "battle.net.exe", "geforcenow.exe", "overwolf.exe",
            "galaxyclient.exe", "upc.exe", "riotclientservices.exe",
            "nvidia geforce experience.exe", "msiafterburner.exe", "rtss.exe"
        };
        trabajar.cleanLevel = MemoryCleanLevel::Light;
        trabajar.requiresConfirmation = false;
        trabajar.allowsForceTermination = false;

        Preset agresivo;
        agresivo.name = "Agresivo";
        agresivo.processKillList = {
            "chrome.exe", "msedge.exe", "firefox.exe",
            "winword.exe", "excel.exe", "powerpnt.exe", "outlook.exe",
            "onedrive.exe", "dropbox.exe", "googledrivesync.exe",
            "teams.exe", "slack.exe",
            "discord.exe", "steam.exe", "epicgameslauncher.exe",
            "battle.net.exe", "geforcenow.exe", "overwolf.exe",
            "galaxyclient.exe", "upc.exe", "riotclientservices.exe",
            "nvidia geforce experience.exe", "msiafterburner.exe", "rtss.exe"
        };
        agresivo.serviceStopList = {
            "Fax", "WSearch", "SysMain", "MapsBroker", "lfsvc",
            "TrkWks", "WerSvc", "XblAuthManager", "XblGameSave",
            "XboxNetApiSvc", "DiagTrack"
        };
        agresivo.cleanLevel = MemoryCleanLevel::Deep;
        agresivo.requiresConfirmation = true;
        agresivo.allowsForceTermination = true;

        std::vector<Preset> defaults = { jugar, trabajar, agresivo };
        for (auto& preset : defaults)
        {
            ensureApplicationMetadata(preset);
        }

        return defaults;
    }

    std::vector<Preset> PresetStore::defaults() const
    {
        return buildDefaults();
    }

    std::vector<Preset> PresetStore::load()
    {
        const std::filesystem::path path = configFilePath();

        if (path.empty())
        {
            return buildDefaults();
        }

        std::ifstream file(path);
        if (!file.is_open())
        {
            // No existe config previa: construir y persistir los defaults.
            auto defaults = buildDefaults();
            save(defaults);
            return defaults;
        }

        try
        {
            json j;
            file >> j;

            if (!j.is_array())
            {
                file.close();
                auto defaults = buildDefaults();
                if (preserveInvalidConfig(path)) save(defaults);
                return defaults;
            }

            std::vector<Preset> presets;
            for (const auto& item : j)
            {
                presets.push_back(presetFromJson(item));
            }

            if (presets.empty())
            {
                file.close();
                auto defaults = buildDefaults();
                if (preserveInvalidConfig(path)) save(defaults);
                return defaults;
            }

            return presets;
        }
        catch (const json::exception&)
        {
            file.close();
            auto defaults = buildDefaults();
            if (preserveInvalidConfig(path)) save(defaults);
            return defaults;
        }
    }

    void PresetStore::save(const std::vector<Preset>& presets)
    {
        const std::filesystem::path path = configFilePath();

        if (path.empty())
        {
            return;
        }

        try
        {
            const std::filesystem::path& fsPath = path;
            std::filesystem::create_directories(fsPath.parent_path());

            json j = json::array();
            for (const auto& preset : presets)
            {
                j.push_back(presetToJson(preset));
            }

            std::filesystem::path temporaryPath = fsPath;
            temporaryPath += ".tmp";

            std::ofstream file(temporaryPath, std::ios::trunc);
            if (!file.is_open())
            {
                return;
            }

            file << j.dump(4);
            file.close();

            if (!file)
            {
                std::error_code error;
                std::filesystem::remove(temporaryPath, error);
                return;
            }

            if (!replaceFile(temporaryPath, fsPath))
            {
                std::error_code error;
                std::filesystem::remove(temporaryPath, error);
            }
        }
        catch (...)
        {
        }
    }
}
