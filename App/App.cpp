#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include <d3d11.h>
#include <dwmapi.h>
#include <windows.h>
#include <tchar.h>
#include <shlobj.h>
#include <filesystem>
#include <fstream>
#include <climits>
#include <cstring>
#include <algorithm>
#include <string>
#include <utility>
#include <vector>
#include "PresetStore.h"
#include "SettingsService.h"
#include "StringUtil.h"
#include "UiState.h"
#include "UiCleanerBridge.h"
#include "UiIdleScreen.h"
#include "UiRunningScreen.h"
#include "UiDoneScreen.h"
#include "UiAggressiveConfirm.h"
#include "UiSettingsScreen.h"
#include "UiDetailsPanel.h"
#include "Resource.h"

#pragma comment(lib, "Dwmapi.lib")

using namespace WannaClean::App;
using namespace WannaClean::Core;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace
{
    ID3D11Device* g_pd3dDevice = nullptr;
    ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
    IDXGISwapChain* g_pSwapChain = nullptr;
    ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;
    bool g_settingsLayoutActive = false;
    std::string g_imguiIniPath;
    std::string g_imguiLogPath;

    constexpr int settingsWindowWidth = 1152;
    constexpr int settingsWindowHeight = 768;

    void configureImGuiStorage(ImGuiIO& io)
    {
        io.IniFilename = nullptr;
        io.LogFilename = nullptr;

        PWSTR localAppData = nullptr;
        if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &localAppData)) ||
            localAppData == nullptr)
        {
            return;
        }

        const std::filesystem::path dataDirectory = std::filesystem::path(localAppData) / L"WannaClean";
        CoTaskMemFree(localAppData);

        std::error_code error;
        std::filesystem::create_directories(dataDirectory, error);
        if (error)
        {
            return;
        }

        try
        {
            g_imguiIniPath = (dataDirectory / L"imgui.ini").string();
            g_imguiLogPath = (dataDirectory / L"imgui_log.txt").string();
            io.IniFilename = g_imguiIniPath.c_str();
            io.LogFilename = g_imguiLogPath.c_str();
        }
        catch (const std::filesystem::filesystem_error&)
        {
            g_imguiIniPath.clear();
            g_imguiLogPath.clear();
        }
    }

    void resizeAndCenterWindow(HWND hwnd, int width, int height)
    {
        HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO info{};
        info.cbSize = sizeof(info);
        if (monitor == nullptr || !GetMonitorInfo(monitor, &info))
        {
            SetWindowPos(hwnd, nullptr, 0, 0, width, height, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
            return;
        }

        const RECT& work = info.rcWork;
        constexpr int windowMargin = 24;
        const int maxWidth = (std::max)(1, static_cast<int>(work.right - work.left) - windowMargin);
        const int maxHeight = (std::max)(1, static_cast<int>(work.bottom - work.top) - windowMargin);
        width = (std::min)(width, maxWidth);
        height = (std::min)(height, maxHeight);
        const int x = work.left + ((work.right - work.left) - width) / 2;
        const int y = work.top + ((work.bottom - work.top) - height) / 2;
        SetWindowPos(hwnd, nullptr, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
    }

    void applyWindowChrome(HWND hwnd)
    {
        COLORREF captionColor = RGB(105, 0, 0);
        COLORREF textColor = RGB(255, 255, 255);
        DwmSetWindowAttribute(hwnd, DWMWA_CAPTION_COLOR, &captionColor, sizeof(captionColor));
        DwmSetWindowAttribute(hwnd, DWMWA_TEXT_COLOR, &textColor, sizeof(textColor));
    }

    ID3D11ShaderResourceView* createTextureFromIcon(ID3D11Device* device, HICON icon)
    {
        if (device == nullptr || icon == nullptr) return nullptr;

        constexpr int iconSize = 128;
        HDC screen = GetDC(nullptr);
        HDC memory = screen != nullptr ? CreateCompatibleDC(screen) : nullptr;
        if (screen == nullptr || memory == nullptr)
        {
            if (memory != nullptr) DeleteDC(memory);
            if (screen != nullptr) ReleaseDC(nullptr, screen);
            return nullptr;
        }

        BITMAPINFO bitmapInfo{};
        bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bitmapInfo.bmiHeader.biWidth = iconSize;
        bitmapInfo.bmiHeader.biHeight = -iconSize;
        bitmapInfo.bmiHeader.biPlanes = 1;
        bitmapInfo.bmiHeader.biBitCount = 32;
        bitmapInfo.bmiHeader.biCompression = BI_RGB;

        void* pixels = nullptr;
        HBITMAP bitmap = CreateDIBSection(screen, &bitmapInfo, DIB_RGB_COLORS, &pixels, nullptr, 0);
        HGDIOBJ previous = bitmap != nullptr ? SelectObject(memory, bitmap) : nullptr;
        ID3D11ShaderResourceView* view = nullptr;

        if (bitmap != nullptr && pixels != nullptr && previous != nullptr &&
            DrawIconEx(memory, 0, 0, icon, iconSize, iconSize, 0, nullptr, DI_NORMAL))
        {
            D3D11_TEXTURE2D_DESC textureInfo{};
            textureInfo.Width = iconSize;
            textureInfo.Height = iconSize;
            textureInfo.MipLevels = 1;
            textureInfo.ArraySize = 1;
            textureInfo.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            textureInfo.SampleDesc.Count = 1;
            textureInfo.Usage = D3D11_USAGE_DEFAULT;
            textureInfo.BindFlags = D3D11_BIND_SHADER_RESOURCE;

            D3D11_SUBRESOURCE_DATA textureData{};
            textureData.pSysMem = pixels;
            textureData.SysMemPitch = iconSize * 4;

            ID3D11Texture2D* texture = nullptr;
            if (SUCCEEDED(device->CreateTexture2D(&textureInfo, &textureData, &texture)))
            {
                device->CreateShaderResourceView(texture, nullptr, &view);
                texture->Release();
            }
        }

        if (previous != nullptr) SelectObject(memory, previous);
        if (bitmap != nullptr) DeleteObject(bitmap);
        DeleteDC(memory);
        ReleaseDC(nullptr, screen);
        return view;
    }

    std::filesystem::path findIconFontPath()
    {
        wchar_t modulePath[MAX_PATH]{};
        DWORD length = GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
        if (length == 0 || length >= MAX_PATH)
        {
            return {};
        }

        std::filesystem::path directory(modulePath);
        directory = directory.parent_path();
        const std::string fileName = "Font Awesome 6 Free-Solid-900.otf";

        for (int attempt = 0; attempt < 3; ++attempt)
        {
            std::filesystem::path candidate = directory / "Assets" / fileName;
            if (std::filesystem::exists(candidate))
            {
                return candidate;
            }
            directory = directory.parent_path();
        }

        return {};
    }

    bool samePresetConfiguration(const Preset& left, const Preset& right)
    {
        auto sameNameList = [](const std::vector<std::string>& a, const std::vector<std::string>& b)
        {
            if (a.size() != b.size())
            {
                return false;
            }

            return std::all_of(a.begin(), a.end(), [&](const std::string& value)
            {
                return std::any_of(b.begin(), b.end(), [&](const std::string& other)
                {
                    return Utf8EqualsIgnoreCase(value, other);
                });
            });
        };

        if (left.name != right.name ||
            !sameNameList(left.processKillList, right.processKillList) ||
            !sameNameList(left.serviceStopList, right.serviceStopList) ||
            left.cleanLevel != right.cleanLevel ||
            left.requiresConfirmation != right.requiresConfirmation ||
            left.allowsForceTermination != right.allowsForceTermination ||
            left.applications.size() != right.applications.size())
        {
            return false;
        }

        return std::all_of(left.applications.begin(), left.applications.end(), [&](const ApplicationConfig& a)
        {
            return std::any_of(right.applications.begin(), right.applications.end(), [&](const ApplicationConfig& b)
            {
                return Utf8EqualsIgnoreCase(a.executableName, b.executableName) &&
                    a.displayName == b.displayName &&
                    a.executablePath == b.executablePath &&
                    a.isBackground == b.isBackground;
            });
        });
    }

    bool samePresetList(const std::vector<Preset>& left, const std::vector<Preset>& right)
    {
        return left.size() == right.size() && std::equal(
            left.begin(),
            left.end(),
            right.begin(),
            samePresetConfiguration);
    }

    bool containsSettingName(const std::vector<std::string>& names, const std::string& name)
    {
        return std::any_of(names.begin(), names.end(), [&](const std::string& value)
            {
                return Utf8EqualsIgnoreCase(value, name);
            });
    }

    void refreshSettingsCatalog(SettingsScreenState& state, std::vector<Preset>& presets, SettingsService& service)
    {
        state.catalog = service.discover(presets);
        state.orderedApplications.clear();
        for (const auto& application : state.catalog.applications)
        {
            if (!containsSettingName(state.hiddenApplications, application.executableName))
            {
                state.orderedApplications.push_back(application.executableName);
            }
        }

        state.orderedServices.clear();
        for (const auto& serviceEntry : state.catalog.services)
        {
            state.orderedServices.push_back(serviceEntry.serviceName);
        }

        state.runningBackgroundApplications.clear();
        for (const auto& application : state.catalog.backgroundApplications)
        {
            state.runningBackgroundApplications.push_back(application.executableName);
        }
        state.refreshRequested = false;
    }

    void applySettingsAction(
        SettingsScreenState& state,
        std::vector<Preset>& presets,
        const std::vector<Preset>& defaults,
        SettingsService& service,
        const SettingsAction& action)
    {
        SettingsEditResult result = SettingsEditResult::Unchanged;

        switch (action.type)
        {
        case SettingsActionType::SetApplicationEnabled:
            result = service.setApplicationEnabled(
                presets, action.presetIndex, action.name, action.enabled,
                action.isBackground, action.displayName, action.executablePath);
            break;
        case SettingsActionType::AddApplication:
            result = service.addApplication(
                presets, action.selectedPresets, action.displayName, action.name);
            if (result == SettingsEditResult::Changed)
            {
                state.hiddenApplications.erase(
                    std::remove_if(state.hiddenApplications.begin(), state.hiddenApplications.end(),
                        [&](const std::string& value) { return Utf8EqualsIgnoreCase(value, action.name); }),
                    state.hiddenApplications.end());
                if (!containsSettingName(state.orderedApplications, action.name))
                {
                    state.orderedApplications.push_back(action.name);
                }
                state.showApplicationAddForm = false;
            }
            if (result == SettingsEditResult::Changed || result == SettingsEditResult::Duplicate)
            {
                state.newApplicationName[0] = {};
                state.newApplicationExecutable[0] = {};
                std::fill(state.newApplicationPresets, state.newApplicationPresets + 3, false);
            }
            break;
        case SettingsActionType::RenameApplication:
            service.renameApplication(presets, action.name, action.displayName);
            break;
        case SettingsActionType::RemoveApplication:
            result = service.removeApplication(presets, action.name);
            if (result == SettingsEditResult::Changed)
            {
                state.hiddenApplications.push_back(action.name);
                state.orderedApplications.erase(
                    std::remove_if(state.orderedApplications.begin(), state.orderedApplications.end(),
                        [&](const std::string& value) { return Utf8EqualsIgnoreCase(value, action.name); }),
                    state.orderedApplications.end());
            }
            break;
        case SettingsActionType::SetServiceEnabled:
            result = service.setServiceEnabled(presets, action.presetIndex, action.name, action.enabled);
            break;
        case SettingsActionType::AddService:
            result = service.addService(presets, action.selectedPresets, action.name);
            if (result == SettingsEditResult::Changed)
            {
                if (!containsSettingName(state.orderedServices, action.name))
                {
                    state.orderedServices.push_back(action.name);
                }
                state.showServiceAddForm = false;
            }
            if (result == SettingsEditResult::Changed || result == SettingsEditResult::Duplicate)
            {
                state.newServiceName[0] = {};
                std::fill(state.newServicePresets, state.newServicePresets + 3, false);
            }
            break;
        case SettingsActionType::RemoveService:
            result = service.removeService(presets, action.name);
            if (result == SettingsEditResult::Changed)
            {
                state.orderedServices.erase(
                    std::remove_if(state.orderedServices.begin(), state.orderedServices.end(),
                        [&](const std::string& value) { return Utf8EqualsIgnoreCase(value, action.name); }),
                    state.orderedServices.end());
            }
            break;
        case SettingsActionType::RestoreDefaults:
            presets = defaults;
            state.hiddenApplications.clear();
            state.refreshRequested = true;
            break;
        case SettingsActionType::None:
            break;
        }

        state.showProtectedApplicationNotice = state.showProtectedApplicationNotice ||
            (result == SettingsEditResult::Protected &&
            (action.type == SettingsActionType::AddApplication ||
             action.type == SettingsActionType::SetApplicationEnabled ||
             action.type == SettingsActionType::RemoveApplication));
        state.showProtectedServiceNotice = state.showProtectedServiceNotice ||
            (result == SettingsEditResult::Protected &&
            (action.type == SettingsActionType::AddService ||
             action.type == SettingsActionType::SetServiceEnabled ||
             action.type == SettingsActionType::RemoveService));
    }

    void applyPendingSettingsActions(
        SettingsScreenState& state,
        std::vector<Preset>& presets,
        const std::vector<Preset>& defaults,
        SettingsService& service)
    {
        std::vector<SettingsAction> actions = std::move(state.pendingActions);
        state.pendingActions.clear();
        state.showProtectedApplicationNotice = false;
        state.showProtectedServiceNotice = false;
        for (const SettingsAction& action : actions)
        {
            applySettingsAction(state, presets, defaults, service, action);
        }
        if (state.refreshRequested)
        {
            refreshSettingsCatalog(state, presets, service);
        }
    }
    bool CreateDeviceD3D(HWND hWnd)
    {
        DXGI_SWAP_CHAIN_DESC sd{};
        sd.BufferCount = 2;
        sd.BufferDesc.Width = 0;
        sd.BufferDesc.Height = 0;
        sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.BufferDesc.RefreshRate.Numerator = 60;
        sd.BufferDesc.RefreshRate.Denominator = 1;
        sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.OutputWindow = hWnd;
        sd.SampleDesc.Count = 1;
        sd.SampleDesc.Quality = 0;
        sd.Windowed = TRUE;
        sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

        UINT createDeviceFlags = 0;
        D3D_FEATURE_LEVEL featureLevel;
        const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };

        HRESULT res = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags,
            featureLevelArray, 2, D3D11_SDK_VERSION, &sd,
            &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);

        if (res != S_OK) return false;

        ID3D11Texture2D* pBackBuffer = nullptr;
        g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
        if (pBackBuffer)
        {
            g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
            pBackBuffer->Release();
        }

        return true;
    }

    void CleanupDeviceD3D()
    {
        if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
        if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
        if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
        if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
    }

    LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
            return true;

        switch (msg)
        {
        case WM_SIZE:
            if (g_pd3dDevice != nullptr && g_pSwapChain != nullptr && wParam != SIZE_MINIMIZED &&
                LOWORD(lParam) > 0 && HIWORD(lParam) > 0)
            {
                if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
                if (FAILED(g_pSwapChain->ResizeBuffers(
                    0, static_cast<UINT>(LOWORD(lParam)), static_cast<UINT>(HIWORD(lParam)), DXGI_FORMAT_UNKNOWN, 0)))
                {
                    return 0;
                }

                ID3D11Texture2D* pBackBuffer = nullptr;
                g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
                if (pBackBuffer)
                {
                    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
                    pBackBuffer->Release();
                }
            }
            return 0;

        case WM_GETMINMAXINFO:
        {
            auto* limits = reinterpret_cast<MINMAXINFO*>(lParam);
            HMONITOR monitor = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
            MONITORINFO info{};
            info.cbSize = sizeof(info);
            if (monitor != nullptr && GetMonitorInfo(monitor, &info))
            {
                constexpr int windowMargin = 24;
                const int workWidth = static_cast<int>(info.rcWork.right - info.rcWork.left);
                const int workHeight = static_cast<int>(info.rcWork.bottom - info.rcWork.top);
                const int minWorkWidth = (std::max)(1, workWidth - windowMargin);
                const int minWorkHeight = (std::max)(1, workHeight - windowMargin);
                limits->ptMinTrackSize.x = (std::min)(g_settingsLayoutActive ? settingsWindowWidth : 760, minWorkWidth);
                limits->ptMinTrackSize.y = (std::min)(g_settingsLayoutActive ? settingsWindowHeight : 560, minWorkHeight);
                limits->ptMaxTrackSize.x = workWidth;
                limits->ptMaxTrackSize.y = workHeight;
                limits->ptMaxSize.x = workWidth;
                limits->ptMaxSize.y = workHeight;
                limits->ptMaxPosition.x = info.rcWork.left - info.rcMonitor.left;
                limits->ptMaxPosition.y = info.rcWork.top - info.rcMonitor.top;
            }
            return 0;
        }

        case WM_SYSCOMMAND:
            if ((wParam & 0xfff0) == SC_KEYMENU)
                return 0;
            break;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProc(hWnd, msg, wParam, lParam);
    }
}

