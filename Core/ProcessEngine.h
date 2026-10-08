#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <functional>
#include <windows.h>
#include "CancellationToken.h"

namespace WannaClean::Core
{
    enum class ProcessCloseOutcome
    {
        ClosedGracefully,
        ForcedTermination,
        StillRunning,
        ForceTerminationFailed,
        ErrorOpeningProcess,
        EnumerationFailed,
        Protected,
        Ignored,
        AlreadyExited
    };

    struct ProcessCloseResult
    {
        std::string processName;
        ProcessCloseOutcome outcome;
        uint64_t estimatedWorkingSetBytes = 0;
    };

    struct ProcessMemoryInfo
    {
        std::string processName;
        DWORD processId;
        uint64_t workingSetBytes;
        bool protectedProcess = false;
    };

    class ProcessEngine
    {
    public:
        std::vector<ProcessCloseResult> closeAll(
            const std::vector<std::string>& killList,
            bool allowForceTermination,
            CancellationToken* cancellation = nullptr,
            const std::function<void()>& onTargetProcessed = {}
        );

        std::vector<std::string> listRunningProcessNames() const;
        std::vector<std::string> listRunningBackgroundApplicationNames() const;

        bool isProtectedProcess(const std::string& processName) const;

        std::string resolveExecutablePath(const std::string& executableName) const;

        std::vector<ProcessMemoryInfo> inspectMemoryUsage(const std::vector<std::string>& targetList) const;

    private:
        bool isProtected(const std::string& processName) const;
        bool isInKillList(const std::string& processName, const std::vector<std::string>& killList) const;
    };
}
