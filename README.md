# BAT Path IntelliSense for Notepad++

为 Windows 批处理文件（`.bat` / `.cmd`）提供**文件与目录路径自动补全**的 Notepad++ 插件。

移植自 VS Code 扩展 [bat-path-intellisense](https://github.com/nicehero/bat-path-intellisense)
（`D:\proj\bat-path-intellisense`），行为与原扩展保持一致。

写 `copy`、`xcopy`、`del`、`call` 这类命令时，只要输入 `.\`、`..\`、`C:\` 或 `\\server\share\`，
就会自动列出对应目录下的文件和文件夹。

## 功能特性

- **自动补全**：输入路径分隔符后弹出候选列表。
- **多种路径形式**：相对路径（`.\`、`..\`、`sub\`）、绝对路径（`C:\...`）、UNC 网络路径（`\\server\share\...`）。
- **目录优先插入**：补全目录时自动补上结尾的 `\`，可继续敲下一级。
- **逐级下钻**：选中一个目录后立即列出它的内容，不用再敲一个字符。
- **引号内同样生效**：`call "C:\tools\` 也能触发。
- **大小写不敏感**：匹配前缀时忽略大小写，与 Windows 文件系统行为一致。
- **可配置**：可关闭自动触发，或只显示目录 / 只显示文件。

## 使用示例

```bat
@echo off

rem 输入 .\ 后列出当前脚本所在目录的内容
copy .\
rem      ↑ src\  sdk\  readme.txt  ...

rem 逐级深入，目录补全后光标停在反斜杠之后
xcopy /E /I .\src\

rem 引号里的路径一样有补全
call "C:\tools\

rem 绝对路径
del D:\temp\

rem UNC 网络路径
dir \\nas\share\
```

## 构建与安装

需要 Visual Studio 的 C++ 工具链（本机用的是 VS2015 x64，已验证）。

```bat
build.bat        :: 编译出 build\BatPathIntelliSense.dll
install.bat      :: 复制到 Notepad++ 的插件目录（需要管理员权限）
```

`install.bat` 会装到：

```
C:\Program Files\Notepad++\plugins\BatPathIntelliSense\BatPathIntelliSense.dll
```

Notepad++ 7.6+ 按 `<插件目录>\<文件夹名>\<文件夹名>.dll` 的约定加载，
所以**文件夹名、DLL 名和 `getName()` 的返回值三者必须一致**。

装好后启动 Notepad++，在 **插件** 菜单里应能看到 `BatPathIntelliSense`。

> 安装前请先关闭 Notepad++ —— 它在运行时会锁住已加载的 DLL，复制会失败。

## 配置

配置存在 Notepad++ 的插件配置目录下：

```
%APPDATA%\Notepad++\plugins\Config\BatPathIntelliSense.ini
```

也可以在 **插件 → BatPathIntelliSense** 菜单里直接勾选，改动会立刻写回 ini：

| 菜单项 | 默认值 | 说明 |
| --- | --- | --- |
| `Auto-trigger completion` | 开 | 输入路径时自动弹出建议；关闭后只能用 `Complete path here` 手动触发。 |
| `Show files` | 开 | 在建议列表中显示文件。 |
| `Show directories` | 开 | 在建议列表中显示目录。 |

手动触发的默认快捷键是 **Ctrl+Alt+Space**。

## 工作原理

与原扩展的对应关系：

| VS Code 扩展（`src/extension.ts`） | 本插件 |
| --- | --- |
| `registerCompletionItemProvider` + 触发字符 | `beNotified()` 里收 `SCN_CHARADDED` |
| `document.lineAt().text` 取光标前行 | `SCI_GETCURLINE`（直接返回光标前整行） |
| `document.uri.fsPath` → `dirname` | `NPPM_GETFULLCURRENTPATH` 去掉文件名 |
| `fs.readdirSync` + 前缀过滤 | `FindFirstFileW(dir + prefix + "*")` |
| 弹出补全列表 | `SCI_AUTOCSHOW(lenEntered, "a\nb\nc")` |
| `item.detail = "Directory"` | `SCI_AUTOCSETTYPESEPARATOR`，列表项写成 `name\?Directory` |
| 目录 `insertText` 补结尾 `\` | 列表项直接写 `name\` |
| `extractPath()` 正则 | `ExtractPathPrefix()`，同样一条 `std::wregex` |
| `resolvePath()` / `splitDirectoryAndFilter()` | `ResolveContext()` / `SplitDirectoryAndFilter()` |
| 3 个配置项 | ini + 可勾选菜单项 |

### 两处架构差异

**1. Scintilla 的补全列表弹出后不会再回调插件。** 用户继续打字时，Scintilla 只在自己
内部过滤那份已经给出的列表。所以本插件在**每次 `SCN_CHARADDED` 时重新枚举目录并重发
`SCI_AUTOCSHOW`** —— 这正好对上了原扩展「每次输入都重新扫描目录」的行为。

**2. `SCN_AUTOCSELECTION` 早于文本插入。** 该通知是在 Scintilla 把选中文本写进文档
*之前* 发出的，所以「选完目录立刻弹下一级」不能在通知回调里直接做。插件用一个隐藏的
message-only 窗口接收 `PostMessage`，在插入完成后再重查一次。

## 已知限制

与原扩展一致的：

- **不展开变量**：`%TEMP%`、`%USERPROFILE%`、`%~dp0` 等不会展开，这类路径没有补全。
- **不支持 `for` / `set` 变量**：`%%i\`、`%MYDIR%\` 无法解析。
- **只按前缀匹配**：不做模糊匹配；目标目录必须真实存在才会给出建议。
- **基准目录固定**：相对路径基于 `.bat` 文件自身所在目录解析，不是工作区根目录，
  也不是终端当前目录；未保存（无磁盘路径）的文件不触发补全。
- **不做权限与网络探测**：无权限或离线的网络路径会静默返回空结果。

Notepad++ 特有的：

- **粘贴不触发**：Scintilla 只在键入字符时发 `SCN_CHARADDED`，粘贴一段路径不会弹出列表。
- **候选顺序**：目录排在文件前面，同类之间不分大小写按名称排序。
  VS Code 版本由编辑器自己排序，两者顺序不完全相同。
- **需要已保存的 `.bat` / `.cmd`**：其它扩展名的文件不触发。

## 开发

```
src/PathCompletion.cpp   核心逻辑：路径提取 + 目录解析 + 枚举（移植自 extension.ts）
src/Plugin.cpp           导出函数、菜单、通知分发、延迟下钻
src/Options.cpp          ini 配置读写
src/Utf8.cpp             宽字符 <-> UTF-8
sdk/                     Notepad++ 官方插件头文件（见下方来源）
test/TestPathCompletion.cpp   核心逻辑的独立测试
```

核心逻辑只依赖 `windows.h`，可以脱离 Notepad++ 单独测试：

```bat
test.bat
```

## 本机环境的三个坑

搭建过程中真实踩到的，脚本里都已绕过，但值得记下来：

1. **PATH 里 MinGW 排在 MSVC 前面。** `F:\mingw64\bin` 在 `F:\vs2015\VC\bin\amd64`
   之前，而 MinGW 带了一个同名 `link.exe`（GNU coreutils 的**硬链接**工具）。
   `cl` 编译完会按 PATH 去找链接器，于是把 MSVC 的链接参数喂给了它，
   结果是刷屏的 `命令行 error D8000` 加上退出码 `0xC0000005` 崩溃。
   脚本的处理是：把 MSVC 的 bin 目录提到 PATH 最前，并且用**绝对路径**调用链接器。

2. **一次 `cl` 调用传多个源文件会崩。** 同样报 `0xC0000005`。逐个文件单独调用就正常，
   所以 `build.bat` 一个文件编译一次，再单独链接。

3. **源码里的中文注释必须配 `/utf-8`。** MSVC 默认按系统代码页（936）读源码，
   而源文件是 UTF-8，不加 `/utf-8` 时带中文注释的三个文件全部编译失败
   （无注释的 `Utf8.cpp` 则正常）。另外 `.bat` 文件本身只能写 ASCII ——
   cmd 也按 OEM 代码页读取批处理。

另外还有个容易误判的点：cmd 的 `if errorlevel N` 是**有符号**比较，
而崩溃的工具返回的是负数退出码，会被判成"成功"。所以脚本最后直接检查产物文件是否存在。

## 头文件来源

`sdk/` 下的头文件取自 [npp-plugins/plugintemplate](https://github.com/npp-plugins/plugintemplate)。

`SCNotification` 的字段布局在 Scintilla 各版本间变过，**不能凭记忆手写**：
本机 Notepad++ 8.6.4 用的 `Sci_Position position` 是 8 字节（`ptrdiff_t`），
所以 `ch` 在 x64 下位于**偏移 32** 而不是 24。写错的话 `SCN_CHARADDED` 读到的
会是位置值而不是字符。已核对过：plugintemplate 的 `Scintilla.h` 与 Notepad++ `v8.6.4`
标签下自带的 `scintilla/include/Scintilla.h` 字段序列完全一致。

## 许可证

MIT
