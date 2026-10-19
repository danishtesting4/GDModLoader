#ifndef GDML_LOADER_H
#define GDML_LOADER_H

#include "gdml/plugin.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GDML_Loader GDML_Loader;

typedef enum GDML_Result {
    GDML_OK = 0,
    GDML_ERROR_INVALID_ARGUMENT = 1,
    GDML_ERROR_INCOMPATIBLE_API = 2,
    GDML_ERROR_LOAD_FAILED = 3,
    GDML_ERROR_MISSING_ENTRY_POINT = 4,
    GDML_ERROR_PLUGIN_INIT_FAILED = 5,
    GDML_ERROR_ALREADY_LOADED = 6,
    GDML_ERROR_OUT_OF_MEMORY = 7
} GDML_Result;

GDML_API GDML_Result GDML_CALL gdml_loader_create(
    const GDML_HostApi* host,
    GDML_Loader** out_loader
);
GDML_API GDML_Result GDML_CALL gdml_loader_load_plugin(
    GDML_Loader* loader,
    const char* utf8_path
);
GDML_API void GDML_CALL gdml_loader_unload_all(GDML_Loader* loader);
GDML_API void GDML_CALL gdml_loader_destroy(GDML_Loader* loader);

#ifdef __cplusplus
}
#endif

#endif
