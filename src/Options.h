// 插件配置。对应 VS Code 扩展里的三个 batPathIntellisense.* 设置项。
//
// 存在 Notepad++ 的插件配置目录（NPPM_GETPLUGINSCONFIGDIR）下的
// BatPathIntelliSense.ini 里，用 Win32 的 profile API 读写。

#pragma once

#include <string>

struct Options
{
    // 输入路径时自动弹出候选；关闭后只能用菜单命令手动触发
    bool autoTrigger = true;

    // 候选列表中是否显示文件
    bool showFiles = true;

    // 候选列表中是否显示目录
    bool showDirectories = true;
};

// 读取配置。ini 不存在或读取失败时保留默认值。
void LoadOptions(Options& options);

// 写回配置。失败时静默返回。
void SaveOptions(const Options& options);

// 取得 ini 的完整路径（内部会缓存）。取不到时返回空串。
std::wstring GetConfigFilePath();
