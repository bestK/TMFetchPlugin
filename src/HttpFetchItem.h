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
    std::wstring m_sample;
    std::wstring m_displayValue; // computed: prefix + value + suffix

    std::atomic<bool> m_enabled{true};
    std::atomic<long long> m_lastRefreshMs{0};
};
