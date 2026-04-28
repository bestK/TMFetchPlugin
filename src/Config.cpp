#include "Config.h"
#include "StringUtil.h"
#include <Windows.h>
#include <shlwapi.h>
#include <algorithm>
#include <fstream>
#include <sstream>

#pragma comment(lib, "shlwapi.lib")

namespace config {

namespace {

std::wstring ReadStr(const wchar_t* section, const wchar_t* key,
                     const wchar_t* def, const std::wstring& ini) {
    wchar_t buf[4096];
    DWORD n = GetPrivateProfileStringW(section, key, def, buf, 4096, ini.c_str());
    return std::wstring(buf, n);
}

int ReadInt(const wchar_t* section, const wchar_t* key, int def, const std::wstring& ini) {
    return (int)GetPrivateProfileIntW(section, key, def, ini.c_str());
}

std::wstring EscapeMultiline(const std::wstring& s) {
    std::wstring out;
    out.reserve(s.size());
    for (wchar_t c : s) {
        switch (c) {
        case L'\\': out += L"\\\\"; break;
        case L'\n': out += L"\\n";  break;
        case L'\r': out += L"\\r";  break;
        default: out.push_back(c);
        }
    }
    return out;
}

std::wstring UnescapeMultiline(const std::wstring& s) {
    std::wstring out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == L'\\' && i + 1 < s.size()) {
            switch (s[i+1]) {
            case L'n':  out.push_back(L'\n'); ++i; break;
            case L'r':  out.push_back(L'\r'); ++i; break;
            case L'\\': out.push_back(L'\\'); ++i; break;
            default: out.push_back(s[i]);
            }
        } else {
            out.push_back(s[i]);
        }
    }
    return out;
}

// Headers may be supplied as either:
//   Headers=Key1: V1 || Key2: V2     (separated by " || " or "\n" or ";;")
//   Header1=...
//   Header2=...
std::vector<std::wstring> ReadHeaders(const wchar_t* section, const std::wstring& ini) {
    std::vector<std::wstring> out;

    auto split_blob = [&](const std::wstring& blob) {
        size_t pos = 0;
        while (pos < blob.size()) {
            size_t a = blob.find(L"||", pos);
            size_t b = blob.find(L";;", pos);
            size_t c = blob.find(L'\n', pos);
            size_t hit = std::min({a, b, c});
            std::wstring part = (hit == std::wstring::npos)
                ? blob.substr(pos)
                : blob.substr(pos, hit - pos);
            part = strutil::Trim(part);
            if (!part.empty()) out.push_back(part);
            if (hit == std::wstring::npos) break;
            pos = hit + ((hit == c) ? 1 : 2);
        }
    };

    auto blob = ReadStr(section, L"Headers", L"", ini);
    if (!blob.empty()) split_blob(blob);

    for (int i = 1; i <= 16; ++i) {
        wchar_t key[16]; swprintf(key, 16, L"Header%d", i);
        auto v = ReadStr(section, key, L"", ini);
        v = strutil::Trim(v);
        if (!v.empty()) out.push_back(v);
    }
    return out;
}

} // namespace

std::wstring IniPath(const std::wstring& configDir) {
    std::wstring p = configDir;
    if (!p.empty() && p.back() != L'\\' && p.back() != L'/') p += L'\\';
    p += L"TMFetchPlugin.ini";
    return p;
}

