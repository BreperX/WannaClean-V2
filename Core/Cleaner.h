#pragma once
#include "Preset.h"
#include "OperationResult.h"
#include "Snapshot.h"
#include "CancellationToken.h"
#include "OperationPlan.h"
#include "OperationProgress.h"

namespace WannaClean::Core
{
    enum class ForceTerminationAuthorization
    {
        NotConfirmed,
        ConfirmedForSession
    };

    class Cleaner
    {
    public:
        OperationResult run(
            const Preset& preset,
            bool allowForceTerminationThisSession = false,
            CancellationToken* cancellation = nullptr,
            const OperationProgressCallback& progress = {});

        OperationResult run(
            const Preset& preset,
            ForceTerminationAuthorization authorization,
            CancellationToken* cancellation = nullptr,
            const OperationProgressCallback& progress = {});

        OperationPlan buildPlan(const Preset& preset) const;
        MemorySnapshot captureSnapshot() const;

        // Cuántos servicios quedaron pendientes de revertir de una
        // operación anterior (incluso de una sesión previa de la app).
        // Pensado para que la UI muestre el aviso en Idle sin conocer
        // PendingServicesStore ni ServiceEngine directamente.
        size_t getPendingServicesCount() const;

        // Revierte todos los servicios pendientes (de cualquier sesión) y
        // limpia el registro. Devuelve cuántos se lograron reiniciar.
        size_t revertPendingServices();
    };
}