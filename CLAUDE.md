# pkmn-violet: Pokémon Violet recompilation

The user's own Pokémon Violet **4.0.0** (title 01008F6008C5E000, Teal Mask / Indigo Disk DLC) as a native Windows x64
program on the shared `../nxrecomp`. Workspace rules and toolchain: `../CLAUDE.md`. Paths are relative to
`pkmn-violet/`; game-derived data stays in `rom/`, `extract/`, `dump/`, `build/`, `mods-ref/`. This file: how to do
things, state, what is open. **`NOTES.md`**: findings, addresses, measurements, by topic. Grep it before proposing a
tool or experiment; add findings there. `README.md`: the public-facing description.

## Start here

From the workspace root (`..\start.ps1` wraps `violetrecomp\`'s scripts, which also work alone):

| Do | Command |
|---|---|
| Play (builds the exe and mods first; log `build\run.log`) | `.\start.ps1 violet` (`-NoBuild`, `-Log 2`; `-Timeout <s>` for unattended runs only) |
| Build | `.\start.ps1 build violet` = `..\build.bat violet` = `violetrecomp\build.bat` (first configure ~25 min; keep Release) |
| Re-lift (after a patches.txt or lifter change; per machine, the build doesn't warn) | from `violetrecomp\`: `py -3.12 ..\..\nxrecomp\tools\lifter\a64lift.py lift ..\build\lifted` (~12 min) |
| Room (UDP 24872) | `.\start.ps1 room` (`-Name`, `-Password`); `-Beacon` adds ldn-beacon in the same console |
| Join a room | `.\start.ps1 violet -Room 127.0.0.1:24872 -Nick Host`, or in game: Multiplayer > Rooms |
| Player package | `.\start.ps1 package violet <dir>` (`-PythonZip`, `-ShaderPack`; `-Mods all` copies the DLLs, not their game-derived data) |
| Stand-still benchmark | `violetrecomp\bench.ps1 <name> [NAME=value ...]` (~140 s, hands off; `-Summary`) |
| New shaders | from `violetrecomp\`: `python ..\..\nxrecomp\tools\shader\translate.py --spv`, then run again |
| F8 tables (after changing saves) | in `violetrecomp\tools\svedit`, game closed: `dotnet run -- monsdata ..\..\..\build\save\<user dir>\main ..\..\..\build\mods` |
| Read a romfs file / schema | `py -3.12 violetrecomp\tools\bfbs.py x <romfs path>.bfbs`; `nxrecomp/tools/trinity.py` (needs numpy and `build\ooz`; run inside `violetrecomp\`) |

Never script input (`NX_INPUT`) unless the user asks. bench.ps1 presses A through the title itself
(`NX_INPUT=300-4000/60:A`; the laptop's title takes input only after ~frame 1,800). Never stop a running game,
room or beacon: the user may be mid-session.

**In game:** Esc frees the mouse and shows the menu bar (F1 pins it): Options (Video: frame rate, render scale, FPS,
borderless / Alt+Enter, never exclusive fullscreen; Controls, `build\settings.ini`; Interface), Mods (windows; Manage
disables a DLL for next start), Multiplayer (`nxrecomp/runtime/net/netmenu.c`). F3 FPS, F5 frame-rate cycle (dev),
F12 frame capture to `build\frames\`. The menu is `nxui.dll` (`nxrecomp/ui/nxui.cpp`).

**Mods** (`build\cmake\mods\`, `NX_MODS=0` none; sources `violetrecomp\mods\<name>\`, shared `mods\common\`):
F6 raids (seeds, dens), F7 items (+ Lua console), F8 mons (box writer, eggs, trainer ids, wild spec, spawns around
you), F9 / F10 weather. Data: `python mods\raids\raiddata.py` (needs `build\ooz`), svedit `monsdata` (above).

**Saves:** `build\save\`; backups and test saves in `build\saves\` (`save-net-a` / `-b`: the user set music volume,
don't overwrite). bench.ps1 copies `save` to `build\save-idle-run`. Closed-game edits: `violetrecomp\tools\svedit`
(.NET 11 + PKHeX.Core 26.10.18; back up `build\save` first).

**Switches** (read once at boot): `NX_ROOM`, `NX_NICK`, `NX_MODS=0`, `NX_RES_SCALE`, `NX_FPS=0`, `NX_TIME`,
`NX_VK_VALIDATION`, `NX_PROFILE`, `NX_GPU_PROF`, `NX_GPU_TIMELINE`, `NX_HITCH_MS`, `NX_WILD_LOG=1`, `NX_WATCH_MEM`,
`NX_IPC_TRACE=<interfaces>`, `NX_KSPIN_US`; the rest: grep `NOTES.md`.

## State (2026-11-11)

- Playable with saves: story, battles, catching, raids, riding; ~170 fps uncapped on the RX 7800 XT, ~70-80 on the
  GTX 1650 Ti laptop (GPU-bound). Known rendering issues: missing letters (likely fixed 2026-11-10, unconfirmed),
  no rain streaks near the camera.
- Local play over rooms between instances (Union Circle, Tera raids) and with a **real Switch** through
  `../ldn-beacon` (raids; a link trade connected on the second join attempt 2026-11-11, result not reported).
  Joining from the menu after an offline start works.
- Event raids: the friend's delivery (7-star Rillaboom, identifier 20230728) shows as an event crystal after a
  restart. Not yet fought or caught.
- Packaged builds read the game from the player's own NSPs and keys (`NX_PACKAGE`, `violetrecomp_pkg`).
- Mods user-checked: F6, F7, F8 box writer and wild species swap. **Not tried in game yet:** F8 eggs, Trainers tab
  and trainers seen, random gender, "Spawn on me" (crash fixed 2026-11-09), row spawn, held items, no-battles,
  all-shiny, the rest of the wild spec; the weather mod (type numbers guessed); the menu beyond start-up.

## Open (the user's list, in order)

0. Try the menu: Options (rebind a key and a pad button), Mods (windows, Manage), Interface, Multiplayer (join,
   switch rooms, leave, rejoin; switch Union Circle sessions). Later (asked, not started): host a room from the menu
   (room_server.exe as a child in a job object, join 127.0.0.1, show the LAN address); the room server on the ESP32.
1. **F8 eggs:** make one, hatch it; trade one to the Switch (shiny for that trainer) and check the hatched mon with
   PKHeX (does SV mark a traded egg's egg location as Link Trade?). NOTES "Eggs and trainer ids".
2. **Event raids:** catch the Rillaboom on the Switch with its clock in the event window; past events from PKHeX's
   lists served as deliveries. NOTES "Event raids".
3. Spawn-pool rules (version exclusives in both, time of day, weights, density). NOTES "Wild spawns".
4. Raid crystals after F6 writes: do they show again (`raidgem:` lines)? NOTES "Raid crystals".
5. Missing letters (check text after a long session) and rain. NOTES "Open rendering issues".
6. Performance. NOTES "Performance" (ranked lists from the reference sweeps).
7. Older GPUs (RX 580): plan in `nxrecomp/docs/GPUS.md`, not started (a friend may take it).
8. Beacon: the Switch's first join attempt drops after ~1.7 s, the second stays (2026-11-11 log). For
   `../ldn-beacon/NOTES.md`.

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
| `dump/romfs/`, `dump/aoc/<id>/`, `dump/control/` | merged RomFS (9.24 GB; `arc/data.trpfs` holds almost everything); DLC RomFS; control.nacp |
| `violetrecomp/` | source: `game.json`, `game.cmake`, `build.bat`, `run.ps1`, `bench.ps1`, `patches.txt`, host code (`fps.c`, `gfx.c`, `lua.c`, `wild.c`, `raidgem.c`), `mods/`, `tools/` (svedit, luabc.py, xref.py, bfbs.py, img.py) |
| `mods-ref/` | others' mods for 4.0.0 (reference) |
| `build/cmake/` | `violetrecomp.exe`, `nxui.dll`, `mods/` |
| `build/analysis/`, `build/lifted/`, `build/image/` | IDA / Ghidra exports, lifted C, module images |
| `build/lua/`, `build/mods/`, `build/bcat/`, `build/peek/` | the game's Lua bytecode; mod data; delivery dumps; NCA headers, tickets, control data |

## Commands

- Re-extract (PowerShell from `pkmn-violet/`: `$env:USERPROFILE="$PWD\..\keys"`, `$n` = `..\.tools\nstool.exe`):
  `& $n -x extract\base rom\<base>.nsp; & $n -x extract\upd rom\<update>.nsp`; ExeFS `& $n --tik
  extract\upd\01008f6008c5e800…11.tik -x /0 dump\exefs extract\upd\d24a763a8882575f1c252dce37021db5.nca`; RomFS the
  same with `--basenca extract\base\ef6d3388d34a30dda23208f98780c610.nca -x /1 dump\romfs`.
- `llvm-nm -D [-C] dump/elf/main.elf` lists imports / exports.
- Decompile: `..\nxrecomp\tools\ghidra\decomp.ps1 <abs out path> <off>...` from `violetrecomp\` (GHIDRA_HOME or
  `-Ghidra <dir>`), IDA batch (`idat -A -S<script>` on a copy of the .i64), or REA (cleanup in `../CLAUDE.md`).