void WriteSample(const std::wstring& iniPath) {
    // Write a UTF-16 LE BOM file so unicode comments/labels are preserved.
    std::wstring sample =
        L"; TMFetchPlugin configuration. Prefer the in-app Options dialog\r\n"
        L"; over hand-editing this file.\r\n"
        L"; In JsonPath, write free text plus '$.path' references.\r\n"
        L"; '$$' emits a literal '$'. '\\n' yields a newline in the output.\r\n"
        L";\r\n"
        L"[Plugin]\r\n"
        L"Interval=10000\r\n"
        L"ItemCount=1\r\n"
        L"\r\n"
        L"[Item0]\r\n"
        L"Name=BTC Price\r\n"
        L"Id=tmfetch_btc\r\n"
        L"URL=https://api.coingecko.com/api/v3/simple/price?ids=bitcoin&vs_currencies=usd\r\n"
        L"Method=GET\r\n"
        L"Headers=Accept: application/json\r\n"
        L"JsonPath=BTC: $$$.bitcoin.usd\r\n"
        L"Interval=30000\r\n"
        L"Timeout=8000\r\n";

    HANDLE h = CreateFileW(iniPath.c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    const wchar_t bom = 0xFEFF;
    DWORD written = 0;
    WriteFile(h, &bom, 2, &written, nullptr);
    WriteFile(h, sample.data(), (DWORD)(sample.size() * sizeof(wchar_t)), &written, nullptr);
    CloseHandle(h);
}

PluginConfig Load(const std::wstring& iniPath) {
    PluginConfig cfg;

    if (!PathFileExistsW(iniPath.c_str())) {
        WriteSample(iniPath);
    }

    cfg.interval_ms = ReadInt(L"Plugin", L"Interval", 5000, iniPath);
    if (cfg.interval_ms < 500) cfg.interval_ms = 500;

    int count = ReadInt(L"Plugin", L"ItemCount", 0, iniPath);
    if (count < 0) count = 0;
    if (count > 32) count = 32;

    for (int i = 0; i < count; ++i) {
        wchar_t section[32]; swprintf(section, 32, L"Item%d", i);

        ItemConfig it;
        it.section     = section;
        it.name        = ReadStr(section, L"Name",        L"", iniPath);
        it.id          = ReadStr(section, L"Id",          L"", iniPath);
        it.url         = ReadStr(section, L"URL",         L"", iniPath);
        it.method      = ReadStr(section, L"Method",      L"GET", iniPath);
        it.jsonpath    = UnescapeMultiline(ReadStr(section, L"JsonPath", L"", iniPath));
        it.headers     = ReadHeaders(section, iniPath);
        std::wstring body_w = ReadStr(section, L"Body", L"", iniPath);
        it.body        = strutil::WideToUtf8(UnescapeMultiline(body_w));
        it.interval_ms = ReadInt(section, L"Interval", 0, iniPath);
        it.timeout_ms  = ReadInt(section, L"Timeout",  8000, iniPath);

        // Skip clearly-invalid entries.
        if (it.url.empty() || it.id.empty()) continue;
        if (it.name.empty()) it.name = it.id;

        cfg.items.push_back(std::move(it));
    }
    return cfg;
}

void Save(const std::wstring& iniPath, const PluginConfig& cfg) {
    auto setStr = [&](const wchar_t* sec, const wchar_t* key, const std::wstring& v) {
        WritePrivateProfileStringW(sec, key, v.c_str(), iniPath.c_str());
    };
    auto setInt = [&](const wchar_t* sec, const wchar_t* key, int v) {
        wchar_t b[32]; swprintf(b, 32, L"%d", v);
        WritePrivateProfileStringW(sec, key, b, iniPath.c_str());
    };

    // Wipe ALL existing [ItemN] sections (probe up to a generous limit).
    for (int i = 0; i < 64; ++i) {
        wchar_t s[16]; swprintf(s, 16, L"Item%d", i);
        WritePrivateProfileStringW(s, nullptr, nullptr, iniPath.c_str());
    }

    setInt(L"Plugin", L"Interval",  cfg.interval_ms);
    setInt(L"Plugin", L"ItemCount", (int)cfg.items.size());

    for (size_t i = 0; i < cfg.items.size(); ++i) {
        wchar_t sec[16]; swprintf(sec, 16, L"Item%zu", i);
        const auto& it = cfg.items[i];
        setStr(sec, L"Name",        it.name);
        setStr(sec, L"Id",          it.id);
        setStr(sec, L"URL",         it.url);
        setStr(sec, L"Method",      it.method);
        std::wstring h;
        for (size_t k = 0; k < it.headers.size(); ++k) {
            if (k) h += L" || ";
            h += it.headers[k];
        }
        setStr(sec, L"Headers",     h);
        setStr(sec, L"Body",        EscapeMultiline(strutil::Utf8ToWide(it.body)));
        setStr(sec, L"JsonPath",    EscapeMultiline(it.jsonpath));
        setInt(sec, L"Interval",    it.interval_ms);
        setInt(sec, L"Timeout",     it.timeout_ms);
    }
    // Flush
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, iniPath.c_str());
}

} // namespace config
