// Notepad++ 插件：BAT 路径补全。
//
// 触发时机与 VS Code 扩展一致：在 .bat / .cmd 文件里输入 .\ ..\ C:\ \\srv\share\
// 这类路径时弹出候选列表，目录选中后自动补上结尾反斜杠并继续下一级。
//
// 与 Scintilla 打交道的两个要点：
//
//   1. 补全列表一旦弹出，Scintilla 不会再回调插件，只在内部过滤那份列表。
//      所以每次按键都要自己重新枚举并重发 SCI_AUTOCSHOW，这样才对得上
//      扩展里「每次输入都重新扫描目录」的行为。
//
//   2. SCN_AUTOCSELECTION 是在 Scintilla 把选中文本插入文档 *之前* 发出的。
//      「选完目录立刻弹下一级」必须等插入完成，因此走延迟消息绕开通知栈。

#include <windows.h>

#include <string>
#include <vector>

#include "Notepad_plus_msgs.h"
#include "Options.h"
#include "PathCompletion.h"
#include "Plugin.h"
#include "PluginInterface.h"
#include "Scintilla.h"
#include "Utf8.h"

// ---------------------------------------------------------------
// 全局状态
// ---------------------------------------------------------------

HWND g_nppHandle = nullptr;

namespace
{
    NppData g_nppData = {};

    HINSTANCE g_hInstance = nullptr;

    Options g_options;

    bool g_optionsLoaded = false;

    // 当前是否有一份由本插件弹出的候选列表。
    // 用来决定「按键是否值得重新算一遍路径」。
    bool g_listActive = false;

    const wchar_t kPluginName[] = L"BatPathIntelliSense";

    // 单行参与正则的长度上限（UTF-8 字节）。超长行只看尾部，
    // 因为路径只可能出现在光标附近。
    const int kMaxLineTailBytes = 4096;

    // -----------------------------------------------------------
    // 取当前编辑器
    // -----------------------------------------------------------

    HWND CurrentScintilla()
    {
        if (g_nppHandle == nullptr)
        {
            return nullptr;
        }

        int view = 0;

        ::SendMessage(
            g_nppHandle, NPPM_GETCURRENTSCINTILLA, 0,
            reinterpret_cast<LPARAM>(&view));

        return (view == 1)
            ? g_nppData._scintillaSecondHandle
            : g_nppData._scintillaMainHandle;
    }

    // -----------------------------------------------------------
    // 读取文档状态
    // -----------------------------------------------------------

    // 当前文件的完整路径。未保存的缓冲区返回空串。
    std::wstring CurrentFilePath()
    {
        std::vector<wchar_t> buffer(32768, L'\0');

        const LRESULT ok = ::SendMessage(
            g_nppHandle,
            NPPM_GETFULLCURRENTPATH,
            static_cast<WPARAM>(buffer.size()),
            reinterpret_cast<LPARAM>(&buffer[0]));

        if (!ok)
        {
            return std::wstring();
        }

        return std::wstring(&buffer[0]);
    }

    // 相对路径一律以脚本自身所在目录为基准，
    // 不是工作区根目录，也不是终端的当前目录。
    std::wstring CurrentFileDirectory(const std::wstring& fullPath)
    {
        const size_t separator = fullPath.find_last_of(L"\\/");

        if (separator == std::wstring::npos)
        {
            return std::wstring();
        }

        return fullPath.substr(0, separator);
    }

    bool IsBatchFile(const std::wstring& fullPath)
    {
        const size_t dot = fullPath.find_last_of(L'.');

        if (dot == std::wstring::npos)
        {
            return false;
        }

        const std::wstring extension = fullPath.substr(dot + 1);

        return _wcsicmp(extension.c_str(), L"bat") == 0
            || _wcsicmp(extension.c_str(), L"cmd") == 0;
    }

    // 光标当前所在行、光标之前的文本。
    std::wstring LineBeforeCaret(HWND scintilla)
    {
        std::vector<char> buffer(256, '\0');
        int length = 0;

        for (;;)
        {
            length = static_cast<int>(::SendMessage(
                scintilla,
                SCI_GETCURLINE,
                static_cast<WPARAM>(buffer.size()),
                reinterpret_cast<LPARAM>(&buffer[0])));

            if (length < static_cast<int>(buffer.size()))
            {
                break;
            }

            buffer.resize(static_cast<size_t>(length) + 1, '\0');
        }

        if (length <= 0)
        {
            return std::wstring();
        }

        const char* start = &buffer[0];
        int usable = length;

        if (usable > kMaxLineTailBytes)
        {
            start += usable - kMaxLineTailBytes;
            usable = kMaxLineTailBytes;

            // 别把多字节字符切一半
            while (usable > 0 && (static_cast<unsigned char>(*start) & 0xC0) == 0x80)
            {
                ++start;
                --usable;
            }
        }

        return Utf8ToWide(start, usable);
    }

    // -----------------------------------------------------------
    // 补全列表的显示与取消
    // -----------------------------------------------------------

    void CancelCompletion(HWND scintilla)
    {
        ::SendMessage(scintilla, SCI_AUTOCCANCEL, 0, 0);
        g_listActive = false;
    }

