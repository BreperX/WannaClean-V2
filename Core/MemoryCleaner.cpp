#include "MemoryCleaner.h"
#include <windows.h>
#include <array>

// NtSetSystemInformation no está en <windows.h> normal — vive en ntdll.dll
// y hay que declararla nosotros mismos, cargándola dinámicamente.
typedef NTSTATUS(WINAPI* NtSetSystemInformation_t)(
    ULONG SystemInformationClass,
    PVOID SystemInformation,
    ULONG SystemInformationLength
    );
typedef NTSTATUS(WINAPI* NtQuerySystemInformation_t)(
    ULONG SystemInformationClass,
    PVOID SystemInformation,
    ULONG SystemInformationLength,
    PULONG ReturnLength);

// Estas constantes son las mismas que usa RAMMap internamente.
// No están documentadas en MSDN porque son APIs internas del kernel.
constexpr ULONG SystemMemoryListInformation = 0x50;
constexpr ULONG MemoryPurgeStandbyList = 4;
constexpr ULONG MemoryFlushModifiedList = 3;
constexpr ULONG MemoryEmptyWorkingSets = 2;
constexpr ULONG SystemMemoryListInformationQuery = 0x50;

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
    bool MemoryCleaner::enableRequiredPrivilege()
    {
        HANDLE hToken = nullptr;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken))
        {
            return false;
        }

        LUID luid{};
        if (!LookupPrivilegeValueA(nullptr, "SeProfileSingleProcessPrivilege", &luid))
        {
            CloseHandle(hToken);
            return false;
        }

        TOKEN_PRIVILEGES tp{};
        tp.PrivilegeCount = 1;
        tp.Privileges[0].Luid = luid;
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

        bool success = AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(tp), nullptr, nullptr) != 0
            && GetLastError() == ERROR_SUCCESS;

        CloseHandle(hToken);
        return success;
    }

    namespace
    {
        bool queryStandbyBytes(uint64_t& bytes)
        {
            HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
            if (ntdll == nullptr)
            {
                return false;
            }

            auto query = reinterpret_cast<NtQuerySystemInformation_t>(
                GetProcAddress(ntdll, "NtQuerySystemInformation"));
            if (query == nullptr)
            {
                return false;
            }

            alignas(SYSTEM_MEMORY_LIST_INFORMATION_PARTIAL) std::array<unsigned char, 4096> buffer{};
            ULONG returnLength = 0;
            const NTSTATUS status = query(
                SystemMemoryListInformationQuery,
                buffer.data(),
                static_cast<ULONG>(buffer.size()),
                &returnLength);
            if (status < 0 || returnLength < sizeof(SYSTEM_MEMORY_LIST_INFORMATION_PARTIAL))
            {
                return false;
            }

            const auto& info = *reinterpret_cast<const SYSTEM_MEMORY_LIST_INFORMATION_PARTIAL*>(buffer.data());

            SYSTEM_INFO systemInfo{};
            GetSystemInfo(&systemInfo);
            uint64_t pages = 0;
            for (SIZE_T priorityCount : info.PageCountByPriority)
            {
                pages += priorityCount;
            }
            bytes = pages * systemInfo.dwPageSize;
            return true;
        }

        // Llama a NtSetSystemInformation con un valor de "comando" de tipo
        // SystemMemoryListInformation. Devuelve true si la llamada tuvo éxito.
        bool callMemoryListOperation(ULONG command)
        {
            HMODULE ntdll = GetModuleHandleA("ntdll.dll");
            if (ntdll == nullptr) return false;

            auto NtSetSystemInformation = reinterpret_cast<NtSetSystemInformation_t>(
                GetProcAddress(ntdll, "NtSetSystemInformation"));

            if (NtSetSystemInformation == nullptr) return false;

            NTSTATUS status = NtSetSystemInformation(
                SystemMemoryListInformation,
                &command,
                sizeof(command));

            return status == 0; // STATUS_SUCCESS es 0
        }
    }

    bool MemoryCleaner::emptyWorkingSets()
    {
        return callMemoryListOperation(MemoryEmptyWorkingSets);
    }

    bool MemoryCleaner::flushModifiedList()
    {
        return callMemoryListOperation(MemoryFlushModifiedList);
    }

    bool MemoryCleaner::purgeStandbyList()
    {
        return callMemoryListOperation(MemoryPurgeStandbyList);
    }

    MemoryCleanResult MemoryCleaner::clean(MemoryCleanLevel level)
    {
        MemoryCleanResult result;

        if (!enableRequiredPrivilege())
        {
            result.succeeded = false;
            result.errorDetail = "No se pudo habilitar el privilegio necesario (¿la app corre como Administrador?)";
            return result;
        }

        bool ok = emptyWorkingSets();

        if (level == MemoryCleanLevel::Deep)
        {
            // Cada operación debe intentarse aunque una anterior falle.
            // El resultado final informa si hubo algún fallo, pero no se
            // aborta prematuramente la limpieza restante.
            bool modifiedListOk = flushModifiedList();
            uint64_t standbyBefore = 0;
            const bool standbyBeforeValid = queryStandbyBytes(standbyBefore);
            bool standbyListOk = purgeStandbyList();
            uint64_t standbyAfter = 0;
            const bool standbyAfterValid = queryStandbyBytes(standbyAfter);
            ok = modifiedListOk && standbyListOk && ok;

            if (standbyBeforeValid && standbyAfterValid)
            {
                result.standbyReductionEstimateBytes = standbyBefore > standbyAfter
                    ? standbyBefore - standbyAfter
                    : 0;
                result.standbyReductionEstimateValid = true;
            }
        }
        else
        {
            result.standbyReductionEstimateValid = true;
        }

        if (!ok)
        {
            result.succeeded = false;
            result.errorDetail = "Una o más operaciones de limpieza de memoria fallaron";
        }

        return result;
    }
}
