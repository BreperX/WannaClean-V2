#pragma once
#include <filesystem>
#include <vector>
#include "Preset.h"

namespace WannaClean::Core
{
    // Carga y guarda la configuración de presets (kill lists, stop lists,
    // checkboxes por app×preset) desde/hacia %LOCALAPPDATA%\WannaClean\.
    // Nunca junto al ejecutable.
    class PresetStore
    {
    public:
        // Carga los presets guardados. Si no existe config previa,
        // construye y persiste los tres presets por defecto
        // (Jugar, Trabajar, Agresivo) con listas genéricas razonables
        // (navegadores comunes, Office, launchers comunes, Discord) —
        // nunca software específico de un usuario en particular.
        std::vector<Preset> load();

        std::vector<Preset> defaults() const;

        // Persiste el estado actual de los presets a disco.
        // Se invoca cuando el usuario agrega/quita una app de un
        // preset desde la UI ("agregar aplicación a este preset").
        void save(const std::vector<Preset>& presets);

    private:
        std::vector<Preset> buildDefaults() const;
        std::filesystem::path configFilePath() const;
    };
}
