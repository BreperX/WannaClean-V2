#include "ProcessEngine.h"
#include "Protected.h"
#include "StringUtil.h"
#include <algorithm>
#include <chrono>
#include <psapi.h>
#include <thread>
#include <tlhelp32.h>
#include <windows.h>

namespace WannaClean::Core
{
    bool ProcessEngine::isProtectedProcess(const std::string& processName) const
    {
        return isProtected(processName);
    }

    std::string ProcessEngine::resolveExecutablePath(const std::string& executableName) const
    {
        const std::wstring wideExecutableName = Utf8ToWide(executableName);
        if (wideExecutableName.empty()) return {};
        wchar_t path[MAX_PATH]{};
        DWORD length = SearchPathW(
            nullptr,
            wideExecutableName.c_str(),
            nullptr,
            MAX_PATH,
            path,
            nullptr);

        if (length > 0 && length < MAX_PATH)
        {
            return WideToUtf8(path);
        }

        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE)
        {
            return {};
        }

        PROCESSENTRY32 entry{};
        entry.dwSize = sizeof(entry);
        std::string result;

        if (Process32First(snapshot, &entry))
        {
            do
            {
                std::string runningName = WideToUtf8(entry.szExeFile);
                if (!Utf8EqualsIgnoreCase(runningName, executableName))
                {
                    continue;
                }

                HANDLE process = OpenProcess(
                    PROCESS_QUERY_LIMITED_INFORMATION,
                    FALSE,
                    entry.th32ProcessID);
                if (process == nullptr)
                {
                    continue;
                }

                wchar_t processPath[MAX_PATH]{};
                DWORD processPathLength = MAX_PATH;
                if (QueryFullProcessImageNameW(
                        process,
                        0,
                        processPath,
                        &processPathLength) != FALSE)
                {
                    result = WideToUtf8(processPath);
                    CloseHandle(process);
                    break;
                }

                CloseHandle(process);
            }
            while (Process32Next(snapshot, &entry));
        }

