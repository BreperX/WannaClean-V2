#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "ProcessEngine.h"
#include "ServiceEngine.h"

namespace WannaClean::Core
{
    // Registro de un fallo individual durante la operación. No aborta el resto de la limpieza, solo se acumula aquí.
    struct OperationError
    {
        std::string target;   // nombre del proceso o servicio que falló
        std::string reason;   // descripción corta del fallo
    };

    // Resultado final de ejecutar un preset. Es lo único que la UI recibe del Core después de correr Cleaner::run(preset).
    struct OperationResult
    {
        int processesClosed = 0;
        int servicesStopped = 0;
        bool cancelled = false;

        std::vector<ProcessCloseResult> processDetails;
        std::vector<ServiceStopResult> serviceDetails;

        uint64_t processWorkingSetEstimateBytes = 0;
        uint64_t standbyReductionEstimateBytes = 0;
        bool standbyReductionEstimateValid = false;
        size_t servicesReverted = 0;

        std::vector<OperationError> errors;
    };
}
