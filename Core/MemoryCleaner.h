#pragma once
#include <cstdint>
#include "Preset.h"
#include "Snapshot.h"

namespace WannaClean::Core
{
    // Resultado de una operación de limpieza de memoria (caché/standby),
    // separado explícitamente de la RAM devuelta por cerrar procesos.
    struct MemoryCleanResult
    {
        uint64_t standbyReductionEstimateBytes = 0;
        bool standbyReductionEstimateValid = false;
        bool succeeded = true;          // false si alguna llamada NT API falló
        std::string errorDetail;        // vacío si succeeded == true
    };

    // Ejecuta las operaciones de limpieza de RAM inspiradas en RAMMap,
    // usando llamadas directas a NT APIs del sistema
    // Light: EmptyWorkingSets únicamente.
    // Deep: EmptyWorkingSets + FlushModifiedList + PurgeStandbyList
    // (las 3 operaciones completas, solo para Agresivo).
    class MemoryCleaner
    {
    public:
        MemoryCleanResult clean(MemoryCleanLevel level);

    private:
        bool emptyWorkingSets();
        bool flushModifiedList();
        bool purgeStandbyList();

        // Habilita el privilegio necesario en el proceso actual
        // (SeProfileSingleProcessPrivilege / SeIncreaseQuotaPrivilege)
        // antes de invocar las NT APIs. Se llama internamente, no expuesto.
        bool enableRequiredPrivilege();
    };
}
