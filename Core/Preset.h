#pragma once
#include <string>
#include <vector>
#include "ApplicationConfig.h"

namespace WannaClean::Core
{
    // Nivel de limpieza de RAM. Light = solo cerrar procesos + trim básico.
    // Deep = las 3 operaciones RAMMap-style completas (EmptyWorkingSets, FlushModifiedList, PurgeStandbyList). Solo Agresivo usa Deep.
    enum class MemoryCleanLevel
    {
        Light,
        Deep
    };

    // Un preset es solo datos: qué cerrar, qué detener, y qué tan a fondo limpiar memoria. Ninguna lógica vive aquí, eso es trabajo de Cleaner.
    struct Preset
    {
        std::string name;                          // "Jugar", "Trabajar", "Agresivo"

        std::vector<std::string> processKillList;   // nombres de ejecutables, ej. "chrome.exe"
        std::vector<std::string> serviceStopList;    // nombres de servicio, ej. "OneSyncSvc"
        std::vector<ApplicationConfig> applications;

        MemoryCleanLevel cleanLevel = MemoryCleanLevel::Light;

        bool requiresConfirmation = false;           // true solo para Agresivo
        bool allowsForceTermination = false;          // true solo para Agresivo (con confirmación explícita en sesión)
    };
}