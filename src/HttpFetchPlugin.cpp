#include "HttpFetchPlugin.h"
#include "HttpClient.h"
#include "JsonPath.h"
#include "OptionsDialog.h"
#include "StringUtil.h"
#include <Windows.h>
#include <shellapi.h>
#include <algorithm>
#include <chrono>
#include <nlohmann/json.hpp>

namespace { HMODULE g_hModule = nullptr; }
void HttpFetchPlugin::SetModule(HMODULE m) { g_hModule = m; }

using namespace std::chrono;
using nlohmann::json;

namespace {

constexpr const wchar_t* kPluginName        = L"HTTP Fetch";
constexpr const wchar_t* kPluginDescription =
    L"Generic HTTP/JSON plugin for TrafficMonitor.\r\n"
    L"Fetches a URL on a timer, extracts a field via JSONPath and shows it "
    L"with a custom label. See TMFetchPlugin.ini in the plugin's config dir.";
constexpr const wchar_t* kPluginAuthor    = L"TMFetchPlugin";
constexpr const wchar_t* kPluginCopyright = L"MIT License";
constexpr const wchar_t* kPluginVersion   = L"1.0.0";
constexpr const wchar_t* kPluginUrl       = L"https://github.com/zhongyang219/TrafficMonitor";

void RefreshOne(HttpFetchItem& item) {
    const ItemConfig c = item.ConfigCopy();

    http::Request req;
    req.url        = c.url;
    req.method     = c.method.empty() ? std::wstring(L"GET") : c.method;
    req.headers    = c.headers;
    req.body       = c.body;
    req.timeout_ms = c.timeout_ms > 0 ? c.timeout_ms : 8000;

    http::Response resp = http::Fetch(req);
    if (!resp.ok) {
        item.SetError(resp.error);
        return;
    }

    // If no JsonPath configured, show the raw body trimmed.
    if (c.jsonpath.empty()) {
        std::string s = strutil::Trim(resp.body);
        item.SetValue(strutil::Utf8ToWide(s));
        return;
    }

    json doc;
    try {
        doc = json::parse(resp.body, nullptr, /*allow_exceptions*/ true,
                          /*ignore_comments*/ true);
    } catch (const std::exception&) {
        item.SetError(L"JSON parse error");
        return;
    }

    // Render the user's free-form template. Bare "$.path" references and
    // "${...}" both work; everything else is literal.
    std::string path_utf8 = strutil::WideToUtf8(c.jsonpath);
    std::string rendered  = jpath::RenderTemplate(doc, path_utf8, "--");
    item.SetValue(strutil::Utf8ToWide(rendered));
}

} // namespace

HttpFetchPlugin& HttpFetchPlugin::Instance() {
    static HttpFetchPlugin g;
    return g;
}

HttpFetchPlugin::HttpFetchPlugin() = default;

HttpFetchPlugin::~HttpFetchPlugin() {
    StopWorker();
}

IPluginItem* HttpFetchPlugin::GetItem(int index) {
    std::lock_guard<std::mutex> lk(m_cfgMtx);
    if (index < 0 || index >= (int)m_items.size()) return nullptr;
    return m_items[index].get();
}

void HttpFetchPlugin::DataRequired() {
    // Wake worker so it can re-check due items immediately.
    m_cv.notify_all();
}

ITMPlugin::OptionReturn HttpFetchPlugin::ShowOptionsDialog(void* hParent) {
    PluginConfig snapshot;
    std::wstring ini;
    {
        std::lock_guard<std::mutex> lk(m_cfgMtx);
        snapshot = m_cfg;
        ini = config::IniPath(m_configDir);
    }
    bool ok = ui::ShowOptions((HWND)hParent, g_hModule, snapshot);
    if (!ok) return OR_OPTION_UNCHANGED;

    config::Save(ini, snapshot);
    Reload();
    return OR_OPTION_CHANGED;
}

const wchar_t* HttpFetchPlugin::GetInfo(PluginInfoIndex index) {
    switch (index) {
    case TMI_NAME:        return kPluginName;
    case TMI_DESCRIPTION: return kPluginDescription;
    case TMI_AUTHOR:      return kPluginAuthor;
    case TMI_COPYRIGHT:   return kPluginCopyright;
    case TMI_VERSION:     return kPluginVersion;
    case TMI_URL:         return kPluginUrl;
    default:              return L"";
    }
}

