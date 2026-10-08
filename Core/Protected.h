#pragma once
#include <array>
#include <string_view>

namespace WannaClean::Core
{
    inline constexpr std::array<std::string_view, 16> ProtectedProcesses = {
        "WannaClean.exe",
        "system",
        "registry",
        "secure system",
        "memory compression",
        "smss.exe",
        "csrss.exe",
        "wininit.exe",
        "winlogon.exe",
        "lsass.exe",
        "services.exe",
        "svchost.exe",
        "dwm.exe",
        "fontdrvhost.exe",
        "explorer.exe",
        "sihost.exe"
    };

    inline constexpr std::array<std::string_view, 22> ProtectedServices = {
        "RpcSs",
        "RpcEptMapper",
        "DcomLaunch",
        "LSM",
        "SamSs",
        "EventLog",
        "WinDefend",
        "WdNisSvc",
        "mpssvc",
        "BFE",
        "PlugPlay",
        "Power",
        "ProfSvc",
        "Schedule",
        "gpsvc",
        "Winmgmt",
        "CryptSvc",
        "Dhcp",
        "Dnscache",
        "nsi",
        "LanmanWorkstation",
        "AudioEndpointBuilder"
    };
}
