#include "UiSettingsScreen.h"
#include "UiTheme.h"
#include "StringUtil.h"
#include "imgui.h"
#include <d3d11.h>
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <unordered_map>
#include <algorithm>
#include <cstring>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#pragma comment(lib, "Version.lib")

namespace WannaClean::App
{
    namespace
    {
        bool sameName(const std::string& left, const std::string& right)
        {
            return WannaClean::Core::Utf8EqualsIgnoreCase(left, right);
        }

        std::string mapUtf8Case(const std::string& value, DWORD flags)
        {
            const std::wstring wide = WannaClean::Core::Utf8ToWide(value);
            if (wide.empty()) return value;
            const int required = LCMapStringEx(LOCALE_NAME_INVARIANT, flags, wide.c_str(), -1,
                nullptr, 0, nullptr, nullptr, 0);
            if (required <= 1) return value;
            std::wstring mapped(static_cast<size_t>(required), L'\0');
            if (LCMapStringEx(LOCALE_NAME_INVARIANT, flags, wide.c_str(), -1,
                mapped.data(), required, nullptr, nullptr, 0) <= 0) return value;
            mapped.pop_back();
            return WannaClean::Core::WideToUtf8(mapped.c_str());
        }

        bool containsName(const std::vector<std::string>& names, const std::string& name)
        {
            return std::any_of(names.begin(), names.end(), [&](const std::string& value)
                {
                    return sameName(value, name);
                });
        }

        SettingsAction& queueAction(SettingsScreenState& state, SettingsActionType type)
        {
            state.pendingActions.emplace_back();
            state.pendingActions.back().type = type;
            return state.pendingActions.back();
        }

        void copyUtf8(char* destination, size_t capacity, const std::string& source)
        {
            if (destination == nullptr || capacity == 0)
            {
                return;
            }

            size_t length = (std::min)(source.size(), capacity - 1);
            while (length > 0 && length < source.size() &&
                (static_cast<unsigned char>(source[length]) & 0xC0) == 0x80)
            {
                --length;
            }
            std::memcpy(destination, source.data(), length);
            destination[length] = '\0';
        }

        bool isProtectedApplication(
            const WannaClean::Core::SettingsCatalog& catalog,
            const std::string& executableName)
        {
            const auto matches = [&](const auto& applications)
            {
                return std::any_of(applications.begin(), applications.end(), [&](const auto& application)
                    {
                        return sameName(application.executableName, executableName) && application.isProtected;
                    });
            };
            return matches(catalog.applications) || matches(catalog.backgroundApplications);
        }

        bool isProtectedService(
            const WannaClean::Core::SettingsCatalog& catalog,
            const std::string& serviceName)
        {
            return std::any_of(catalog.services.begin(), catalog.services.end(), [&](const auto& service)
                {
                    return sameName(service.serviceName, serviceName) && service.isProtected;
                });
        }
        std::unordered_map<std::string, ID3D11ShaderResourceView*> iconCache;
        ID3D11ShaderResourceView* g_placeholderIcon = nullptr;
        std::unordered_map<std::string, std::string> descriptionCache;

        constexpr int kIconSize = 32;
        constexpr uint32_t kIconCacheMagic = 0x57434943; // "WCIC"
        constexpr uint32_t kIconCacheVersion = 1;

#pragma pack(push, 1)
        struct IconCacheHeader
        {
            uint32_t magic;
            uint32_t version;
            uint32_t width;
            uint32_t height;
        };
#pragma pack(pop)
        uint64_t hashKey(const std::string& key)
        {
            uint64_t hash = 1469598103934665603ULL;
            for (unsigned char c : key)
            {
                hash ^= c;
                hash *= 1099511628211ULL;
            }
            return hash;
        }

