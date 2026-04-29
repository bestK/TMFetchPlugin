#include "HttpFetchItem.h"
#include "JsonPath.h"
#include "StringUtil.h"
#include <nlohmann/json.hpp>
#include <Windows.h>
#include <vector>

using namespace std::chrono;

namespace {
// Strip CRs (Windows edit controls produce CRLF; we want to work with LF only).
std::wstring NormalizeNewlines(const std::wstring& s) {
    std::wstring out;
    out.reserve(s.size());
    for (wchar_t c : s) {
        if (c == L'\r') continue;
        out.push_back(c);
    }
    return out;
}

// Split on LF. Empty input -> single empty line so callers don't have to
// handle the empty-vector case.
std::vector<std::wstring> SplitLines(const std::wstring& s) {
    std::vector<std::wstring> out;
    std::wstring line;
    for (wchar_t c : s) {
        if (c == L'\n') { out.push_back(line); line.clear(); }
        else line.push_back(c);
    }
    out.push_back(line);
    return out;
}

// Collapse newlines to a single space; used for the GetItemValueText
// fallback path (tooltips, menus, accessibility).
std::wstring FlattenToSingleLine(const std::wstring& s) {
    std::wstring out;
    out.reserve(s.size());
    bool prevSpace = false;
    for (wchar_t c : s) {
        if (c == L'\r') continue;
        if (c == L'\n') c = L' ';
        if (c == L' ' && prevSpace) continue;
        out.push_back(c);
        prevSpace = (c == L' ');
    }
    while (!out.empty() && out.back() == L' ') out.pop_back();
    size_t leading = out.find_first_not_of(L' ');
    if (leading == std::wstring::npos) return L"";
    return out.substr(leading);
}

// Render the template with every "$.path" substituted by `placeholder`,
// keeping any newlines the user put in.
std::wstring DeriveDisplay(const std::wstring& tpl, const std::string& placeholder) {
    if (tpl.empty()) return placeholder.empty() ? L"--"
                                                : strutil::Utf8ToWide(placeholder);
    nlohmann::json empty = nlohmann::json::object();
    std::string rendered = jpath::RenderTemplate(empty, strutil::WideToUtf8(tpl), placeholder);
    auto out = NormalizeNewlines(strutil::Utf8ToWide(rendered));
    return out.empty() ? std::wstring(L"--") : out;
}
} // namespace

namespace {
// Each getter returning const wchar_t* must keep the bytes alive at least until
// TrafficMonitor finishes using the pointer. Since these are called only from
// TrafficMonitor's UI thread, a thread_local snapshot buffer is safe.
const wchar_t* SnapshotReturn(const std::wstring& src) {
    static thread_local std::wstring buf;
    buf = src;
    return buf.c_str();
}
} // namespace

HttpFetchItem::HttpFetchItem(const ItemConfig& cfg)
    : m_id(cfg.id), m_name(cfg.name.empty() ? cfg.id : cfg.name), m_cfg(cfg)
{
    m_sampleValue  = DeriveDisplay(cfg.jsonpath, "9999");
    m_displayValue = DeriveDisplay(cfg.jsonpath, "--");
}

// m_id/m_name are immutable; safe to return c_str() without lock.
const wchar_t* HttpFetchItem::GetItemName() const { return m_name.c_str(); }
const wchar_t* HttpFetchItem::GetItemId()   const { return m_id.c_str(); }

// Label is intentionally always empty: the host renders plugin items in a
// single row, and any text we put here ends up cached in TM's per-item
// override table - which we'd then have to clean up. Everything user-visible
// goes through GetItemValueText.
const wchar_t* HttpFetchItem::GetItemLableText() const {
    return L"";
}

const wchar_t* HttpFetchItem::GetItemValueText() const {
    std::lock_guard<std::mutex> lk(m_mtx);
    // TM ignores this when IsCustomDraw() returns true, but it's still used
    // for tooltips / menu strings, so flatten newlines for safety.
    return SnapshotReturn(FlattenToSingleLine(m_displayValue));
}

