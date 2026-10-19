#include <gdml/sdk.hpp>

class MyFirstMod final : public gdml::Mod {
public:
    bool on_load(gdml::Host& host) override {
        host_ = host;
        host_.info("My First GDML Mod loaded.");
        return true;
    }

    void on_unload() override {
        host_.info("My First GDML Mod unloaded.");
    }

private:
    gdml::Host host_;
};

GDML_DEFINE_MOD(MyFirstMod)
