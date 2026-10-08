#pragma once
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include "CancellationToken.h"
#include "Preset.h"
#include "OperationResult.h"
#include "Cleaner.h"

namespace WannaClean::App
{
    // Ejecuta operaciones del Core en segundo plano.
    class UiCleanerBridge
    {
    public:
        ~UiCleanerBridge();
        void startPreset(const WannaClean::Core::Preset& preset, bool allowForceTerminationThisSession);
        void startRevertPendingServices();

        void requestCancellation();

        void waitForCompletion();
        bool isRunning() const;
        void getProgress(float& fraction, std::string& status) const;
        bool hasNewResult() const;
        WannaClean::Core::OperationResult takeResult();

    private:
        void runInBackground(WannaClean::Core::Preset preset, bool allowForceTerminationThisSession);
        void revertInBackground();

        std::atomic<bool> running{ false };
        std::atomic<bool> newResultAvailable{ false };
        WannaClean::Core::CancellationToken cancellation;
        std::thread worker;

        mutable std::mutex resultMutex;
        WannaClean::Core::OperationResult lastResult;
        std::atomic<float> progressFraction{ 0.0f };
        mutable std::mutex progressMutex;
        std::string progressStatus = "Preparando la operacion";
    };
}
