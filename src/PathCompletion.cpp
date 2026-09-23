#include "PathCompletion.h"

#include <windows.h>

#include <algorithm>
#include <regex>

#include "Utf8.h"

namespace
{
    // ---------------------------------------------------------------
    // 小工具
    // ---------------------------------------------------------------

    bool IsSeparator(wchar_t c)
    {
        return c == L'\\' || c == L'/';
    }

    void EnsureTrailingBackslash(std::wstring& path)
    {
        if (!path.empty() && !IsSeparator(path.back()))
        {
            path += L'\\';
        }
    }

    bool IsExistingDirectory(const std::wstring& path)
    {
        if (path.empty())
        {
            return false;
        }

        const DWORD attributes = GetFileAttributesW(path.c_str());

        if (attributes == INVALID_FILE_ATTRIBUTES)
        {
            return false;
        }

        return (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    }

    bool StartsWithNoCase(const std::wstring& text, const std::wstring& prefix)
    {
        if (text.size() < prefix.size())
        {
            return false;
        }

        if (prefix.empty())
        {
            return true;
        }

        return _wcsnicmp(text.c_str(), prefix.c_str(), prefix.size()) == 0;
    }

    // 把输入路径里的 '/' 统一成 '\'。BAT 两种都认，但 Windows 的文件 API 只认后者。
    std::wstring NormalizeSeparators(const std::wstring& input)
    {
        std::wstring result = input;

        for (size_t i = 0; i < result.size(); ++i)
        {
            if (result[i] == L'/')
            {
                result[i] = L'\\';
            }
        }

        return result;
    }

    bool IsAbsolutePath(const std::wstring& normalized)
    {
        // C:\...
        if (normalized.size() >= 3 &&
            iswalpha(normalized[0]) &&
            normalized[1] == L':' &&
            normalized[2] == L'\\')
        {
            return true;
        }

        // \\server\share\...
        if (normalized.size() >= 2 &&
            normalized[0] == L'\\' &&
            normalized[1] == L'\\')
        {
            return true;
        }

        return false;
    }

    // 相对于 baseDirectory 解析 rel，并消掉其中的 '.' 与 '..'。
    //
    // GetFullPathNameW 正好满足需要：它做规范化，但不要求路径真实存在
    // （这与 Node 的 path.resolve 语义一致，也是扩展依赖的行为）。
    std::wstring ResolveRelative(const std::wstring& baseDirectory, const std::wstring& rel)
    {
        std::wstring combined = baseDirectory;
        EnsureTrailingBackslash(combined);
        combined += rel;

        std::vector<wchar_t> buffer(MAX_PATH);

        for (;;)
        {
            const DWORD written = GetFullPathNameW(
                combined.c_str(),
                static_cast<DWORD>(buffer.size()),
                &buffer[0],
                nullptr);

            if (written == 0)
            {
                return combined;
            }

            // 返回值大于缓冲区说明缓冲区不够，此时返回值是所需长度
            if (written < buffer.size())
            {
                return std::wstring(&buffer[0], written);
            }

            buffer.resize(static_cast<size_t>(written) + 1);
        }
    }

    // 将 C:\project\assets\abc 拆成
    //   directory = C:\project\assets\
    //   filter    = abc
    void SplitDirectoryAndFilter(const std::wstring& value, CompletionContext& out)
    {
        if (value.empty())
        {
            out.directory.clear();
            out.filter.clear();
            return;
        }

        // 结尾的反斜杠表示「正在查看这个目录」，此时没有过滤前缀
        if (IsSeparator(value.back()))
        {
            out.directory = value;
            out.filter.clear();
            return;
        }

        const size_t separator = value.find_last_of(L"\\/");

        if (separator == std::wstring::npos)
        {
            out.directory.clear();
            out.filter = value;
            return;
        }

        out.directory = value.substr(0, separator + 1);
        out.filter = value.substr(separator + 1);
    }

    const std::wregex& PathPattern()
    {
        // 与 extension.ts 中 extractPath() 的正则等价：
        //
        //   /(?:^|\s|["'])((?:\.\.?[\\/]|[A-Za-z]:[\\/]|[^"' \t]+[\\/])[^"' \t]*)$/
        //
        // 取最后一个空白或引号之后、且一直延伸到行尾的那段，作为候选路径。
        static const std::wregex pattern(
            L"(?:^|\\s|[\"'])((?:\\.\\.?[\\\\/]|[A-Za-z]:[\\\\/]|[^\"' \\t]+[\\\\/])[^\"' \\t]*)$",
            std::regex::ECMAScript);

        return pattern;
    }
}

bool ExtractPathPrefix(const std::wstring& textBeforeCaret, std::wstring& outPath)
{
    outPath.clear();

    if (textBeforeCaret.empty())
    {
        return false;
    }

    std::wsmatch match;

    if (!std::regex_search(textBeforeCaret, match, PathPattern()))
    {
        return false;
    }

    // '$' 已经保证匹配贴着行尾，这里再确认一次，避免某些实现对
    // 行尾换行的宽松处理带来意外结果
    if (match[0].second != textBeforeCaret.end())
    {
        return false;
    }

    outPath = match[1].str();

    return !outPath.empty();
}

bool ResolveContext(
    const std::wstring& baseDirectory,
    const std::wstring& inputPath,
    CompletionContext& out)
{
    out.directory.clear();
    out.filter.clear();

    if (inputPath.empty())
    {
        return false;
    }

    const std::wstring normalized = NormalizeSeparators(inputPath);

    // 绝对路径与 UNC 路径：不需要基准目录
    if (IsAbsolutePath(normalized))
    {
        SplitDirectoryAndFilter(normalized, out);
        return !out.directory.empty() || !out.filter.empty();
    }

    if (baseDirectory.empty())
    {
        return false;
    }

    const std::wstring absolute = ResolveRelative(baseDirectory, normalized);

    // 已经输入到一个真实存在的目录（.\assets\）时，直接扫描它
    if (IsSeparator(normalized.back()))
    {
        std::wstring directory = absolute;
        EnsureTrailingBackslash(directory);

        if (IsExistingDirectory(directory))
        {
            out.directory = directory;
            out.filter.clear();
            return true;
        }
    }

    // 否则把最后一段当作过滤前缀
    SplitDirectoryAndFilter(absolute, out);

    return !out.directory.empty() || !out.filter.empty();
}

bool EnumerateEntries(
    const CompletionContext& ctx,
    bool showFiles,
    bool showDirectories,
    std::vector<PathEntry>& out)
{
    out.clear();

    if (ctx.directory.empty())
    {
        return false;
    }

    std::wstring directory = ctx.directory;
    EnsureTrailingBackslash(directory);

    // Windows 文件名本身不可能含 '*' 或 '?'，但用户键入的前缀里可能有。
    // 那样 FindFirstFileW 会把它当通配符解释，于是退化成「全量枚举 + 手工比对」。
    const bool filterHasWildcard =
        ctx.filter.find_first_of(L"*?") != std::wstring::npos;

    const std::wstring pattern = filterHasWildcard
        ? directory + L"*"
        : directory + ctx.filter + L"*";

    WIN32_FIND_DATAW findData;
    const HANDLE handle = FindFirstFileW(pattern.c_str(), &findData);

    if (handle == INVALID_HANDLE_VALUE)
    {
        // 目录不存在、没有权限、或是离线的网络路径。
        // 与扩展一致：静默返回空，不打扰用户。
        return false;
    }

    do
    {
        const std::wstring name = findData.cFileName;

        if (name == L"." || name == L"..")
        {
            continue;
        }

        const bool isDirectory =
            (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;

        if (isDirectory && !showDirectories)
        {
            continue;
        }

        if (!isDirectory && !showFiles)
        {
            continue;
        }

        // 只有退化成全量枚举时才需要手工做前缀比对；
        // 走通配符路径时系统已经按前缀过滤过了，且同样忽略大小写。
        if (filterHasWildcard && !StartsWithNoCase(name, ctx.filter))
        {
            continue;
        }

        PathEntry entry;
        entry.name = name;
        entry.isDirectory = isDirectory;

        out.push_back(entry);

    } while (FindNextFileW(handle, &findData));

    FindClose(handle);

    // 目录排在文件前面，方便逐级深入；同类之间不分大小写按名称排序
    std::sort(out.begin(), out.end(), [](const PathEntry& left, const PathEntry& right)
    {
        if (left.isDirectory != right.isDirectory)
        {
            return left.isDirectory;
        }

        return _wcsicmp(left.name.c_str(), right.name.c_str()) < 0;
    });

    return true;
}

std::string BuildAutoCompleteList(const std::vector<PathEntry>& entries)
{
    std::wstring list;

    for (size_t i = 0; i < entries.size(); ++i)
    {
        if (i > 0)
        {
            list += L'\n';
        }

        list += entries[i].name;

        if (entries[i].isDirectory)
        {
            // 补上结尾的反斜杠，选中后光标停在其后，可直接接着敲下一级
            list += L'\\';
        }

        // '?' 之后的部分是「类型」，只影响显示，不会被插入正文
        list += entries[i].isDirectory ? L"?Directory" : L"?File";
    }

    return WideToUtf8(list);
}
