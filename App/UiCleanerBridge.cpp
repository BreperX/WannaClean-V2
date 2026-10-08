#include "UiCleanerBridge.h"
#include <exception>
#include <utility>

namespace WannaClean::App
{
    UiCleanerBridge::~UiCleanerBridge()
    {
        requestCancellation();
        waitForCompletion();
    }

    void UiCleanerBridge::startPreset(const WannaClean::Core::Preset& preset, bool allowForceTerminationThisSession)
    {
        if (running)
        {
            return; // ya hay una operación en curso, ignoramos el clic duplicado
        }

        if (worker.joinable())
        {
            worker.join();
        }

        cancellation.reset();
        progressFraction = 0.0f;
        {
            std::lock_guard<std::mutex> lock(progressMutex);
            progressStatus = "Preparando la operacion";
        }
        running = true;
        newResultAvailable = false;

        try
        {
            worker = std::thread(
                &UiCleanerBridge::runInBackground,
                this,
                preset,
                allowForceTerminationThisSession);
        }
        catch (const std::exception& exception)
        {
            WannaClean::Core::OperationResult result;
            result.errors.push_back({ "Core", exception.what() });
            {
                std::lock_guard<std::mutex> lock(resultMutex);
                lastResult = std::move(result);
            }
            newResultAvailable = true;
            running = false;
        }
    }

    void UiCleanerBridge::startRevertPendingServices()
    {
        if (running)
        {
            return;
        }

        if (worker.joinable())
        {
            worker.join();
        }

        progressFraction = 0.0f;
        {
            std::lock_guard<std::mutex> lock(progressMutex);
            progressStatus = "Revirtiendo servicios detenidos";
        }
        running = true;
        newResultAvailable = false;

        try
        {
            worker = std::thread(&UiCleanerBridge::revertInBackground, this);
        }
        catch (const std::exception& exception)
        {
            WannaClean::Core::OperationResult result;
            result.errors.push_back({ "Servicios", exception.what() });
            {
                std::lock_guard<std::mutex> lock(resultMutex);
                lastResult = std::move(result);
            }
            newResultAvailable = true;
            running = false;
        }
    }

    void UiCleanerBridge::revertInBackground()
    {
        WannaClean::Core::OperationResult result;
        try
        {
            WannaClean::Core::Cleaner cleaner;
            result.servicesReverted = cleaner.revertPendingServices();
            progressFraction = 1.0f;
            std::lock_guard<std::mutex> lock(progressMutex);
            progressStatus = "Restauracion de servicios completada";
        }
        catch (const std::exception& exception)
        {
            result.errors.push_back({ "Servicios", exception.what() });
        }
        catch (...)
        {
            result.errors.push_back({ "Servicios", "Fallo inesperado al revertir servicios" });
        }

        {
            std::lock_guard<std::mutex> lock(resultMutex);
            lastResult = std::move(result);
        }
        newResultAvailable = true;
        running = false;
    }

    void UiCleanerBridge::runInBackground(WannaClean::Core::Preset preset, bool allowForceTerminationThisSession)
    {
        WannaClean::Core::OperationResult result;

        try
        {
            WannaClean::Core::Cleaner cleaner;
            result = cleaner.run(
                preset,
                allowForceTerminationThisSession,
                &cancellation,
                [this](float fraction, const std::string& status)
                {
                    progressFraction = fraction;
                    std::lock_guard<std::mutex> lock(progressMutex);
                    progressStatus = status;
                });
        }
        catch (const std::exception& exception)
        {
            result.errors.push_back({ "Core", exception.what() });
        }
        catch (...)
        {
            result.errors.push_back({ "Core", "Fallo inesperado durante la operación" });
        }

        {
            std::lock_guard<std::mutex> lock(resultMutex);
            lastResult = result;
        }

        newResultAvailable = true;
        running = false;
    }

    void UiCleanerBridge::requestCancellation()
    {
        cancellation.requestCancellation();
    }

    void UiCleanerBridge::waitForCompletion()
    {
        if (worker.joinable())
        {
            worker.join();
        }
    }

    bool UiCleanerBridge::isRunning() const
    {
        return running;
    }

    void UiCleanerBridge::getProgress(float& fraction, std::string& status) const
    {
        fraction = progressFraction.load();
        std::lock_guard<std::mutex> lock(progressMutex);
        status = progressStatus;
    }

    bool UiCleanerBridge::hasNewResult() const
    {
        return newResultAvailable;
    }

    WannaClean::Core::OperationResult UiCleanerBridge::takeResult()
    {
        std::lock_guard<std::mutex> lock(resultMutex);
        newResultAvailable = false;
        return lastResult;
    }
}
