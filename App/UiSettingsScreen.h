#pragma once

#include <cstddef>
#include <string>
#include <vector>
#include "Preset.h"
#include "SettingsCatalog.h"

struct ID3D11Device;

namespace WannaClean::App
{
    enum class SettingsActionType
    {
        None,
        SetApplicationEnabled,
        AddApplication,
        RenameApplication,
        RemoveApplication,
        SetServiceEnabled,
        AddService,
        RemoveService,
        RestoreDefaults
    };

    struct SettingsAction
    {
        SettingsActionType type = SettingsActionType::None;
        std::size_t presetIndex = 0;
        std::string name;
        std::string displayName;
        std::string executablePath;
        bool enabled = false;
        bool isBackground = false;
        std::vector<bool> selectedPresets;
    };

    struct SettingsScreenState
    {
        char applicationFilter[128]{};
        char serviceFilter[128]{};
        char newApplicationName[128]{};
        char newApplicationExecutable[128]{};
        char editApplicationName[128]{};
        bool newApplicationPresets[3]{};
        bool showApplicationAddForm = false;
        char newServiceName[128]{};
        bool newServicePresets[3]{};
        bool showServiceAddForm = false;
        bool showApplicationSearchHelp = false;
        bool showApplicationAddHelp = false;
        bool showGeneralHelp = false;
        bool showServiceHelp = false;
        bool showServiceAddHelp = false;
        bool showResetConfirmation = false;
        bool showDiscardConfirmation = false;
        bool showApplicationRemoveConfirmation = false;
        bool showServiceRemoveConfirmation = false;
        bool showProtectedApplicationNotice = false;
        bool showProtectedServiceNotice = false;
        int savedMessageFrames = 0;
        std::string applicationBeingEdited;
        bool editingApplication = false;
        bool focusApplicationEditor = false;
        bool showServices = false;
        bool refreshRequested = false;
        std::vector<std::string> runningBackgroundApplications;
        std::vector<std::string> orderedApplications;
        std::vector<std::string> orderedServices;
        std::vector<std::string> hiddenApplications;
        std::string applicationRemoveCandidate;
        std::string serviceRemoveCandidate;
        WannaClean::Core::SettingsCatalog catalog;
        std::vector<SettingsAction> pendingActions;
    };

    void RenderSettingsScreen(
        SettingsScreenState& state,
        const std::vector<WannaClean::Core::Preset>& presets,
        ID3D11Device* device,
        bool resetSession,
        bool& closeRequested,
        bool& saveRequested,
        bool dirty);

    void ReleaseSettingsIconCache();
}
