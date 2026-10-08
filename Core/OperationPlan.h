#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include "Preset.h"

namespace WannaClean::Core
{
    struct ActiveProcessTarget
    {
        std::string processName;
        uint32_t processId = 0;
        uint64_t estimatedWorkingSet = 0;
        bool protectedProcess = false;
    };

    struct ServiceTarget
    {
        std::string serviceName;
        bool exists = false;
        bool running = false;
        bool protectedService = false;
    };

    struct OperationPlan
    {
        std::string presetName;
        std::vector<std::string> processNames;
        std::vector<std::string> serviceNames;
        std::vector<ActiveProcessTarget> activeProcesses;
        std::vector<ServiceTarget> services;
        MemoryCleanLevel cleanLevel = MemoryCleanLevel::Light;
        bool requiresConfirmation = false;
        bool allowsForceTermination = false;
    };
}
