#include "Options.h"

#include <windows.h>
#include <string>

#include "Notepad_plus_msgs.h"
#include "Plugin.h"
#include "PluginInterface.h"

namespace
{
    const wchar_t kIniFileName[] = L"BatPathIntelliSense.ini";

    const wchar_t kSection[] = L"Options";

    const wchar_t kKeyAutoTrigger[] = L"autoTrigger";
    const wchar_t kKeyShowFiles[] = L"showFiles";
    const wchar_t kKeyShowDirectories[] = L"showDirectories";

    // NPPM_GETPLUGINSCONFIGDIR 的路径在同一会话内不会变，缓存一次即可
    std::wstring g_configFilePath;
    bool g_configPathResolved = false;

    bool ReadBool(const wchar_t* key, bool defaultValue)
    {
        if (g_configFilePath.empty())
        {
            return defaultValue;
        }

        const UINT value = GetPrivateProfileIntW(
            kSection, key, defaultValue ? 1 : 0, g_configFilePath.c_str());

        return value != 0;
    }

    void WriteBool(const wchar_t* key, bool value)
    {
        if (g_configFilePath.empty())
        {
            return;
        }

        WritePrivateProfileStringW(
            kSection, key, value ? L"1" : L"0", g_configFilePath.c_str());
    }
}

std::wstring GetConfigFilePath()
{
    if (g_configPathResolved)
    {
        return g_configFilePath;
    }

    g_configPathResolved = true;

    if (g_nppHandle == nullptr)
    {
        return g_configFilePath;
    }

    // 两次调用：先问长度，再取内容
    const int needed = static_cast<int>(::SendMessage(
        g_nppHandle, NPPM_GETPLUGINSCONFIGDIR, 0, 0));

    if (needed <= 0)
    {
        return g_configFilePath;
    }

    std::wstring directory(static_cast<size_t>(needed) + 1, L'\0');

    const LRESULT ok = ::SendMessage(
        g_nppHandle,
        NPPM_GETPLUGINSCONFIGDIR,
        static_cast<WPARAM>(directory.size()),
        reinterpret_cast<LPARAM>(&directory[0]));

    if (!ok)
    {
        g_configFilePath.clear();
        return g_configFilePath;
    }

    directory.resize(wcslen(directory.c_str()));

    if (directory.empty())
    {
        return g_configFilePath;
    }

    if (directory.back() != L'\\' && directory.back() != L'/')
    {
        directory += L'\\';
    }

    g_configFilePath = directory + kIniFileName;

    return g_configFilePath;
}

void LoadOptions(Options& options)
{
    GetConfigFilePath();

    options.autoTrigger = ReadBool(kKeyAutoTrigger, options.autoTrigger);
    options.showFiles = ReadBool(kKeyShowFiles, options.showFiles);
    options.showDirectories = ReadBool(kKeyShowDirectories, options.showDirectories);
}

void SaveOptions(const Options& options)
{
    GetConfigFilePath();

    WriteBool(kKeyAutoTrigger, options.autoTrigger);
    WriteBool(kKeyShowFiles, options.showFiles);
    WriteBool(kKeyShowDirectories, options.showDirectories);
}
