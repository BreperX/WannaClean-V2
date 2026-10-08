#pragma once
#pragma once

#include <string>
#include <vector>
#include <functional>
#include "CancellationToken.h"

namespace WannaClean::Core
{
    enum class ServiceStopOutcome
    {
        Stopped,      // se detuvo correctamente
        FailedToStop, // se intentó detener y falló
        Protected,    // nunca se tocó, está en la lista protegida
        Ignored       // no estaba en la stop list activa, se dejó en paz
    };

    struct ServiceInspection
    {
        std::string serviceName;
        bool exists = false;
        bool running = false;
        bool protectedService = false;
    };

    struct ServiceStopResult
    {
        std::string serviceName;
        ServiceStopOutcome outcome;
    };

    // Estado original de un servicio, guardado antes de tocarlo.
    // Es lo que permite "Revertir cambios" más adelante — nunca se
    // restaura automáticamente al cerrar la ventana de WannaClean.
    struct ServiceOriginalState
    {
        std::string serviceName;
        bool wasRunning = false;
    };

    // Árbol de decisión de servicios, análogo al de ProcessEngine:
    // Protected service? -> nunca se detiene
    // En stop list activa? -> detener y registrar estado original
    // Si no -> Ignore, sin excepción
    class ServiceEngine
    {
    public:
        std::vector<ServiceStopResult> stopAll(
            const std::vector<std::string>& stopList,
            CancellationToken* cancellation = nullptr,
            const std::function<void()>& onTargetProcessed = {});

        std::vector<ServiceInspection> inspectStopList(
            const std::vector<std::string>& stopList) const;

        bool isProtectedService(const std::string& serviceName) const;

        // Revierte lo modificado en la última llamada a stopAll() de ESTA
        // instancia (memoria interna).
        void revert();

        // Revierte una lista externa de servicios (ej. leída de
        // PendingServicesStore tras reabrir la app). No depende de que esta
        // instancia haya hecho el stopAll() original.
        std::vector<std::string> revertList(const std::vector<ServiceOriginalState>& toRevert);

        const std::vector<ServiceOriginalState>& getLastModifiedServices() const;

    private:
        bool isProtected(const std::string& serviceName) const;
        bool isInStopList(const std::string& serviceName, const std::vector<std::string>& stopList) const;

        std::vector<ServiceOriginalState> lastModifiedServices;
    };
}
