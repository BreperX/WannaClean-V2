#include <iostream>
#include <algorithm>
#include <iomanip>
#include <limits>
#include <string>
#include <vector>
#include <windows.h>
#include "PresetStore.h"
#include "Cleaner.h"
#include "ProcessEngine.h"
#include "ServiceEngine.h"
#include "SettingsService.h"
#include "CancellationToken.h"
#include "StringUtil.h"

namespace
{
    bool sameName(const std::string& left, const std::string& right)
    {
        return WannaClean::Core::Utf8EqualsIgnoreCase(left, right);
    }

    bool containsName(const std::vector<std::string>& names, const std::string& name)
    {
        return std::any_of(names.begin(), names.end(), [&](const std::string& item)
        {
            return sameName(item, name);
        });
    }

    void printPresetOptions(const std::vector<WannaClean::Core::Preset>& presets)
    {
        std::cout << "Presets disponibles:\n";
        for (size_t i = 0; i < presets.size(); ++i)
        {
            std::cout << "  " << i << ": " << presets[i].name << '\n';
        }
    }

    void printMemoryMegabytes(uint64_t bytes);

    const char* settingsResultName(WannaClean::Core::SettingsEditResult result)
    {
        using WannaClean::Core::SettingsEditResult;
        switch (result)
        {
        case SettingsEditResult::Changed: return "cambio aplicado";
        case SettingsEditResult::Unchanged: return "sin cambios";
        case SettingsEditResult::Protected: return "elemento protegido";
        case SettingsEditResult::Duplicate: return "elemento duplicado";
        case SettingsEditResult::Invalid: return "datos inválidos";
        }
        return "resultado desconocido";
    }

    void configureApplicationMatrix(
        WannaClean::Core::PresetStore& store,
        std::vector<WannaClean::Core::Preset>& presets)
    {
        using namespace WannaClean::Core;

        ProcessEngine engine;
        std::vector<std::string> applications = engine.listRunningProcessNames();
        const std::vector<std::string> backgroundApplications = engine.listRunningBackgroundApplicationNames();

        const auto addUnique = [&](const std::string& executable)
        {
            if (!executable.empty() && !containsName(applications, executable))
            {
                applications.push_back(executable);
            }
        };

        for (const auto& preset : presets)
        {
            for (const auto& name : preset.processKillList) addUnique(name);
            for (const auto& application : preset.applications) addUnique(application.executableName);
        }
        for (const auto& name : backgroundApplications) addUnique(name);

        if (applications.empty())
        {
            std::cout << "No se encontraron aplicaciones configurables.\n";
            return;
        }

        std::cout << "\nAplicaciones:\n";
        for (size_t i = 0; i < applications.size(); ++i)
        {
            std::cout << "  " << i << ": " << applications[i]
                << (containsName(backgroundApplications, applications[i]) ? " [SEGUNDO PLANO]" : "")
                << "\n";
        }

        std::cout << "Indice de aplicacion (-1 para cancelar): ";
        int applicationIndex = -1;
        std::cin >> applicationIndex;

        if (applicationIndex < 0 ||
            static_cast<size_t>(applicationIndex) >= applications.size())
        {
            return;
        }

        std::cout << "Presets:\n";
        for (size_t i = 0; i < presets.size(); ++i)
        {
            bool enabled = containsName(
                presets[i].processKillList,
                applications[applicationIndex]);

            std::cout << "  " << i << ": " << presets[i].name
                      << " [" << (enabled ? 'X' : ' ') << "]\n";
        }

        std::cout << "Indice de preset (-1 para cancelar): ";
        int presetIndex = -1;
        std::cin >> presetIndex;

        if (presetIndex < 0 || static_cast<size_t>(presetIndex) >= presets.size())
        {
            return;
        }

        const std::string& application = applications[applicationIndex];
        const bool enabled = containsName(presets[presetIndex].processKillList, application);
        SettingsService settings;
        const SettingsEditResult result = settings.setApplicationEnabled(
            presets,
            static_cast<size_t>(presetIndex),
            application,
            !enabled,
            containsName(backgroundApplications, application),
            application,
            engine.resolveExecutablePath(application));

        if (result == SettingsEditResult::Changed)
            std::cout << (enabled ? "Aplicación quitada del preset.\n" : "Aplicación agregada al preset.\n");
        else
            std::cout << "No se pudo cambiar la aplicación: " << settingsResultName(result) << ".\n";

        store.save(presets);
    }

