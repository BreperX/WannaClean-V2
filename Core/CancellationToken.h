#pragma once

#include <atomic>

namespace WannaClean::Core
{
    class CancellationToken
    {
    public:
        void reset() noexcept
        {
            cancelled.store(false, std::memory_order_relaxed);
        }

        void requestCancellation() noexcept
        {
            cancelled.store(true, std::memory_order_relaxed);
        }

        bool isCancellationRequested() const noexcept
        {
            return cancelled.load(std::memory_order_relaxed);
        }

    private:
        std::atomic<bool> cancelled{ false };
    };
}
