#ifndef GDML_SDK_HPP
#define GDML_SDK_HPP

#include "gdml/plugin.h"

#include <exception>
#include <type_traits>

namespace gdml {

template <typename ModType>
int load_mod(const GDML_HostApi* api) noexcept;

template <typename ModType>
void unload_mod() noexcept;

template <typename ModType>
ModType& mod_instance() {
    static ModType mod;
    return mod;
}

class Host {
public:
    bool initialize(const GDML_HostApi* api) noexcept {
        if (api == nullptr ||
            api->struct_size < sizeof(GDML_HostApi) ||
            api->api_version != GDML_HOST_API_VERSION ||
            api->log == nullptr) {
            return false;
        }
        api_ = *api;
        initialized_ = true;
        return true;
    }

    void log(GDML_LogLevel level, const char* message) const noexcept {
        if (initialized_ && message != nullptr) {
            api_.log(api_.context, level, message);
        }
    }

    void info(const char* message) const noexcept {
        log(GDML_LOG_INFO, message);
    }

    void warning(const char* message) const noexcept {
        log(GDML_LOG_WARNING, message);
    }

    void error(const char* message) const noexcept {
        log(GDML_LOG_ERROR, message);
    }

    bool is_available() const noexcept {
        return initialized_;
    }

private:
    GDML_HostApi api_{};
    bool initialized_ = false;
};

class Mod {
public:
    virtual ~Mod() = default;

    virtual bool on_load(Host& host) = 0;
    virtual void on_unload() {}

private:
    Host host_;
    Host& sdk_host() noexcept {
        return host_;
    }

    template <typename ModType>
    friend int load_mod(const GDML_HostApi* api) noexcept;
    template <typename ModType>
    friend void unload_mod() noexcept;
};

template <typename ModType>
int load_mod(const GDML_HostApi* api) noexcept {
    static_assert(std::is_base_of<Mod, ModType>::value,
                  "GDML mod classes must derive from gdml::Mod");
    auto& mod = mod_instance<ModType>();
    auto& host = mod.sdk_host();
    if (!host.initialize(api)) {
        return 0;
    }

    try {
        if (!mod.on_load(host)) {
            host.error("The mod reported that initialization failed.");
            return 0;
        }
    } catch (const std::exception& error) {
        host.error(error.what());
        return 0;
    } catch (...) {
        host.error("The mod threw an unknown exception during initialization.");
        return 0;
    }
    return 1;
}

template <typename ModType>
void unload_mod() noexcept {
    static_assert(std::is_base_of<Mod, ModType>::value,
                  "GDML mod classes must derive from gdml::Mod");
    auto& mod = mod_instance<ModType>();
    try {
        mod.on_unload();
    } catch (const std::exception& error) {
        mod.sdk_host().error(error.what());
    } catch (...) {
        mod.sdk_host().error("The mod threw an unknown exception during shutdown.");
    }
}

} // namespace gdml

#define GDML_DEFINE_MOD(ModType) \
    extern "C" GDML_PLUGIN_EXPORT int GDML_CALL gdml_plugin_init( \
        const GDML_HostApi* host \
    ) { \
        return ::gdml::load_mod<ModType>(host); \
    } \
    extern "C" GDML_PLUGIN_EXPORT void GDML_CALL gdml_plugin_shutdown(void) { \
        ::gdml::unload_mod<ModType>(); \
    }

#endif