        CloseHandle(snapshot);
        return result;
    }

    bool ProcessEngine::isProtected(const std::string& processName) const
    {
        return std::any_of(ProtectedProcesses.begin(), ProtectedProcesses.end(),
            [&](std::string_view p)
            {
                return Utf8EqualsIgnoreCase(std::string(p), processName);
            });
    }

    bool ProcessEngine::isInKillList(
        const std::string& processName,
        const std::vector<std::string>& killList) const
    {
        return std::any_of(killList.begin(), killList.end(),
            [&](const std::string& k)
            {
                return Utf8EqualsIgnoreCase(k, processName);
            });
    }

    namespace
    {
        struct ProcessTarget
        {
            std::string processName;
            DWORD processId = 0;

            // Memoria estimada antes del cierre.
            uint64_t estimatedWorkingSetBefore = 0;
            FILETIME creationTime{};
        };

        enum class ProcessState
        {
            Running,
            Exited,
            AccessDenied,
            QueryFailed
        };

        struct CloseWindowsContext
        {
            DWORD processId;
        };

        BOOL CALLBACK CloseWindowsForProcess(HWND hwnd, LPARAM lParam)
        {
            auto* context = reinterpret_cast<CloseWindowsContext*>(lParam);

            DWORD windowProcessId = 0;
            GetWindowThreadProcessId(hwnd, &windowProcessId);

            if (windowProcessId == context->processId)
            {
                PostMessage(hwnd, WM_CLOSE, 0, 0);
            }

            return TRUE;
        }

        bool getProcessMemoryUsage(HANDLE process, uint64_t& memoryBytes)
        {
            PROCESS_MEMORY_COUNTERS_EX counters{};

            if (!GetProcessMemoryInfo(
                process,
                reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                sizeof(counters)))
            {
                return false;
            }

            memoryBytes = static_cast<uint64_t>(counters.WorkingSetSize);
            return true;
        }

        bool getProcessCreationTime(HANDLE process, FILETIME& creationTime)
        {
            FILETIME exitTime{}, kernelTime{}, userTime{};
            return GetProcessTimes(process, &creationTime, &exitTime, &kernelTime, &userTime) != FALSE;
        }

        bool isCriticalProcess(HANDLE process)
        {
            using IsProcessCriticalFunction = BOOL(WINAPI*)(HANDLE, PBOOL);
            static const auto function = reinterpret_cast<IsProcessCriticalFunction>(
                GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "IsProcessCritical"));
            BOOL critical = FALSE;
            return function != nullptr && function(process, &critical) != FALSE && critical != FALSE;
        }

        bool isSameProcessInstance(HANDLE first, HANDLE second)
        {
            FILETIME firstCreation{}, secondCreation{};
            return getProcessCreationTime(first, firstCreation) &&
                getProcessCreationTime(second, secondCreation) &&
                CompareFileTime(&firstCreation, &secondCreation) == 0;
        }

        ProcessState getProcessState(DWORD processId)
        {
            SetLastError(ERROR_SUCCESS);

            HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, processId);
            if (process == nullptr)
            {
                DWORD error = GetLastError();

                if (error == ERROR_INVALID_PARAMETER)
                {
                    return ProcessState::Exited;
                }

                if (error == ERROR_ACCESS_DENIED)
                {
                    return ProcessState::AccessDenied;
                }

                return ProcessState::QueryFailed;
            }

            DWORD waitResult = WaitForSingleObject(process, 0);
            DWORD waitError = waitResult == WAIT_FAILED ? GetLastError() : ERROR_SUCCESS;

            CloseHandle(process);

            if (waitResult == WAIT_TIMEOUT)
            {
                return ProcessState::Running;
            }

            if (waitResult == WAIT_OBJECT_0)
            {
                return ProcessState::Exited;
            }

            if (waitError == ERROR_ACCESS_DENIED)
            {
                return ProcessState::AccessDenied;
            }

            return ProcessState::QueryFailed;
        }

        bool waitForProcessExit(HANDLE process, DWORD timeoutMilliseconds)
        {
            return WaitForSingleObject(process, timeoutMilliseconds) == WAIT_OBJECT_0;
        }
    }

    std::vector<ProcessCloseResult> ProcessEngine::closeAll(
        const std::vector<std::string>& killList,
        bool allowForceTermination,
        CancellationToken* cancellation,
        const std::function<void()>& onTargetProcessed)
    {
        std::vector<ProcessCloseResult> results;
        std::vector<ProcessTarget> targets;

        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE)
        {
            results.push_back({
                "Enumeración de procesos",
                ProcessCloseOutcome::EnumerationFailed,
                0
            });
            return results;
        }

        PROCESSENTRY32 entry{};
        entry.dwSize = sizeof(PROCESSENTRY32);

        // Captura los objetivos antes de cerrarlos.
        if (Process32First(snapshot, &entry))
        {
            do
            {
                if (cancellation != nullptr && cancellation->isCancellationRequested())
                {
                    break;
                }

                std::string processName = WideToUtf8(entry.szExeFile);
                DWORD processId = entry.th32ProcessID;

                if (isProtected(processName))
                {
                    if (isInKillList(processName, killList))
                    {
                        results.push_back({ processName, ProcessCloseOutcome::Protected, 0 });
                    }
                    continue;
                }

                if (!isInKillList(processName, killList))
                {
                    continue;
                }
                HANDLE process = OpenProcess(
                    PROCESS_QUERY_INFORMATION |
                    PROCESS_VM_READ |
                    SYNCHRONIZE,
                    FALSE,
                    processId);

                if (process == nullptr)
                {
                    const ProcessCloseOutcome outcome = getProcessState(processId) == ProcessState::Exited
                        ? ProcessCloseOutcome::AlreadyExited
                        : ProcessCloseOutcome::ErrorOpeningProcess;
                    results.push_back({
                        processName,
                        outcome,
                        0
                        });

                    continue;
                }

                if (isCriticalProcess(process))
                {
                    CloseHandle(process);
                    results.push_back({ processName, ProcessCloseOutcome::Protected, 0 });
                    continue;
                }

                uint64_t estimatedWorkingSetBefore = 0;
                bool memoryRead = getProcessMemoryUsage(
                    process,
                    estimatedWorkingSetBefore);
                FILETIME creationTime{};
                const bool creationTimeRead = getProcessCreationTime(process, creationTime);

                CloseHandle(process);

                if (!memoryRead || !creationTimeRead)
                {
                    results.push_back({
                        processName,
                        ProcessCloseOutcome::ErrorOpeningProcess,
                        0
                        });

                    continue;
                }

                targets.push_back({
                    processName,
                    processId,
                    estimatedWorkingSetBefore,
                    creationTime
                    });

            } while (Process32Next(snapshot, &entry));
        }
        else
        {
            results.push_back({
                "Enumeración de procesos",
                ProcessCloseOutcome::EnumerationFailed,
                0
            });
        }

        CloseHandle(snapshot);

        // Cierra los objetivos capturados.
        for (const auto& target : targets)
        {
            if (cancellation != nullptr && cancellation->isCancellationRequested())
            {
                break;
            }

            struct ProgressStep
            {
                const std::function<void()>& callback;
                ~ProgressStep() { if (callback) callback(); }
            } progressStep{ onTargetProcessed };

            HANDLE process = OpenProcess(
                SYNCHRONIZE |
                PROCESS_QUERY_INFORMATION |
                PROCESS_VM_READ,
                FALSE,
                target.processId);

            if (process == nullptr)
            {
                // El proceso pudo terminar entre ambas fases.
                ProcessState state = getProcessState(target.processId);

                if (state == ProcessState::Exited)
                {
                    results.push_back({
                        target.processName,
                        ProcessCloseOutcome::AlreadyExited,
                        0
                        });
                }
                else
                {
                    results.push_back({
                        target.processName,
                        ProcessCloseOutcome::ErrorOpeningProcess,
                        0
                        });
                }

                continue;
            }

            FILETIME currentCreationTime{};
            if (!getProcessCreationTime(process, currentCreationTime) ||
                CompareFileTime(&target.creationTime, &currentCreationTime) != 0)
            {
                results.push_back({ target.processName, ProcessCloseOutcome::AlreadyExited, 0 });
                CloseHandle(process);
                continue;
            }

            if (isCriticalProcess(process))
            {
                results.push_back({ target.processName, ProcessCloseOutcome::Protected, 0 });
                CloseHandle(process);
                continue;
            }

            CloseWindowsContext context{
                target.processId
            };

            EnumWindows(
                CloseWindowsForProcess,
                reinterpret_cast<LPARAM>(&context));

            constexpr DWORD normalCloseTimeoutMilliseconds = 3000;

            bool closedGracefully = waitForProcessExit(
                process,
                normalCloseTimeoutMilliseconds);

            if (closedGracefully)
            {
                results.push_back({
                    target.processName,
                    ProcessCloseOutcome::ClosedGracefully,
                    target.estimatedWorkingSetBefore
                    });

                CloseHandle(process);
                continue;
            }

            if (!allowForceTermination)
            {
                results.push_back({
                    target.processName,
                    ProcessCloseOutcome::StillRunning,
                    0
                    });

                CloseHandle(process);
                continue;
            }

            // Solo Agresivo confirmado permite forzar el cierre.
            HANDLE processForTermination = OpenProcess(
                PROCESS_TERMINATE | SYNCHRONIZE | PROCESS_QUERY_INFORMATION,
                FALSE,
                target.processId);

            if (processForTermination == nullptr)
            {
                ProcessState state = getProcessState(target.processId);

                if (state == ProcessState::Exited)
                {
                    results.push_back({
                        target.processName,
                        ProcessCloseOutcome::AlreadyExited,
                        0
                        });
                }
                else
                {
                    results.push_back({
                        target.processName,
                        ProcessCloseOutcome::ForceTerminationFailed,
                        0
                        });
                }

                CloseHandle(process);
                continue;
            }

            if (!isSameProcessInstance(process, processForTermination))
            {
                results.push_back({ target.processName, ProcessCloseOutcome::ForceTerminationFailed, 0 });
                CloseHandle(processForTermination);
                CloseHandle(process);
                continue;
            }

            if (isCriticalProcess(processForTermination))
            {
                results.push_back({ target.processName, ProcessCloseOutcome::Protected, 0 });
                CloseHandle(processForTermination);
                CloseHandle(process);
                continue;
            }

            if (TerminateProcess(processForTermination, 1))
            {
                const bool exited = waitForProcessExit(processForTermination, 3000);
                results.push_back({
                    target.processName,
                    exited ? ProcessCloseOutcome::ForcedTermination : ProcessCloseOutcome::ForceTerminationFailed,
                    exited ? target.estimatedWorkingSetBefore : 0
                });
            }
            else
            {
                results.push_back({
                    target.processName,
                    ProcessCloseOutcome::ForceTerminationFailed,
                    0
                    });
            }

            CloseHandle(processForTermination);
            CloseHandle(process);
        }

        return results;
    }

    std::vector<ProcessMemoryInfo> ProcessEngine::inspectMemoryUsage(
        const std::vector<std::string>& targetList) const
    {
        std::vector<ProcessMemoryInfo> result;

        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE)
        {
            return result;
        }

        PROCESSENTRY32 entry{};
        entry.dwSize = sizeof(PROCESSENTRY32);

        if (Process32First(snapshot, &entry))
        {
            do
            {
                std::string processName = WideToUtf8(entry.szExeFile);

                if (!isInKillList(processName, targetList))
                {
                    continue;
                }

                HANDLE process = OpenProcess(
                    PROCESS_QUERY_INFORMATION | PROCESS_VM_READ,
                    FALSE,
                    entry.th32ProcessID);

                if (process == nullptr)
                {
                    continue;
                }

                uint64_t memoryBytes = 0;
                bool memoryRead = getProcessMemoryUsage(
                    process,
                    memoryBytes);

                CloseHandle(process);

                if (!memoryRead)
                {
                    continue;
                }

                result.push_back({
                    processName,
                    entry.th32ProcessID,
                    memoryBytes,
                    isProtected(processName)
                    });

            } while (Process32Next(snapshot, &entry));
        }

        CloseHandle(snapshot);
        return result;
    }

    std::vector<std::string> ProcessEngine::listRunningProcessNames() const
    {
        std::vector<std::string> names;

        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE)
        {
            return names;
        }

        PROCESSENTRY32 entry{};
        entry.dwSize = sizeof(PROCESSENTRY32);

        if (Process32First(snapshot, &entry))
        {
            do
            {
                std::string processName = WideToUtf8(entry.szExeFile);
                DWORD processId = entry.th32ProcessID;

                if (isProtected(processName))
                {
                    continue;
                }

                struct FindWindowContext
                {
                    DWORD processId;
                    bool found;
                };

                FindWindowContext context{
                    processId,
                    false
                };

                EnumWindows(
                    [](HWND hwnd, LPARAM lParam) -> BOOL
                    {
                        auto* context =
                            reinterpret_cast<FindWindowContext*>(lParam);

                        DWORD windowProcessId = 0;
                        GetWindowThreadProcessId(hwnd, &windowProcessId);

                        if (windowProcessId == context->processId &&
                            IsWindowVisible(hwnd))
                        {
                            context->found = true;
                            return FALSE;
                        }

                        return TRUE;
                    },
                    reinterpret_cast<LPARAM>(&context));

                if (context.found &&
                    std::find(names.begin(), names.end(), processName) == names.end())
                {
                    names.push_back(processName);
                }

            } while (Process32Next(snapshot, &entry));
        }

        CloseHandle(snapshot);
        return names;
    }
}
