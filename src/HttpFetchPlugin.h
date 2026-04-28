#pragma once
#include <Windows.h>
#include "PluginInterface.h"
#include "Config.h"
#include "HttpFetchItem.h"
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

class HttpFetchPlugin : public ITMPlugin {
public:
    static HttpFetchPlugin& Instance();

    HttpFetchPlugin();
    ~HttpFetchPlugin();

    // ITMPlugin
    IPluginItem* GetItem(int index) override;
    void DataRequired() override;
    OptionReturn ShowOptionsDialog(void* hParent) override;
    const wchar_t* GetInfo(PluginInfoIndex index) override;
    void OnExtenedInfo(ExtendedInfoIndex index, const wchar_t* data) override;
    void OnInitialize(ITrafficMonitor* pApp) override;

    // Set by DllMain so the options dialog can load resources from this DLL.
    static void SetModule(HMODULE m);

private:
    void StartWorker();
    void StopWorker();
    void WorkerLoop();
    void Reload();

    std::mutex m_cfgMtx;
    std::wstring m_configDir;
    PluginConfig m_cfg;
    std::vector<std::unique_ptr<HttpFetchItem>> m_items;

    std::thread m_worker;
    std::atomic<bool> m_stop{false};
    std::condition_variable m_cv;
    std::mutex m_cvMtx;

    std::atomic<bool> m_initialized{false};
};
