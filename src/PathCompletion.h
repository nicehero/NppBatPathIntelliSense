// 路径补全的核心逻辑。
//
// 本模块是 VS Code 扩展 bat-path-intellisense 中 src/extension.ts 的
// 直接移植：路径提取 -> 基准目录解析 -> 目录枚举 -> 前缀过滤 -> 生成候选。
//
// 对应关系：
//   extension.ts: extractPath()              -> ExtractPathPrefix()
//   extension.ts: resolvePath()              -> ResolveContext()
//   extension.ts: splitDirectoryAndFilter()  -> SplitDirectoryAndFilter()（本文件内部）
//   extension.ts: fs.readdirSync + 过滤      -> EnumerateEntries()

#pragma once

#include <string>
#include <vector>

// 从光标前的整行文本中提取“可能正在输入的路径”。
//
// 支持的形式与扩展一致：
//
//   copy .\             copy ..\abc\
//   xcopy /E .\assets\  call "C:\tools\
//   del D:\temp\        dir \\nas\share\
//
// 提取不到时返回 false（此时调用方应当取消已弹出的候选列表）。
bool ExtractPathPrefix(const std::wstring& textBeforeCaret, std::wstring& outPath);

// 把 BAT 中的路径拆成「要扫描的目录」和「正在输入的文件名前缀」
struct CompletionContext
{
    std::wstring directory;  // 要扫描的真实目录，通常以反斜杠结尾
    std::wstring filter;     // 文件名前缀，可为空
};

// 以 baseDirectory 为基准解析 inputPath。
//
// 基准目录固定为当前 .bat 脚本自身所在目录，与扩展的行为一致；
// 不使用工作区根目录，也不使用终端当前目录。
bool ResolveContext(
    const std::wstring& baseDirectory,
    const std::wstring& inputPath,
    CompletionContext& out);

struct PathEntry
{
    std::wstring name;
    bool isDirectory = false;
};

// 扫描 ctx.directory，按 ctx.filter 做前缀过滤（忽略大小写）。
//
// 目录排在文件前面，同类之间不分大小写按名称排序。
// 目录本身不存在或无权限时返回 false。
bool EnumerateEntries(
    const CompletionContext& ctx,
    bool showFiles,
    bool showDirectories,
    std::vector<PathEntry>& out);

// 拼成 Scintilla 的自动补全列表串：条目之间用 '\n' 分隔。
//
// 目录条目带结尾反斜杠，选中后光标停在其后，可继续输入下一级。
// 条目中的 '?' 之后是「类型」，只用于显示，不会被插入正文。
std::string BuildAutoCompleteList(const std::vector<PathEntry>& entries);
