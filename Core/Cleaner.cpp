#include "Cleaner.h"
#include "ProcessEngine.h"
#include "ServiceEngine.h"
#include "MemoryCleaner.h"
#include "Logger.h"
#include <windows.h>
#include "PendingServicesStore.h"
#include "StringUtil.h"
#include <algorithm>

typedef NTSTATUS(WINAPI* NtQuerySystemInformation_t)(
    ULONG SystemInformationClass,
    PVOID SystemInformation,
    ULONG SystemInformationLength,
    PULONG ReturnLength
    );

constexpr ULONG SystemMemoryListInformationQuery = 0x50; // misma clase que MemoryCleaner

// Layout parcial de SYSTEM_MEMORY_LIST_INFORMATION (estructura interna,
// documentada de forma no oficial por la comunidad de sistemas Windows).
// Solo declaramos los campos que necesitamos leer.
struct SYSTEM_MEMORY_LIST_INFORMATION_PARTIAL
{
    SIZE_T ZeroPageCount;
    SIZE_T FreePageCount;
    SIZE_T ModifiedPageCount;
    SIZE_T ModifiedNoWritePageCount;
    SIZE_T BadPageCount;
    SIZE_T PageCountByPriority[8];
    SIZE_T RepurposedPagesByPriority[8];
    SIZE_T ModifiedPageCountPageFile;
};

namespace WannaClean::Core
{
    namespace
    {
        // Standby list = suma de PageCountByPriority (las páginas "en caché"
        // están repartidas en 8 niveles de prioridad internos de Windows).
        // Se devuelve en páginas; el llamador convierte a bytes.
        bool queryStandbyAndModifiedPages(uint64_t& outStandbyPages, uint64_t& outModifiedPages)
        {
            HMODULE ntdll = GetModuleHandleA("ntdll.dll");
            if (ntdll == nullptr) return false;

            auto NtQuerySystemInformation = reinterpret_cast<NtQuerySystemInformation_t>(
                GetProcAddress(ntdll, "NtQuerySystemInformation"));

            if (NtQuerySystemInformation == nullptr) return false;

            SYSTEM_MEMORY_LIST_INFORMATION_PARTIAL info{};
            ULONG returnLength = 0;

            NTSTATUS status = NtQuerySystemInformation(
                SystemMemoryListInformationQuery,
                &info,
                sizeof(info),
                &returnLength);

            if (status != 0) return false; // STATUS_SUCCESS es 0

            uint64_t standby = 0;
            for (int i = 0; i < 8; ++i)
            {
                standby += info.PageCountByPriority[i];
            }

            outStandbyPages = standby;
            outModifiedPages = info.ModifiedPageCount;
            return true;
        }
    }

    size_t Cleaner::getPendingServicesCount() const
    {
        PendingServicesStore store;
        return store.load().size();
    }

    OperationPlan Cleaner::buildPlan(const Preset& preset) const
    {
        OperationPlan plan;
        plan.presetName = preset.name;
        plan.processNames = preset.processKillList;
        plan.serviceNames = preset.serviceStopList;
        plan.cleanLevel = preset.cleanLevel;
        plan.requiresConfirmation = preset.requiresConfirmation;
        plan.allowsForceTermination = preset.allowsForceTermination;

        ProcessEngine processEngine;
        for (const auto& process : processEngine.inspectMemoryUsage(preset.processKillList))
        {
            plan.activeProcesses.push_back({
                process.processName,
                process.processId,
                process.workingSetBytes,
                process.protectedProcess
            });
        }

        ServiceEngine serviceEngine;
        for (const auto& service : serviceEngine.inspectStopList(preset.serviceStopList))
        {
            plan.services.push_back({
                service.serviceName,
                service.exists,
                service.running,
                service.protectedService
            });
        }

        return plan;
    }

    size_t Cleaner::revertPendingServices()
    {
        PendingServicesStore store;
        auto pending = store.load();

        if (pending.empty()) return 0;

        std::vector<ServiceOriginalState> toRevert;
        for (const auto& p : pending)
        {
            toRevert.push_back({ p.serviceName, p.wasRunning });
        }

        ServiceEngine serviceEngine;
        auto failed = serviceEngine.revertList(toRevert);

        if (failed.empty())
        {
            store.clear();
        }
        else
        {
            std::vector<PendingService> stillPending;

            for (const auto& service : pending)
            {
                if (std::find(
                        failed.begin(),
                        failed.end(),
                        service.serviceName) != failed.end())
                {
                    stillPending.push_back(service);
                }
            }

            store.save(stillPending);
        }

        return pending.size() - failed.size();
    }

