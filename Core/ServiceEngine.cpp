#include "ServiceEngine.h"
#include "Protected.h"
#include "StringUtil.h"
#include <algorithm>
#include <windows.h>

namespace WannaClean::Core
{
    bool ServiceEngine::isProtectedService(const std::string& serviceName) const
    {
        return isProtected(serviceName);
    }

    bool ServiceEngine::isProtected(const std::string& serviceName) const
    {
        return std::any_of(ProtectedServices.begin(), ProtectedServices.end(),
            [&](std::string_view s) { return Utf8EqualsIgnoreCase(std::string(s), serviceName); });
    }

    bool ServiceEngine::isInStopList(const std::string& serviceName, const std::vector<std::string>& stopList) const
    {
        return std::any_of(stopList.begin(), stopList.end(),
            [&](const std::string& s) { return Utf8EqualsIgnoreCase(s, serviceName); });
    }

    namespace
    {
        // Consulta si el servicio existe y está corriendo, y su tipo de
        // arranque actual (necesario para poder revertir después).
        enum class ServiceQueryResult
        {
            Found,
            Missing,
            Failed
        };

        ServiceQueryResult queryServiceState(
            SC_HANDLE scm,
            const std::string& serviceName,
            bool& outIsRunning)
        {
            const std::wstring wideName = Utf8ToWide(serviceName);
            if (wideName.empty()) return ServiceQueryResult::Failed;
            SC_HANDLE service = OpenServiceW(
                scm,
                wideName.c_str(),
                SERVICE_QUERY_STATUS);
            if (service == nullptr)
            {
                return GetLastError() == ERROR_SERVICE_DOES_NOT_EXIST
                    ? ServiceQueryResult::Missing
                    : ServiceQueryResult::Failed;
            }

            SERVICE_STATUS status{};
            if (!QueryServiceStatus(service, &status))
            {
                CloseServiceHandle(service);
                return ServiceQueryResult::Failed;
            }

            outIsRunning = status.dwCurrentState == SERVICE_RUNNING;

            CloseServiceHandle(service);
            return ServiceQueryResult::Found;
        }

        bool waitForServiceStopped(SC_HANDLE service, DWORD timeoutMilliseconds)
        {
            const ULONGLONG deadline = GetTickCount64() + timeoutMilliseconds;

            while (true)
            {
                SERVICE_STATUS_PROCESS status{};
                DWORD bytesNeeded = 0;

                if (!QueryServiceStatusEx(
                        service,
                        SC_STATUS_PROCESS_INFO,
                        reinterpret_cast<LPBYTE>(&status),
                        sizeof(status),
                        &bytesNeeded))
                {
                    return false;
                }

                if (status.dwCurrentState == SERVICE_STOPPED)
                {
                    return true;
                }

                if (status.dwCurrentState != SERVICE_STOP_PENDING)
                {
                    return false;
                }

                if (GetTickCount64() >= deadline)
                {
                    return false;
                }

                DWORD waitMilliseconds = 100;
                if (status.dwWaitHint != 0)
                {
                    waitMilliseconds = status.dwWaitHint / 10;
                    waitMilliseconds = std::clamp(waitMilliseconds, 50ul, 500ul);
                }

                Sleep(waitMilliseconds);
            }
        }

        bool waitForServiceRunning(SC_HANDLE service, DWORD timeoutMilliseconds)
        {
            const ULONGLONG deadline = GetTickCount64() + timeoutMilliseconds;

            while (true)
            {
                SERVICE_STATUS_PROCESS status{};
                DWORD bytesNeeded = 0;

                if (!QueryServiceStatusEx(
                        service,
                        SC_STATUS_PROCESS_INFO,
                        reinterpret_cast<LPBYTE>(&status),
                        sizeof(status),
                        &bytesNeeded))
                {
                    return false;
                }

                if (status.dwCurrentState == SERVICE_RUNNING)
                {
                    return true;
                }

                if (status.dwCurrentState != SERVICE_START_PENDING ||
                    GetTickCount64() >= deadline)
                {
                    return false;
                }

                DWORD waitMilliseconds = 100;
                if (status.dwWaitHint != 0)
                {
                    waitMilliseconds = status.dwWaitHint / 10;
                    waitMilliseconds = std::clamp(waitMilliseconds, 50ul, 500ul);
                }

                Sleep(waitMilliseconds);
            }
        }
    }