    void ShowCompletion(HWND scintilla, const std::wstring& filter, const std::string& list)
    {
        // 条目之间用 '\n' 分隔；'?' 之后是显示用的类型，不会被插入正文
        ::SendMessage(scintilla, SCI_AUTOCSETSEPARATOR, '\n', 0);
        ::SendMessage(scintilla, SCI_AUTOCSETTYPESEPARATOR, '?', 0);

        // Windows 文件名大小写不敏感，让 Scintilla 的匹配也如此
        ::SendMessage(scintilla, SCI_AUTOCSETIGNORECASE, TRUE, 0);
        ::SendMessage(
            scintilla,
            SCI_AUTOCSETCASEINSENSITIVEBEHAVIOUR,
            SC_CASEINSENSITIVEBEHAVIOUR_IGNORECASE,
            0);

        ::SendMessage(scintilla, SCI_AUTOCSETAUTOHIDE, TRUE, 0);

        // lenEntered 是「选中条目时要被替换掉的字符数」。
        // Scintilla 的位置是 UTF-8 字节偏移，不是字符数。
        const int lenEntered = static_cast<int>(WideToUtf8(filter).size());

        ::SendMessage(
            scintilla,
            SCI_AUTOCSHOW,
            static_cast<WPARAM>(lenEntered),
            reinterpret_cast<LPARAM>(list.c_str()));

        g_listActive = true;
    }

    // -----------------------------------------------------------
    // 延迟消息用的隐藏窗口
    // -----------------------------------------------------------

    const wchar_t kMessageWindowClass[] = L"BatPathIntelliSense.MessageWindow";

    const UINT kMsgDeferredRefresh = WM_APP + 1;

    HWND g_messageWindow = nullptr;

    void RefreshCompletion(HWND scintilla);

    LRESULT CALLBACK MessageWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        if (message == kMsgDeferredRefresh)
        {
            HWND scintilla = CurrentScintilla();

            if (scintilla != nullptr)
            {
                RefreshCompletion(scintilla);
            }

            return 0;
        }

        return DefWindowProcW(window, message, wParam, lParam);
    }

    void CreateMessageWindow()
    {
        WNDCLASSEXW windowClass = {};

        windowClass.cbSize = sizeof(windowClass);
        windowClass.lpfnWndProc = MessageWindowProc;
        windowClass.hInstance = g_hInstance;
        windowClass.lpszClassName = kMessageWindowClass;

        RegisterClassExW(&windowClass);

        g_messageWindow = CreateWindowExW(
            0,
            kMessageWindowClass,
            L"",
            0,
            0, 0, 0, 0,
            HWND_MESSAGE,
            nullptr,
            g_hInstance,
            nullptr);
    }

    void EnsureInitialized()
    {
        if (!g_optionsLoaded)
        {
            LoadOptions(g_options);
            g_optionsLoaded = true;
        }

        if (g_messageWindow == nullptr)
        {
            CreateMessageWindow();
        }
    }

    // -----------------------------------------------------------
    // 核心：重新计算并更新候选列表
    // -----------------------------------------------------------

    void RefreshCompletion(HWND scintilla)
    {
        if (scintilla == nullptr)
        {
            return;
        }

        const std::wstring fullPath = CurrentFilePath();

        // 未保存的缓冲区没有磁盘路径，无从解析相对路径 —— 与扩展一致
        if (fullPath.empty() || !IsBatchFile(fullPath))
        {
            if (g_listActive)
            {
                CancelCompletion(scintilla);
            }

            return;
        }

        std::wstring inputPath;

        if (!ExtractPathPrefix(LineBeforeCaret(scintilla), inputPath))
        {
            if (g_listActive)
            {
                CancelCompletion(scintilla);
            }

            return;
        }

        CompletionContext context;

        if (!ResolveContext(CurrentFileDirectory(fullPath), inputPath, context))
        {
            if (g_listActive)
            {
                CancelCompletion(scintilla);
            }

            return;
        }

        std::vector<PathEntry> entries;

        if (!EnumerateEntries(
                context,
                g_options.showFiles,
                g_options.showDirectories,
                entries)
            || entries.empty())
        {
            if (g_listActive)
            {
                CancelCompletion(scintilla);
            }

            return;
        }

        ShowCompletion(scintilla, context.filter, BuildAutoCompleteList(entries));
    }

    // SCN_AUTOCSELECTION 的 text 可能带 "?类型" 后缀，
    // 要去掉后缀再看最后一个字符是不是路径分隔符。
    bool SelectionIsDirectory(const char* text)
    {
        if (text == nullptr || *text == '\0')
        {
            return false;
        }

        const std::string selection(text);

        const size_t typeSeparator = selection.find('?');

        const size_t end = (typeSeparator == std::string::npos)
            ? selection.size()
            : typeSeparator;

        if (end == 0)
        {
            return false;
        }

        const char last = selection[end - 1];

        return last == '\\' || last == '/';
    }

    // -----------------------------------------------------------
    // 菜单命令
    // -----------------------------------------------------------

    // Ctrl+Alt+Space：手动唤出候选列表
    ShortcutKey g_shortcutManual = {};

    const size_t kCommandCount = 4;

    FuncItem g_funcItems[kCommandCount] = {};

    void SetFuncItem(
        size_t index,
        const wchar_t* name,
        PFUNCPLUGINCMD function,
        ShortcutKey* shortcut)
    {
        wcsncpy_s(g_funcItems[index]._itemName, menuItemSize, name, _TRUNCATE);

        g_funcItems[index]._pFunc = function;
        g_funcItems[index]._cmdID = static_cast<int>(index);
        g_funcItems[index]._init2Check = false;
        g_funcItems[index]._pShKey = shortcut;
    }

    void SyncMenuChecks()
    {
        ::SendMessage(
            g_nppHandle, NPPM_SETMENUITEMCHECK, 1, g_options.autoTrigger ? TRUE : FALSE);
        ::SendMessage(
            g_nppHandle, NPPM_SETMENUITEMCHECK, 2, g_options.showFiles ? TRUE : FALSE);
        ::SendMessage(
            g_nppHandle, NPPM_SETMENUITEMCHECK, 3, g_options.showDirectories ? TRUE : FALSE);
    }

    void CommandCompleteNow()
    {
        EnsureInitialized();
        RefreshCompletion(CurrentScintilla());
    }

    void CommandToggleAutoTrigger()
    {
        EnsureInitialized();

        g_options.autoTrigger = !g_options.autoTrigger;

        SaveOptions(g_options);
        SyncMenuChecks();
    }

    void CommandToggleShowFiles()
    {
        EnsureInitialized();

        g_options.showFiles = !g_options.showFiles;

        SaveOptions(g_options);
        SyncMenuChecks();
    }

    void CommandToggleShowDirectories()
    {
        EnsureInitialized();

        g_options.showDirectories = !g_options.showDirectories;

        SaveOptions(g_options);
        SyncMenuChecks();
    }
}