int APIENTRY _tWinMain(HINSTANCE hInstance, HINSTANCE, LPTSTR, int)
{
    HICON appIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_APP));
    HICON smallIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_SMALL));
    WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_CLASSDC, WndProc, 0L, 0L,
        hInstance, appIcon, LoadCursor(nullptr, IDC_ARROW), nullptr, nullptr, _T("WannaClean"), smallIcon };
    RegisterClassEx(&wc);

    const DWORD fixedWindowStyle = WS_OVERLAPPEDWINDOW & ~(WS_THICKFRAME | WS_MAXIMIZEBOX);
    HWND hwnd = CreateWindow(wc.lpszClassName, _T("WannaClean v2"), fixedWindowStyle,
        100, 100, 760, 560, nullptr, nullptr, wc.hInstance, nullptr);

    resizeAndCenterWindow(hwnd, 760, 560);
    applyWindowChrome(hwnd);

    if (!CreateDeviceD3D(hwnd))
    {
        CleanupDeviceD3D();
        UnregisterClass(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    HICON idleIcon = static_cast<HICON>(LoadImageW(
        hInstance,
        MAKEINTRESOURCEW(IDI_APP),
        IMAGE_ICON,
        128,
        128,
        LR_DEFAULTCOLOR));
    ID3D11ShaderResourceView* idleIconTexture = createTextureFromIcon(g_pd3dDevice, idleIcon);
    if (idleIcon != nullptr) DestroyIcon(idleIcon);

    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    configureImGuiStorage(io);

    ImFontConfig defaultFontConfig{};
    defaultFontConfig.SizePixels = 15.0f;
    defaultFontConfig.GlyphRanges = io.Fonts->GetGlyphRangesDefault();
    io.Fonts->AddFontDefault(&defaultFontConfig);
    static const ImWchar iconRanges[] = { 0xf000, 0xf8ff, 0 };
    bool iconFontLoaded = false;
    HRSRC iconFontResource = FindResource(hInstance, MAKEINTRESOURCE(IDR_ICON_FONT), RT_RCDATA);
    if (iconFontResource != nullptr)
    {
        HGLOBAL loadedResource = LoadResource(hInstance, iconFontResource);
        const void* fontData = loadedResource != nullptr ? LockResource(loadedResource) : nullptr;
        const DWORD fontDataSize = SizeofResource(hInstance, iconFontResource);
        if (fontData != nullptr && fontDataSize > 0)
        {
            ImFontConfig iconConfig{};
            iconConfig.MergeMode = true;
            iconConfig.PixelSnapH = true;
            iconConfig.FontDataOwnedByAtlas = false;
            iconFontLoaded = io.Fonts->AddFontFromMemoryTTF(
                const_cast<void*>(fontData),
                static_cast<int>(fontDataSize),
                15.0f,
                &iconConfig,
                iconRanges) != nullptr;
        }
    }

    if (!iconFontLoaded)
    {
        const std::filesystem::path iconFontPath = findIconFontPath();
        if (!iconFontPath.empty())
        {
            std::ifstream fontFile(iconFontPath, std::ios::binary | std::ios::ate);
            std::streamsize fileSize = 0;
            if (fontFile)
            {
                fileSize = static_cast<std::streamsize>(fontFile.tellg());
            }
            if (fileSize > 0 && fileSize <= INT_MAX)
            {
                std::vector<char> fontData(static_cast<size_t>(fileSize));
                fontFile.seekg(0, std::ios::beg);
                if (fontFile.read(fontData.data(), fileSize))
                {
                    void* atlasData = IM_ALLOC(fontData.size());
                    if (atlasData != nullptr)
                    {
                        std::memcpy(atlasData, fontData.data(), fontData.size());
                        ImFontConfig iconConfig{};
                        iconConfig.MergeMode = true;
                        iconConfig.PixelSnapH = true;
                        iconConfig.FontDataOwnedByAtlas = true;
                        iconFontLoaded = io.Fonts->AddFontFromMemoryTTF(
                            atlasData, static_cast<int>(fontData.size()), 15.0f,
                            &iconConfig, iconRanges) != nullptr;
                        if (!iconFontLoaded) IM_FREE(atlasData);
                    }
                }
            }
        }
    }
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 0.0f;
    style.ChildRounding = 0.0f;
    style.FrameRounding = 2.0f;
    style.PopupRounding = 0.0f;
    style.ScrollbarRounding = 0.0f;
    style.GrabRounding = 0.0f;
    style.WindowPadding = ImVec2(20.0f, 18.0f);
    style.FramePadding = ImVec2(12.0f, 7.0f);
    style.ItemSpacing = ImVec2(10.0f, 9.0f);
    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.IndentSpacing = 18.0f;

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg] = ImVec4(0.025f, 0.018f, 0.02f, 1.0f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.045f, 0.035f, 0.038f, 1.0f);
    colors[ImGuiCol_Border] = ImVec4(0.42f, 0.08f, 0.09f, 1.0f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.085f, 0.06f, 0.065f, 1.0f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.35f, 0.02f, 0.02f, 1.0f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.65f, 0.03f, 0.03f, 1.0f);
    colors[ImGuiCol_Button] = ImVec4(0.68f, 0.025f, 0.045f, 1.0f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.90f, 0.025f, 0.055f, 1.0f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.24f, 0.02f, 0.03f, 1.0f);
    colors[ImGuiCol_Header] = ImVec4(0.30f, 0.04f, 0.05f, 1.0f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.65f, 0.025f, 0.025f, 1.0f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.85f, 0.04f, 0.04f, 1.0f);
    colors[ImGuiCol_CheckMark] = ImVec4(0.96f, 0.16f, 0.19f, 1.0f);
    colors[ImGuiCol_Text] = ImVec4(0.92f, 0.92f, 0.92f, 1.0f);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.62f, 0.57f, 0.57f, 1.0f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.28f, 0.015f, 0.015f, 1.0f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.55f, 0.025f, 0.025f, 1.0f);
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.18f, 0.01f, 0.01f, 1.0f);

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);
    PresetStore presetStore;
    std::vector<Preset> presets = presetStore.load();
    std::vector<Preset> settingsDraft;
    std::vector<Preset> settingsOriginal;
    const std::vector<Preset> defaultPresets = presetStore.defaults();

    UiState uiState = UiState::Idle;
    UiCleanerBridge bridge;
    Cleaner cleaner;
    SettingsService settingsService;
    size_t pendingServicesCount = cleaner.getPendingServicesCount();

    OperationResult lastResult;
    bool showDetails = false;
    bool showSettings = false;
    bool settingsDirty = false;
    bool resetSettingsSession = false;
    SettingsScreenState settingsScreenState;
    IdleScreenState idleScreenState;
    bool showAggressiveConfirm = false;
    int pendingAggressivePresetIndex = -1;
    OperationPlan pendingAggressivePlan;
    UiState stateAfterServiceRevert = UiState::Idle;

    bool done = false;
    while (!done)
    {
        MSG msg;
        while (PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT)
                done = true;
        }
        if (done) break;

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        ImGuiIO& frameIo = ImGui::GetIO();
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(frameIo.DisplaySize, ImGuiCond_Always);
        ImGui::Begin(
            "WannaCleanRoot",
            nullptr,
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoScrollWithMouse);

        if ((uiState == UiState::Running || uiState == UiState::RevertingServices) && bridge.hasNewResult())
        {
            OperationResult completedResult = bridge.takeResult();
            if (uiState == UiState::RevertingServices)
            {
                pendingServicesCount = cleaner.getPendingServicesCount();
                uiState = stateAfterServiceRevert;
            }
            else
            {
                lastResult = std::move(completedResult);
                pendingServicesCount = cleaner.getPendingServicesCount();
                uiState = UiState::Done;
                showDetails = true;
            }
        }

        bool revertPendingServicesRequested = false;

        if (showSettings)
        {
            bool closeRequested = false;
            bool saveRequested = false;
            settingsDirty = !samePresetList(settingsDraft, settingsOriginal);
            RenderSettingsScreen(
                settingsScreenState,
                settingsDraft,
                g_pd3dDevice,
                resetSettingsSession,
                closeRequested,
                saveRequested,
                settingsDirty);
            resetSettingsSession = false;
            applyPendingSettingsActions(settingsScreenState, settingsDraft, defaultPresets, settingsService);
            settingsDirty = !samePresetList(settingsDraft, settingsOriginal);

            if (saveRequested)
            {
                presets = settingsDraft;
                presetStore.save(presets);
                settingsOriginal = settingsDraft;
                settingsDirty = false;
            }
            if (closeRequested && settingsDirty)
            {
                closeRequested = false;
                settingsScreenState.showDiscardConfirmation = true;
            }
            if (closeRequested)
            {
                settingsDirty = false;
                showSettings = false;
                g_settingsLayoutActive = false;
                resizeAndCenterWindow(hwnd, 760, 560);
            }
        }
        else if (showAggressiveConfirm)
        {
            bool confirmed = false;
            bool cancelled = false;
            RenderAggressiveConfirmDialog(pendingAggressivePlan, confirmed, cancelled);

            if (confirmed)
            {
                bridge.startPreset(presets[pendingAggressivePresetIndex], true);
                uiState = UiState::Running;
                showAggressiveConfirm = false;
            }
            else if (cancelled)
            {
                showAggressiveConfirm = false;
                pendingAggressivePresetIndex = -1;
            }
        }
        else
        {
            switch (uiState)
            {
            case UiState::Idle:
            {
                bool settingsRequested = false;
                int clicked = RenderIdleScreen(
                    presets,
                    idleIconTexture,
                    idleScreenState,
                    pendingServicesCount,
                    settingsRequested,
                    revertPendingServicesRequested);

                if (settingsRequested)
                {
                    settingsDraft = presets;
                    settingsOriginal = presets;
                    settingsDirty = false;
                    resetSettingsSession = true;
                    g_settingsLayoutActive = true;
                    resizeAndCenterWindow(hwnd, settingsWindowWidth, settingsWindowHeight);
                    showSettings = true;
                }
                else if (clicked >= 0)
                {
                    if (presets[clicked].requiresConfirmation)
                    {
                        pendingAggressivePresetIndex = clicked;
                        pendingAggressivePlan = cleaner.buildPlan(presets[clicked]);
                        showAggressiveConfirm = true;
                    }
                    else
                    {
                        bridge.startPreset(presets[clicked], false);
                        uiState = UiState::Running;
                    }
                }
                break;
            }

            case UiState::Running:
            {
                float progress = 0.0f;
                std::string progressStatus;
                bridge.getProgress(progress, progressStatus);
                if (RenderRunningScreen(progress, progressStatus, true))
                {
                    bridge.requestCancellation();
                }
                break;
            }

            case UiState::RevertingServices:
            {
                float progress = 0.0f;
                std::string progressStatus;
                bridge.getProgress(progress, progressStatus);
                RenderRunningScreen(progress, progressStatus, false);
                break;
            }

            case UiState::Done:
            {
                bool detailsRequested = false;
                RenderDoneScreen(lastResult, detailsRequested, showDetails);
                if (detailsRequested)
                {
                    showDetails = !showDetails;
                }
                if (showDetails)
                {
                    const float detailsHeight = (std::max)(80.0f, ImGui::GetContentRegionAvail().y - 50.0f);
                    ImGui::BeginChild("OperationDetailsPanel", ImVec2(0.0f, detailsHeight), true);
                    RenderDetailsPanel(lastResult);
                    ImGui::EndChild();
                }

                const bool hasPendingServices = pendingServicesCount > 0;
                const char* revertLabel = "Revertir servicios detenidos";
                const float revertButtonWidth = (std::max)(
                    220.0f,
                    ImGui::CalcTextSize(revertLabel).x +
                    ImGui::GetStyle().FramePadding.x * 2.0f + 8.0f);
                const float actionsWidth = hasPendingServices
                    ? revertButtonWidth + ImGui::GetStyle().ItemSpacing.x + 160.0f
                    : 160.0f;
                ImGui::SetCursorPosX(
                    ImGui::GetCursorPosX() +
                    (ImGui::GetContentRegionAvail().x - actionsWidth) * 0.5f);
                if (hasPendingServices)
                {
                    if (ImGui::Button(revertLabel, ImVec2(revertButtonWidth, 36.0f)))
                    {
                        revertPendingServicesRequested = true;
                    }
                    ImGui::SameLine();
                }

                if (ImGui::Button("Volver al inicio", ImVec2(160.0f, 36.0f)))
                {
                    uiState = UiState::Idle;
                    showDetails = false;
                }
                break;
            }
            }
        }

        ImGui::End();

        if (revertPendingServicesRequested)
        {
            stateAfterServiceRevert = uiState;
            bridge.startRevertPendingServices();
            uiState = UiState::RevertingServices;
        }

        ImGui::Render();
        const float clearColor[4] = { 0.1f, 0.1f, 0.1f, 1.0f };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clearColor);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        const bool needsResponsiveInput = showSettings || showAggressiveConfirm;
        g_pSwapChain->Present(needsResponsiveInput ? 1 : 0, 0);
        if (!needsResponsiveInput)
        {
            Sleep(uiState == UiState::Running ? 50 : 100);
        }
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ReleaseSettingsIconCache();
    ImGui::DestroyContext();

    if (idleIconTexture != nullptr) idleIconTexture->Release();

    CleanupDeviceD3D();
    DestroyWindow(hwnd);
    UnregisterClass(wc.lpszClassName, wc.hInstance);

    return 0;
}
