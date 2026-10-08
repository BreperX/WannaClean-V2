#include "PendingServicesStore.h"
#include "AtomicFile.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <shlobj.h>
#include <string>

using json = nlohmann::json;

namespace WannaClean::Core
{
    namespace
    {
        bool preserveInvalidFile(const std::filesystem::path& path)
        {
            std::filesystem::path backup = path;
            backup += ".corrupt";
            std::error_code error;
            unsigned int suffix = 1;
            while (std::filesystem::exists(backup, error))
            {
                error.clear();
                backup = path;
                backup += ".corrupt." + std::to_string(suffix++);
            }
            return std::filesystem::copy_file(path, backup,
                std::filesystem::copy_options::none, error) && !error;
        }
    }

    std::filesystem::path PendingServicesStore::filePath() const
    {
        PWSTR localAppData = nullptr;
        HRESULT result = SHGetKnownFolderPath(
            FOLDERID_LocalAppData,
            KF_FLAG_DEFAULT,
            nullptr,
            &localAppData);

        if (FAILED(result) || localAppData == nullptr)
        {
            return {};
        }

        std::filesystem::path path(localAppData);
        CoTaskMemFree(localAppData);

        path /= "WannaClean";
        path /= "pending_services.json";
        return path;
    }

    void PendingServicesStore::save(const std::vector<PendingService>& pending)
    {
        const std::filesystem::path path = filePath();

        if (path.empty())
        {
            return;
        }

        try
        {
            const std::filesystem::path& fsPath = path;
            std::filesystem::create_directories(fsPath.parent_path());

            json j = json::array();
            for (const auto& p : pending)
            {
                json item;
                item["serviceName"] = p.serviceName;
                item["wasRunning"] = p.wasRunning;
                j.push_back(item);
            }

            std::filesystem::path temporaryPath = fsPath;
            temporaryPath += ".tmp";

            std::ofstream file(temporaryPath, std::ios::trunc);
            if (!file.is_open())
            {
                return;
            }

            file << j.dump(4);
            file.close();

            if (!file)
            {
                std::error_code error;
                std::filesystem::remove(temporaryPath, error);
                return;
            }

            if (!replaceFile(temporaryPath, fsPath))
            {
                std::error_code error;
                std::filesystem::remove(temporaryPath, error);
            }
        }
        catch (...)
        {
        }
    }

    std::vector<PendingService> PendingServicesStore::load() const
    {
        std::vector<PendingService> result;
        const std::filesystem::path path = filePath();

        if (path.empty())
        {
            return result;
        }

        std::ifstream file(path);
        if (!file.is_open())
        {
            return result; // no hay archivo, no hay pendientes - vacío está bien
        }

        try
        {
            json j;
            file >> j;
            if (!j.is_array())
            {
                file.close();
                preserveInvalidFile(path);
                return result;
            }

            for (const auto& item : j)
            {
                PendingService p;
                p.serviceName = item.value("serviceName", "");
                p.wasRunning = item.value("wasRunning", false);
                result.push_back(p);
            }
        }
        catch (const json::exception&)
        {
            result.clear();
            file.close();
            preserveInvalidFile(path);
        }

        return result;
    }

    void PendingServicesStore::clear()
    {
        const std::filesystem::path path = filePath();

        if (path.empty())
        {
            return;
        }

        std::error_code ec;
        std::filesystem::remove(path, ec);
        // Si falla el borrado (permisos, archivo en uso), no crasheamos;
        // el próximo save() lo sobreescribirá de todas formas.
    }
}
