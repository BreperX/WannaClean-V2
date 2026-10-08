#pragma once

#include <filesystem>
#include <windows.h>

namespace WannaClean::Core
{
    inline bool replaceFile(const std::filesystem::path& temporary, const std::filesystem::path& target)
    {
        return MoveFileExW(
            temporary.c_str(),
            target.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
    }
}
