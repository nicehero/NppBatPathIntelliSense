// 宽字符 <-> UTF-8 转换。
//
// Notepad++ 自 5.x 起内部固定为 UTF-8（isUnicode() 恒为 TRUE），
// 因此 Scintilla 的缓冲区、SCI_GETCURLINE 的返回值、SCI_AUTOCSHOW 的
// 列表串全部是 UTF-8 字节；而 Win32 文件 API 需要宽字符。两边来回倒。

#pragma once

#include <string>

// utf8 可以不是以 NUL 结尾的（SCI_GETCURLINE 返回的缓冲区就是如此），
// 所以由调用方给出字节数。
std::wstring Utf8ToWide(const char* utf8, int byteLen);

std::string WideToUtf8(const std::wstring& wide);
