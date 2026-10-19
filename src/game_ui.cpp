#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include "game_ui.h"

#include <MinHook.h>

#include <algorithm>
#include <cfloat>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <vector>
#include <string>

class GDMLButtonTarget {
public:
    void open_mod_list(void* sender);
    void close_mod_list(void* sender);
    void show_installed_tab(void* sender);
    void show_about_tab(void* sender);
};

namespace {

constexpr std::uintptr_t kMenuLayerInitRva = 0x4720c0;
constexpr std::uintptr_t kMenuItemCreateRva = 0x488a0;

constexpr unsigned char kMenuLayerInitSignature[] = {
    0x48, 0x8b, 0xc4, 0x48, 0x89, 0x58, 0x08, 0x55,
    0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56
};

struct CCSize {
    float width;
    float height;
};

struct CCColor4B {
    unsigned char r;
    unsigned char g;
    unsigned char b;
    unsigned char a;
};

using MenuLayerInitFn = bool(__fastcall*)(void*);
using GetChildrenFn = void*(__fastcall*)(void*);
using ArrayCountFn = unsigned int(__fastcall*)(void*);
using ArrayObjectAtIndexFn = void*(__fastcall*)(void*, unsigned int);
using GetPositionXFn = float(__fastcall*)(void*);
using GetPositionYFn = float(__fastcall*)(void*);
using GetScaleXFn = float(__fastcall*)(void*);
using GetContentSizeFn = const CCSize*(__fastcall*)(void*);
using SetContentSizeFn = void(__fastcall*)(void*, const CCSize&);
using SetPositionXFn = void(__fastcall*)(void*, float);
using SetPositionYFn = void(__fastcall*)(void*, float);
using SetPositionFn = void(__fastcall*)(void*, float, float);
using SetScaleFn = void(__fastcall*)(void*, float);
using SetVisibleFn = void(__fastcall*)(void*, bool);
using AddChildFn = void(__fastcall*)(void*, void*);
using CreateSpriteFn = void*(__fastcall*)(const char*);
using CreateColorLayerFn = void*(__fastcall*)(const CCColor4B&);
using CreateLabelFn = void*(__fastcall*)(const char*, const char*);
using CreateMenuFn = void*(__fastcall*)();
using CreateButtonSpriteFn = void*(__fastcall*)(
    const char*, int, int, float, bool, const char*, const char*, float
);
using CreateMenuItemFn = void*(__fastcall*)(void*, void*, void*, void*);

struct CocosApi {
    GetChildrenFn get_children = nullptr;
    ArrayCountFn array_count = nullptr;
    ArrayObjectAtIndexFn array_object_at_index = nullptr;
    GetPositionXFn get_position_x = nullptr;
    GetPositionYFn get_position_y = nullptr;
    GetScaleXFn get_scale_x = nullptr;
    GetContentSizeFn get_content_size = nullptr;
    SetContentSizeFn set_content_size = nullptr;
    SetPositionXFn set_position_x = nullptr;
    SetPositionYFn set_position_y = nullptr;
    SetPositionFn set_position = nullptr;
    SetScaleFn set_scale = nullptr;
    SetVisibleFn set_visible = nullptr;
    AddChildFn add_child = nullptr;
    AddChildFn add_node_child = nullptr;
    CreateSpriteFn create_sprite = nullptr;
    CreateColorLayerFn create_color_layer = nullptr;
    CreateLabelFn create_label = nullptr;
    CreateMenuFn create_menu = nullptr;
    CreateButtonSpriteFn create_button_sprite = nullptr;
    CreateMenuItemFn create_menu_item = nullptr;
};

MenuLayerInitFn g_original_menu_layer_init = nullptr;
CocosApi g_cocos;
GDML_LogFn g_log = nullptr;
void* g_log_context = nullptr;
std::string g_icon_path;
std::vector<GDML_ModInfo> g_mods;
void* g_menu_layer = nullptr;
void* g_mod_list_overlay = nullptr;
void* g_installed_tab = nullptr;
void* g_about_tab = nullptr;

// The CCMenuItem selectors are member functions of GDMLButtonTarget, so the
// target pointer passed to the game must actually point at a
// GDMLButtonTarget. Previously the MenuLayer pointer was passed instead,
// which only worked by accident and is undefined behavior.
GDMLButtonTarget& button_target() {
    static GDMLButtonTarget target{};
    return target;
}

void log_message(GDML_LogLevel level, const char* message) {
    if (g_log != nullptr) {
        g_log(g_log_context, level, message);
    }
}

template <typename Function>
Function find_cocos_function(HMODULE cocos, const char* name) {
    return reinterpret_cast<Function>(GetProcAddress(cocos, name));
}

bool initialize_cocos_api(HMODULE game, HMODULE cocos) {
    g_cocos.get_children = find_cocos_function<GetChildrenFn>(
        cocos, "?getChildren@CCNode@cocos2d@@UEAAPEAVCCArray@2@XZ"
    );
    g_cocos.array_count = find_cocos_function<ArrayCountFn>(
        cocos, "?count@CCArray@cocos2d@@QEBAIXZ"
    );
    g_cocos.array_object_at_index = find_cocos_function<ArrayObjectAtIndexFn>(
        cocos, "?objectAtIndex@CCArray@cocos2d@@QEAAPEAVCCObject@2@I@Z"
    );
    g_cocos.get_position_x = find_cocos_function<GetPositionXFn>(
        cocos, "?getPositionX@CCNode@cocos2d@@UEAAMXZ"
    );
    g_cocos.get_position_y = find_cocos_function<GetPositionYFn>(
        cocos, "?getPositionY@CCNode@cocos2d@@UEAAMXZ"
    );
    g_cocos.get_scale_x = find_cocos_function<GetScaleXFn>(
        cocos, "?getScaleX@CCNode@cocos2d@@UEAAMXZ"
    );
    g_cocos.get_content_size = find_cocos_function<GetContentSizeFn>(
        cocos, "?getContentSize@CCNode@cocos2d@@UEBAAEBVCCSize@2@XZ"
    );
    g_cocos.set_content_size = find_cocos_function<SetContentSizeFn>(
        cocos, "?setContentSize@CCNode@cocos2d@@UEAAXAEBVCCSize@2@@Z"
    );
    g_cocos.set_position_x = find_cocos_function<SetPositionXFn>(
        cocos, "?setPositionX@CCNode@cocos2d@@UEAAXM@Z"
    );
    g_cocos.set_position_y = find_cocos_function<SetPositionYFn>(
        cocos, "?setPositionY@CCNode@cocos2d@@UEAAXM@Z"
    );
    g_cocos.set_position = find_cocos_function<SetPositionFn>(
        cocos, "?setPosition@CCNode@cocos2d@@UEAAXMM@Z"
    );
    g_cocos.set_scale = find_cocos_function<SetScaleFn>(
        cocos, "?setScale@CCNode@cocos2d@@UEAAXM@Z"
    );
    g_cocos.set_visible = find_cocos_function<SetVisibleFn>(
        cocos, "?setVisible@CCNode@cocos2d@@UEAAX_N@Z"
    );
    g_cocos.add_child = find_cocos_function<AddChildFn>(
        cocos, "?addChild@CCMenu@cocos2d@@UEAAXPEAVCCNode@2@@Z"
    );
    g_cocos.add_node_child = find_cocos_function<AddChildFn>(
        cocos, "?addChild@CCNode@cocos2d@@UEAAXPEAV12@@Z"
    );
    g_cocos.create_sprite = find_cocos_function<CreateSpriteFn>(
        cocos, "?create@CCSprite@cocos2d@@SAPEAV12@PEBD@Z"
    );
    g_cocos.create_color_layer = find_cocos_function<CreateColorLayerFn>(
        cocos, "?create@CCLayerColor@cocos2d@@SAPEAV12@AEBU_ccColor4B@2@@Z"
    );
    g_cocos.create_label = find_cocos_function<CreateLabelFn>(
        cocos, "?create@CCLabelBMFont@cocos2d@@SAPEAV12@PEBD0@Z"
    );
    g_cocos.create_menu = find_cocos_function<CreateMenuFn>(
        cocos, "?create@CCMenu@cocos2d@@SAPEAV12@XZ"
    );
    g_cocos.create_button_sprite = reinterpret_cast<CreateButtonSpriteFn>(
        reinterpret_cast<std::uintptr_t>(game) + 0x42be0
    );
    g_cocos.create_menu_item = reinterpret_cast<CreateMenuItemFn>(
        reinterpret_cast<std::uintptr_t>(game) + kMenuItemCreateRva
    );

    return g_cocos.get_children != nullptr &&
           g_cocos.array_count != nullptr &&
           g_cocos.array_object_at_index != nullptr &&
           g_cocos.get_position_x != nullptr &&
           g_cocos.get_position_y != nullptr &&
           g_cocos.get_scale_x != nullptr &&
           g_cocos.get_content_size != nullptr &&
           g_cocos.set_content_size != nullptr &&
           g_cocos.set_position_x != nullptr &&
           g_cocos.set_position_y != nullptr &&
           g_cocos.set_position != nullptr &&
           g_cocos.set_scale != nullptr &&
           g_cocos.set_visible != nullptr &&
           g_cocos.add_child != nullptr &&
           g_cocos.add_node_child != nullptr &&
           g_cocos.create_sprite != nullptr &&
           g_cocos.create_color_layer != nullptr &&
           g_cocos.create_label != nullptr &&
           g_cocos.create_menu != nullptr &&
           g_cocos.create_button_sprite != nullptr;
}

void* create_label(const std::string& text, float scale, float x, float y) {
    void* label = g_cocos.create_label(text.c_str(), "goldFont.fnt");
    if (label == nullptr) {
        return nullptr;
    }
    g_cocos.set_scale(label, scale);
    g_cocos.set_position(label, x, y);
    return label;
}

bool add_label(void* parent, const std::string& text, float scale, float x, float y) {
    void* label = create_label(text, scale, x, y);
    if (label == nullptr) {
        return false;
    }
    g_cocos.add_node_child(parent, label);
    return true;
}

template <typename Selector>
void* selector_address(Selector selector) {
    static_assert(sizeof(Selector) == sizeof(void*));
    void* address = nullptr;
    std::memcpy(&address, &selector, sizeof(address));
    return address;
}

void* create_menu_control(
    void* menu,
    void* selector,
    const char* text,
    float x,
    float y
) {
    void* sprite = g_cocos.create_button_sprite(
        text, 0, 0, 0.6f, false, "goldFont.fnt", "GJ_button_05.png", 0.0f
    );
    if (sprite == nullptr) {
        return nullptr;
    }
    void* item = g_cocos.create_menu_item(
        sprite, nullptr, &button_target(), selector
    );
    if (item == nullptr) {
        return nullptr;
    }
    g_cocos.set_scale(item, 0.6f);
    g_cocos.set_position(item, x, y);
    g_cocos.add_child(menu, item);
    return item;
}

void show_mod_list(void* menu_layer) {
    if (menu_layer == nullptr) {
        return;
    }

    if (g_mod_list_overlay != nullptr) {
        g_cocos.set_visible(g_mod_list_overlay, true);
        // Restore a predictable tab state instead of whatever was left over.
        if (g_installed_tab != nullptr && g_about_tab != nullptr) {
            g_cocos.set_visible(g_installed_tab, true);
            g_cocos.set_visible(g_about_tab, false);
        }
        return;
    }

    const CCSize* window = g_cocos.get_content_size(menu_layer);
    if (window == nullptr || window->width < 480.0f || window->height < 320.0f) {
        log_message(GDML_LOG_ERROR, "Could not determine the game window size for the mod list.");
        return;
    }

    const CCSize overlay_size{window->width, window->height};
    const CCColor4B backdrop_color{0, 0, 0, 160};
    void* overlay = g_cocos.create_color_layer(backdrop_color);
    if (overlay == nullptr) {
        log_message(GDML_LOG_ERROR, "Could not create the mod-list backdrop.");
        return;
    }
    g_cocos.set_content_size(overlay, overlay_size);
    g_cocos.set_position(overlay, 0.0f, 0.0f);

    constexpr float panel_width = 560.0f;
    constexpr float panel_height = 430.0f;
    const CCColor4B panel_color{15, 28, 58, 250};
    void* panel = g_cocos.create_color_layer(panel_color);
    if (panel == nullptr) {
        log_message(GDML_LOG_ERROR, "Could not create the mod-list panel.");
        return;
    }

    const float width = (std::min)(panel_width, window->width - 64.0f);
    const float height = (std::min)(panel_height, window->height - 64.0f);
    g_cocos.set_content_size(panel, CCSize{width, height});
    g_cocos.set_position(panel, (window->width - width) * 0.5f,
                         (window->height - height) * 0.5f);
    g_cocos.add_node_child(overlay, panel);

    if (!add_label(panel, "GDML MOD LOADER", 0.75f, width * 0.5f, height - 38.0f)) {
        log_message(GDML_LOG_ERROR, "Could not create the mod-list title.");
        return;
    }

    void* installed_tab = g_cocos.create_color_layer(CCColor4B{0, 0, 0, 0});
    void* about_tab = g_cocos.create_color_layer(CCColor4B{0, 0, 0, 0});
    if (installed_tab == nullptr || about_tab == nullptr) {
        log_message(GDML_LOG_ERROR, "Could not create GDML tab pages.");
        return;
    }
    g_cocos.set_content_size(installed_tab, CCSize{width - 36.0f, height - 152.0f});
    g_cocos.set_content_size(about_tab, CCSize{width - 36.0f, height - 152.0f});
    g_cocos.set_position(installed_tab, 18.0f, 64.0f);
    g_cocos.set_position(about_tab, 18.0f, 64.0f);
    g_cocos.add_node_child(panel, installed_tab);
    g_cocos.add_node_child(panel, about_tab);

    const auto loaded_count = static_cast<std::size_t>(std::count_if(
        g_mods.begin(), g_mods.end(),
        [](const GDML_ModInfo& mod) { return mod.loaded; }
    ));
    const std::string summary =
        std::to_string(loaded_count) + " loaded / " +
        std::to_string(g_mods.size()) + " DLLs found";
    if (!add_label(installed_tab, summary, 0.48f, (width - 36.0f) * 0.5f,
                   height - 184.0f)) {
        log_message(GDML_LOG_ERROR, "Could not create the mod-list summary.");
        return;
    }

    constexpr std::size_t max_visible_mods = 7;
    const float first_row_y = height - 224.0f;
    const float row_spacing = 29.0f;
    if (g_mods.empty()) {
        if (!add_label(installed_tab, "No mod DLLs found in GDMods.", 0.48f,
                       (width - 36.0f) * 0.5f, first_row_y)) {
            log_message(GDML_LOG_ERROR, "Could not create the empty mod-list message.");
            return;
        }
    } else {
        const std::size_t visible_count = (std::min)(g_mods.size(), max_visible_mods);
        for (std::size_t index = 0; index < visible_count; ++index) {
            const auto& mod = g_mods[index];
            const std::string row =
                std::string(mod.loaded ? "[LOADED] " : "[FAILED] ") + mod.name;
            if (!add_label(installed_tab, row, 0.42f, (width - 36.0f) * 0.5f,
                           first_row_y - row_spacing * static_cast<float>(index))) {
                log_message(GDML_LOG_ERROR, "Could not create a mod-list entry.");
                return;
            }
        }
        if (g_mods.size() > visible_count) {
            const auto remaining = g_mods.size() - visible_count;
            const std::string more = "... and " + std::to_string(remaining) + " more";
            if (!add_label(installed_tab, more, 0.42f, (width - 36.0f) * 0.5f,
                           first_row_y - row_spacing * static_cast<float>(visible_count))) {
                log_message(GDML_LOG_ERROR, "Could not create the remaining-mods message.");
                return;
            }
        }
    }

    if (!add_label(
            about_tab, "GDML  |  Geometry Dash 2.2091", 0.52f,
            (width - 36.0f) * 0.5f, height - 184.0f
        ) ||
        !add_label(about_tab, "Native Windows x64 mod loader", 0.44f,
                   (width - 36.0f) * 0.5f, height - 226.0f) ||
        !add_label(about_tab, "Place plugin DLL files in the GDMods folder.", 0.40f,
                   (width - 36.0f) * 0.5f, height - 270.0f) ||
        !add_label(about_tab, "Mods load automatically when the game starts.", 0.40f,
                   (width - 36.0f) * 0.5f, height - 306.0f) ||
        !add_label(about_tab, "Restart the game after changing installed mods.", 0.38f,
                   (width - 36.0f) * 0.5f, height - 342.0f)) {
        log_message(GDML_LOG_ERROR, "Could not create the GDML information tab.");
        return;
    }

    void* menu = g_cocos.create_menu();
    if (menu == nullptr) {
        log_message(GDML_LOG_ERROR, "Could not create the GDML tab controls.");
        return;
    }

    using Selector = void (GDMLButtonTarget::*)(void*);
    const Selector installed_selector = &GDMLButtonTarget::show_installed_tab;
    const Selector about_selector = &GDMLButtonTarget::show_about_tab;
    const Selector close_selector = &GDMLButtonTarget::close_mod_list;
    if (create_menu_control(
            menu, selector_address(installed_selector), "INSTALLED",
            width * 0.32f, height - 92.0f
        ) == nullptr ||
        create_menu_control(
            menu, selector_address(about_selector), "ABOUT",
            width * 0.68f, height - 92.0f
        ) == nullptr ||
        create_menu_control(
            menu, selector_address(close_selector), "CLOSE",
            width * 0.5f, 25.0f
        ) == nullptr) {
        log_message(GDML_LOG_ERROR, "Could not create a GDML tab control.");
        return;
    }
    g_cocos.add_node_child(panel, menu);

    g_cocos.add_node_child(menu_layer, overlay);
    g_mod_list_overlay = overlay;
    g_installed_tab = installed_tab;
    g_about_tab = about_tab;
    g_cocos.set_visible(g_about_tab, false);
    log_message(GDML_LOG_INFO, "Opened the in-game GDML tabbed mod manager.");
}

void add_gdml_button(void* menu_layer) {
    void* layer_children = g_cocos.get_children(menu_layer);
    if (layer_children == nullptr || g_cocos.array_count(layer_children) <= 1) {
        log_message(GDML_LOG_WARNING, "MenuLayer has no bottom button menu.");
        return;
    }

    const unsigned int layer_child_count = g_cocos.array_count(layer_children);
    void* newgrounds_menu = nullptr;
    void* newgrounds_button = nullptr;
    float rightmost_menu_x = -FLT_MAX;
    for (unsigned int index = 0; index < layer_child_count; ++index) {
        void* candidate_menu = g_cocos.array_object_at_index(layer_children, index);
        if (candidate_menu == nullptr ||
            g_cocos.get_position_y(candidate_menu) > 100.0f) {
            continue;
        }

        void* candidate_children = g_cocos.get_children(candidate_menu);
        if (candidate_children == nullptr) {
            continue;
        }

        const unsigned int candidate_count = g_cocos.array_count(candidate_children);
        if (candidate_count < 1 || candidate_count > 3) {
            continue;
        }

        const float candidate_x = g_cocos.get_position_x(candidate_menu);
        if (candidate_x <= rightmost_menu_x) {
            continue;
        }

        void* candidate_button = nullptr;
        float closest_origin_distance = FLT_MAX;
        for (unsigned int child_index = 0; child_index < candidate_count; ++child_index) {
            void* child = g_cocos.array_object_at_index(candidate_children, child_index);
            if (child == nullptr) {
                continue;
            }

            const CCSize* size = g_cocos.get_content_size(child);
            if (size == nullptr || size->width < 50.0f || size->height < 45.0f) {
                continue;
            }
            const float child_x = g_cocos.get_position_x(child);
            const float child_y = g_cocos.get_position_y(child);
            const float distance = child_x * child_x + child_y * child_y;
            if (distance < closest_origin_distance) {
                closest_origin_distance = distance;
                candidate_button = child;
            }
        }
        if (candidate_button != nullptr) {
            rightmost_menu_x = candidate_x;
            newgrounds_menu = candidate_menu;
            newgrounds_button = candidate_button;
        }
    }

    if (newgrounds_menu == nullptr || newgrounds_button == nullptr) {
        log_message(GDML_LOG_WARNING, "Could not identify the bottom-right Newgrounds button.");
        return;
    }

    void* button_sprite = g_cocos.create_sprite(g_icon_path.c_str());
    if (button_sprite == nullptr) {
        log_message(GDML_LOG_ERROR, "Could not load GDMods\\gdml-button.png.");
        return;
    }

    using Selector = void (GDMLButtonTarget::*)(void*);
    static_assert(sizeof(Selector) == sizeof(void*));
    const Selector selector = &GDMLButtonTarget::open_mod_list;
    void* selector_ptr = nullptr;
    std::memcpy(&selector_ptr, &selector, sizeof(selector_ptr));

    void* button = g_cocos.create_menu_item(
        button_sprite, nullptr, &button_target(), selector_ptr
    );
    if (button == nullptr) {
        log_message(GDML_LOG_ERROR, "Could not create the GDML menu item.");
        return;
    }

    const float target_scale = g_cocos.get_scale_x(newgrounds_button);
    const float effective_target_scale = target_scale > 0.0f ? target_scale : 1.0f;
    const CCSize* target_size = g_cocos.get_content_size(newgrounds_button);
    const CCSize* button_size = g_cocos.get_content_size(button);
    if (target_size == nullptr || button_size == nullptr ||
        target_size->width <= 0.0f || button_size->width <= 0.0f) {
        log_message(GDML_LOG_ERROR, "Could not determine Newgrounds button dimensions.");
        return;
    }

    constexpr float kButtonGap = 4.0f;
    const float x = g_cocos.get_position_x(newgrounds_button) -
        (target_size->width * effective_target_scale + button_size->width) * 0.5f -
        kButtonGap;
    const float y = g_cocos.get_position_y(newgrounds_button);
    g_cocos.set_position_x(button, x);
    g_cocos.set_position_y(button, y);
    g_cocos.add_child(newgrounds_menu, button);

    char diagnostic[192]{};
    sprintf_s(
        diagnostic, "Added 48px GDML immediately left of Newgrounds at (%.2f, %.2f).",
        static_cast<double>(x), static_cast<double>(y)
    );
    log_message(GDML_LOG_INFO, diagnostic);
}

bool __fastcall menu_layer_init_hook(void* menu_layer) {
    const bool initialized = g_original_menu_layer_init(menu_layer);
    if (initialized) {
        // A new MenuLayer now owns the main menu. Any overlay created for the
        // previous MenuLayer has been destroyed along with it, so keeping the
        // old pointers would be a use-after-free the next time the mod list
        // is opened. Reset them before installing the new button.
        g_menu_layer = menu_layer;
        g_mod_list_overlay = nullptr;
        g_installed_tab = nullptr;
        g_about_tab = nullptr;
        add_gdml_button(menu_layer);
    }
    return initialized;
}

} // namespace

