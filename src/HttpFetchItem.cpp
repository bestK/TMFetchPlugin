#include "HttpFetchItem.h"
#include "JsonPath.h"
#include "StringUtil.h"
#include <nlohmann/json.hpp>

using namespace std::chrono;

namespace {
// Split the rendered template at its first newline. CR is treated as a
// line-break too (and stripped); a CRLF counts as one. Output is the
// "label" (text before the first break) and the "value" (everything
// after, with internal CRs stripped but LFs preserved).
struct LabelValue { std::wstring label, value; };
LabelValue SplitFirstLine(const std::wstring& s) {
    LabelValue lv;
    size_t i = 0;
    for (; i < s.size(); ++i) {
        if (s[i] == L'\n' || s[i] == L'\r') break;
        lv.label.push_back(s[i]);
    }
    if (i < s.size()) {
        if (s[i] == L'\r' && i + 1 < s.size() && s[i+1] == L'\n') ++i;
        ++i;
        for (; i < s.size(); ++i) {
            if (s[i] == L'\r') continue;
            lv.value.push_back(s[i]);
        }
    }
    return lv;
}

// Derive a sample string from the user's template by substituting every
// "$.path" reference with `placeholder`. Returns the rendered (still
// possibly multi-line) text; callers split into label/value.
std::wstring DeriveSample(const std::wstring& tpl, const std::string& placeholder) {
    if (tpl.empty()) return placeholder.empty() ? L"--"
                                                : strutil::Utf8ToWide(placeholder);
    nlohmann::json empty = nlohmann::json::object();
    std::string rendered = jpath::RenderTemplate(empty, strutil::WideToUtf8(tpl), placeholder);
    return strutil::Utf8ToWide(rendered);
}

// Apply DeriveSample then split. Helper used by ctor / UpdateConfig.
LabelValue DeriveSampleLV(const std::wstring& tpl, const std::string& placeholder) {
    auto lv = SplitFirstLine(DeriveSample(tpl, placeholder));
    if (lv.label.empty() && lv.value.empty()) lv.value = L"--";
    return lv;
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
    auto sLV = DeriveSampleLV(cfg.jsonpath, "9999");
    m_sampleLabel = sLV.label;
    m_sampleValue = sLV.value;
    auto dLV = DeriveSampleLV(cfg.jsonpath, "--");
    m_displayLabel = dLV.label;
    m_displayValue = dLV.value;
}

// m_id/m_name are immutable; safe to return c_str() without lock.
const wchar_t* HttpFetchItem::GetItemName() const { return m_name.c_str(); }
const wchar_t* HttpFetchItem::GetItemId()   const { return m_id.c_str(); }

// The first line of the rendered template is exposed as the "label" line
// (drawn above the value in TrafficMonitor's two-row layout). If the
// template has no newline the label is empty and only the value renders.
const wchar_t* HttpFetchItem::GetItemLableText() const {
    std::lock_guard<std::mutex> lk(m_mtx);
    return SnapshotReturn(m_displayLabel);
}

const wchar_t* HttpFetchItem::GetItemValueText() const {
    std::lock_guard<std::mutex> lk(m_mtx);
    return SnapshotReturn(m_displayValue);
}

const wchar_t* HttpFetchItem::GetItemValueSampleText() const {
    std::lock_guard<std::mutex> lk(m_mtx);
    // Width hint should cover the wider of the two lines.
    return SnapshotReturn(m_sampleValue.size() >= m_sampleLabel.size()
                              ? m_sampleValue : m_sampleLabel);
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
    auto lv = SplitFirstLine(v);
    if (lv.label.empty() && lv.value.empty()) lv.value = L"--";
    std::lock_guard<std::mutex> lk(m_mtx);
    m_displayLabel = lv.label;
    m_displayValue = lv.value;
}

void HttpFetchItem::SetError(const std::wstring& /*msg*/) {
    std::lock_guard<std::mutex> lk(m_mtx);
    m_displayLabel.clear();
    m_displayValue = L"--";
}

void HttpFetchItem::UpdateConfig(const ItemConfig& cfg) {
    std::lock_guard<std::mutex> lk(m_mtx);
    // Note: m_id and m_name are intentionally NOT mutated - TrafficMonitor
    // may have cached the c_str() pointers and renames could invalidate them.
    m_cfg = cfg;
    m_cfg.id   = m_id;        // keep stable id
    m_cfg.name = m_name;      // keep stable name
    auto sLV = DeriveSampleLV(cfg.jsonpath, "9999");
    m_sampleLabel = sLV.label;
    m_sampleValue = sLV.value;
    m_enabled.store(true, std::memory_order_relaxed);
    // Force a fresh refresh on next worker tick.
    m_lastRefreshMs.store(0);
}

void HttpFetchItem::Disable() {
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        m_displayLabel.clear();
        m_displayValue = L"--";
    }
    m_enabled.store(false, std::memory_order_relaxed);
}
