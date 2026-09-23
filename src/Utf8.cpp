#include "Utf8.h"

#include <windows.h>

std::wstring Utf8ToWide(const char* utf8, int byteLen)
{
    if (utf8 == nullptr || byteLen <= 0)
    {
        return std::wstring();
    }

    const int wideLen = MultiByteToWideChar(
        CP_UTF8, 0, utf8, byteLen, nullptr, 0);

    if (wideLen <= 0)
    {
        return std::wstring();
    }

    std::wstring result(static_cast<size_t>(wideLen), L'\0');

    MultiByteToWideChar(
        CP_UTF8, 0, utf8, byteLen, &result[0], wideLen);

    return result;
}

std::string WideToUtf8(const std::wstring& wide)
{
    if (wide.empty())
    {
        return std::string();
    }

    const int byteLen = WideCharToMultiByte(
        CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()),
        nullptr, 0, nullptr, nullptr);

    if (byteLen <= 0)
    {
        return std::string();
    }

    std::string result(static_cast<size_t>(byteLen), '\0');

    WideCharToMultiByte(
        CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()),
        &result[0], byteLen, nullptr, nullptr);

    return result;
}
