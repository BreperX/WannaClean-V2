#pragma once
#include <filesystem>
#include <vector>

namespace WannaClean::Core
{

    struct PendingService
    {
        std::string serviceName;
        bool wasRunning = false; // qué restaurar si el usuario revierte
    };

    class PendingServicesStore
    {
    public:

        void save(const std::vector<PendingService>& pending);

        // Lee la lista actual. Vacía si no hay nada pendiente o el archivo
        // no existe - no hay "defaults" aquí, a diferencia de PresetStore.
        std::vector<PendingService> load() const;

        // Borra el registro por completo (llamado tras un revert exitoso).
        void clear();

    private:
        std::filesystem::path filePath() const;
    };
}