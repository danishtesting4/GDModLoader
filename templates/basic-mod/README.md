# My First GDML Mod

This is a minimal Windows x64 C++ mod project for Geometry Dash 2.2091. Derive
from `gdml::Mod`, implement `on_load`, and use the supplied `gdml::Host` to log.
`GDML_DEFINE_MOD` wires up the loader entry points.

## Build

Install Visual Studio 2022 C++ build tools and CMake. From this directory, build
the template:

```powershell
.\build.ps1
```

The plugin DLL is written to `build/Release/my_first_mod.dll`. To build and
install it in one step, pass the directory containing `GeometryDash.exe`:

```powershell
.\build.ps1 -GameDirectory "C:\path\to\Geometry Dash"
```

## Install

If you did not use `-GameDirectory`, copy `my_first_mod.dll` into the `GDMods`
folder next to `GeometryDash.exe`, then start the game with `gdml_launcher.exe`.
The GDML Installed tab and `GDMods/loader.log` report whether the mod loaded.

Build your own mod by editing `src/mod.cpp`. Return `true` from `on_load` after
successful initialization and release mod-owned resources from `on_unload`.
The SDK catches exceptions at the C plugin boundary and reports them in the
loader log.

## Current SDK boundary

The current GDML host API provides plugin lifecycle callbacks and logging. It
does not yet provide stable Geometry Dash gameplay, rendering, or event APIs.
This template builds and loads a mod, but does not by itself change game
behavior. Plugins execute as native code with the game's permissions; only
install code you trust.
