# pkmn-violet: Pokémon Violet recompilation

The user's own Pokémon Violet **4.0.0** (title 01008F6008C5E000, Teal Mask / Indigo Disk DLC) as a native Windows
x64 program, on the shared `../nxrecomp`. Workspace rules and toolchain: `../CLAUDE.md`. Paths are relative to
`pkmn-violet/`; game-derived data stays in `rom/`, `extract/`, `dump/`, `build/`, `mods-ref/`. **History, findings,
addresses and experiments are in `NOTES.md`**: grep it before proposing a tool or experiment, add findings there;
this file is how-to, state and what is open.

## Start here

From the workspace root (`..\start.ps1` wraps `violetrecomp\`'s scripts, which also work alone):

| Do | Command |
|---|---|
| Play (builds first; log `build\run.log`) | `.\start.ps1 violet` (`-NoBuild`, `-Log 2`, `-Timeout <s>` for unattended runs only) |
| Build (first configure ~25 min; keep Release) | `.\start.ps1 build violet` = `violetrecomp\build.bat` = `..\build.bat violet` |
| Re-lift (after a patches.txt / lifter change; per machine, the build doesn't warn) | `py -3.12 ..\..\nxrecomp\tools\lifter\a64lift.py lift ..\build\lifted` from `violetrecomp\` (~12 min) |
| Host a room (UDP 24872) | `.\start.ps1 room` (`-Name`, `-Password`); join in Multiplayer > Rooms |
| Join directly | `.\start.ps1 violet -Room 127.0.0.1:24872 -Nick Host` (otherwise starts offline; old browser `NX_ROOM_BROWSER=1`) |
| Real Switch in the room | `.\start.ps1 beacon` (`../ldn-beacon/CLAUDE.md`) |
| Player package | `.\start.ps1 package violet <dir>` (`-PythonZip` / `-ShaderPack`; `-Mods all` copies mod DLLs, not their game-derived data) |
| Stand-still benchmark | `violetrecomp\bench.ps1 <name> [NAME=value ...]` (~140 s, hands off; `-Summary`; 60 s is too short) |
| New shaders | `python ..\nxrecomp\tools\shader\translate.py --spv` from `violetrecomp\`, then run again |

Never script input (`NX_INPUT`) unless the user asks. bench.ps1 presses A through the title
(`NX_INPUT=300-4000/60:A`; the GTX 1650 Ti laptop's title takes input only after ~frame 1,800).

**In game:** Esc frees the mouse and shows the menu bar (F1 pins it): Options > Video (frame rate, render scale, FPS
counter, borderless fullscreen / Alt+Enter: never exclusive fullscreen), Controls (all rebindable, `build\settings.ini`),
Interface (themes, menu size); Mods (each mod's windows; Manage disables a DLL for next start); Multiplayer (rooms,
nickname, direct connect, leave; `net/netmenu.c`). F3 FPS, F5 frame-rate cycle (dev), F12 frame capture to
`build\frames\`. The menu is `nxui.dll` (ui/nxui.cpp; without it, plain hotkeys).

**Mods** (DLLs in `build\cmake\mods\`, `NX_MODS=0` none; sources `mods\<name>\`, shared `mods\common\`):
violet_raids (F6: find seeds, write dens), violet_items (F7: give / set, Lua console in the game's VM), violet_mons
(F8: into the boxes, wild spawns to order, "Around you", held items, Egg, Trainers), violet_weather (F9 rain, F10
window). Data: `python mods\raids\raiddata.py` (needs `build\ooz`), `mons_*.txt` / `items.txt` from svedit
`monsdata` / `items` into `build\mods\`.

**Saves:** `build\save\`; backups and test saves in `build\saves\` (`save-net-a` / `-b` for two instances).
bench.ps1 copies `save` to `build\save-idle-run`. Closed-game edits: `violetrecomp\tools\svedit` (.NET 11 +
PKHeX.Core 26.10.18; back up `build\save` first).

**Switches** (read at boot): `NX_ROOM`, `NX_NICK`, `NX_MODS=0`, `NX_RES_SCALE`, `NX_FPS=0`, `NX_VK_VALIDATION`,
`NX_PROFILE`, `NX_GPU_PROF`, `NX_GPU_TIMELINE`, `NX_HITCH_MS`, `NX_WILD_LOG=1`, `NX_WATCH_MEM`,
`NX_IPC_TRACE=<interfaces>`; the rest: grep `NOTES.md`.

## State (2026-11-11)

- Playable with saves: the story (battles, catching, raids, riding); ~170 fps uncapped on the RX 7800 XT, ~70-80 on
  the GTX 1650 Ti laptop (GPU-bound). Known rendering issues: missing letters (run-time glyph atlas), no rain streaks.
- Local play over rooms works between instances (Union Circle, Tera raids) and with a **real Switch** through
  `../ldn-beacon` (three complete raids). Joining from the menu after an offline start works (user-checked).
- Packaged builds read the game from the player's own NSPs and keys (`NX_PACKAGE`, `violetrecomp_pkg`).
- Mods user-checked: F6, F7, F8 box writer and wild species swap. **Not yet tried in game:** F8 "Spawn on me" (crash
  fixed 2026-11-09), row spawn, held items, no-battles toggle, all-shiny, the rest of the wild spec, eggs; the
  weather mod (type numbers guessed); the new menu beyond start-up.

## Open (the user's list, in order)

0. Try the new menu: Options (rebind a key and a pad button), Mods (windows, Manage), Interface, Multiplayer (join,
   switch rooms, leave, rejoin; switch Union Circle sessions). Later (asked, not started): host a room from the menu
   (starts room_server.exe as a child in a job object, joins 127.0.0.1, shows the LAN address); the room server on
   the ESP32 (ldn-beacon, Open).
1. **F8 eggs:** make one, hatch it; trade one to a second instance and hatch there (shiny for that trainer?). Confirm
   the wild spec on a caught mon (overworld vs battle shiny). NOTES.md "Eggs and trainer ids in F8".
2. Spawn-pool rules (wrap the encounter filter / per-entry rules; density). NOTES.md "Spawn-pool rules".
3. **Raid crystals after F6 writes**: rebuild redone 2026-11-09 (dens re-placed from the save by e23ec8); does the
   crystal show? (`raidgem:` lines in run.log). **Event raids** (parked): a friend's 2023-07-28 delivery is in
   `build\save`, waiting for a day reroll; forcing it is next. NOTES.md "Raid crystals", "Event raids".
4. Missing letters and rain: the 2026-11-10 texture fixes (NOTES.md "ZA Recomp") likely cover the letters: check text
   after a long session; rain: NOTES.md "Open rendering issues".
5. Performance: NOTES.md "Performance work", "TotK", "Second reference sweep" (ranked lists).
6. Older GPUs (RX 580: no `EXT_vertex_input_dynamic_state`, `KHR_index_type_uint8`, wave32): plan in
   `nxrecomp/docs/GPUS.md`, not started (a friend may take it).

## ROMs

| File in `rom/` | What |
|---|---|
| `Pokemon Violet [01008F6008C5E000][v0].nsp` (6.4 GB) | base 1.0.0, SDK 13.3.5. Program NCA `ef6d3388…`, control `e9f7d8c6…` |
| `Pokemon Violet [01008F6008C5E800][v786432].nsp` (5.0 GB) | update **4.0.0**, SDK 13.3.13, key gen 17. Program NCA `d24a763a…`, control `48b4e53e…`; ticket `01008f6008c5e800…11.tik` |
| `Pokemon Violet [DLC …] [01008F6008C5F001][v0].nsp` (118 KB) | DLC unlock (content ships in the update): PublicData NCA `16a4c25a…`; ticket `01008f6008c5f001…0f.tik` |

## Files

| Path | What |
|---|---|
| `dump/exefs/`, `dump/elf/` | 4.0.0 code (main, sdk, subsdk0, rtld, main.npdm); ELF copies, IDA `*.i64` |
| `dump/romfs/`, `dump/aoc/<id>/`, `dump/control/` | merged RomFS (9.24 GB); DLC RomFS; control.nacp |
| `violetrecomp/` | source: `game.json`, `game.cmake`, `build.bat`, `run.ps1`, `bench.ps1`, host code (`fps.c`, `gfx.c`, `lua.c`, `wild.c`, `raidgem.c`), `patches.txt`, `mods/`, `tools/` (svedit, luabc.py, xref.py, bfbs.py, img.py) |
| `mods-ref/` | others' mods for 4.0.0 (reference) |
| `build/cmake/` | `violetrecomp.exe`, `mods/` |
| `build/analysis/`, `build/lifted/`, `build/image/` | IDA / Ghidra exports, lifted C, module images |
| `build/lua/`, `build/mods/`, `build/peek/` | the game's Lua bytecode; mod data; NCA headers, tickets, control data |

## Commands

- Re-extract (`extract/` was deleted; PowerShell from `pkmn-violet/`: `$env:USERPROFILE="$PWD\..\keys"`,
  `$n` = `..\.tools\nstool.exe`): `& $n -x extract\base rom\<base>.nsp; & $n -x extract\upd rom\<update>.nsp`, ExeFS
  `& $n --tik extract\upd\01008f6008c5e800…11.tik -x /0 dump\exefs extract\upd\d24a763a8882575f1c252dce37021db5.nca`,
  RomFS the same with `--basenca extract\base\ef6d3388d34a30dda23208f98780c610.nca -x /1 dump\romfs`.
- `llvm-nm -D [-C] dump/elf/main.elf` lists imports / exports.
- Decompile: `..\nxrecomp\tools\ghidra\decomp.ps1 <abs out path> <off>...` from `violetrecomp\` (GHIDRA_HOME or
  `-Ghidra <dir>`), IDA batch (`idat -A -S<script>` on a copy of the .i64), or REA (cleanup in `../CLAUDE.md`).
