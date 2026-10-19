#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include "gdml/sdk.hpp"

#include <MinHook.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>

namespace {

constexpr std::uintptr_t kPlayLayerInitRva = 0x5039e0;
constexpr std::uintptr_t kGameLayerUpdateRva = 0x3406d0;
constexpr std::uintptr_t kPlayLayerExitRva = 0x51b550;
constexpr std::uintptr_t kGetCurrentPercentRva = 0x512300;
constexpr DWORD kGameTimestamp = 0x6ad2d987;

constexpr std::array<unsigned char, 24> kPlayLayerInitSignature{
    0x48, 0x8b, 0xc4, 0x48, 0x89, 0x58, 0x10, 0x48,
    0x89, 0x70, 0x18, 0x48, 0x89, 0x78, 0x20, 0x55,
    0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57
};

constexpr std::array<unsigned char, 24> kGameLayerUpdateSignature{
    0x48, 0x8b, 0xc4, 0x48, 0x89, 0x58, 0x18, 0x55,
    0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56,
    0x41, 0x57, 0x48, 0x8d, 0x6c, 0x24, 0x90, 0x48
};

constexpr std::array<unsigned char, 24> kPlayLayerExitSignature{
    0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b,
    0xd9, 0xe8, 0x52, 0x34, 0x00, 0x00, 0xff, 0x15,
    0xac, 0x47, 0x1b, 0x00, 0x80, 0xb8, 0xe1, 0x00
};

constexpr std::array<unsigned char, 24> kGetCurrentPercentSignature{
    0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x80, 0xb9,
    0x7e, 0x31, 0x00, 0x00, 0x00, 0x48, 0x8b, 0xd9,
    0x74, 0x18, 0x48, 0x8b, 0x81, 0x68, 0x09, 0x00
};

struct CCSize {
    float width;
    float height;
};

using PlayLayerInitFn = bool(__fastcall*)(void*, void*, bool);
using GameLayerUpdateFn = void(__fastcall*)(void*, float);
using PlayLayerExitFn = void(__fastcall*)(void*);
using GetCurrentPercentFn = float(__fastcall*)(void*);
using CreateLabelFn = void*(__fastcall*)(const char*, const char*);
using AddChildFn = void(__fastcall*)(void*, void*);
using SetPositionFn = void(__fastcall*)(void*, float, float);
using SetScaleFn = void(__fastcall*)(void*, float);
using SetStringFn = void(__fastcall*)(void*, const char*);
using GetContentSizeFn = const CCSize*(__fastcall*)(void*);

PlayLayerInitFn g_original_play_layer_init = nullptr;
GameLayerUpdateFn g_original_game_layer_update = nullptr;
PlayLayerExitFn g_original_play_layer_exit = nullptr;
GetCurrentPercentFn g_get_current_percent = nullptr;
CreateLabelFn g_create_label = nullptr;
AddChildFn g_add_child = nullptr;
SetPositionFn g_set_position = nullptr;
SetScaleFn g_set_scale = nullptr;
SetStringFn g_set_string = nullptr;
GetContentSizeFn g_get_content_size = nullptr;

gdml::Host* g_host = nullptr;
void* g_play_layer = nullptr;
void* g_fps_label = nullptr;
void* g_percent_label = nullptr;
std::chrono::steady_clock::time_point g_last_fps_sample{};
unsigned int g_frame_count = 0;
unsigned int g_displayed_fps = 0;

void log_message(GDML_LogLevel level, const char* message) {
    if (g_host != nullptr) {
        g_host->log(level, message);
    }
}

template <typename Function>
Function cocos_function(HMODULE cocos, const char* name) {
    return reinterpret_cast<Function>(GetProcAddress(cocos, name));
}

bool initialize_cocos_api(HMODULE cocos) {
    g_create_label = cocos_function<CreateLabelFn>(
        cocos, "?create@CCLabelBMFont@cocos2d@@SAPEAV12@PEBD0@Z"
    );
    g_add_child = cocos_function<AddChildFn>(
        cocos, "?addChild@CCNode@cocos2d@@UEAAXPEAV12@@Z"
    );
    g_set_position = cocos_function<SetPositionFn>(
        cocos, "?setPosition@CCNode@cocos2d@@UEAAXMM@Z"
    );
    g_set_scale = cocos_function<SetScaleFn>(
        cocos, "?setScale@CCNode@cocos2d@@UEAAXM@Z"
    );
    g_set_string = cocos_function<SetStringFn>(
        cocos, "?setString@CCLabelBMFont@cocos2d@@QEAAXPEBD@Z"
    );
    g_get_content_size = cocos_function<GetContentSizeFn>(
        cocos, "?getContentSize@CCNode@cocos2d@@UEBAAEBVCCSize@2@XZ"
    );
    return g_create_label != nullptr && g_add_child != nullptr &&
           g_set_position != nullptr && g_set_scale != nullptr &&
           g_set_string != nullptr && g_get_content_size != nullptr;
}

bool is_supported_game(HMODULE game) {
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(game);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0) {
        return false;
    }
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(
        reinterpret_cast<const unsigned char*>(game) + dos->e_lfanew
    );
    if (nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        nt->FileHeader.TimeDateStamp != kGameTimestamp) {
        return false;
    }