    MemorySnapshot Cleaner::captureSnapshot() const
    {
        MemorySnapshot snap;

        MEMORYSTATUSEX memStatus{};
        memStatus.dwLength = sizeof(memStatus);

        if (GlobalMemoryStatusEx(&memStatus))
        {
            snap.total = memStatus.ullTotalPhys;
            snap.available = memStatus.ullAvailPhys;
            snap.used = memStatus.ullTotalPhys - memStatus.ullAvailPhys;
            snap.physicalMemoryValid = true;
        }

        SYSTEM_INFO sysInfo{};
        GetSystemInfo(&sysInfo);
        uint64_t pageSize = sysInfo.dwPageSize;

        uint64_t standbyPages = 0;
        uint64_t modifiedPages = 0;

        if (queryStandbyAndModifiedPages(standbyPages, modifiedPages))
        {
            snap.standby = standbyPages * pageSize;
            snap.modified = modifiedPages * pageSize;
            snap.standbyValid = true;
            snap.modifiedValid = true;
        }
        // Si falla la consulta NT API, standby/modified quedan en 0
        // (valor por defecto del struct) — no crasheamos, snap sigue
        // siendo válido con total/available/used al menos.

        return snap;
    }


    OperationResult Cleaner::run(
        const Preset& preset,
        bool allowForceTerminationThisSession,
        CancellationToken* cancellation,
        const OperationProgressCallback& progress)
    {
        return run(
            preset,
            allowForceTerminationThisSession
                ? ForceTerminationAuthorization::ConfirmedForSession
                : ForceTerminationAuthorization::NotConfirmed,
            cancellation,
            progress);
    }

