#ifndef GDML_GAME_UI_H
#define GDML_GAME_UI_H

#include "gdml/plugin.h"

#include <cstddef>
#include <string>
#include <vector>

struct GDML_ModInfo {
    std::string name;
    bool loaded;
};

bool gdml_install_game_ui_hooks(
    const std::vector<GDML_ModInfo>& mods,
    const char* icon_path,
    GDML_LogFn log,
    void* log_context
);

#endif