void GDMLButtonTarget::open_mod_list(void*) {
    show_mod_list(g_menu_layer);
}

void GDMLButtonTarget::close_mod_list(void*) {
    if (g_mod_list_overlay != nullptr) {
        g_cocos.set_visible(g_mod_list_overlay, false);
    }
}

void GDMLButtonTarget::show_installed_tab(void*) {
    if (g_installed_tab != nullptr && g_about_tab != nullptr) {
        g_cocos.set_visible(g_installed_tab, true);
        g_cocos.set_visible(g_about_tab, false);
    }
}

void GDMLButtonTarget::show_about_tab(void*) {
    if (g_installed_tab != nullptr && g_about_tab != nullptr) {
        g_cocos.set_visible(g_installed_tab, false);
        g_cocos.set_visible(g_about_tab, true);
    }
}

bool gdml_install_game_ui_hooks(
    const std::vector<GDML_ModInfo>& mods,
    const char* icon_path,
    GDML_LogFn log,
    void* log_context
) {
    g_mods = mods;
    g_mod_list_overlay = nullptr;
    g_installed_tab = nullptr;
    g_about_tab = nullptr;
    g_log = log;
    g_log_context = log_context;
    g_icon_path = icon_path != nullptr ? icon_path : "";
    if (g_icon_path.empty()) {
        log_message(GDML_LOG_ERROR, "GDML menu button asset path is empty.");
        return false;
    }

    HMODULE game = GetModuleHandleW(nullptr);
    HMODULE cocos = GetModuleHandleW(L"libcocos2d.dll");
    if (game == nullptr || cocos == nullptr) {
        log_message(GDML_LOG_WARNING, "GDML main-menu integration requires Geometry Dash 2.2091.");
        return false;
    }

    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(game);
    // PE headers always live in the first page of the image; reject
    // implausible e_lfanew values before dereferencing the NT headers.
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 ||
        dos->e_lfanew > 0x1000) {
        log_message(GDML_LOG_WARNING, "The game executable has an invalid PE header.");
        return false;
    }
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(
        reinterpret_cast<const unsigned char*>(game) + dos->e_lfanew
    );
    if (nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        nt->OptionalHeader.SizeOfImage < kMenuLayerInitRva + sizeof(kMenuLayerInitSignature) ||
        nt->OptionalHeader.SizeOfImage < kMenuItemCreateRva + 16) {
        log_message(GDML_LOG_WARNING, "The game executable is not the supported Windows x64 build.");
        return false;
    }

    auto* menu_layer_init = reinterpret_cast<unsigned char*>(game) + kMenuLayerInitRva;
    if (std::memcmp(menu_layer_init, kMenuLayerInitSignature,
                    sizeof(kMenuLayerInitSignature)) != 0) {
        log_message(GDML_LOG_WARNING, "GDML main-menu integration does not recognize this Geometry Dash build.");
        return false;
    }

    if (!initialize_cocos_api(game, cocos)) {
        log_message(GDML_LOG_ERROR, "Required Cocos2d menu functions are not available.");
        return false;
    }

    const MH_STATUS init_status = MH_Initialize();
    if (init_status != MH_OK && init_status != MH_ERROR_ALREADY_INITIALIZED) {
        log_message(GDML_LOG_ERROR, "MinHook could not initialize the main-menu hook.");
        return false;
    }

    const auto hook_status = MH_CreateHook(
        menu_layer_init,
        reinterpret_cast<void*>(&menu_layer_init_hook),
        reinterpret_cast<void**>(&g_original_menu_layer_init)
    );
    if (hook_status != MH_OK) {
        log_message(GDML_LOG_ERROR, "MinHook could not create the MenuLayer::init hook.");
        return false;
    }

    if (MH_EnableHook(menu_layer_init) != MH_OK) {
        MH_RemoveHook(menu_layer_init);
        log_message(GDML_LOG_ERROR, "MinHook could not enable the MenuLayer::init hook.");
        return false;
    }

    log_message(GDML_LOG_INFO, "Installed the Geometry Dash 2.2091 main-menu hook.");
    return true;
}
