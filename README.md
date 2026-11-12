# First Person Narrative Launcher — Source Code

Source code for `FirstPersonNarrativeLauncher.exe`, a Windows launcher for configuring and launching Alan Wake 2 with the First Person Narrative mod. This repository contains the launcher source code, UI assets, default settings, and a settings test. It does not include the mod's source code or game files.

## Building

You need Windows, Visual Studio 2022 with the Desktop development with C++ workload and Windows SDK, and CMake 3.20 or later.

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

The executable is created at `build/Release/FirstPersonNarrativeLauncher.exe`. The settings in `FirstPersonMod.ini` are embedded in the executable during the build. The game and mod must be installed separately to use the launcher with the mod.

The Inter fonts are distributed under the license in [Inter-OFL.txt](assets/launcher/Inter-OFL.txt). No license is specified in the original project for the remaining code and assets.