void HttpFetchPlugin::OnExtenedInfo(ExtendedInfoIndex index, const wchar_t* data) {
    if (index == EI_CONFIG_DIR && data) {
        bool needInit = false;
        {
            std::lock_guard<std::mutex> lk(m_cfgMtx);
            if (m_configDir != data) {
                m_configDir = data;
                needInit = true;
            }
        }
        if (needInit) {
            Reload();
            if (!m_initialized.exchange(true)) {
                StartWorker();
            }
        }
    }
}

void HttpFetchPlugin::OnInitialize(ITrafficMonitor* /*pApp*/) {
    // EI_CONFIG_DIR is normally delivered around the same time and triggers
    // Reload + StartWorker(). Nothing else to do here for now.
}

void HttpFetchPlugin::Reload() {
    std::wstring ini;
    {
        std::lock_guard<std::mutex> lk(m_cfgMtx);
        ini = config::IniPath(m_configDir);
    }
    PluginConfig newCfg = config::Load(ini);

    std::lock_guard<std::mutex> lk(m_cfgMtx);
    m_cfg = newCfg;

    // CRITICAL: TrafficMonitor caches the IPluginItem* pointers returned by
    // GetItem(). We must NEVER destroy or move existing item objects after
    // they have been handed to the host - doing so triggers use-after-free
    // crashes deep inside MFC's message dispatch.
    //
    // Strategy: match new config items to existing slots by stable id and
    // update fields in place. Newly added items are appended. Items removed
    // from the config are kept alive but disabled (worker skips them, value
    // becomes the placeholder). Reordering takes effect after restart.

    std::vector<bool> matched(m_items.size(), false);
    for (const auto& nic : newCfg.items) {
        bool found = false;
        for (size_t i = 0; i < m_items.size(); ++i) {
            if (matched[i]) continue;
            if (m_items[i]->StableId() == nic.id) {
                m_items[i]->UpdateConfig(nic);
                matched[i] = true;
                found = true;
                break;
            }
        }
        if (!found) {
            m_items.push_back(std::make_unique<HttpFetchItem>(nic));
        }
    }
    for (size_t i = 0; i < matched.size(); ++i) {
        if (!matched[i]) m_items[i]->Disable();
    }

    m_cv.notify_all();
}

void HttpFetchPlugin::StartWorker() {
    if (m_worker.joinable()) return;
    m_stop.store(false);
    m_worker = std::thread(&HttpFetchPlugin::WorkerLoop, this);
}

void HttpFetchPlugin::StopWorker() {
    // IMPORTANT: never join the worker here.
    //
    // This is called from the static HttpFetchPlugin destructor, which runs
    // during DLL unload (loader lock held). The worker thread may be blocked
    // for several seconds inside a synchronous WinHTTP call; joining would
    // make TrafficMonitor.exe hang on shutdown and prevent the process from
    // releasing its single-instance mutex - exactly what the user reported as
    // "已有一个进程在运行" when relaunching.
    //
    // We signal the worker to stop and detach. If the worker is mid-request,
    // the OS will reap it as part of normal process termination. Resources
    // are released by the kernel; no leaks at process exit.
    m_stop.store(true);
    m_cv.notify_all();
    if (m_worker.joinable()) m_worker.detach();
}

void HttpFetchPlugin::WorkerLoop() {
    while (!m_stop.load()) {
        // Snapshot current items + global interval under lock; we can't keep
        // the lock during HTTP I/O.
        std::vector<HttpFetchItem*> snapshot;
        int globalInterval = 5000;
        {
            std::lock_guard<std::mutex> lk(m_cfgMtx);
            globalInterval = m_cfg.interval_ms;
            snapshot.reserve(m_items.size());
            for (auto& p : m_items) snapshot.push_back(p.get());
        }

        auto now = steady_clock::now();
        long long minWaitMs = 5000;
        for (HttpFetchItem* it : snapshot) {
            if (m_stop.load()) return;
            if (!it->IsEnabled()) continue;
            int interval = it->IntervalMs(globalInterval);
            if (it->DueForRefresh(now)) {
                RefreshOne(*it);
                it->MarkRefreshed(steady_clock::now());
                minWaitMs = std::min<long long>(minWaitMs, interval);
            } else {
                // we don't track per-item next-due here; sleep granularity is
                // fine-grained enough since we re-check after minWaitMs.
                minWaitMs = std::min<long long>(minWaitMs, 1000);
            }
        }
        if (snapshot.empty()) minWaitMs = 2000;

        std::unique_lock<std::mutex> lk(m_cvMtx);
        m_cv.wait_for(lk, milliseconds(minWaitMs), [this]{ return m_stop.load(); });
    }
}