const wchar_t* HttpFetchItem::GetItemValueSampleText() const {
    std::lock_guard<std::mutex> lk(m_mtx);
    return SnapshotReturn(FlattenToSingleLine(m_sampleValue));
}

bool HttpFetchItem::IsCustomDraw() const {
    std::lock_guard<std::mutex> lk(m_mtx);
    return m_displayValue.find(L'\n') != std::wstring::npos;
}

int HttpFetchItem::GetItemWidthEx(void* hDC) const {
    HDC dc = reinterpret_cast<HDC>(hDC);
    if (!dc) return 0;
    std::wstring sample;
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        sample = m_sampleValue;
    }
    int max_w = 0;
    for (auto& line : SplitLines(sample)) {
        if (line.empty()) continue;
        SIZE sz{};
        GetTextExtentPoint32W(dc, line.c_str(), (int)line.size(), &sz);
        if (sz.cx > max_w) max_w = sz.cx;
    }
    // A small horizontal padding (matches what TM's default draw leaves).
    return max_w + 4;
}

void HttpFetchItem::DrawItem(void* hDC, int x, int y, int w, int h, bool dark_mode) {
    HDC dc = reinterpret_cast<HDC>(hDC);
    if (!dc) return;

    std::wstring text;
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        text = m_displayValue;
    }
    auto lines = SplitLines(text);
    if (lines.empty()) return;

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, dark_mode ? RGB(255, 255, 255) : RGB(0, 0, 0));

    const int n = (int)lines.size();
    const int line_h = h / n;
    for (int i = 0; i < n; ++i) {
        RECT r{ x, y + i * line_h, x + w,
                (i == n - 1) ? (y + h) : (y + (i + 1) * line_h) };
        DrawTextW(dc, lines[i].c_str(), (int)lines[i].size(), &r,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    }
}

ItemConfig HttpFetchItem::ConfigCopy() const {
    std::lock_guard<std::mutex> lk(m_mtx);
    return m_cfg;
}

int HttpFetchItem::IntervalMs(int globalDefault) const {
    std::lock_guard<std::mutex> lk(m_mtx);
    int v = m_cfg.interval_ms > 0 ? m_cfg.interval_ms : globalDefault;
    if (v < 500) v = 500;
    return v;
}

bool HttpFetchItem::DueForRefresh(steady_clock::time_point now) const {
    long long last = m_lastRefreshMs.load();
    if (last == 0) return true;
    auto now_ms = duration_cast<milliseconds>(now.time_since_epoch()).count();
    return (now_ms - last) >= IntervalMs(/*globalDefault*/ 5000);
}

void HttpFetchItem::MarkRefreshed(steady_clock::time_point now) {
    auto ms = duration_cast<milliseconds>(now.time_since_epoch()).count();
    m_lastRefreshMs.store(ms);
}

void HttpFetchItem::SetValue(const std::wstring& v) {
    auto norm = NormalizeNewlines(v);
    if (norm.empty()) norm = L"--";
    std::lock_guard<std::mutex> lk(m_mtx);
    m_displayValue = norm;
}

void HttpFetchItem::SetError(const std::wstring& /*msg*/) {
    std::lock_guard<std::mutex> lk(m_mtx);
    m_displayValue = L"--";
}

void HttpFetchItem::UpdateConfig(const ItemConfig& cfg) {
    std::lock_guard<std::mutex> lk(m_mtx);
    // Note: m_id and m_name are intentionally NOT mutated - TrafficMonitor
    // may have cached the c_str() pointers and renames could invalidate them.
    m_cfg = cfg;
    m_cfg.id   = m_id;        // keep stable id
    m_cfg.name = m_name;      // keep stable name
    m_sampleValue = DeriveDisplay(cfg.jsonpath, "9999");
    m_enabled.store(true, std::memory_order_relaxed);
    // Force a fresh refresh on next worker tick.
    m_lastRefreshMs.store(0);
}

void HttpFetchItem::Disable() {
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        m_displayValue = L"--";
    }
    m_enabled.store(false, std::memory_order_relaxed);
}
