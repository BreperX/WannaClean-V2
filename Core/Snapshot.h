#pragma once
#include <cstdint>

namespace WannaClean::Core
{
    // Foto instantánea del estado de memoria del sistema, en bytes. No mezclar con OperationResult: esto es el estado bruto de Windows
    struct MemorySnapshot
    {
        uint64_t total = 0;      // RAM física total del sistema
        uint64_t available = 0;  // RAM disponible reportada por Windows (incluye standby)
        uint64_t used = 0;       // RAM activamente en uso
        uint64_t standby = 0;    // Cache/standby list (recuperable, pero no gratis)
        uint64_t modified = 0;   // Modified page list (pendiente de escritura a disco)

        bool physicalMemoryValid = false;
        bool standbyValid = false;
        bool modifiedValid = false;
    };
}