    void printPlan(const WannaClean::Core::Cleaner& cleaner,
        const WannaClean::Core::Preset& preset)
    {
        using namespace WannaClean::Core;

        auto plan = cleaner.buildPlan(preset);

        std::cout << "\nPlan: " << plan.presetName << "\n";
        std::cout << "Procesos activos detectados:\n";

        for (const auto& process : plan.activeProcesses)
        {
            std::cout << "  - " << process.processName << " (PID " << process.processId << ", ";
            printMemoryMegabytes(process.estimatedWorkingSet);
            std::cout << ")" << (process.protectedProcess ? " [PROTEGIDO]" : "") << "\n";
        }

        std::cout << "Servicios de la lista:\n";
        for (const auto& service : plan.services)
        {
            std::cout << "  - " << service.serviceName
                      << (service.protectedService ? " [PROTEGIDO]" : "")
                      << (service.exists ? (service.running ? " [EN EJECUCIÓN]" : " [DETENIDO]")
                                         : " [NO ENCONTRADO]")
                      << "\n";
        }
    }

    bool runCoreTests()
    {
        using namespace WannaClean::Core;

        PresetStore store;
        const auto presets = store.defaults();
        int passed = 0;
        int failed = 0;
        const auto check = [&](const char* name, bool condition)
        {
            std::cout << (condition ? "[OK]   " : "[FALLO] ") << name << '\n';
            condition ? ++passed : ++failed;
        };

        check("Los presets predeterminados incluyen Jugar, Trabajar y Agresivo",
            presets.size() == 3 && presets[0].name == "Jugar" &&
            presets[1].name == "Trabajar" && presets[2].name == "Agresivo");

        ProcessEngine processEngine;
        check("La protección de procesos permite y bloquea los nombres esperados",
            processEngine.isProtectedProcess("explorer.exe") &&
            !processEngine.isProtectedProcess("example-user-app.exe"));

        ServiceEngine serviceEngine;
        check("La protección de servicios permite y bloquea los nombres esperados",
            serviceEngine.isProtectedService("RpcSs") &&
            !serviceEngine.isProtectedService("ExampleUserService"));

        Cleaner cleaner;
        if (!presets.empty())
        {
            const auto plan = cleaner.buildPlan(presets.back());
            check("El plan conserva las listas, el nivel y la autorización del preset",
                plan.presetName == presets.back().name &&
                plan.processNames == presets.back().processKillList &&
                plan.serviceNames == presets.back().serviceStopList &&
                plan.cleanLevel == presets.back().cleanLevel &&
                plan.requiresConfirmation == presets.back().requiresConfirmation &&
                plan.allowsForceTermination == presets.back().allowsForceTermination);
        }
        else
        {
            check("El plan conserva las listas, el nivel y la autorización del preset", false);
        }

        CancellationToken token;
        const bool startsActive = !token.isCancellationRequested();
        token.requestCancellation();
        const bool cancels = token.isCancellationRequested();
        token.reset();
        check("El token de cancelación inicia activo, cambia y se puede reiniciar",
            startsActive && cancels && !token.isCancellationRequested());

        SettingsService settings;
        auto editedPresets = store.defaults();
        check("Los ajustes rechazan objetivos protegidos e índices inválidos",
            settings.setApplicationEnabled(editedPresets, 0, "explorer.exe", true, false, {}, {}) ==
                SettingsEditResult::Protected &&
            settings.setApplicationEnabled(editedPresets, editedPresets.size(), "probe.exe", true, false, {}, {}) ==
                SettingsEditResult::Invalid &&
            settings.setServiceEnabled(editedPresets, 0, "RpcSs", true) == SettingsEditResult::Protected);

        const std::string testExecutable = "codex-coretests-probe.exe";
        const auto appResult = settings.setApplicationEnabled(
            editedPresets, 0, testExecutable, true, true, "CoreTests Probe", {});
        const bool appAdded = appResult == SettingsEditResult::Changed &&
            containsName(editedPresets[0].processKillList, testExecutable) &&
            std::any_of(editedPresets[0].applications.begin(), editedPresets[0].applications.end(),
                [&](const ApplicationConfig& application)
                {
                    return sameName(application.executableName, testExecutable) && application.isBackground;
                });
        const auto appRemoved = settings.removeApplication(editedPresets, testExecutable);
        check("Los cambios de aplicaciones agregan metadatos y permiten eliminar la entrada",
            appAdded && appRemoved == SettingsEditResult::Changed &&
            !containsName(editedPresets[0].processKillList, testExecutable));

        editedPresets = store.defaults();
        const std::string addedExecutable = "codex-coretests-added.exe";
        const std::vector<bool> applicationPresets = { false, true, false };
        const auto addedApplication = settings.addApplication(
            editedPresets, applicationPresets, "CoreTests Added", addedExecutable);
        const auto duplicateApplication = settings.addApplication(
            editedPresets, applicationPresets, "CoreTests Added", addedExecutable);
        const bool applicationSelectionIsCorrect =
            !containsName(editedPresets[0].processKillList, addedExecutable) &&
            containsName(editedPresets[1].processKillList, addedExecutable) &&
            !containsName(editedPresets[2].processKillList, addedExecutable);
        const auto addedApplicationRemoved = settings.removeApplication(editedPresets, addedExecutable);
        check("Agregar aplicaciones respeta los presets elegidos y rechaza duplicados",
            addedApplication == SettingsEditResult::Changed && applicationSelectionIsCorrect &&
            duplicateApplication == SettingsEditResult::Duplicate &&
            addedApplicationRemoved == SettingsEditResult::Changed);

        editedPresets = store.defaults();
        const std::string testService = "CodexCoreTestsProbe";
        const std::vector<bool> selectedPresets = { true, false, false };
        const auto serviceAdded = settings.addService(editedPresets, selectedPresets, testService);
        const bool selectedServiceAdded = containsName(editedPresets[0].serviceStopList, testService);
        const bool unselectedServiceOmitted = !containsName(editedPresets[1].serviceStopList, testService);
        const auto duplicateService = settings.addService(editedPresets, selectedPresets, testService);
        const auto serviceRemoved = settings.removeService(editedPresets, testService);
        check("Los servicios se agregan", serviceAdded == SettingsEditResult::Changed);
        check("Los servicios respetan los presets seleccionados",
            selectedServiceAdded && unselectedServiceOmitted);
        check("Los servicios duplicados se rechazan", duplicateService == SettingsEditResult::Duplicate);
        check("Los servicios se pueden eliminar", serviceRemoved == SettingsEditResult::Changed);

        std::cout << "\nResultado: " << passed << " correctas, " << failed << " fallidas.\n";
        return failed == 0;
    }