    std::vector<ServiceStopResult> ServiceEngine::stopAll(
        const std::vector<std::string>& stopList,
        CancellationToken* cancellation,
        const std::function<void()>& onTargetProcessed)
    {
        std::vector<ServiceStopResult> results;
        lastModifiedServices.clear();

        SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
        if (scm == nullptr)
        {
            for (const auto& serviceName : stopList)
            {
                if (cancellation != nullptr && cancellation->isCancellationRequested())
                {
                    break;
                }

                if (onTargetProcessed) onTargetProcessed();
                const auto outcome = isProtected(serviceName)
                    ? ServiceStopOutcome::Protected
                    : ServiceStopOutcome::FailedToStop;
                results.push_back({ serviceName, outcome });
            }
            return results;
        }

        for (const auto& serviceName : stopList)
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

            if (isProtected(serviceName))
            {
                results.push_back({ serviceName, ServiceStopOutcome::Protected });
                continue;
            }

            bool wasRunning = false;
            const ServiceQueryResult query = queryServiceState(scm, serviceName, wasRunning);
            if (query == ServiceQueryResult::Missing ||
                (query == ServiceQueryResult::Found && !wasRunning))
            {
                results.push_back({ serviceName, ServiceStopOutcome::Ignored });
                continue;
            }
            if (query == ServiceQueryResult::Failed)
            {
                results.push_back({ serviceName, ServiceStopOutcome::FailedToStop });
                continue;
            }

            const std::wstring wideName = Utf8ToWide(serviceName);
            SC_HANDLE service = wideName.empty() ? nullptr : OpenServiceW(
                scm,
                wideName.c_str(),
                SERVICE_STOP | SERVICE_QUERY_STATUS);
            if (service == nullptr)
            {
                results.push_back({ serviceName, ServiceStopOutcome::FailedToStop });
                continue;
            }

            SERVICE_STATUS status{};
            bool stopped = ControlService(service, SERVICE_CONTROL_STOP, &status) != FALSE;
            if (stopped)
            {
                stopped = waitForServiceStopped(service, 5000);
            }
            CloseServiceHandle(service);

            if (stopped)
            {
                lastModifiedServices.push_back({
                    serviceName,
                    wasRunning
                });
            }

            results.push_back({
                serviceName,
                stopped ? ServiceStopOutcome::Stopped : ServiceStopOutcome::FailedToStop
            });
        }

        CloseServiceHandle(scm);
        return results;
    }
    std::vector<ServiceInspection> ServiceEngine::inspectStopList(
        const std::vector<std::string>& stopList) const
    {
        std::vector<ServiceInspection> result;

        SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
        if (scm == nullptr)
        {
            for (const auto& serviceName : stopList)
            {
                result.push_back({
                    serviceName,
                    false,
                    false,
                    isProtected(serviceName)
                });
            }

            return result;
        }

        for (const auto& serviceName : stopList)
        {
            ServiceInspection inspection;
            inspection.serviceName = serviceName;
            inspection.protectedService = isProtected(serviceName);

            inspection.exists = queryServiceState(
                scm,
                serviceName,
                inspection.running) == ServiceQueryResult::Found;

            result.push_back(inspection);
        }

        CloseServiceHandle(scm);
        return result;
    }

    std::vector<std::string> ServiceEngine::revertList(const std::vector<ServiceOriginalState>& toRevert)
    {
        std::vector<std::string> failed;

        if (toRevert.empty()) return failed;

        SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
        if (scm == nullptr)
        {
            for (const auto& original : toRevert)
            {
                if (original.wasRunning)
                {
                    failed.push_back(original.serviceName);
                }
            }

            return failed;
        }

        for (const auto& original : toRevert)
        {
            if (!original.wasRunning) continue; // no estaba corriendo, nada que reiniciar

            const std::wstring wideName = Utf8ToWide(original.serviceName);
            SC_HANDLE hService = wideName.empty() ? nullptr : OpenServiceW(
                scm,
                wideName.c_str(),
                SERVICE_START | SERVICE_QUERY_STATUS);

            if (hService == nullptr)
            {
                failed.push_back(original.serviceName);
                continue;
            }

            bool restored = StartServiceW(hService, 0, nullptr) != FALSE;

            if (!restored && GetLastError() == ERROR_SERVICE_ALREADY_RUNNING)
            {
                restored = true;
            }

            if (restored)
            {
                restored = waitForServiceRunning(hService, 5000);
            }

            if (!restored)
            {
                failed.push_back(original.serviceName);
            }

            CloseServiceHandle(hService);
        }

        CloseServiceHandle(scm);
        return failed;
    }

    void ServiceEngine::revert()
    {
        revertList(lastModifiedServices);
        lastModifiedServices.clear();
    }



    const std::vector<ServiceOriginalState>& ServiceEngine::getLastModifiedServices() const
    {
        return lastModifiedServices;
    }
}
