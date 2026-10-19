#ifndef GDML_PLUGIN_H
#define GDML_PLUGIN_H

#include <stdint.h>

#if defined(_WIN32)
#define GDML_CALL __cdecl
#if defined(GDML_BUILDING_LOADER)
#define GDML_API __declspec(dllexport)
#else
#define GDML_API __declspec(dllimport)
#endif
#define GDML_PLUGIN_EXPORT __declspec(dllexport)
#else
#define GDML_CALL
#define GDML_API __attribute__((visibility("default")))
#define GDML_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define GDML_HOST_API_VERSION 1u

typedef enum GDML_LogLevel {
    GDML_LOG_INFO = 0,
    GDML_LOG_WARNING = 1,
    GDML_LOG_ERROR = 2
} GDML_LogLevel;

typedef void(GDML_CALL* GDML_LogFn)(
    void* context,
    GDML_LogLevel level,
    const char* message
);

/* The host API pointer is valid until the loader unloads the plugin. */
typedef struct GDML_HostApi {
    uint32_t struct_size;
    uint32_t api_version;
    void* context;
    GDML_LogFn log;
} GDML_HostApi;

typedef int(GDML_CALL* GDML_PluginInitFn)(const GDML_HostApi* host);
typedef void(GDML_CALL* GDML_PluginShutdownFn)(void);

typedef GDML_PluginInitFn gdml_plugin_init_fn;
typedef GDML_PluginShutdownFn gdml_plugin_shutdown_fn;

#ifdef __cplusplus
}
#endif

#endif
