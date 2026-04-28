#include "PluginInterface.h"
#include "HttpFetchPlugin.h"
#include <Windows.h>

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID /*lpReserved*/) {
    switch (reason) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        HttpFetchPlugin::SetModule(hModule);
        break;
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}

#ifdef __cplusplus
extern "C" {
#endif

__declspec(dllexport) ITMPlugin* TMPluginGetInstance() {
    return &HttpFetchPlugin::Instance();
}

#ifdef __cplusplus
}
#endif
