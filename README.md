# GDModLoader

GDModLoader is an early native mod-loading foundation for Geometry Dash
2.2091. The Windows x64 launcher starts the game suspended, injects a small
bootstrap DLL, loads plugins from `GDMods`, then lets the game start. The
versioned C plugin API is shared by Windows and Android builds.

The editable vector source is `assets/gdml.svg`. Windows CMake configuration
generates multi-size `gdml.ico` and a `gdml.png` preview in `build/generated`,
and embeds the icon in the launcher executable.

On the verified Windows x64 2.2091 build, GDML adds a small round **GDML** logo
button immediately left of Newgrounds. Clicking it opens an in-game list of
discovered plugin DLLs and shows which ones loaded successfully. The manager
has **Installed** and **About** tabs. Mods load when the game starts; enable or
disable controls, plugin UI APIs, and Android game integration are not
implemented yet. Plugins run with the game's full permissions; only load mods
you trust.

The Windows build also packages an experimental **GDML QoL** plugin. On the
verified 2.2091 executable it adds an FPS counter and a two-decimal progress
readout during gameplay. Practice music synchronization remains controlled by
Geometry Dash's existing practice option. The plugin checks the executable
timestamp and code signatures before installing hooks; on other builds it
stays inactive.

## Build and test on Windows

Use a Visual Studio C++ toolchain and CMake:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

The integration test launches a tiny test process through the real launcher
and verifies that the example plugin initializes before that process starts.
It does not launch or modify your Geometry Dash installation. The
game-specific menu hook only installs in the verified Geometry Dash 2.2091
process.

## Run with Geometry Dash 2.2091

1. Copy `gdml_launcher.exe`, `gdml_bootstrap.dll`, and `gdml_loader.dll` from
   `build/Release` into the folder containing `GeometryDash.exe`.
2. Copy `build/Release/GDMods/gdml-button.png` into the `GDMods` folder beside
   the game; the menu button uses this round logo image.
3. Copy `gdml_qol.dll` from `build/Release/GDMods` into that `GDMods` folder
   for the FPS and precise-progress HUD. Copy any other trusted plugin DLLs
   there as well. Do not copy `example_mod.dll` there unless you built it for
   your game.
4. Start the game using `gdml_launcher.exe`. You can pass a different x64 game
   executable as the first argument.
5. Check `GDMods/loader.log` if a plugin fails to load.

The launcher leaves the original game executable unchanged. The bootstrap
loads matching plugins in filename order, installs the 2.2091 menu hook, and
waits for initialization before allowing the game to start. Windows
configuration fetches the MinHook dependency at a pinned upstream revision.
The installer also includes MinHook's BSD license at
`share/licenses/GDModLoader/MinHook-LICENSE.txt`.

## Making a mod

Start with [`templates/basic-mod`](templates/basic-mod). It contains a
buildable CMake project, an example C++ mod class, a `build.ps1` script, and
setup/install instructions. Run `.\build.ps1` to build it, or provide
`-GameDirectory` to build and install the DLL in one step.

Include `gdml/sdk.hpp`, derive a class from `gdml::Mod`, implement `on_load`
and optionally `on_unload`, then add `GDML_DEFINE_MOD(YourModClass)`. The SDK
handles the exported C entry points, API checks, logging, and exception
boundary. The current host API supports lifecycle callbacks and logging
only—it does not yet expose stable gameplay, rendering, or event hooks, so a
basic mod can load but cannot yet change game behavior through supported GDML
APIs. Mods run with the game's full permissions; only load code you trust.

Android uses `.so` plugins built for the same ABI as the host process. Android
sandbox and linker rules require a separate, platform-specific bootstrap and
an app-accessible plugin location; neither is included yet.
