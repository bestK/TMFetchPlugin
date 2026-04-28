#pragma once
#include <string>

namespace strutil {

std::wstring Utf8ToWide(const std::string& s);
std::string  WideToUtf8(const std::wstring& s);
std::string  WideToAnsi(const std::wstring& s);
std::wstring AnsiToWide(const std::string& s);

std::wstring Trim(const std::wstring& s);
std::string  Trim(const std::string& s);

bool IEquals(const std::wstring& a, const std::wstring& b);

} // namespace strutil
