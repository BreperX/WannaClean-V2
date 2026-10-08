#pragma once
#include <string>
#include <utility>
#include <windows.h>

namespace WannaClean::Core
{
    // Convierte una wide string (UTF-16, lo que usa Windows internamente)
    // a std::string normal (UTF-8). Se necesita en cualquier punto donde
    // la Windows API nos entregue WCHAR/wstring y queramos guardarlo en
    // nuestros structs, que usan std::string por simplicidad.
    inline std::string WideToUtf8(const wchar_t* wide)
    {
        if (wide == nullptr) return {};

        int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
        if (sizeNeeded <= 0) return {};

        std::string result(static_cast<size_t>(sizeNeeded), '\0');
        if (WideCharToMultiByte(CP_UTF8, 0, wide, -1, result.data(), sizeNeeded, nullptr, nullptr) == 0)
        {
            return {};
        }
        result.pop_back();
        return result;
    }

    inline std::wstring Utf8ToWide(const std::string& utf8)
    {
        if (utf8.empty()) return {};
        const int length = MultiByteToWideChar(
            CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
        if (length <= 0) return {};

        std::wstring result(static_cast<size_t>(length), L'\0');
        if (MultiByteToWideChar(
            CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), static_cast<int>(utf8.size()), result.data(), length) == 0)
        {
            return {};
        }
        return result;
    }

    inline bool Utf8EqualsIgnoreCase(const std::string& left, const std::string& right)
    {
        const std::wstring wideLeft = Utf8ToWide(left);
        const std::wstring wideRight = Utf8ToWide(right);
        if ((wideLeft.empty() && !left.empty()) || (wideRight.empty() && !right.empty()))
        {
            return left == right;
        }
        return CompareStringOrdinal(wideLeft.c_str(), -1, wideRight.c_str(), -1, TRUE) == CSTR_EQUAL;
    }
}