// ---------------------------------------------------------------
// Notepad++ 要求的导出函数
// ---------------------------------------------------------------

extern "C" __declspec(dllexport) void setInfo(NppData notepadPlusData)
{
    g_nppData = notepadPlusData;
    g_nppHandle = notepadPlusData._nppHandle;
}

extern "C" __declspec(dllexport) const wchar_t* getName()
{
    return kPluginName;
}

extern "C" __declspec(dllexport) FuncItem* getFuncsArray(int* nbF)
{
    g_shortcutManual._isCtrl = true;
    g_shortcutManual._isAlt = true;
    g_shortcutManual._isShift = false;
    g_shortcutManual._key = VK_SPACE;

    SetFuncItem(0, L"Complete path here", CommandCompleteNow, &g_shortcutManual);

    SetFuncItem(1, L"Auto-trigger completion", CommandToggleAutoTrigger, nullptr);

    SetFuncItem(2, L"Show files", CommandToggleShowFiles, nullptr);

    SetFuncItem(3, L"Show directories", CommandToggleShowDirectories, nullptr);

    *nbF = static_cast<int>(kCommandCount);

    return g_funcItems;
}

extern "C" __declspec(dllexport) void beNotified(SCNotification* notifyCode)
{
    if (notifyCode == nullptr)
    {
        return;
    }

    switch (notifyCode->nmhdr.code)
    {
    case NPPN_READY:
        EnsureInitialized();
        SyncMenuChecks();
        break;

    case NPPN_SHUTDOWN:
        if (g_messageWindow != nullptr)
        {
            DestroyWindow(g_messageWindow);
            g_messageWindow = nullptr;
        }
        break;

    case SCN_CHARADDED:
        // 自动触发关闭时，只有菜单命令能唤出候选列表
        if (!g_options.autoTrigger && !g_listActive)
        {
            break;
        }

        // 路径可能从任一个字符开始变化，但没必要每个字符都重算；
        // 只在「路径触发字符」或「候选列表正开着」时才动手。
        if (notifyCode->ch == '\\' || notifyCode->ch == '/' ||
            notifyCode->ch == '.'  || notifyCode->ch == '"' ||
            notifyCode->ch == ' '  || g_listActive)
        {
            RefreshCompletion(CurrentScintilla());
        }
        break;

    case SCN_AUTOCSELECTION:
        // 此通知早于文本插入，所以这里只做标记，稍后再重查
        g_listActive = false;

        if (SelectionIsDirectory(notifyCode->text))
        {
            EnsureInitialized();

            if (g_messageWindow != nullptr)
            {
                ::PostMessage(g_messageWindow, kMsgDeferredRefresh, 0, 0);
            }
        }
        break;

    case SCN_AUTOCCANCELLED:
        g_listActive = false;
        break;

    default:
        break;
    }
}

extern "C" __declspec(dllexport) LRESULT messageProc(UINT, WPARAM, LPARAM)
{
    return TRUE;
}

extern "C" __declspec(dllexport) BOOL isUnicode()
{
    return TRUE;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        g_hInstance = instance;
    }

    return TRUE;
}
