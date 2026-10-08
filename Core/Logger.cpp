#include "Logger.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <shlobj.h>
#include <sstream>

namespace WannaClean::Core
{
    namespace
    {
        std::filesystem::path logPath()
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
            std::error_code error;
            std::filesystem::create_directories(path, error);
            if (error)
            {
                return {};
            }

            path /= "wanna_clean.log";
            return path;
        }

        void write(const char* level, const std::string& message)
        {
            try
            {
                const std::filesystem::path path = logPath();
                if (path.empty())
                {
                    return;
                }

                std::ofstream file(path, std::ios::app);
                if (!file.is_open())
                {
                    return;
                }

                const auto now = std::chrono::system_clock::now();
                const std::time_t time = std::chrono::system_clock::to_time_t(now);
                std::tm localTime{};
                localtime_s(&localTime, &time);

                file << std::put_time(&localTime, "%Y-%m-%d %H:%M:%S")
                     << " [" << level << "] " << message << '\n';
            }
            catch (...)
            {
                // El logging nunca debe detener la limpieza.
            }
        }
    }

    void Logger::info(const std::string& message)
    {
        write("INFO", message);
    }

    void Logger::error(const std::string& message)
    {
        write("ERROR", message);
    }
}
