#pragma once
#include <d3d11.h>
#include <vector>
#include "Preset.h"

namespace WannaClean::App
{
    struct IdleScreenState
    {
        bool showHelp = false;
    };
}

namespace WannaClean::App
{
    int RenderIdleScreen(
        const std::vector<WannaClean::Core::Preset>& presets,
        ID3D11ShaderResourceView* logoTexture,
        IdleScreenState& state,
        size_t pendingServicesCount,
        bool& settingsRequested,
        bool& revertPendingServicesRequested);
}
