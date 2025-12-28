# Logicraft

Logicraft is a 3D sandbox for building digital circuits. Place wires and logic blocks, watch signals move through your builds, and turn ideas into working machines.

![7-segment counter circuit](images/7seg-counter.png)

Highlights:
- Build real logic systems with visual feedback.
- Mix architecture blocks with electronics to make readable machines.
- Included example map: `maps/syslog.bulldog` (logic circuits + a 7-segment counter).

## What you can build
- Counters, clocks, and timers
- Combinational logic (adders, decoders, muxes)
- Visual gadgets (LED readouts, 7-seg displays)

## Logic blocks

![all blocks available](images/blocks.png)

- Wires, Button, LED, Sign
- AND, OR, NOT, XOR
- D Flip-Flop
- Adder
- Splitter, Merger
- Decoder, Multiplexer
- Comparator
- Clock

## World blocks
- Grass, Dirt, Stone, Wood, Leaves
- Water, Plank, Sand, Glass

## Play (Windows) - players only
If you have the installer:
- Run the `Logicraft-*.exe` installer.
- Launch from the Start Menu or the desktop shortcut.

If you only have a build folder:
- Run `logicraft.exe` next to `images/`, `maps/`, and `config.cfg`.

## Controls (Players)
- Mouse: look around
- W/A/S/D (or Z/Q on AZERTY): move
- Space: jump (double tap to toggle fly)
- Shift: descend (in fly mode)
- Left click: break block
- Right click: place block / toggle button / edit sign
- Mouse wheel: cycle hotbar
- 1-8: select hotbar slot
- E: open inventory
- Q: open block settings (button, splitter/merger, wire info, clock)
- R: return to spawn
- F11: toggle fullscreen
- ESC: pause menu / close dialogs
- Tab: switch fields in edit menus, suggest save name in save menu

## Maps and saves (Players)
- Press `ESC` and choose "SAVE / LOAD" to open the save menu.
- "Create" saves a new map with the name you type.
- "Overwrite" replaces the selected save.
- "Load" opens the selected save.


```
==================================================
  PLAYERS: You can ignore everything below this.
==================================================
```

## Dev and build (Windows) - for contributors

### Build from source
Prerequisites:
- CMake 3.21+
- vcpkg (example: `C:\\vcpkg`)
- Visual Studio Build Tools 2022 (cl.exe)

Configure and build (PowerShell):
```powershell
cmake -B build -S . -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_TOOLCHAIN_FILE="C:/vcpkg/scripts/buildsystems/vcpkg.cmake" `
  -DVCPKG_TARGET_TRIPLET=x64-windows

cmake --build build --config Release
```

Run:
```powershell
.\build\Release\logicraft.exe
```

### Create the installer
Prerequisite:
- NSIS (needed by CPack to generate the installer)

Optional icon (app + installer):
- Put `images/logicraft.ico` (multi-size .ico recommended).

Build and package:
```powershell
cmake -S . -B build
cmake --build build --config Release
cpack -C Release --config build\CPackConfig.cmake
```
The installer will be created in `build`.

## Project structure
- `src/` : C++ code
- `images/` : textures (BMP)
- `maps/` : save files (`.bulldog`)
- `config.cfg` : user configuration
- `CMakeLists.txt` + `vcpkg.json` : build and dependencies
- `build/` : CMake output (ignored by git)

## Notes
- SDL2 and GLEW are linked dynamically by default. DLLs are copied next to the exe so it runs without extra setup.
- To avoid DLL copies, use `-DVCPKG_APPLOCAL_DEPS=OFF` and add `C:\\vcpkg\\installed\\x64-windows\\bin` to `PATH`, or use the static triplet `x64-windows-static`.
