# 开发与发布

面向本仓库维护者的文档，不涉及插件使用者。使用说明见 [README](README.md)。

## 构建环境

- Visual Studio 的 C++ 工具链（本仓库在 VS2015 Update 3 / MSVC 19.00.24210 x64 上验证过）
- Windows SDK 的 `rc.exe` —— 版本资源需要它，`vcvarsall.bat` 会把它放进 PATH

```bat
build.bat        :: 编译出 build\BatPathIntelliSense.dll
test.bat         :: 编译并运行核心逻辑的独立测试
```

## 已踩过的坑

以下几个坑都是搭建时真实踩到的，脚本里已经绕过。**改动构建脚本时请保留这些处理。**

### 1. PATH 里 MinGW 排在 MSVC 前面

`F:\mingw64\bin` 排在 `F:\vs2015\VC\bin\amd64` 之前，而 MinGW 带了一个同名 `link.exe`
——那是 GNU coreutils 的**硬链接**工具，不是链接器。`cl` 编译完按 PATH 去找链接器时会命中它，
把 MSVC 的链接参数喂进去，结果是刷屏的 `cl: 命令行 error D8000` 加上退出码 `0xC0000005` 崩溃。

注意 `where link` 有时会显示 MSVC 的排在前面，但实际仍会命中 MinGW 的——别用 `where` 的输出下结论。

处理：把 MSVC 的 bin 目录提到 PATH 最前，并且用**绝对路径**调用链接器。

### 2. 一次 `cl` 调用传多个源文件会崩

同样报 `0xC0000005`。已二分确认与 `/Fo` 的形式、`/Fd`、PATH 顺序都无关，
只与"一次传几个文件"有关。逐个文件单独调用则正常。

处理：一个文件编译一次，`.obj` 攒齐后单独跑一次链接。

### 3. 中文注释必须配 `/utf-8`

MSVC 默认按系统代码页（936）读源码，而源文件是 UTF-8。不加 `/utf-8` 时，
带中文注释的三个文件全部编译失败，而不含中文注释的 `Utf8.cpp` 正常
——这个对比很容易让人误判成代码问题。

同理，`.bat` 和 `.ps1` 文件本身只能写 ASCII：cmd 按 OEM 代码页读批处理，
Windows PowerShell 5.1 按 ANSI 代码页读脚本（后者会让 here-string 边界失配）。

### 另一个容易误判的点

cmd 的 `if errorlevel N` 是**有符号**比较，而崩溃的工具返回的是负数退出码，
会被判成"成功"。所以构建脚本最后直接检查产物文件是否存在，而不只看退出码。

## 头文件来源

`sdk/` 下的头文件取自 [npp-plugins/plugintemplate](https://github.com/npp-plugins/plugintemplate)。

`SCNotification` 的字段布局在 Scintilla 各版本间变过，**不能凭记忆手写**：
Notepad++ 8.6.4 用的 `Sci_Position position` 是 8 字节（`ptrdiff_t`），
所以 `ch` 在 x64 下位于**偏移 32** 而不是 24。写错的话 `SCN_CHARADDED` 读到的
会是位置值而不是字符。

已核对过：plugintemplate 的 `Scintilla.h` 与 Notepad++ `v8.6.4` 标签下自带的
`scintilla/include/Scintilla.h` 字段序列完全一致。

## 发布到「插件管理」

Notepad++ 没有 VS Code `vsce publish` 那样的一键上传。机制是：Notepad++ 的插件管理
从 [notepad-plus-plus/nppPluginList](https://github.com/notepad-plus-plus/nppPluginList)
仓库读一份清单（JSON 封装成签名 DLL 分发），清单里的 `repository` 字段直接指向
GitHub Release 上的 zip。

三份清单互相独立：`pl.x86.json` / `pl.x64.json` / `pl.arm64.json`。
**只发 x64 完全可以**，现有清单里就有多个插件只存在于 x64。
CI 对每个架构分别跑 `validator.py <arch>`，`Win32` 那一跑只会校验 `pl.x86.json`，
不会要求本插件有 32 位版本。

### 打包

身份信息（`GitHubOwner` / `RepoName` / `Author`）在 `tools/package.ps1` 顶部，本仓库已填好。

```bat
package.bat
```

它会：编译 → 打包 zip → 算出 SHA-256 → 打印可直接粘贴的 JSON 条目 →
跑一遍 `tools/preflight.py` 模拟上游校验。产物：

- `build\BatPathIntelliSense_x64.zip` —— 要上传到 GitHub Release 的那个
- `build\pl.x64.entry.json` —— 要加进 `pl.x64.json` 的那一条

### 版本号

版本号只在 **`src/BatPathIntelliSense.rc`** 里写一次，打包脚本从编好的 DLL 里反读，
所以 JSON 条目和二进制不可能不一致。改版本改那里即可。

### 上游 CI 的硬性要求

以下是从 `nppPluginList` 的 `validator.py` 源码里读出来的：

| 要求 | 说明 |
| --- | --- |
| `id` | **zip 文件**的 SHA-256。每次重新打包都会变（zip 内含时间戳），所以别在生成条目后又重新打包 |
| zip 结构 | `<folder-name>.dll` 必须在 **zip 根目录**。套一层文件夹会校验失败，即使文件存在 |
| 版本资源 | DLL 必须带 VERSIONINFO |
| 版本一致性 | DLL 的 4 段 FILEVERSION 必须等于 JSON `version` 补齐到 4 段。即 JSON `"0.1.0"` ↔ DLL `0.1.0.0` |
| JSON Schema | 还有 `pl.schema` 一道结构校验（字段类型、`version` 与 `id` 的正则等） |
| 唯一性 | `folder-name`、`display-name`、`repository` 不能与现有条目重复 |

### 发布步骤

1. 把 `BatPathIntelliSense_x64.zip` 传成 GitHub Release 资源，tag 为 `v<版本号>`
   （与条目里 `repository` 的 URL 对应）。**确认那个 URL 直接返回 zip 而不是 HTML 页面**
   ——URL 错了的话校验器报的是"不是合法 zip"，误导性很强。
2. fork `notepad-plus-plus/nppPluginList`，把条目加进 `src/pl.x64.json` 的 `npp-plugins` 数组，提 PR。
   条目按字母序插在对应位置（清单整体并非严格排序，但新增条目都按字母序放）。
3. CI 需要维护者批准后才会运行（fork PR 的默认行为）。

调试时可以用上游自己的校验器，把清单临时缩成单条目避免下载全部插件：

```bash
python3 validator.py x64
```

**发布新版本时**：改 `.rc` 里的版本 → `package.bat` → 传新 Release →
更新清单里的 `version`、`repository`、`id` 三个字段 → 再提一次 PR。