        std::filesystem::path getIconCacheDirectory()
        {
            PWSTR localAppData = nullptr;
            std::filesystem::path result;
            if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &localAppData)))
            {
                result = std::filesystem::path(localAppData) / L"WannaClean" / L"icons";
                CoTaskMemFree(localAppData);
            }
            return result;
        }

        std::filesystem::path iconCacheFilePath(const std::string& executableName)
        {
            const std::filesystem::path directory = getIconCacheDirectory();
            if (directory.empty()) return {};
            const std::string lowerName = mapUtf8Case(executableName, LCMAP_LOWERCASE);

            char nameBuffer[32]{};
            std::snprintf(nameBuffer, sizeof(nameBuffer), "%016llx.icon",
                static_cast<unsigned long long>(hashKey(lowerName)));

            return directory / nameBuffer;
        }

        bool loadIconFromDisk(const std::string& executableName, std::vector<uint8_t>& outPixels)
        {
            const std::filesystem::path filePath = iconCacheFilePath(executableName);
            if (filePath.empty()) return false;

            std::ifstream file(filePath, std::ios::binary);
            if (!file) return false;

            IconCacheHeader header{};
            file.read(reinterpret_cast<char*>(&header), sizeof(header));
            if (!file || header.magic != kIconCacheMagic || header.version != kIconCacheVersion)
            {
                return false;
            }
            if (header.width != kIconSize || header.height != kIconSize)
            {
                return false;
            }

            size_t byteCount = static_cast<size_t>(header.width) * header.height * 4;
            outPixels.resize(byteCount);
            file.read(reinterpret_cast<char*>(outPixels.data()), static_cast<std::streamsize>(byteCount));
            return static_cast<bool>(file) || file.eof();
        }

        void saveIconToDisk(const std::string& executableName, const void* pixels)
        {
            const std::filesystem::path directory = getIconCacheDirectory();
            if (directory.empty()) return;
            std::error_code error;
            std::filesystem::create_directories(directory, error);
            if (error) return;

            const std::filesystem::path filePath = iconCacheFilePath(executableName);
            if (filePath.empty()) return;

            std::ofstream file(filePath, std::ios::binary | std::ios::trunc);
            if (!file) return;

            IconCacheHeader header{};
            header.magic = kIconCacheMagic;
            header.version = kIconCacheVersion;
            header.width = kIconSize;
            header.height = kIconSize;

            file.write(reinterpret_cast<const char*>(&header), sizeof(header));
            file.write(reinterpret_cast<const char*>(pixels), kIconSize * kIconSize * 4);
        }

        ID3D11ShaderResourceView* createTextureFromPixels(ID3D11Device* device, const void* pixels)
        {
            D3D11_TEXTURE2D_DESC description{};
            description.Width = kIconSize;
            description.Height = kIconSize;
            description.MipLevels = 1;
            description.ArraySize = 1;
            description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            description.SampleDesc.Count = 1;
            description.Usage = D3D11_USAGE_DEFAULT;
            description.BindFlags = D3D11_BIND_SHADER_RESOURCE;

            D3D11_SUBRESOURCE_DATA data{};
            data.pSysMem = pixels;
            data.SysMemPitch = kIconSize * 4;

            ID3D11Texture2D* texture = nullptr;
            ID3D11ShaderResourceView* view = nullptr;
            if (SUCCEEDED(device->CreateTexture2D(&description, &data, &texture)))
            {
                device->CreateShaderResourceView(texture, nullptr, &view);
                texture->Release();
            }
            return view;
        }

        constexpr const char* iconApplications = "\xef\x81\x8b";
        constexpr const char* iconServices = "\xef\x84\x90";
        constexpr const char* iconSettings = "\xef\x80\x93";
        constexpr const char* iconRefresh = "\xef\x80\xa1";
        constexpr const char* iconHelp = "\xef\x81\x9a";
        constexpr const char* iconTrash = "\xef\x8b\xad";
        constexpr const char* iconSave = "\xef\x83\x87";
        constexpr const char* iconBack = "\xef\x81\xA0";
        constexpr const char* iconEdit = "\xef\x81\x84";

        std::string catalogApplicationPath(
            const WannaClean::Core::SettingsCatalog& catalog,
            const std::string& executable)
        {
            const auto findPath = [&](const auto& applications) -> std::string
            {
                const auto entry = std::find_if(applications.begin(), applications.end(), [&](const auto& application)
                    {
                        return sameName(application.executableName, executable);
                    });
                return entry == applications.end() ? std::string{} : entry->executablePath;
            };
            std::string path = findPath(catalog.applications);
            return path.empty() ? findPath(catalog.backgroundApplications) : path;
        }
        std::string fileDescription(const std::string& executablePath);

        std::string friendlyName(const std::vector<WannaClean::Core::Preset>& presets,
            const std::string& executable,
            const WannaClean::Core::SettingsCatalog& catalog)
        {
            for (const auto& preset : presets)
            {
                for (const auto& application : preset.applications)
                {
                    if (sameName(application.executableName, executable) &&
                        !application.displayName.empty() &&
                        !sameName(application.displayName, executable))
                    {
                        return application.displayName;
                    }

                    if (sameName(application.executableName, executable) &&
                        !application.executablePath.empty())
                    {
                        std::string description = fileDescription(application.executablePath);
                        if (!description.empty())
                        {
                            return description;
                        }
                    }

                }
            }

            const std::string path = catalogApplicationPath(catalog, executable);
            if (!path.empty())
            {
                std::string description = fileDescription(path);
                if (!description.empty())
                {
                    return description;
                }
            }
            std::string result = executable;
            const std::string suffix = ".exe";
            if (result.size() > suffix.size() &&
                result.compare(result.size() - suffix.size(), suffix.size(), suffix) == 0)
            {
                result.erase(result.size() - suffix.size());
            }

            if (!result.empty())
            {
                if (result[0] >= 'a' && result[0] <= 'z')
                {
                    result[0] = static_cast<char>(result[0] - 'a' + 'A');
                }
            }

            return result;
        }

        bool matchesFilter(const std::string& value, const char* filter)
        {
            if (filter[0] == '\0')
            {
                return true;
            }

            std::string lowerValue = value;
            std::string lowerFilter = filter;
            lowerValue = mapUtf8Case(lowerValue, LCMAP_LOWERCASE);
            lowerFilter = mapUtf8Case(lowerFilter, LCMAP_LOWERCASE);
            return lowerValue.find(lowerFilter) != std::string::npos;
        }

        float buttonWidth(const char* label)
        {
            return ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2.0f;
        }

        void alignButtonsRight(float width)
        {
            float available = ImGui::GetContentRegionAvail().x;
            if (available > width)
            {
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + available - width);
            }
        }

        void prepareCenteredPopup()
        {
            ImGuiViewport* viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(
                viewport->GetCenter(),
                ImGuiCond_Appearing,
                ImVec2(0.5f, 0.5f));
            ImGui::SetNextWindowSizeConstraints(
                ImVec2(420.0f, 0.0f),
                ImVec2(760.0f, 620.0f));
        }

        std::string fileDescription(const std::string& executablePath)
        {
            if (executablePath.empty())
            {
                return {};
            }

            auto cached = descriptionCache.find(executablePath);
            if (cached != descriptionCache.end())
            {
                return cached->second;
            }

            std::string description;
            const std::wstring widePath = WannaClean::Core::Utf8ToWide(executablePath);
            if (widePath.empty())
            {
                descriptionCache.emplace(executablePath, description);
                return description;
            }
            DWORD ignored = 0;
            DWORD size = GetFileVersionInfoSizeW(widePath.c_str(), &ignored);
            if (size > 0)
            {
                std::vector<unsigned char> buffer(size);
                if (GetFileVersionInfoW(widePath.c_str(), 0, size, buffer.data()) != FALSE)
                {
                    struct Translation
                    {
                        WORD language;
                        WORD codePage;
                    };

                    Translation* translations = nullptr;
                    UINT translationSize = 0;
                    if (VerQueryValueW(
                        buffer.data(),
                        L"\\VarFileInfo\\Translation",
                        reinterpret_cast<LPVOID*>(&translations),
                        &translationSize) != FALSE &&
                        translationSize >= sizeof(Translation))
                    {
                        LPWSTR value = nullptr;
                        UINT valueSize = 0;
                        const auto queryDescription = [&](WORD language, WORD codePage)
                        {
                            wchar_t subBlock[128]{};
                            swprintf_s(
                                subBlock,
                                L"\\StringFileInfo\\%04x%04x\\FileDescription",
                                language,
                                codePage);
                            return VerQueryValueW(buffer.data(), subBlock,
                                reinterpret_cast<LPVOID*>(&value), &valueSize) != FALSE &&
                                value != nullptr && valueSize > 1;
                        };

                        bool found = false;
                        for (UINT index = 0; index < translationSize / sizeof(Translation) && !found; ++index)
                        {
                            found = queryDescription(translations[index].language, translations[index].codePage);
                        }
                        if (!found)
                        {
                            found = queryDescription(0x0409, 0x04B0) || queryDescription(0x0409, 0x04E4);
                        }
                        if (found)
                        {
                            description = WannaClean::Core::WideToUtf8(value);
                        }
                    }
                }
            }

            descriptionCache.emplace(executablePath, description);
            return description;
        }

        ID3D11ShaderResourceView* placeholderIconView(ID3D11Device* device)
        {
            if (g_placeholderIcon != nullptr)
            {
                return g_placeholderIcon;
            }

            HICON icon = LoadIcon(nullptr, IDI_APPLICATION);
            if (icon == nullptr)
            {
                return nullptr;
            }

            HDC screen = GetDC(nullptr);
            HDC memory = CreateCompatibleDC(screen);
            BITMAPINFO bitmapInfo{};
            bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bitmapInfo.bmiHeader.biWidth = kIconSize;
            bitmapInfo.bmiHeader.biHeight = -kIconSize;
            bitmapInfo.bmiHeader.biPlanes = 1;
            bitmapInfo.bmiHeader.biBitCount = 32;
            bitmapInfo.bmiHeader.biCompression = BI_RGB;
            void* pixels = nullptr;
            HBITMAP bitmap = CreateDIBSection(screen, &bitmapInfo, DIB_RGB_COLORS, &pixels, nullptr, 0);
            HGDIOBJ previous = bitmap != nullptr ? SelectObject(memory, bitmap) : nullptr;

            if (bitmap != nullptr && pixels != nullptr)
            {
                std::memset(pixels, 0, kIconSize * kIconSize * 4);
                DrawIconEx(memory, 0, 0, icon, kIconSize, kIconSize, 0, nullptr, DI_NORMAL);
                g_placeholderIcon = createTextureFromPixels(device, pixels);
            }

            if (previous != nullptr) SelectObject(memory, previous);
            if (bitmap != nullptr) DeleteObject(bitmap);
            if (memory != nullptr) DeleteDC(memory);
            if (screen != nullptr) ReleaseDC(nullptr, screen);

            return g_placeholderIcon;
        }
        ID3D11ShaderResourceView* loadIcon(
            ID3D11Device* device,
            const std::string& executableName,
            const std::string& executablePath)
        {
            if (device == nullptr)
            {
                return nullptr;
            }

            auto cached = iconCache.find(executableName);
            if (cached != iconCache.end())
            {
                return cached->second;
            }
            {
                std::vector<uint8_t> diskPixels;
                if (loadIconFromDisk(executableName, diskPixels))
                {
                    ID3D11ShaderResourceView* view = createTextureFromPixels(device, diskPixels.data());
                    if (view != nullptr)
                    {
                        iconCache.emplace(executableName, view);
                        return view;
                    }
                }
            }
            if (!executablePath.empty())
            {
                const std::wstring widePath = WannaClean::Core::Utf8ToWide(executablePath);
                HICON icon = nullptr;
                bool ownsIcon = false;
                SHFILEINFOW fileInfo{};
                if (!widePath.empty() && SHGetFileInfoW(widePath.c_str(), 0, &fileInfo, sizeof(fileInfo),
                    SHGFI_ICON | SHGFI_SMALLICON) != 0)
                {
                    icon = fileInfo.hIcon;
                    ownsIcon = true;
                }
                if (icon == nullptr)
                {
                    if (!widePath.empty()) ExtractIconExW(widePath.c_str(), 0, nullptr, &icon, 1);
                    ownsIcon = icon != nullptr;
                }

                if (icon != nullptr)
                {
                    HDC screen = GetDC(nullptr);
                    HDC memory = CreateCompatibleDC(screen);
                    BITMAPINFO bitmapInfo{};
                    bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                    bitmapInfo.bmiHeader.biWidth = kIconSize;
                    bitmapInfo.bmiHeader.biHeight = -kIconSize;
                    bitmapInfo.bmiHeader.biPlanes = 1;
                    bitmapInfo.bmiHeader.biBitCount = 32;
                    bitmapInfo.bmiHeader.biCompression = BI_RGB;
                    void* pixels = nullptr;
                    HBITMAP bitmap = CreateDIBSection(screen, &bitmapInfo, DIB_RGB_COLORS, &pixels, nullptr, 0);
                    HGDIOBJ previous = bitmap != nullptr ? SelectObject(memory, bitmap) : nullptr;

                    ID3D11ShaderResourceView* view = nullptr;
                    if (bitmap != nullptr && pixels != nullptr)
                    {
                        std::memset(pixels, 0, kIconSize * kIconSize * 4);
                        DrawIconEx(memory, 0, 0, icon, kIconSize, kIconSize, 0, nullptr, DI_NORMAL);
                        saveIconToDisk(executableName, pixels);
                        view = createTextureFromPixels(device, pixels);
                    }

                    if (previous != nullptr) SelectObject(memory, previous);
                    if (bitmap != nullptr) DeleteObject(bitmap);
                    if (memory != nullptr) DeleteDC(memory);
                    if (screen != nullptr) ReleaseDC(nullptr, screen);
                    if (ownsIcon) DestroyIcon(icon);

                    if (view != nullptr)
                    {
                        iconCache.emplace(executableName, view);
                        return view;
                    }
                }
            }
            return placeholderIconView(device);
        }

        std::string applicationPath(
            const std::vector<WannaClean::Core::Preset>& presets,
            const std::string& executable,
            const WannaClean::Core::SettingsCatalog& catalog)
        {
            for (const auto& preset : presets)
            {
                for (const auto& application : preset.applications)
                {
                    if (sameName(application.executableName, executable))
                    {
                        if (!application.executablePath.empty())
                        {
                            return application.executablePath;
                        }
                        break;
                    }
                }
            }

            return catalogApplicationPath(catalog, executable);
        }
    }

    void ReleaseSettingsIconCache()
    {
        for (auto& entry : iconCache)
        {
            if (entry.second != nullptr)
            {
                entry.second->Release();
            }
        }
        iconCache.clear();

        if (g_placeholderIcon != nullptr)
        {
            g_placeholderIcon->Release();
            g_placeholderIcon = nullptr;
        }
    }

    void RenderSettingsScreen(
        SettingsScreenState& state,
        const std::vector<WannaClean::Core::Preset>& presets,
        ID3D11Device* device,
        bool resetSession,
        bool& closeRequested,
        bool& saveRequested,
        bool dirty)
    {
        closeRequested = false;
        saveRequested = false;
        auto& applicationFilter = state.applicationFilter;
        auto& serviceFilter = state.serviceFilter;
        auto& newApplicationName = state.newApplicationName;
        auto& newApplicationExecutable = state.newApplicationExecutable;
        auto& editApplicationName = state.editApplicationName;
        auto& newApplicationPresets = state.newApplicationPresets;
        auto& showApplicationAddForm = state.showApplicationAddForm;
        auto& newServiceName = state.newServiceName;
        auto& newServicePresets = state.newServicePresets;
        auto& showServiceAddForm = state.showServiceAddForm;
        auto& showApplicationSearchHelp = state.showApplicationSearchHelp;
        auto& showApplicationAddHelp = state.showApplicationAddHelp;
        auto& showGeneralHelp = state.showGeneralHelp;
        auto& showServiceHelp = state.showServiceHelp;
        auto& showServiceAddHelp = state.showServiceAddHelp;
        auto& showResetConfirmation = state.showResetConfirmation;
        auto& showDiscardConfirmation = state.showDiscardConfirmation;
        auto& showApplicationRemoveConfirmation = state.showApplicationRemoveConfirmation;
        auto& showServiceRemoveConfirmation = state.showServiceRemoveConfirmation;
        auto& savedMessageFrames = state.savedMessageFrames;
        auto& applicationBeingEdited = state.applicationBeingEdited;
        auto& editingApplication = state.editingApplication;
        auto& focusApplicationEditor = state.focusApplicationEditor;
        auto& showServices = state.showServices;
        auto& runningBackgroundApplications = state.runningBackgroundApplications;
        auto& orderedApplications = state.orderedApplications;
        auto& orderedServices = state.orderedServices;
        auto& hiddenApplications = state.hiddenApplications;
        auto& applicationRemoveCandidate = state.applicationRemoveCandidate;
        auto& serviceRemoveCandidate = state.serviceRemoveCandidate;
        if (resetSession)
        {
            state.refreshRequested = true;
            runningBackgroundApplications.clear();
            hiddenApplications.clear();
            orderedApplications.clear();
            orderedServices.clear();
            newApplicationName[0] = '\0';
            newApplicationExecutable[0] = '\0';
            newServiceName[0] = '\0';
            std::fill(newApplicationPresets, newApplicationPresets + 3, false);
            std::fill(newServicePresets, newServicePresets + 3, false);
            showApplicationAddForm = false;
            showServiceAddForm = false;
            editingApplication = false;
            focusApplicationEditor = false;
        }
        if (savedMessageFrames > 0)
        {
            --savedMessageFrames;
        }

        const std::vector<std::string>& applications = orderedApplications;

        ImGui::Text("AJUSTES");
        ImGui::SameLine();
        alignButtonsRight(92.0f);
        if (ImGui::Button((std::string(iconHelp) + " Ayuda##general-help").c_str(), ImVec2(92.0f, 30.0f)))
        {
            showGeneralHelp = true;
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Ayuda general sobre ajustes");
        }
        ImGui::TextDisabled("Elige que aplicaciones y servicios se procesan en cada preset.");
        ImGui::Separator();

        bool applicationTabWasActive = !showServices;
        if (applicationTabWasActive) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22f, 0.045f, 0.05f, 1.0f));
        if (ImGui::Button((std::string(iconApplications) + " Aplicaciones").c_str(), ImVec2(150.0f, 32.0f)))
        {
            showServices = false;
        }
        if (applicationTabWasActive) ImGui::PopStyleColor();
        ImGui::SameLine();
        bool serviceTabWasActive = showServices;
        if (serviceTabWasActive) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22f, 0.045f, 0.05f, 1.0f));
        if (ImGui::Button((std::string(iconServices) + " Servicios").c_str(), ImVec2(150.0f, 32.0f)))
        {
            showServices = true;
        }
        if (serviceTabWasActive) ImGui::PopStyleColor();

        ImGui::Separator();
        float panelHeight = (std::max)(240.0f, ImGui::GetContentRegionAvail().y - 118.0f);
        ImGui::BeginChild(
            "SettingsPanel",
            ImVec2(0.0f, panelHeight),
            true);

        if (!showServices)
        {
            UiTheme::sectionHeading("APLICACIONES A CERRAR");
            ImGui::InputTextWithHint(
                "##application-filter",
                "Buscar aplicación...",
                applicationFilter,
                sizeof(applicationFilter));
            ImGui::SameLine();
            if (ImGui::Button((std::string(iconHelp)).c_str()))
            {
                showApplicationSearchHelp = true;
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Ayuda de busqueda");
            }

            const float addApplicationAreaHeight = showApplicationAddForm || editingApplication ? 175.0f : 56.0f;
            float tableHeight = (std::max)(100.0f, ImGui::GetContentRegionAvail().y - addApplicationAreaHeight);
            const int applicationColumnCount = static_cast<int>(presets.size()) + 2;
            auto applicationMatchesFilter = [&](const std::string& executable)
            {
                if (applicationFilter[0] == '\0') return true;
                const std::string displayName = friendlyName(presets, executable, state.catalog);
                return matchesFilter(executable, applicationFilter) ||
                    matchesFilter(displayName, applicationFilter);
            };

            std::vector<const std::string*> visibleApplications;
            visibleApplications.reserve(applications.size());
            for (const auto& executable : applications)
            {
                if (applicationMatchesFilter(executable)) visibleApplications.push_back(&executable);
            }

            std::vector<const std::string*> visibleBackgroundApplications;
            visibleBackgroundApplications.reserve(runningBackgroundApplications.size());
            for (const auto& executable : runningBackgroundApplications)
            {
                if (containsName(orderedApplications, executable) ||
                    containsName(hiddenApplications, executable))
                {
                    continue;
                }
                if (applicationMatchesFilter(executable))
                {
                    visibleBackgroundApplications.push_back(&executable);
                }
            }

            if (ImGui::BeginTable(
                "ApplicationsMatrix",
                applicationColumnCount,
                ImGuiTableFlags_Resizable |
                    ImGuiTableFlags_BordersInnerV |
                    ImGuiTableFlags_BordersOuter |
                    ImGuiTableFlags_ScrollY |
                    ImGuiTableFlags_SizingStretchProp,
                ImVec2(0.0f, tableHeight)))
            {
                ImGui::TableSetupColumn("Aplicación", ImGuiTableColumnFlags_WidthStretch, 2.0f);
                for (const auto& preset : presets)
                {
                    ImGui::TableSetupColumn(preset.name.c_str(), ImGuiTableColumnFlags_WidthStretch, 1.0f);
                }
                ImGui::TableSetupColumn("Acción", ImGuiTableColumnFlags_WidthFixed, 100.0f);
                ImGui::TableHeadersRow();

                auto drawApplicationRow = [&](const std::string& executable, bool background)
                {
                    const std::string displayName = friendlyName(presets, executable, state.catalog);
                    const bool protectedProcess = isProtectedApplication(state.catalog, executable);
                    const std::string executablePath = applicationPath(presets, executable, state.catalog);
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::PushID(executable.c_str());

                    ID3D11ShaderResourceView* icon = loadIcon(device, executable, executablePath);
                    const float applicationRowY = ImGui::GetCursorPosY();
                    if (icon != nullptr)
                    {
                        ImGui::SetCursorPosY(applicationRowY + 5.0f);
                        ImGui::Image(icon, ImVec2(24.0f, 24.0f));
                        ImGui::SetCursorPosY(applicationRowY);
                        ImGui::SameLine();
                    }
                    ImGui::BeginGroup();
                    ImGui::Text("%s", displayName.c_str());
                    ImGui::TextDisabled("%s", executable.c_str());
                    ImGui::EndGroup();

                    for (size_t presetIndex = 0; presetIndex < presets.size(); ++presetIndex)
                    {
                        const auto& preset = presets[presetIndex];
                        ImGui::TableSetColumnIndex(static_cast<int>(presetIndex) + 1);
                        bool enabled = containsName(preset.processKillList, executable);
                        ImGui::PushID(preset.name.c_str());
                        if (protectedProcess) ImGui::BeginDisabled();
                        if (ImGui::Checkbox("##application-enabled", &enabled) && !protectedProcess)
                        {
                            SettingsAction& action = queueAction(state, SettingsActionType::SetApplicationEnabled);
                            action.presetIndex = presetIndex;
                            action.name = executable;
                            action.enabled = enabled;
                            action.isBackground = background;
                            action.displayName = displayName;
                            action.executablePath = executablePath;
                        }
                        if (protectedProcess) ImGui::EndDisabled();
                        ImGui::PopID();
                    }
                    ImGui::TableSetColumnIndex(applicationColumnCount - 1);
                    if (protectedProcess)
                    {
                        ImGui::TextDisabled("Protegido");
                    }
                    else
                    {
                        if (ImGui::Button(
                            (std::string(iconEdit) + "##edit-application").c_str(),
                            ImVec2(38.0f, 28.0f)))
                        {
                            applicationBeingEdited = executable;
                            copyUtf8(editApplicationName, sizeof(editApplicationName), displayName);
                            editingApplication = true;
                            focusApplicationEditor = true;
                        }
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Editar nombre visible");
                        ImGui::SameLine();
                        if (ImGui::Button(
                            (std::string(iconTrash) + "##remove-application").c_str(),
                            ImVec2(38.0f, 28.0f)))
                        {
                            applicationRemoveCandidate = executable;
                            showApplicationRemoveConfirmation = true;
                        }
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Eliminar de la lista");
                    }

                    ImGui::PopID();
                };

                auto drawClippedApplicationRows = [&](const std::vector<const std::string*>& rows, bool background)
                {
                    ImGuiListClipper clipper;
                    clipper.Begin(static_cast<int>(rows.size()));
                    while (clipper.Step())
                    {
                        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
                        {
                            drawApplicationRow(*rows[static_cast<size_t>(row)], background);
                        }
                    }
                };

                drawClippedApplicationRows(visibleApplications, false);

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextColored(
                    ImVec4(1.0f, 0.35f, 0.35f, 1.0f),
                    "APLICACIONES EN SEGUNDO PLANO");

                const bool backgroundRowsVisible = !visibleBackgroundApplications.empty();
                drawClippedApplicationRows(visibleBackgroundApplications, true);

                if (!backgroundRowsVisible)
                {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextDisabled(applicationFilter[0] == '\0'
                        ? "No se encontraron aplicaciones en segundo plano."
                        : "No hay aplicaciones en segundo plano que coincidan con la búsqueda.");
                }
                ImGui::EndTable();
            }

            ImGui::Separator();
            if (!editingApplication)
            {
                const std::string applicationFormLabel = showApplicationAddForm
                    ? "- Nueva aplicación"
                    : "+ Nueva aplicación";
                if (ImGui::Button((applicationFormLabel + "##toggle-application-form").c_str(), ImVec2(205.0f, 30.0f)))
                {
                    showApplicationAddForm = !showApplicationAddForm;
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip(showApplicationAddForm
                        ? "Ocultar formulario para agregar una aplicación"
                        : "Mostrar formulario para agregar una aplicación");
                }
                ImGui::SameLine();
                if (ImGui::Button((std::string(iconHelp) + "##application-add-help").c_str()))
                {
                    showApplicationAddHelp = true;
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Ayuda para agregar una aplicación");
                }
            }

            if (editingApplication)
            {
                ImGui::Text("Editar aplicación");
                if (focusApplicationEditor)
                {
                    ImGui::SetScrollHereY(1.0f);
                    ImGui::SetKeyboardFocusHere();
                    focusApplicationEditor = false;
                }
                ImGui::InputText("Nombre visible", editApplicationName, sizeof(editApplicationName));
                ImGui::TextDisabled("Ejecutable: %s", applicationBeingEdited.c_str());
                if (ImGui::Button((std::string(iconEdit) + " Editar aplicación").c_str()) &&
                    editApplicationName[0] != '\0')
                {
                    SettingsAction& action = queueAction(state, SettingsActionType::RenameApplication);
                    action.name = applicationBeingEdited;
                    action.displayName = editApplicationName;
                    editingApplication = false;
                    applicationBeingEdited.clear();
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancelar##edit-application"))
                {
                    editingApplication = false;
                    applicationBeingEdited.clear();
                }
            }
            else if (showApplicationAddForm)
            {
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::InputTextWithHint(
                    "##new-application-name",
                    "Nombre visible",
                    newApplicationName,
                    sizeof(newApplicationName));
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::InputTextWithHint(
                    "##new-application-executable",
                    "Ejecutable (ej.: chrome.exe)",
                    newApplicationExecutable,
                    sizeof(newApplicationExecutable));
                ImGui::TextDisabled("Incluir en:");
                ImGui::SameLine();
                for (size_t i = 0; i < presets.size() && i < 3; ++i)
                {
                    if (i > 0)
                    {
                        ImGui::SameLine();
                    }
                    ImGui::Checkbox(presets[i].name.c_str(), &newApplicationPresets[i]);
                }
                bool addPresetSelected = false;
                for (size_t i = 0; i < presets.size() && i < 3; ++i)
                {
                    addPresetSelected = addPresetSelected || newApplicationPresets[i];
                }
                const float addApplicationButtonWidth = 230.0f;
                ImGui::SameLine();
                const float addApplicationSpace = ImGui::GetContentRegionAvail().x;
                if (addApplicationSpace >= addApplicationButtonWidth)
                {
                    ImGui::SetCursorPosX(
                        ImGui::GetCursorPosX() + addApplicationSpace - addApplicationButtonWidth);
                }
                else
                {
                    ImGui::NewLine();
                }
                ImGui::BeginDisabled(!addPresetSelected);
                if (!addPresetSelected)
                {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.10f, 0.10f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.16f, 0.12f, 0.12f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.48f, 0.46f, 0.46f, 1.0f));
                }
                const bool addApplicationClicked = ImGui::Button(
                    (std::string(iconApplications) + " Agregar aplicación").c_str(),
                    ImVec2(addApplicationButtonWidth, 34.0f));
                if (!addPresetSelected) ImGui::PopStyleColor(3);
                ImGui::EndDisabled();
                if (addApplicationClicked &&
                    newApplicationName[0] != '\0' &&
                    newApplicationExecutable[0] != '\0')
                {
                    SettingsAction& action = queueAction(state, SettingsActionType::AddApplication);
                    action.name = newApplicationExecutable;
                    action.displayName = newApplicationName;
                    action.selectedPresets.assign(
                        newApplicationPresets,
                        newApplicationPresets + 3);
                }
            }
        }
        else
        {
            const std::vector<std::string>& services = orderedServices;

            UiTheme::sectionHeading("SERVICIOS A DETENER");
            ImGui::InputTextWithHint(
                "##service-filter",
                "Buscar servicio...",
                serviceFilter,
                sizeof(serviceFilter));
            ImGui::SameLine();
            if (ImGui::Button((std::string(iconHelp) + "##service-help").c_str()))
            {
                showServiceHelp = true;
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Ayuda de servicios");
            }

            constexpr float addServiceAreaHeight = 150.0f;
            float tableHeight = (std::max)(100.0f, ImGui::GetContentRegionAvail().y - addServiceAreaHeight);
            const int serviceColumnCount = static_cast<int>(presets.size()) + 2;
            if (ImGui::BeginTable(
                "ServicesMatrix",
                serviceColumnCount,
                ImGuiTableFlags_Resizable |
                    ImGuiTableFlags_BordersInnerV |
                    ImGuiTableFlags_BordersOuter |
                    ImGuiTableFlags_ScrollY |
                    ImGuiTableFlags_SizingStretchProp,
                ImVec2(0.0f, tableHeight)))
            {
                ImGui::TableSetupColumn("Servicio", ImGuiTableColumnFlags_WidthStretch, 2.0f);
                for (const auto& preset : presets)
                {
                    ImGui::TableSetupColumn(preset.name.c_str(), ImGuiTableColumnFlags_WidthStretch, 1.0f);
                }
                ImGui::TableSetupColumn("Acción", ImGuiTableColumnFlags_WidthFixed, 100.0f);
                ImGui::TableHeadersRow();

                for (const auto& service : services)
                {
                    if (!matchesFilter(service, serviceFilter))
                    {
                        continue;
                    }
                    const bool protectedService = isProtectedService(state.catalog, service);
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::PushID(service.c_str());
                    ImGui::Text("%s", service.c_str());

                    for (size_t presetIndex = 0; presetIndex < presets.size(); ++presetIndex)
                    {
                        const auto& preset = presets[presetIndex];
                        ImGui::TableSetColumnIndex(static_cast<int>(presetIndex) + 1);
                        bool enabled = containsName(preset.serviceStopList, service);
                        ImGui::PushID(preset.name.c_str());
                        if (protectedService) ImGui::BeginDisabled();
                        if (ImGui::Checkbox("##service-enabled", &enabled) && !protectedService)
                        {
                            SettingsAction& action = queueAction(state, SettingsActionType::SetServiceEnabled);
                            action.presetIndex = presetIndex;
                            action.name = service;
                            action.enabled = enabled;
                        }
                        if (protectedService) ImGui::EndDisabled();
                        ImGui::PopID();
                    }

                    ImGui::TableSetColumnIndex(serviceColumnCount - 1);
                    if (protectedService)
                    {
                        ImGui::TextDisabled("Protegido");
                    }
                    else if (ImGui::Button(
                        (std::string(iconTrash) + "##remove-service").c_str(),
                        ImVec2(38.0f, 28.0f)))
                    {
                        serviceRemoveCandidate = service;
                        showServiceRemoveConfirmation = true;
                    }
                    if (ImGui::IsItemHovered())
                    {
                        ImGui::SetTooltip("Eliminar de la lista");
                    }
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }

            ImGui::Separator();
            const std::string serviceFormLabel = showServiceAddForm
                ? "- Nuevo servicio"
                : "+ Nuevo servicio";
            if (ImGui::Button((serviceFormLabel + "##toggle-service-form").c_str(), ImVec2(205.0f, 30.0f)))
            {
                showServiceAddForm = !showServiceAddForm;
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip(showServiceAddForm
                    ? "Ocultar formulario para agregar un servicio"
                    : "Mostrar formulario para agregar un servicio");
            }
            ImGui::SameLine();
            if (ImGui::Button((std::string(iconHelp) + "##service-add-help").c_str()))
            {
                showServiceAddHelp = true;
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Ayuda para agregar un servicio");
            }

            if (showServiceAddForm)
            {
                ImGui::SetNextItemWidth(-1.0f);
                ImGui::InputTextWithHint(
                    "##new-service-name",
                    "Nombre exacto (ej.: WSearch)",
                    newServiceName,
                    sizeof(newServiceName));
                ImGui::TextDisabled("Incluir en:");
                ImGui::SameLine();
                for (size_t i = 0; i < presets.size() && i < 3; ++i)
                {
                    if (i > 0)
                    {
                        ImGui::SameLine();
                    }
                    ImGui::Checkbox(presets[i].name.c_str(), &newServicePresets[i]);
                }

            bool servicePresetSelected = false;
            for (size_t i = 0; i < presets.size() && i < 3; ++i)
            {
                servicePresetSelected = servicePresetSelected || newServicePresets[i];
            }
            const float addButtonWidth = 190.0f;
            ImGui::SameLine();
            const float addServiceSpace = ImGui::GetContentRegionAvail().x;
            if (addServiceSpace >= addButtonWidth)
            {
                ImGui::SetCursorPosX(
                    ImGui::GetCursorPosX() + addServiceSpace - addButtonWidth);
            }
            else
            {
                ImGui::NewLine();
            }
            ImGui::BeginDisabled(!servicePresetSelected);
            if (!servicePresetSelected)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.10f, 0.10f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.16f, 0.12f, 0.12f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.48f, 0.46f, 0.46f, 1.0f));
            }
            const bool addServiceClicked = ImGui::Button(
                (std::string(iconServices) + " Agregar servicio").c_str(),
                ImVec2(addButtonWidth, 34.0f));
            if (!servicePresetSelected) ImGui::PopStyleColor(3);
            ImGui::EndDisabled();
            if (addServiceClicked && newServiceName[0] != '\0')
            {
                SettingsAction& action = queueAction(state, SettingsActionType::AddService);
                action.name = newServiceName;
                action.selectedPresets.assign(newServicePresets, newServicePresets + 3);
            }
            }
        }        ImGui::EndChild();

        alignButtonsRight(180.0f + 255.0f + ImGui::GetStyle().ItemSpacing.x);
        if (ImGui::Button((std::string(iconRefresh) + " Actualizar lista").c_str(), ImVec2(180.0f, 30.0f)))
        {
            state.refreshRequested = true;
            hiddenApplications.clear();
        }
        ImGui::SameLine();
        if (ImGui::Button((std::string(iconSettings) + " Restaurar predeterminados").c_str(), ImVec2(255.0f, 30.0f)))
        {
            showResetConfirmation = true;
        }

        if (showApplicationRemoveConfirmation)
        {
            ImGui::OpenPopup("Eliminar aplicacion");
            showApplicationRemoveConfirmation = false;
        }
        prepareCenteredPopup();
        if (ImGui::BeginPopupModal("Eliminar aplicacion", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextWrapped("Estas seguro de que quieres eliminar esta aplicacion de la lista?");
            ImGui::TextDisabled("Puedes agregarla nuevamente en cualquier momento.");
            alignButtonsRight(buttonWidth("Eliminar") + buttonWidth("Cancelar") + ImGui::GetStyle().ItemSpacing.x);
            if (ImGui::Button("Eliminar##confirm-application"))
            {
                SettingsAction& action = queueAction(state, SettingsActionType::RemoveApplication);
                action.name = applicationRemoveCandidate;
                applicationRemoveCandidate.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancelar##cancel-application"))
            {
                applicationRemoveCandidate.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        if (showServiceRemoveConfirmation)
        {
            ImGui::OpenPopup("Eliminar servicio");
            showServiceRemoveConfirmation = false;
        }
        prepareCenteredPopup();
        if (ImGui::BeginPopupModal("Eliminar servicio", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextWrapped("Estas seguro de que quieres eliminar este servicio de la lista?");
            ImGui::TextDisabled("Puedes agregarlo nuevamente en cualquier momento.");
            alignButtonsRight(buttonWidth("Eliminar") + buttonWidth("Cancelar") + ImGui::GetStyle().ItemSpacing.x);
            if (ImGui::Button("Eliminar##confirm-service"))
            {
                SettingsAction& action = queueAction(state, SettingsActionType::RemoveService);
                action.name = serviceRemoveCandidate;
                serviceRemoveCandidate.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancelar##cancel-service"))
            {
                serviceRemoveCandidate.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        if (state.showProtectedApplicationNotice)
        {
            ImGui::OpenPopup("Aplicacion protegida");
            state.showProtectedApplicationNotice = false;
        }
        prepareCenteredPopup();
        if (ImGui::BeginPopupModal("Aplicacion protegida", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextWrapped("Esta aplicacion esta protegida y no se puede agregar a la lista.");
            alignButtonsRight(buttonWidth("Entendido"));
            if (ImGui::Button("Entendido##protected-application"))
            {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        if (state.showProtectedServiceNotice)
        {
            ImGui::OpenPopup("Servicio protegido");
            state.showProtectedServiceNotice = false;
        }
        prepareCenteredPopup();
        if (ImGui::BeginPopupModal("Servicio protegido", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextWrapped("Este servicio esta protegido y no se puede agregar a la lista.");
            alignButtonsRight(buttonWidth("Entendido"));
            if (ImGui::Button("Entendido##protected-service"))
            {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        if (showGeneralHelp)
        {
            ImGui::OpenPopup("Ayuda general");
            showGeneralHelp = false;
        }


        prepareCenteredPopup();
        if (ImGui::BeginPopupModal("Ayuda general", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextWrapped("WannaClean cierra aplicaciones y detiene servicios segun el preset que elijas.");
            ImGui::TextWrapped("En Aplicaciones, la tabla incluye programas con ventana y aplicaciones en segundo plano.");
            ImGui::TextWrapped("Las aplicaciones agregadas manualmente aparecen en esa misma tabla.");
            ImGui::TextWrapped("En Servicios puedes buscar, agregar y asignar servicios a cada preset.");
            ImGui::TextWrapped("Guarda los cambios antes de volver.");
            alignButtonsRight(buttonWidth("Entendido"));
            if (ImGui::Button("Entendido##general-help-close")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        if (showApplicationSearchHelp)
        {
            ImGui::OpenPopup("Ayuda de busqueda");
            showApplicationSearchHelp = false;
        }
        prepareCenteredPopup();
        if (ImGui::BeginPopupModal("Ayuda de busqueda", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextWrapped("Busca por nombre visible o ejecutable en toda la tabla, incluidas las aplicaciones en segundo plano.");
            ImGui::BulletText("Microsoft Excel tambien puede aparecer como excel.exe.");
            ImGui::BulletText("Mozilla Firefox tambien puede aparecer como firefox.exe.");
            alignButtonsRight(buttonWidth("Entendido"));
            if (ImGui::Button("Entendido##search-help-close")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        if (showApplicationAddHelp)
        {
            ImGui::OpenPopup("Ayuda para agregar aplicacion");
            showApplicationAddHelp = false;
        }
        prepareCenteredPopup();
        if (ImGui::BeginPopupModal("Ayuda para agregar aplicacion", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextWrapped("Haz clic en \"+ Nueva aplicación\" para desplegar el formulario.");
            ImGui::BulletText("Escribe el nombre visible y el ejecutable, y elige en qué preset incluirla.");
            ImGui::BulletText("Después, haz clic en el botón \"Agregar aplicación\".");
            ImGui::BulletText("Las aplicaciones agregadas manualmente aparecen en la lista principal.");
            ImGui::BulletText("Nombre visible: el nombre que veras en esta pantalla.");
            ImGui::BulletText("Abre el Administrador de tareas y entra en la pestana Detalles.");
            ImGui::BulletText("Copia el nombre del archivo, por ejemplo chrome.exe.");
            ImGui::TextWrapped("Si no sabes que estas agregando, no lo agregues: podria ser un proceso esencial.");
            alignButtonsRight(buttonWidth("Entendido"));
            if (ImGui::Button("Entendido##add-help-close")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        if (showServiceHelp)
        {
            ImGui::OpenPopup("Ayuda de servicios");
            showServiceHelp = false;
        }
        prepareCenteredPopup();
        if (ImGui::BeginPopupModal("Ayuda de servicios", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextWrapped("Busca servicios por su nombre en la lista de Servicios.");
            ImGui::BulletText("Abre services.msc.");
            ImGui::BulletText("Usa el nombre que aparece en la lista.");
            ImGui::BulletText("Ejemplo: Windows Search usa WSearch.");
            ImGui::TextWrapped("Los servicios protegidos no pueden modificarse.");
            alignButtonsRight(buttonWidth("Entendido"));
            if (ImGui::Button("Entendido##service-help-close")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        if (showServiceAddHelp)
        {
            ImGui::OpenPopup("Ayuda para agregar servicio");
            showServiceAddHelp = false;
        }
        prepareCenteredPopup();
        if (ImGui::BeginPopupModal("Ayuda para agregar servicio", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextWrapped("Haz clic en \"+ Nuevo servicio\" para desplegar el formulario.");
            ImGui::BulletText("Escribe el nombre exacto y elige en qué preset incluirlo.");
            ImGui::BulletText("Después, haz clic en el botón \"Agregar servicio\".");
            ImGui::BulletText("Puedes encontrarlo en services.msc o en el Administrador de tareas.");
            ImGui::BulletText("En el Administrador de tareas, abre la pestana Servicios.");
            ImGui::BulletText("Ejemplo: Windows Search usa WSearch.");
            ImGui::TextWrapped("Si no sabes que estas agregando, no lo agregues: podria ser un servicio esencial.");
            alignButtonsRight(buttonWidth("Entendido"));
            if (ImGui::Button("Entendido##service-add-help-close")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        if (showResetConfirmation)
        {
            ImGui::OpenPopup("Restaurar ajustes");
            showResetConfirmation = false;
        }
        prepareCenteredPopup();
        if (ImGui::BeginPopupModal("Restaurar ajustes", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextWrapped("Se reemplazaran tus listas personalizadas por los valores predeterminados.");
            alignButtonsRight(buttonWidth("Restaurar") + buttonWidth("Cancelar") + ImGui::GetStyle().ItemSpacing.x);
            if (ImGui::Button("Restaurar"))
            {
                queueAction(state, SettingsActionType::RestoreDefaults);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancelar##reset")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        ImGui::Separator();
        if (dirty)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.1f, 1.0f), "Cambios sin guardar");
        }
        else if (savedMessageFrames == 0)
        {
            ImGui::TextDisabled("Sin cambios pendientes");
        }
        if (savedMessageFrames > 0)
        {
            if (dirty)
            {
                ImGui::SameLine();
            }
            ImGui::TextColored(
                ImVec4(0.35f, 1.0f, 0.45f, 1.0f),
                "Cambios guardados correctamente");
        }

        if (ImGui::Button((std::string(iconSave) + " Guardar cambios").c_str(), ImVec2(165.0f, 30.0f)))
        {
            saveRequested = true;
            savedMessageFrames = 90;
        }

        ImGui::SameLine();
        alignButtonsRight(165.0f);
        if (ImGui::Button((std::string(iconBack) + " Volver").c_str(), ImVec2(165.0f, 30.0f)))
        {
            if (dirty)
            {
                showDiscardConfirmation = true;
            }
            else
            {
                closeRequested = true;
            }
        }

        if (showDiscardConfirmation)
        {
            ImGui::OpenPopup("Descartar cambios");
            showDiscardConfirmation = false;
        }
        prepareCenteredPopup();
        if (ImGui::BeginPopupModal("Descartar cambios", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextWrapped("Hay cambios sin guardar. Quieres descartarlos y volver?");
            alignButtonsRight(buttonWidth("Descartar") + buttonWidth("Cancelar") + ImGui::GetStyle().ItemSpacing.x);
            if (ImGui::Button("Descartar##settings"))
            {
                closeRequested = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancelar##settings"))
            {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }
}
