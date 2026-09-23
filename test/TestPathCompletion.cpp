// 核心逻辑的独立测试。
//
// PathCompletion.cpp 只依赖 windows.h，可以脱离 Notepad++ 单独编译运行，
// 所以路径提取与解析这些最容易出错的移植部分能直接验证。
//
// 用 test.bat 构建运行。

#include <windows.h>

#include <cstdio>
#include <string>
#include <vector>

#include "../src/PathCompletion.h"
#include "../src/Utf8.h"

namespace
{
    int g_passed = 0;
    int g_failed = 0;

    void Report(bool ok, const wchar_t* what, const std::wstring& detail)
    {
        if (ok)
        {
            ++g_passed;
            wprintf(L"  PASS  %ls\n", what);
        }
        else
        {
            ++g_failed;
            wprintf(L"  FAIL  %ls   ->  %ls\n", what, detail.c_str());
        }
    }

    void ExpectExtract(const wchar_t* line, const wchar_t* expected)
    {
        std::wstring actual;
        const bool found = ExtractPathPrefix(line, actual);

        const bool ok = (expected == nullptr)
            ? !found
            : (found && actual == expected);

        std::wstring detail = L"got=";
        detail += found ? actual : L"(none)";

        Report(ok, line, detail);
    }

    void ExpectResolve(
        const std::wstring& base,
        const wchar_t* input,
        const wchar_t* expectedDirectory,
        const wchar_t* expectedFilter)
    {
        CompletionContext context;
        const bool ok = ResolveContext(base, input, context);

        const bool matched = ok
            && context.directory == expectedDirectory
            && context.filter == expectedFilter;

        std::wstring detail = L"dir=";
        detail += context.directory;
        detail += L" filter=";
        detail += context.filter;

        std::wstring label = input;
        Report(matched, label.c_str(), detail);
    }

    void ExpectNames(
        const std::wstring& base,
        const wchar_t* input,
        bool showFiles,
        bool showDirectories,
        const std::vector<std::wstring>& mustContain)
    {
        CompletionContext context;

        if (!ResolveContext(base, input, context))
        {
            Report(false, input, L"resolve failed");
            return;
        }

        std::vector<PathEntry> entries;

        if (!EnumerateEntries(context, showFiles, showDirectories, entries))
        {
            Report(false, input, L"enumerate failed");
            return;
        }

        std::wstring names;

        for (size_t i = 0; i < entries.size(); ++i)
        {
            if (i > 0)
            {
                names += L", ";
            }

            names += entries[i].name;

            if (entries[i].isDirectory)
            {
                names += L"\\";
            }
        }

        bool ok = true;

        for (size_t i = 0; i < mustContain.size(); ++i)
        {
            bool found = false;

            for (size_t j = 0; j < entries.size(); ++j)
            {
                if (entries[j].name == mustContain[i])
                {
                    found = true;
                    break;
                }
            }

            if (!found)
            {
                ok = false;
            }
        }

        std::wstring label = input;
        label += L"  [";
        label += names;
        label += L"]";

        // 过滤关闭时，被排除的类别不该出现
        Report(ok, label.c_str(), L"missing expected entry");

        // 若要求只显示目录，确认结果里没有文件
        if (!showFiles)
        {
            bool hasFile = false;

            for (size_t j = 0; j < entries.size(); ++j)
            {
                if (!entries[j].isDirectory)
                {
                    hasFile = true;
                }
            }

            Report(!hasFile, L"  (no files when showFiles=false)", L"found a file");
        }

        if (!showDirectories)
        {
            bool hasDir = false;

            for (size_t j = 0; j < entries.size(); ++j)
            {
                if (entries[j].isDirectory)
                {
                    hasDir = true;
                }
            }

            Report(!hasDir, L"  (no dirs when showDirectories=false)", L"found a dir");
        }
    }
}

int wmain()
{
    SetConsoleOutputCP(CP_UTF8);

    // 用本仓库自身当夹具目录，它同时含有目录、.cpp/.h 文件和以大写字母开头的名字。
    const std::wstring base = L"D:\\proj\\NppBatPathIntellisense";
    const std::wstring parent = L"D:\\proj";

    wprintf(L"=== ExtractPathPrefix (与 extension.ts 的 extractPath 对照) ===\n");
    ExpectExtract(L"copy .\\", L".\\");
    ExpectExtract(L"xcopy /E /I .\\assets\\", L".\\assets\\");
    ExpectExtract(L"call \"C:\\tools\\", L"C:\\tools\\");
    ExpectExtract(L"del D:\\temp\\", L"D:\\temp\\");
    ExpectExtract(L"dir \\\\nas\\share\\", L"\\\\nas\\share\\");
    ExpectExtract(L"copy ..\\lib\\x", L"..\\lib\\x");
    ExpectExtract(L"copy sub\\", L"sub\\");
    ExpectExtract(L"rem note .\\deep\\path", L".\\deep\\path");

    // 不该触发的
    ExpectExtract(L"echo hello", nullptr);
    ExpectExtract(L"copy .\\ ", nullptr);
    ExpectExtract(L"set X=1", nullptr);
    ExpectExtract(L"", nullptr);

    wprintf(L"\n=== ResolveContext ===\n");
    ExpectResolve(base, L".\\", (base + L"\\").c_str(), L"");
    ExpectResolve(base, L".\\sr", (base + L"\\").c_str(), L"sr");
    ExpectResolve(base, L".\\src\\", (base + L"\\src\\").c_str(), L"");
    ExpectResolve(base, L".\\src\\Pl", (base + L"\\src\\").c_str(), L"Pl");
    ExpectResolve(base, L"..\\", (parent + L"\\").c_str(), L"");
    ExpectResolve(base, L"D:\\temp\\", L"D:\\temp\\", L"");
    ExpectResolve(base, L"D:\\temp\\ab", L"D:\\temp\\", L"ab");

    wprintf(L"\n=== EnumerateEntries ===\n");
    ExpectNames(base, L".\\sr", true, true, { L"src" });
    ExpectNames(base, L".\\", true, true, { L"src", L"sdk", L"build", L"build.bat" });
    ExpectNames(base, L".\\", false, true, { L"src", L"sdk", L"build" });
    ExpectNames(base, L".\\src\\Pl", true, true, { L"Plugin.cpp", L"Plugin.h" });
    ExpectNames(base, L".\\SRC\\", true, true, { L"Plugin.cpp" });  // 大小写不敏感

    wprintf(L"\n=== 结果 ===\n");
    wprintf(L"pass=%d  fail=%d\n", g_passed, g_failed);

    return g_failed == 0 ? 0 : 1;
}