    OperationResult Cleaner::run(
        const Preset& preset,
        ForceTerminationAuthorization authorization,
        CancellationToken* cancellation,
        const OperationProgressCallback& progress)
    {
        OperationResult result;
        const size_t processStepCount = preset.processKillList.size();
        const size_t serviceStepCount = preset.serviceStopList.size();
        const size_t totalSteps = processStepCount + serviceStepCount + 1;
        size_t completedSteps = 0;
        auto reportProgress = [&](const std::string& status)
        {
            if (progress) progress(static_cast<float>(completedSteps) / totalSteps, status);
        };
        reportProgress("Preparando la operacion");

        Logger::info("Iniciando preset: " + preset.name);

        MemorySnapshot before = captureSnapshot();

        if (!before.physicalMemoryValid)
        {
            result.errors.push_back({
                "Memoria del sistema",
                "No se pudo obtener el estado de la memoria física"
            });
        }

        if (!before.standbyValid || !before.modifiedValid)
        {
            Logger::error("No se pudo consultar completamente el estado de standby/modified");
        }

        // --- Procesos ---
        ProcessEngine processEngine;
        bool allowForce = preset.allowsForceTermination &&
            authorization == ForceTerminationAuthorization::ConfirmedForSession;
        size_t processedProcesses = 0;
        auto processStep = [&]()
        {
            if (processedProcesses < processStepCount)
            {
                ++processedProcesses;
                ++completedSteps;
                reportProgress("Revisando aplicaciones (" + std::to_string(processedProcesses) + " de " + std::to_string(processStepCount) + ")");
            }
        };
        auto processResults = processEngine.closeAll(
            preset.processKillList,
            allowForce,
            cancellation,
            processStep);
        if (cancellation == nullptr || !cancellation->isCancellationRequested())
        {
            while (processedProcesses < processStepCount) processStep();
        }
        if (processStepCount == 0) reportProgress("Revisando aplicaciones");
        result.processDetails = processResults;

        uint64_t processWorkingSetEstimate = 0;
        for (const auto& pr : processResults)
        {
            switch (pr.outcome)
            {
            case ProcessCloseOutcome::ClosedGracefully:
            case ProcessCloseOutcome::ForcedTermination:
                result.processesClosed++;
                processWorkingSetEstimate += pr.estimatedWorkingSetBytes;
                break;

            case ProcessCloseOutcome::StillRunning:
                // Se informa como cierre incompleto.
                result.errors.push_back({ pr.processName, "Sigue ejecutándose (no se pudo cerrar sin forzar)" });
                break;

            case ProcessCloseOutcome::ForceTerminationFailed:
                result.errors.push_back({ pr.processName, "No se pudo forzar el cierre" });
                break;

            case ProcessCloseOutcome::ErrorOpeningProcess:
                result.errors.push_back({ pr.processName, "Error técnico: no se pudo acceder al proceso" });
                break;

            case ProcessCloseOutcome::EnumerationFailed:
                result.errors.push_back({ pr.processName, "No se pudo enumerar los procesos" });
                break;

            case ProcessCloseOutcome::Protected:
                Logger::info("Proceso protegido, no se toca: " + pr.processName);
                break;

            case ProcessCloseOutcome::Ignored:
                Logger::info("Proceso ignorado, fuera de la kill list: " + pr.processName);
                break;

            case ProcessCloseOutcome::AlreadyExited:
                Logger::info("Proceso terminó antes de poder cerrarlo: " + pr.processName);
                break;
            }

            if (pr.outcome == ProcessCloseOutcome::ClosedGracefully ||
                pr.outcome == ProcessCloseOutcome::ForcedTermination)
            {
                Logger::info(
                    "Proceso cerrado: " + pr.processName +
                    ", working set estimado=" + std::to_string(pr.estimatedWorkingSetBytes));
            }
            else if (pr.outcome == ProcessCloseOutcome::StillRunning ||
                     pr.outcome == ProcessCloseOutcome::ForceTerminationFailed ||
                     pr.outcome == ProcessCloseOutcome::ErrorOpeningProcess)
            {
                Logger::error("No se pudo completar el proceso: " + pr.processName);
            }
        }
        result.processWorkingSetEstimateBytes = processWorkingSetEstimate;

        if (cancellation != nullptr && cancellation->isCancellationRequested())
        {
            result.cancelled = true;
            result.errors.push_back({ "Operación", "Cancelada antes de procesar servicios" });
            Logger::info("Preset cancelado: " + preset.name);
            return result;
        }

        // --- Servicios ---
        ServiceEngine serviceEngine;
        size_t processedServices = 0;
        auto serviceStep = [&]()
        {
            if (processedServices < serviceStepCount)
            {
                ++processedServices;
                ++completedSteps;
                reportProgress("Revisando servicios (" + std::to_string(processedServices) + " de " + std::to_string(serviceStepCount) + ")");
            }
        };
        auto serviceResults = serviceEngine.stopAll(
            preset.serviceStopList,
            cancellation,
            serviceStep);
        if (cancellation == nullptr || !cancellation->isCancellationRequested())
        {
            while (processedServices < serviceStepCount) serviceStep();
        }
        if (serviceStepCount == 0) reportProgress("Revisando servicios");
        result.serviceDetails = serviceResults;

        for (const auto& sr : serviceResults)
        {
            if (sr.outcome == ServiceStopOutcome::Stopped)
            {
                result.servicesStopped++;
                Logger::info("Servicio detenido: " + sr.serviceName);
            }
            else if (sr.outcome == ServiceStopOutcome::FailedToStop)
            {
                result.errors.push_back({ sr.serviceName, "No se pudo detener" });
                Logger::error("No se pudo detener el servicio: " + sr.serviceName);
            }
            else if (sr.outcome == ServiceStopOutcome::Protected)
            {
                Logger::info("Servicio protegido, no se toca: " + sr.serviceName);
            }
            else if (sr.outcome == ServiceStopOutcome::Ignored)
            {
                Logger::info("Servicio ignorado: " + sr.serviceName);
            }
        }

        // Guarda los servicios modificados para la restauracion posterior.
        if (!serviceEngine.getLastModifiedServices().empty())
        {
            PendingServicesStore pendingStore;
            std::vector<PendingService> pending = pendingStore.load();

            for (const auto& original : serviceEngine.getLastModifiedServices())
            {
                const bool alreadyPending = std::any_of(
                    pending.begin(),
                    pending.end(),
                    [&](const PendingService& item)
                    {
                        return Utf8EqualsIgnoreCase(item.serviceName, original.serviceName);
                    });
                if (!alreadyPending)
                {
                    pending.push_back({ original.serviceName, original.wasRunning });
                }
            }

            pendingStore.save(pending);
        }

        if (cancellation != nullptr && cancellation->isCancellationRequested())
        {
            result.cancelled = true;
            result.errors.push_back({ "Operacion", "Cancelada antes de limpiar memoria" });
            Logger::info("Preset cancelado: " + preset.name);
            return result;
        }
        // --- Memoria ---
        reportProgress("Limpiando memoria");
        MemoryCleaner memoryCleaner;
        auto memResult = memoryCleaner.clean(preset.cleanLevel);

        if (cancellation != nullptr && cancellation->isCancellationRequested())
        {
            result.cancelled = true;
            result.errors.push_back({ "Operación", "Cancelada durante la limpieza de memoria" });
            Logger::info("Preset cancelado: " + preset.name);
            return result;
        }

        result.standbyReductionEstimateBytes = memResult.standbyReductionEstimateBytes;
        result.standbyReductionEstimateValid = memResult.standbyReductionEstimateValid;
        if (!memResult.succeeded)
        {
            result.errors.push_back({ "Limpieza de RAM", memResult.errorDetail });
        }

        ++completedSteps;
        reportProgress("Operacion completada");

        Logger::info(
            "Preset finalizado: " + preset.name +
            ", procesos cerrados=" + std::to_string(result.processesClosed) +
            ", servicios detenidos=" + std::to_string(result.servicesStopped) +
            ", errores=" + std::to_string(result.errors.size()));

        return result;
    }
}