    const auto image_size = nt->OptionalHeader.SizeOfImage;
    const auto* base = reinterpret_cast<const unsigned char*>(game);
    const auto matches = [base, image_size](
        std::uintptr_t rva, const auto& signature
    ) {
        return rva <= image_size &&
               signature.size() <= image_size - rva &&
               std::memcmp(base + rva, signature.data(), signature.size()) == 0;
    };
    return matches(kPlayLayerInitRva, kPlayLayerInitSignature) &&
           matches(kGameLayerUpdateRva, kGameLayerUpdateSignature) &&
           matches(kPlayLayerExitRva, kPlayLayerExitSignature) &&
           matches(kGetCurrentPercentRva, kGetCurrentPercentSignature);
}

void create_overlay_labels(void* play_layer) {
    const CCSize* size = g_get_content_size(play_layer);
    if (size == nullptr || size->width < 320.0f || size->height < 240.0f) {
        log_message(GDML_LOG_ERROR, "Could not determine the gameplay HUD dimensions.");
        return;
    }

    g_fps_label = g_create_label("FPS: --", "goldFont.fnt");
    g_percent_label = g_create_label("0.00%", "goldFont.fnt");
    if (g_fps_label == nullptr || g_percent_label == nullptr) {
        g_fps_label = nullptr;
        g_percent_label = nullptr;
        log_message(GDML_LOG_ERROR, "Could not create the GDML QoL HUD labels.");
        return;
    }

    g_set_scale(g_fps_label, 0.42f);
    g_set_scale(g_percent_label, 0.48f);
    g_set_position(g_fps_label, size->width - 42.0f, size->height - 16.0f);
    g_set_position(g_percent_label, size->width * 0.5f, size->height - 16.0f);
    g_add_child(play_layer, g_fps_label);
    g_add_child(play_layer, g_percent_label);
    log_message(GDML_LOG_INFO, "Added the FPS and precise-progress HUD.");
}

bool __fastcall play_layer_init_hook(void* self, void* level, bool dont_create_objects) {
    const bool initialized = g_original_play_layer_init(self, level, dont_create_objects);
    if (initialized) {
        g_play_layer = self;
        g_fps_label = nullptr;
        g_percent_label = nullptr;
        g_frame_count = 0;
        g_displayed_fps = 0;
        g_last_fps_sample = std::chrono::steady_clock::now();
        create_overlay_labels(self);
    }
    return initialized;
}

void __fastcall game_layer_update_hook(void* self, float dt) {
    g_original_game_layer_update(self, dt);
    if (self != g_play_layer) {
        return;
    }

    ++g_frame_count;
    const auto now = std::chrono::steady_clock::now();
    const auto elapsed = std::chrono::duration<float>(now - g_last_fps_sample).count();
    if (elapsed >= 0.5f) {
        g_displayed_fps = static_cast<unsigned int>(
            static_cast<float>(g_frame_count) / elapsed + 0.5f
        );
        g_frame_count = 0;
        g_last_fps_sample = now;
        if (g_fps_label != nullptr) {
            char text[32]{};
            sprintf_s(text, "FPS: %u", g_displayed_fps);
            g_set_string(g_fps_label, text);
        }
    }

    if (g_percent_label != nullptr) {
        const float percent = g_get_current_percent(self);
        if (std::isfinite(percent)) {
            char text[32]{};
            sprintf_s(text, "%.2f%%", (std::clamp)(percent, 0.0f, 100.0f));
            g_set_string(g_percent_label, text);
        }
    }
}

