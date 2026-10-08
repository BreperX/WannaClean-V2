#include "ProcessEngine.h"
#include "StringUtil.h"
#include <algorithm>
#include <tlhelp32.h>
#include <unordered_set>
#include <utility>

namespace WannaClean::Core
{
    std::vector<std::string> ProcessEngine::listRunningBackgroundApplicationNames() const
    {
        std::unordered_set<DWORD> visibleProcessIds;
        EnumWindows(
            [](HWND window, LPARAM parameter) -> BOOL
            {
                if (!IsWindowVisible(window))
                {
                    return TRUE;
                }

                DWORD processId = 0;
                GetWindowThreadProcessId(window, &processId);
                if (processId != 0)
                {
                    auto* ids = reinterpret_cast<std::unordered_set<DWORD>*>(parameter);
                    ids->insert(processId);
                }
                return TRUE;
            },
            reinterpret_cast<LPARAM>(&visibleProcessIds));

        wchar_t windowsDirectory[MAX_PATH]{};
        const UINT windowsDirectoryLength = GetWindowsDirectoryW(windowsDirectory, MAX_PATH);
        if (windowsDirectoryLength == 0 || windowsDirectoryLength >= MAX_PATH)
        {
            return {};
        }

        DWORD currentSessionId = 0;
        if (!ProcessIdToSessionId(GetCurrentProcessId(), &currentSessionId))
        {
            return {};
        }

        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE)
        {
            return {};
        }

        std::vector<std::string> names;
        PROCESSENTRY32 entry{};
        entry.dwSize = sizeof(entry);

        std::vector<std::string> visibleProcessNames;
        if (Process32First(snapshot, &entry))
        {
            do
            {
                if (visibleProcessIds.find(entry.th32ProcessID) != visibleProcessIds.end())
                {
                    const std::string name = WideToUtf8(entry.szExeFile);
                    if (std::none_of(visibleProcessNames.begin(), visibleProcessNames.end(), [&](const std::string& existing)
                        {
                            return Utf8EqualsIgnoreCase(existing, name);
                        }))
                    {
                        visibleProcessNames.push_back(name);
                    }
                }
            }
            while (Process32Next(snapshot, &entry));

            entry.dwSize = sizeof(entry);
            if (Process32First(snapshot, &entry))
            {
                do
                {
                    const DWORD processId = entry.th32ProcessID;
                    const std::string processName = WideToUtf8(entry.szExeFile);
                    const bool executableHasVisibleWindow = std::any_of(
                        visibleProcessNames.begin(),
                        visibleProcessNames.end(),
                        [&](const std::string& visibleName)
                        {
                            return Utf8EqualsIgnoreCase(visibleName, processName);
                        });
                    if (processId == 0 || isProtected(processName) || executableHasVisibleWindow)
                    {
                        continue;
                    }

                    DWORD sessionId = 0;
                    if (!ProcessIdToSessionId(processId, &sessionId) || sessionId != currentSessionId)
                    {
                        continue;
                    }

                    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
                    if (process == nullptr)
                    {
                        continue;
                    }

                    wchar_t processPath[MAX_PATH]{};
                    DWORD pathLength = MAX_PATH;
                    const bool pathResolved = QueryFullProcessImageNameW(
                        process,
                        0,
                        processPath,
                        &pathLength) != FALSE;
                    CloseHandle(process);

                    const bool isWindowsPath = pathResolved && pathLength >= windowsDirectoryLength &&
                        CompareStringOrdinal(
                            processPath,
                            static_cast<int>(windowsDirectoryLength),
                            windowsDirectory,
                            static_cast<int>(windowsDirectoryLength),
                            TRUE) == CSTR_EQUAL &&
                        (processPath[windowsDirectoryLength] == L'\\' ||
                         processPath[windowsDirectoryLength] == L'/' ||
                         processPath[windowsDirectoryLength] == L'\0');
                    if (!pathResolved || isWindowsPath)
                    {
                        continue;
                    }

                    std::string name = processName;
                    if (std::none_of(names.begin(), names.end(), [&](const std::string& existing)
                        {
                            return Utf8EqualsIgnoreCase(existing, name);
                        }))
                    {
                        names.push_back(std::move(name));
                    }
                }
                while (Process32Next(snapshot, &entry));
            }
        }

        CloseHandle(snapshot);
        return names;
    }
}
