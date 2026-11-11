# pkmn-violet

Pokémon Violet 4.0.0 with its DLC (The Teal Mask, The Indigo Disk), statically recompiled into a native Windows x64
program. The game's own code runs as C lifted from its AArch64 machine code, over the shared
[ns-port](https://github.com/HelixNX/ns-port) runtime: an emulated Switch kernel and services, with the game's
graphics (NVN) replayed through Vulkan.

The story plays with saves: battles, catching, Tera raids, riding. It also adds an
unlocked frame rate, render scale and an in-game menu. Local play works over online rooms, between copies of the
recompilation and with a real Switch through [ldn-beacon](https://github.com/HelixNX/ldn-beacon).

## What works

| | |
|---|---|
| Story, battles, catching, raids, riding, DLC | **works**, with saves |
| Frame rate | any (30, 60, 120, uncapped) at normal game speed; ~170 fps uncapped on an RX 7800 XT, ~70-80 on a GTX 1650 Ti laptop |
| Render scale | 1-4x, switchable while playing |
| Local play (Union Circle, Tera raids) | **works** over rooms (the protocol Citra, yuzu and Eden rooms speak), between recompilations and with a real Switch through ldn-beacon. Link trades connect; a complete trade is not confirmed yet |
| Event raids | a delivery copied from another save shows up after a restart (no Nintendo servers) |
| Known issues | some letters missing in text (a likely fix awaits checking), no rain streaks near the camera |
| GPUs | Vulkan 1.3 with 32-wide compute subgroups and dynamic vertex input; tested on an NVIDIA GTX 1650 Ti and an AMD RX 7800 XT. Older GPUs (e.g. RX 580) are not supported yet |

## What you need

- **Windows x64** and a GPU from the list above.
- **Your own copy of the game**, dumped from your console: the base game, update **4.0.0** (v786432) and, if you have
  it, the DLC, as NSPs. The update version matters: the recompilation is made for exactly that build.
- **Your console's keys** (`prod.keys`, `title.keys`) and **firmware** (for a few system data archives, e.g. the
  word filter local play needs).
- To build: Visual Studio 2022 with "C++ Clang tools", Python 3.12 (`capstone`, `unicorn`, `numpy`), Ghidra 12 or
  IDA 9 for the one-time function export, and about 40 GB of disk. The workspace README lists everything.

Nothing from the game is in this repository, and nothing built from it ever will be: every file the program needs is
made on your machine from your own dumps.

## Get it and build

This repo goes inside the ns-port workspace, as `ns-port/pkmn-violet/`:

```powershell
git clone https://github.com/HelixNX/ns-port.git
cd ns-port
.\start.ps1 clone violet            # or: git clone https://github.com/HelixNX/pkmn-violet.git pkmn-violet
```

Put your NSPs in `pkmn-violet/rom/`, then follow the workspace README's one-time setup: unpack and dump the game,
export its functions, lift them to C (~12 min), and build (~25 min the first time; later builds take seconds to
minutes).

## Play

```powershell
.\start.ps1 violet                   # builds what changed, then starts; the log goes to pkmn-violet\build\run.log
.\start.ps1 violet -NoBuild          # start without building
```

Saves are in `pkmn-violet\build\save\`. Back them up before trying anything risky.

**Controls.** Keyboard and mouse or any controller SDL supports. Keys are rebindable in the menu (Options >
Controls; kept in `build\settings.ini`). Default keyboard: WASD left stick, mouse right stick (click to capture,
Esc to release).

**The menu.** Esc frees the mouse and shows the menu bar (F1 keeps it open):

| Menu | |
|---|---|
| Options | Video (frame rate, render scale, FPS counter, borderless fullscreen / Alt+Enter), Controls, Interface (themes, size) |
| Mods | each mod's windows; Manage turns a mod off for the next start |
| Multiplayer | rooms, nickname, direct connect, leave |

Hotkeys: F3 FPS counter, F12 saves the current frame to `build\frames\`.

## Local play

Two players each run the recompilation (or one runs it with a real Switch next to them) and meet in a room:

```powershell
.\start.ps1 room                                         # host a room on UDP 24872 (-Name, -Password)
.\start.ps1 violet -Room 127.0.0.1:24872 -Nick Host      # join it directly; or start normally and use Multiplayer > Rooms
```

Then use the game's own local play: Union Circle, Tera raids with a link code. For a real Switch,
`.\start.ps1 room -Beacon` also starts ldn-beacon with an ESP32-S3 on USB as its radio (see its README).

## Mods

Mods are DLLs in `build\cmake\mods\`, each with its own windows in the menu. They work on the running game: a mon
or item written by a mod is in your save once the game saves. `NX_MODS=0` starts with none.

| Key | Mod | |
|---|---|---|
| F6 | Tera raids | every den's raid from its seed; write seeds for a chosen species, shiny, Tera type |
| F7 | Items | give or set any item; a Lua console in the game's own script VM |
| F8 | Pokémon | a made-to-order mon or egg into your boxes; wild spawns as a chosen species (shiny, IVs, moves...); spawn groups on you; the trainer ids of your boxed mons |
| F9 / F10 | Weather | toggle rain; set any weather |

The F8 mod needs tables made from PKHeX's data: `dotnet run -- monsdata <save>\main ..\..\..\build\mods` in
`violetrecomp\tools\svedit`, with the game closed (again after switching saves). Eggs follow PKHeX's legality rules for
an unhatched egg, and can be made to hatch shiny for whoever hatches them, e.g. a friend you trade them to.

`svedit` also edits a closed save (ride upgrades, raid difficulty, copying blocks between saves). Back up
`build\save` first.

## For players: a package

```powershell
.\start.ps1 package violet <folder>
```

makes a folder with the program and its tools, but nothing from the game. A player puts their own NSPs, keys and
firmware in its `game\` folder. The program reads the game from the NSPs directly; nothing is extracted.

## How it works

- **Lifting.** The game's three code modules (main 52 MB, the SDK, a movie library: ~16 million instructions) are
  exported with Ghidra or IDA, then every function is lifted to C, compiled with clang-cl and linked into one
  program. C++ exceptions, jump tables and setjmp / longjmp are handled at lift time. A few instructions are patched
  through `violetrecomp/patches.txt` (frame-rate clocks, graphics options, hooks for the mods).
- **The runtime** emulates the Switch's kernel (threads, synchronisation, memory) and the services the game asks for:
  file system over the game's RomFS, audio (Wwise's output, Bink movies), input, saves, time, local play over rooms.
- **Graphics.** NVN calls are recorded and replayed into Vulkan on the render thread. The game's Maxwell shaders are
  translated to SPIR-V by our own translator (all 23,000 shaders of the game ahead of time, or as they appear).
- **Frame rate.** The game steps its clocks by a fixed duration per frame. `fps.c` gives them the real frame time,
  so any frame rate runs at normal speed.

The history of all of it, every finding and every measured change, is in [`NOTES.md`](NOTES.md).

## Credits

- **[PKHeX](https://github.com/kwsch/PKHeX)** (kwsch and contributors): `svedit` uses PKHeX.Core, and its
  documented formats and legality rules shaped the raid and egg tools. The mods' code is our own.
- **The Eden / yuzu / suyu projects**: their shader recompiler and room protocol were read for behaviour; the code
  here is our own.
- **Fl4sh**'s "60 FPS" / "Dynamic FPS" patches, used to find the game's frame clocks.
- Other recompilation projects read for ideas: Savage Wind (Breath of the Wild), the Pokémon
  Legends Z-A and Tears of the Kingdom recompilations.
- [Dear ImGui](https://github.com/ocornut/imgui), [SDL3](https://www.libsdl.org/), [Opus](https://opus-codec.org/),
  [glslang](https://github.com/KhronosGroup/glslang), and [ooz](https://github.com/powzix/ooz) (Oodle Kraken, in a
  separate tool for reading the game's packed files).

## Legal

Not affiliated with or endorsed by Nintendo, The Pokémon Company or Game Freak. This repository holds no code, data,
keys or firmware from Nintendo or the game: the program only runs with files you dump from your own console and game.
Use it with a game you own.