void __fastcall play_layer_exit_hook(void* self) {
    if (self == g_play_layer) {
        g_play_layer = nullptr;
        g_fps_label = nullptr;
        g_percent_label = nullptr;
    }
    g_original_play_layer_exit(self);
}

bool create_hook(
    HMODULE game,
    std::uintptr_t rva,
    void* detour,
    void** original
) {
    return MH_CreateHook(
        reinterpret_cast<unsigned char*>(game) + rva, detour, original
    ) == MH_OK;
}

void remove_hooks(HMODULE game) {
    if (game == nullptr) {
        return;
    }
    const std::array<std::uintptr_t, 3> rvas{
        kPlayLayerInitRva, kGameLayerUpdateRva, kPlayLayerExitRva
    };
    for (const auto rva : rvas) {
        void* target = reinterpret_cast<unsigned char*>(game) + rva;
        MH_DisableHook(target);
        MH_RemoveHook(target);
    }
    g_play_layer = nullptr;
    g_fps_label = nullptr;
    g_percent_label = nullptr;
}

class GDMLQolMod final : public gdml::Mod {
public:
    bool on_load(gdml::Host& host) override {
        g_host = &host;
        HMODULE game = GetModuleHandleW(nullptr);
        if (game == nullptr || !is_supported_game(game)) {
            host.warning("GDML QoL HUD is inactive: this is not the verified Geometry Dash 2.2091 build.");
            return true;
        }

        HMODULE cocos = GetModuleHandleW(L"libcocos2d.dll");
        if (cocos == nullptr || !initialize_cocos_api(cocos)) {
            host.error("GDML QoL HUD could not resolve the required Cocos2d functions.");
            return false;
        }

        const MH_STATUS init_status = MH_Initialize();
        if (init_status != MH_OK && init_status != MH_ERROR_ALREADY_INITIALIZED) {
            host.error("GDML QoL HUD could not initialize MinHook.");
            return false;
        }

        const bool hooks_created =
            create_hook(game, kPlayLayerInitRva,
                        reinterpret_cast<void*>(&play_layer_init_hook),
                        reinterpret_cast<void**>(&g_original_play_layer_init)) &&
            create_hook(game, kGameLayerUpdateRva,
                        reinterpret_cast<void*>(&game_layer_update_hook),
                        reinterpret_cast<void**>(&g_original_game_layer_update)) &&
            create_hook(game, kPlayLayerExitRva,
                        reinterpret_cast<void*>(&play_layer_exit_hook),
                        reinterpret_cast<void**>(&g_original_play_layer_exit));
        if (!hooks_created) {
            remove_hooks(game);
            host.error("GDML QoL HUD could not create all gameplay hooks.");
            return false;
        }

        const std::array<std::uintptr_t, 3> rvas{
            kPlayLayerInitRva, kGameLayerUpdateRva, kPlayLayerExitRva
        };
        for (const auto rva : rvas) {
            if (MH_EnableHook(reinterpret_cast<unsigned char*>(game) + rva) != MH_OK) {
                remove_hooks(game);
                host.error("GDML QoL HUD could not enable all gameplay hooks.");
                return false;
            }
        }

        m_game = game;
        host.info("GDML QoL HUD enabled: FPS counter and two-decimal progress.");
        host.info("Practice music sync remains controlled by Geometry Dash's existing practice option.");
        return true;
    }

    void on_unload() override {
        remove_hooks(m_game);
        m_game = nullptr;
        g_host = nullptr;
    }

private:
    HMODULE m_game = nullptr;
};

} // namespace

GDML_DEFINE_MOD(GDMLQolMod)
