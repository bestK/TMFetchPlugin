#pragma once
#include "PluginInterface.h"
#include "Config.h"
#include <atomic>
#include <chrono>
#include <mutex>
#include <string>

class HttpFetchItem : public IPluginItem {
public:
    explicit HttpFetchItem(const ItemConfig& cfg);

    // IPluginItem
    const wchar_t* GetItemName() const override;
    const wchar_t* GetItemId() const override;
    const wchar_t* GetItemLableText() const override;
    const wchar_t* GetItemValueText() const override;
    const wchar_t* GetItemValueSampleText() const override;

    // Custom-draw path: enabled only when the rendered template contains a
    // newline. For single-line items we return false and let TrafficMonitor
    // do its default draw, which respects user-set fonts / colours / themes.
    bool IsCustomDraw() const override;
    void DrawItem(void* hDC, int x, int y, int w, int h, bool dark_mode) override;
    int  GetItemWidthEx(void* hDC) const override;

    // Item-side helpers used by the plugin's worker thread.
    // ConfigCopy returns a snapshot under lock so the worker never reads a
    // string that's being mutated by a UI-thread UpdateConfig().
    ItemConfig ConfigCopy() const;

    int  IntervalMs(int globalDefault) const;
    bool DueForRefresh(std::chrono::steady_clock::time_point now) const;
    void MarkRefreshed(std::chrono::steady_clock::time_point now);
    void SetValue(const std::wstring& v);
    void SetError(const std::wstring& msg);

    // Live-update the item's configuration in-place (no realloc of the
    // IPluginItem object - TrafficMonitor holds raw pointers!).
    void UpdateConfig(const ItemConfig& cfg);

    // Mark this item as removed in the latest config. The slot is kept alive
    // (TrafficMonitor still has its pointer) but the worker stops fetching it
    // and the displayed value becomes the placeholder.
    void Disable();
    bool IsEnabled() const { return m_enabled.load(std::memory_order_relaxed); }

    const std::wstring& StableId() const { return m_id; } // immutable after ctor

private:
    // Immutable for the lifetime of the object. Returned by GetItemId() and
    // GetItemName() without locking, so they MUST NOT be mutated after the
    // host has been handed this pointer.
    std::wstring m_id;
    std::wstring m_name;

    mutable std::mutex m_mtx;
    ItemConfig m_cfg;
    // Both fields keep newlines if the user's template has them. When
    // present, IsCustomDraw() flips to true and DrawItem() renders one
    // line per row. GetItemValueText() returns a flattened single-row
    // view of m_displayValue so TM still has something sensible to cache
    // / display in tooltips and menus.
    std::wstring m_sampleValue;     // multi-line sample (placeholder=9999)
    std::wstring m_displayValue;    // multi-line latest value

    std::atomic<bool> m_enabled{true};
    std::atomic<long long> m_lastRefreshMs{0};
};
