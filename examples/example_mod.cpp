#include "gdml/plugin.h"

static const GDML_HostApi* g_host = nullptr;

extern "C" GDML_PLUGIN_EXPORT int GDML_CALL gdml_plugin_init(const GDML_HostApi* host) {
    if (host == nullptr ||
        host->struct_size < sizeof(GDML_HostApi) ||
        host->api_version != GDML_HOST_API_VERSION ||
        host->log == nullptr) {
        return 0;
    }

    g_host = host;
    g_host->log(g_host->context, GDML_LOG_INFO, "Example mod initialized.");
    return 1;
}

extern "C" GDML_PLUGIN_EXPORT void GDML_CALL gdml_plugin_shutdown(void) {
    if (g_host != nullptr) {
        g_host->log(g_host->context, GDML_LOG_INFO, "Example mod shut down.");
        g_host = nullptr;
    }
}
