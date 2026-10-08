#pragma once

namespace WannaClean::App
{
    // Estados principales de la ventana.
    enum class UiState
    {
        Idle,
        Running,
        RevertingServices,
        Done
    };
}