    void printMemoryMegabytes(uint64_t bytes)
    {
        std::cout << std::fixed << std::setprecision(2)
            << (static_cast<double>(bytes) / 1048576.0) << " MB";
    }
}

int main(int argc, char* argv[])
{
    using namespace WannaClean::Core;

    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    if (argc > 1 && (std::string(argv[1]) == "--test" || std::string(argv[1]) == "-t"))
    {
        return runCoreTests() ? 0 : 1;
    }
    if (argc > 1 && (std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h"))
    {
        std::cout << "WannaClean CoreTests\n"
            << "  --test  Ejecuta pruebas seguras del núcleo y termina con código de resultado.\n"
            << "  --help  Muestra esta ayuda.\n";
        return 0;
    }
    if (argc > 1)
    {
        std::cerr << "Argumento desconocido. Usa --help para ver las opciones.\n";
        return 2;
    }

    std::cout << "WannaClean CoreTests\n"
        << "Herramientas de inspección y pruebas del núcleo. No ejecuta limpiezas.\n\n"
        << "Opciones:\n"
        << "  i: Inspeccionar memoria estimada por proceso\n"
        << "  t: Ejecutar pruebas seguras\n"
        << "  c: Configurar una aplicación por preset\n"
        << "  p: Mostrar el plan actual de un preset\n\n"
        << "Elige una opción: ";

    std::string input;
    if (!(std::cin >> input)) return 2;
    if (input == "t" || input == "T") return runCoreTests() ? 0 : 1;
    if (input != "i" && input != "I" && input != "c" && input != "C" && input != "p" && input != "P")
    {
        std::cout << "Opción inválida. No se ejecutaron operaciones.\n";
        return 2;
    }

    PresetStore store;
    auto presets = store.load();
    Cleaner cleaner;

    if (input == "c" || input == "C")
    {
        configureApplicationMatrix(store, presets);
    }
    else
    {
        const bool inspectMemory = input == "i" || input == "I";
        std::cout << (inspectMemory
            ? "\nInspeccionar memoria estimada por preset\n"
            : "\nConsultar el plan de un preset\n");
        printPresetOptions(presets);
        if (presets.empty())
        {
            std::cout << "No hay presets disponibles.\n";
            return 0;
        }

        std::cout << "Escribe el número del preset que quieres consultar: ";
        std::string indexText;
        if (!(std::cin >> indexText)) return 2;

        try
        {
            const size_t index = std::stoul(indexText);
            if (index >= presets.size())
            {
                std::cout << "Índice fuera de rango.\n";
                return 2;
            }

            if (!inspectMemory)
            {
                printPlan(cleaner, presets[index]);
            }
            else
            {
                ProcessEngine engine;
                const auto info = engine.inspectMemoryUsage(presets[index].processKillList);
                std::cout << "\nProcesos activos de la lista de '" << presets[index].name << "':\n";

                uint64_t total = 0;
                for (const auto& process : info)
                {
                    std::cout << "  " << process.processName << " (PID " << process.processId << "): ";
                    printMemoryMegabytes(process.workingSetBytes);
                    std::cout << (process.protectedProcess ? " [PROTEGIDO]" : "") << '\n';
                    total += process.workingSetBytes;
                }

                std::cout << "\nTotal estimado: ";
                printMemoryMegabytes(total);
                std::cout << " en " << info.size() << " proceso(s).\n";
            }
        }
        catch (...)
        {
            std::cout << "Entrada inválida.\n";
            return 2;
        }
    }

    std::cout << "\nPresiona Enter para salir...";
    std::cin.ignore((std::numeric_limits<std::streamsize>::max)(), '\n');
    std::cin.get();
    return 0;
}
