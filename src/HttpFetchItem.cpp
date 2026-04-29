#include "HttpFetchItem.h"
#include "JsonPath.h"
#include "StringUtil.h"
#include <nlohmann/json.hpp>

using namespace std::chrono;

namespace {
// TrafficMonitor renders each plugin item as a SINGLE row (its taskbar
// 2-row layout pairs neighbouring items, it does NOT split one item into
// label-on-top + value-on-bottom). So newlines in the template would just
// be discarded by the host - we collapse them to spaces ourselves to keep
// the output predictable. CRs are stripped.
std::wstring FlattenToSingleLine(const std::wstring& s) {
    std::wstring out;
    out.reserve(s.size());
    bool prevSpace = false;
    for (wchar_t c : s) {
        if (c == L'\r') continue;
        if (c == L'\n') c = L' ';
        if (c == L' ' && prevSpace) continue;     // dedupe spaces
        out.push_back(c);
        prevSpace = (c == L' ');
    }
    while (!out.empty() && out.back() == L' ') out.pop_back();
    size_t leading = out.find_first_not_of(L' ');
    if (leading == std::wstring::npos) return L"";
    return out.substr(leading);
}

// Derive a width-hint / placeholder string by rendering the template with
// every "$.path" substituted by `placeholder`, then flattened to one row.
std::wstring DeriveDisplay(const std::wstring& tpl, const std::string& placeholder) {
    if (tpl.empty()) return placeholder.empty() ? L"--"
                                                : strutil::Utf8ToWide(placeholder);
    nlohmann::json empty = nlohmann::json::object();
    std::string rendered = jpath::RenderTemplate(empty, strutil::WideToUtf8(tpl), placeholder);
    auto flat = FlattenToSingleLine(strutil::Utf8ToWide(rendered));
    return flat.empty() ? L"--" : flat;
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
    m_sampleLabel.clear();
    m_sampleValue  = DeriveDisplay(cfg.jsonpath, "9999");
    m_displayLabel.clear();
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
    return SnapshotReturn(m_displayValue);
}

const wchar_t* HttpFetchItem::GetItemValueSampleText() const {
    std::lock_guard<std::mutex> lk(m_mtx);
    return SnapshotReturn(m_sampleValue);
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
    auto flat = FlattenToSingleLine(v);
    if (flat.empty()) flat = L"--";
    std::lock_guard<std::mutex> lk(m_mtx);
    m_displayValue = flat;
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
