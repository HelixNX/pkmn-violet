# pkmn-violet: notes

Findings, addresses, measurements and rule-outs, by topic. `CLAUDE.md` holds how to do things, state and what is
open. Grep this before proposing a tool or an experiment. Condensed 2026-11-23: session-by-session accounts (run
names, every A/B pair, wrong turns) are in git history (`git log -p NOTES.md`, before that date). Addresses are
main.elf offsets (main = 0x7100000000) unless noted; `main+x` in host stacks is truncated past 16 MB (`#line` is
24-bit in CodeView): main+416f28 means main+3416f28.

## The game (4.0.0 ExeFS)

| Module | text | ro | data+bss | Notes |
|---|---|---|---|---|
| main | 0x343fc90 (52.2 MB, ~13.7 M insns) | 0xf420a0 | 0x368868 + 0xe6e38 | game, engine ("Trinity"), all middleware static; 1,013 imports |
| sdk | 0x5d5660 (5.8 MB) | 0x6add7c | | NintendoSDK 13.3.13 (MK8: 4.4.0) |
| subsdk0 | 0x33e7d0 (3.2 MB) | 0x210194 | | `movie::` (android NuPlayer/ACodec), as MK8 |

- ~16 M instructions (3.4x MK8). **C++ exceptions used** (std, gRPC, Havok, engine). libc musl-style from the sdk.
- Graphics: NintendoSDK_gfx 13.3.13 (`nn::gfx`) + NintendoWare G3d / Vfx / Ui2d / Font over NVN; `glslc*` weak, never
  called. Middleware in main: Havok AI 2021.1, Oodle, Wwise, Bink 2 (`romfs/demo/movie/*.bk2`), Lua, FlatBuffers,
  nlib LZ4, Pia 6.20.1 + NPLN 1.8.3 + gRPC + OpenSSL + libcurl.
- Services in main.npdm (36): acc:u0 aoc:u appletOE audin:u audout:u audren:u banana bcat:u bsd:u bsdcfg btm:u
  caps:su caps:u csrng ectx:aw friend:u fsp-srv htc:tenv htcs hwopus ldn:u ldr:ro mii:u mm:u nfc:mf:u nfc:user
  nfp:user ngct:u nifm:u nim:eca notif:a nsd:u nvdrv pctl pl:u prepo:u sfdnsres time:u vi:u.
- Keys: every NCA decrypts with `../keys` (key gens 14, 15, 17). `title.keys` has no Violet entries: update and DLC
  carry tickets (`--tik`). DLC owned via `game.json` "dlc": [1]; its PublicData RomFS is one `keyfile.dat`.
- **RomFS** (30 files, 9.24 GB): almost all `arc/data.trpfs` (8.0 GB): `ONEPACK\0`, u64 offset of a FlatBuffers table
  {pack hashes, pack offsets} (23,589 packs). Pack hash = FNV-1a 64 with basis `0xcbf29ce484222645` (not ...2325) of
  the name as `data.trpfd` stores it (slashes dropped). Pack = {file hashes, files[]}; file {field 1 = 3 / field 2 =
  4 when Oodle, u64 size, data}. Oodle Kraken via ooz (`deps/ooz`, GPLv3+) in `oozbatch.exe`. 408,641 files, 34 GB.
  `nxrecomp/tools/trinity.py` reads any file by path (needs numpy; inside `violetrecomp\`).
- Game logic is **Haxe compiled to Lua 5.4** (`haxe_lua/invoker.h`, CallImpl); scripts in romfs
  `script/lua/bin/release/{main,main_dynamic,dll_*}/*.blua` with debug info and obfuscated names (classes `C<hash>`,
  statics `S<hash>`, methods `F<hash>`, natives `c<hash>` / `f<hash>`); extracted to `build/lua/`.
- Data files are FlatBuffers with `.bfbs` schemas next to them (`violetrecomp/tools/bfbs.py`). Names:
  `message/dat/English/common/*.dat`. Personal table `avalon/data/personal_array.bin` field 0 = {DevID, form,
  national, ?}. Species are stored by DevID (internal index; Espathra national 956 -> 927).
- ldn:u's local communication id is the NACP's first LocalCommunicationId = Scarlet's 0100A3D008C5C000 (both games
  meet on it). Dev builds read `dump/control/control.nacp`.
- **Memory setup** (`main+146e5f0`, the Switch's settings over the game's PC-like defaults): shadow size (settings
  +48) 2048 -> 1024, +52 4 -> 2 (cascades?); Trinity heaps (`[x19]`: Gfx2 768 -> 900 MB, Vfx2 96 -> 68, Physics
  64 -> 140, Ui2d 64 -> 32, Fs 1 GB -> 800 MB, Lua 96 -> 104, Sound 64, EventSystem 64 -> 8, Object 256; created
  by name in the caller of `sub_146EA10`, hexrays main_7.c); NVN memory pools (`[x19+0x40]`), slot 4 320 -> 268 MB.

Plan V0-V8 (framework, lifter, boot, main loop, graphics, audio / movies / input / saves, playable) done; V7
performance and PC features ongoing (quality options are ours: render scale, AA, shadow size; LOD / draw distance /
grass are the game's data); V8 packaging done as `package.ps1` (open: a one-folder build kit). **Never ship anything
built from the game**, not even to owners.

## Lifter and build

- Bases: main 0x7100000000, subsdk0 0x7104800000, sdk 0x7105000000 (sizes 0x47d3000, 0x69a000, 0xda8000).
- Function export: IDA (main 37 min, 165,000 functions) or Ghidra 12.1.3 (`tools/ghidra/ghidra_export.ps1 -Seeds`;
  same `<module>_ida.json`; keep "Non-Returning Functions - Discovered" off: it cut 9,706 sdk functions short).
  a64lift repeats function / jump-table recovery to a fixed point.
- All 15.86 M instructions lift (6 header words in main stay traps). Jump tables IDA missed recovered by pattern
  (`Mod.recover_switches`); BL-target function recovery; fall-through into IDA-split functions; weak imports nobody
  defines get trap stubs (gRPC TLS, `getentropy`). `_writes` is exact (a Bink thread once jumped into an
  unrecovered table because `ldr w19, [x7], #4` counted as writing Rt2).
- Lua's luaD_throw needs setjmp / longjmp: host jump buffer table 256 per thread.
- **Lift:** 911 files, ~1.2 GB of C. The module analysis is cached in `build/modcache`, keyed on a hash of the
  analysis code (`analysis_version`): a lifter change elsewhere keeps it. Workers min(logical cores, (free - 1.5 GB) /
  0.6 GB). Full lift: desktop 25 s (16 workers, warm cache), laptop 131 s (7 workers); analysis of all modules ~86 s
  when redone. (2026-11-23 speed-ups: BR prefilter in `_maybe_table`, words from a uint32 view, compact cache, all
  chunks to the pool at once, switches found by bisect; output byte-identical.)
- **Build:** LIFT_JOBS=0 = min(cores, RAM / 2 GB), 3 GB per lifted compile job above -O1; -O3 for lifted code and
  runtime since 2026-11-16 (`NX_LIFT_OPT`); full build ~4.4 min desktop, ~20 min laptop. Tests: `a64test` (`py -3.12`,
  capstone + unicorn).
- **`patches.txt`**: "if" alternates switched at run time, "call" / "after" host hooks (`h_<name>(Ctx *)`, Win64).
  The lifter patches no branches: hook the instruction before or after. **Lifted code is per machine: a patches.txt
  or lifter change needs a re-lift; the build doesn't warn.**
- Calling convention per game (game.json `lift_cc`): Violet **sysv** (no xmm6-15 saves). Calls store x0-x17 only when
  written since the last call and reload only what is read after (`call_masks`, exact); x19-x29 only for callees that
  read them from their caller (`csr_flow`: per entry, must-written / must-spilled, needs passed through calls and tail
  jumps; funcs.h `CSR_<entry>`, `SAVE_FOR`; BLR targets flagged in bit 0 of the function table). Returns keep SAVEC
  (a callee's Ctx slot is the live value of registers it never touched). A hook site counts as reading x29 unless its
  function wrote it on every path (lua.c `h_gc_fullgc` reads the caller's frame). Lessons the analysis needed, each
  from a crash: spilled-on-every-path is never the caller's; per path and per entry, not per function;
  `unw_getcontext` is a capture, not a use; setjmp passes nothing on. Debug lifts: `NX_CSR_POISON=1`,
  `NX_CSR_KEEP=bl,blr,fp,x`, `NX_CSR_DEBUG=1`.
- **Exclusive loops fused** (`excl_fusions`, `NX_EXCL_FUSE=0` off): `LD(A)XR; ops; ST(L)XR; CBNZ` as one host atomic:
  exchange (18,822), fetch-add / sub (32,240), else a CAS loop (65,156). Kept out: branches into the sequence, Xn
  written in it, hook sites, Rt = Rn, LDXP / STXP.
- **PGO** (opt-in): `NX_PGO=gen` instruments lifted code, a64rt and the exe (the runtime writes `pgo-<pid>.profraw`
  every 30 s and at exit); `llvm-profdata merge`, then `NX_PGO=use NX_PGO_PROFILE=x.profdata` (tree `build/cmake_pgo`,
  profile `build/pgo/violet.profdata` from ~25 min of play; raw 6.1 GB). A profile fits one lift. IR-level
  instrumentation (`-fprofile-generate`) would make the instrumented exe faster to play: not tried.
- Not worth it: C vs C++ for generated code; decompiling the game to recompile natively; `NX_STACK_PLAIN`.

## Runtime fixes (kernel, services, audio)

- Start-up: SDK 13 `nn::init::Start` with DT_FINI, TIPC (sm:), MapMemory from .bss / Normal memory, GetInfo 16 / 20,
  MapPhysicalMemory (the game's 12 GB allocator; data also in the alias region), time shared memory,
  CreateManagedDisplaySeparableLayer, nvnProgramSetShadersExt, fsp 53 / other titles' saves = not found, bcat
  (empty), csrng, btm, audout:u (Wwise, 48 kHz stereo), hwopus (libopus), bsd:u (ENETDOWN), ngct:u; unknown commands
  answer with a stub object. Library applets: 0x11 keyboard (a Win32 input box, swkbd.c; config texts 4 bytes earlier
  than thought: header 0x34, sub 0xb6, guide 0x1b8, limit 0x3bc); web 0x13, 0x14, 0x17-0x1a.
- Lost wake-up in `condvar_wait`: the key's waiter flag must be set before the mutex is released.
- audout: GetReleasedAudioOutBuffers zeroes the rest of the output array; buffers released on a playback clock.
  audren GetWorkBufferSize = the console's sum (0x1d000 for 32 voices; more overran a shader file: the ~4,300-frame
  Bink crash, found with `NX_WATCH_MEM` on the nlib heap's span tags). REV7+ mix sections start with a 0x20 header
  {0xCAFEBABE, count}, each mix's id at +0x10.
- hid writes all 7 lifos of all 10 npads (missing ones drifted the cursor); six-axis lifos at NPAD +0x1758, 0x708 each.
- Guest timed waits use QPC µs; `timeBeginPeriod(1)`; EcoQoS opted out; svcSleepThread spins its last 0.25 ms.
- Threads: the game pins main + render thread (5c) to core 0, workers 5a / 5b to 1 / 2. The kernel moves a core's
  owner to an idle core when a thread is ready behind it (`core_move`, `NX_CORE_MIGRATE=0` off: a save's helper thread
  held 5a's core for ~65 ms). `kspin` (polls ~20 µs before sleeping, `NX_KSPIN_US`) off by default: no gain.
- Saves are written in place (a crash mid-save can corrupt `main`); Z-A writes `<file>.pending` + MoveFileEx.

## Graphics

### Translator (Maxwell -> GLSL -> SPIR-V)

- Supports BRX jump tables, PBK/BRK, loops, storage images (bound or bindless; NVN keeps image handles in c[2] at
  0x120 + 8k), subgroup ops, global ATOM, etc.; compute code starts at control word 0x708. c0[0x640] / c0[0x644] =
  base vertex / instance.
- Checked against Eden on 6,009 shaders; NaN handling (`.SAT` -> `sat()`, FMNMX -> NMin / NMax, F2I NaN -> 0x80000000;
  DenormFlushToZero / SignedZeroInfNanPreserve declared), FMUL.RZ, I2F.RZ, .FTZ, IPA.MULTIPLY's multiplier, TMML.
  Latent (wrong but unused): HSETP2 compare bits, F2F f16 rounding, I2F.U32.NEG, FMUL scale 7, PSETP 2nd dest,
  ISCADD .PO, SHFL UP/IDX masks, BFE.S offset >= 32, IPA centroid / offset, S2R Y direction, depth-compare with
  LL/bias/1D, AOFFI on non-integer fetches. Unsupported: IMAD, IMUL, PRMT, SHF, P2R, CSET, doubles, most f16, TLD4
  PTP, multisample TLDS.
- Vertex-input variants for 8/16-bit integer formats (`translate.py --only=<hash>:<mask>`); only mask `01000000` is
  needed (XFB skinning bone indices). Alpha test is a specialization constant (ALPHA_FUNC); the integer-texture
  choice of a fixed slot is the specialization constant `TEX_INT`.
- Render scale in shaders: bit 18 of a handle marks a scaled texture, pc.scale its factor; the sub-pixel offset is
  worked out once per shader (`rs_init`: 20 software integer divisions per scaled texelFetch were ~28% of the
  lighting shader). Not done: XMAD triples -> one multiply, FSWZADD -> dFdxFine, `precise` (try if z-fighting).

### Shaders from the game files

- `nxrecomp/tools/shader/extract/` (inside violetrecomp: `build.bat ..\build\ooz`, then `extract.py`, 80 s ->
  `build/shaders_all/`, 23,083 .bin); `translate.py --spv ..\build\shaders_all` (340 s): 23,081 translated (two
  never used in play fail). `package.ps1 -Out <dir>` translates everything and builds `shaders.pak`.
- BNSH: standalone (`.trsha`, `.bnsh`), in VFXB effects, SARC UI archives, 2 in `main.elf` (file offsets 0x43ec000,
  0x43f4000). GLSLC control starts `34 12 76 98`, code `78 56 34 12`. nvn.c's shader hash = FNV-1a of
  code[0 : control+0x704 (else 0x30 + control+0x6f8)].
- Shader JIT: a host thread runs `translate.py --spv --only=@build/shaders/jit_list.txt`; draws wait (NX_SHADER_JIT=0
  off). SPIR-V in `build/shaders/spv/<translator version>`: a translator change retranslates everything. Shaders
  with a translation are loaded ahead on their own thread when the game creates them (`NX_SHADER_PRELOAD=0` off).
  Trick: swap a hand-edited SPIR-V in (move `pipelines.bin*` aside).

### Vulkan side (gpu.c, vk.c)

- **Spec-correct only**: early bugs relied on NVIDIA's handling of undefined behaviour; check with NX_VK_VALIDATION
  (known messages: four SPIR-V ones on AMD, `vkResetQueryPool` with NX_GPU_TIMELINE). GPU-assisted validation caused
  TDRs on AMD. Device loss: VK_LAYER_LUNARG_crash_diagnostic + NX_PIPE_LOG.
- AMD fixes: compute needs 32-wide subgroups (`vk.warp32`); unwritten vertex outputs read (0,0,0,1); TLDS 2D ARRAY.LZ
  coordinates; single-sampled alpha to coverage -> alpha >= 0.5 test (in the pipeline key); no D24 (D32S8 / D32);
  32-bit integer textures get same-bits float views; robustness2 where supported; unsupported blits skipped and
  logged (no depth blit destination: the half-res depth `960x540 f53` is drawn by `depth_blit`).
- Targets render all layers of their view (the tone-map's 32^3 LUT is drawn one slice per instance). Descriptor set 3
  = texture pool entries as storage images.
- CPU-written textures: only ranges of uploaded textures are watched (write watch, own thread); containers upload
  guest memory and copy in GPU-written parts; grown or partially written images upload first. GPU-written linear
  textures are copied back at submission end (the profile photo).
- **Render scale 1-4x** (live, `rescale_all`): 16:9 targets from 120x68 up are made N times larger when first drawn
  into; a colour target drawn with a scaled depth bound joins (`image_promote`: the menus' 3D player / mons). Box
  downscale to the window (vk.c `downscale`).
- New images' layout change, zeroing and first upload go into the slot's `pre` command buffer; image budget from
  VK_EXT_memory_budget (90% of the heap's budget less mirrors / staging). Pipelines: built on 2 workers with the draw
  skipped (NX_ASYNC_SHADERS, netplay builds), cache `build/pipelines.bin(s)`, `pipelines.list` prebuilt at start.
- Real occlusion queries for the samples-passed counter (`gpu_query.c`, `NX_OCCLUSION=0` off; the old made-up total
  made far house lights and the moon flare; user 2026-11-13: nothing looks off). `SetRenderEnableConditional` ignored.
- Feedback loops (`NX_FEEDBACK_LOG=1`): fs `3614c5d014241256` reads and writes one 1x1 f46 target (lead for too-bright
  glows); fs `8f2c86b8511ac03b` samples 1080p depth with depth writes on.
- Left from the 2026-11-14 code review: texture pool entries repointed under recorded draws, ClearColor layer 0 only /
  no channel masks, D32F copied out where the game has D24S8, ASTC decoded under gpu_cs (spread over helpers).

### Graphics patches (gfx.c, patches.txt)

Improved Graphic Quality (`if gfx`, `NX_QUALITY=0` off), no DOF (`if nodof`, `NX_DOF=1` keeps it), **2K shadows**
(`if shadow`, `NX_SHADOWS2K=0` off; on since 2026-11-23, user-checked): `main+146e80c` mov w8 #0x400 -> #0x800 and
`main+146e770` mov w9 #0x10c00000 -> #0x14000000 (NVN pool slot 4 at the game's own 320 MB: with the shadows alone,
opening the map asked that pool for 56 KB and got null; `main+c28198` -> best-fit allocator `c28244`, `c281d0`
reads through the null). `h_heap_oom` (`main+c28230`, only after a failed allocation) logs size, the heap's words
(NVN pool: "HNVN" at +0x158, size +0x128) and the frame chain. A 4K option would need that pool sized for it.
`NX_GFX=0` all off. main+x in `build/image/main.img` is at file offset 96 + nsym * 16 + strtab size + x.

### FSR 2 anti-aliasing (2026-11-21/23, user-checked)

- AMD FidelityFX FSR 2.2.1 (`deps/FidelityFX-FSR2`, MIT). `tools/fsr2/fsr2_shaders.py` builds each pass's SPIR-V in 8
  variants (depth inverted, sharpening, reprojection Lanczos table: the backend asks for it on wave64 AMD) with the
  SDK's glslang and the resource tables with spirv-cross; no AMD executables run. `runtime/fsr2.cpp` (context,
  jitter, dispatch), `runtime/gpu_taa.c`, `shaders/taa_mv.comp`.
- **Frame map** (frame 12000, 1x, 376 draws): 128 XFB skinning draws, a 1024^2 shadow map (52), the G-buffer (102
  draws, colour slots 1-4, depth f53 1920x1080), quarter-size passes, the lighting triangle (vs `5250e040`, fs
  `fd87a85c`) into `@12f5ee2000` (RGBA16F) + 31 transparents, a bloom chain, **the tone-map fs `4b80c6ba33bcbb5b`
  into the shown image**, 20 HUD draws, a last copy. **Camera:** vertex c[5] = view-projection by columns, reversed-Z
  infinite, near 0.32, vertical FOV 46°.
- game.json `"taa": {"resolve_fs": ..., "camera_cb": 5}`. Jitter (viewport offset) on draws with the scene depth but
  not full-screen triangles; after the tone-map draw: camera motion vectors (one reprojection matrix last VP x inverse
  VP, **made in double**: in float the camera translation left it off by a fraction of a pixel, differently each
  frame, which flickered edges and distant shadows), FSR 2 into RGBA16F, blit back, then the HUD. +0.25 ms GPU at
  1080p. Overworld log (`taa: last 600 frames`): every frame resolved, ~750 jittered draws a frame.
- Menu: Options > Video > Anti-aliasing (Off / FSR 2 native), Sharpening, Jitter (dev); settings.ini `taa`,
  `taa_sharpness`; NX_TAA / NX_TAA_SHARPNESS decide at start when set. UiHost version 4.
- Open: upscaling presets (Quality 1.5x, Balanced 1.7x, Performance 2x, the user's 1.33x) need a fractional render
  size plus a negative texture LOD bias; reactive mask. Frame generation dropped (the user, 2026-11-23: camera-only
  motion vectors would ghost moving creatures).

### Open rendering issues

- **Missing letters:** whole glyphs blank ("Mareep" as " a eep"), whole again later; seen again 2026-11-12 after the
  CPU-written texture fixes (F12 capture `build/frames/frame_23259*`: targets `240x135 f63 @12f1c36000`, `1920x1080
  f63 @12f6752000`, `30x16 f63 @12f1bd1800`). Suspect the run-time glyph atlas. Not looked at yet.
- **Rain:** splashes, distant sheets and wet surfaces show; no streaks near the camera (the half-res effects layer
  `960x540 f41 @12f4f6a000` has 7 lit pixels). Weather mod tests (laptop, `build/frames/rain*`): forced rain leaves the
  sky blue; thunderstorm's 8 tall quads (vs `7a1f81cac22350a1` fs `df61a3468379ebd4`) pass 0 samples even with depth
  test off. Ruled out: skipped draws, particle clocks, the depth copy. Lead: the half-res depth test (ds
  `@12f541a000`, written by `d4ea93c14e5c47f0`). Test natural rain after the prologue.
- The spinning Poké Ball sign above Pokémon Centers ghosts at its edges (2x; compare at 1x, then F12).
- Unconfirmed whether still there: the Let's Go glow too bright; huge glows from `?` / `!` markers; the "…" bubble
  shows only grey corners (draws `de9e66ba…` / `f3fbc253…`); cyan-white streaks in Mesagoza. Validation still reports:
  shadow pass R8_UINT target sampled as float, A2C without alpha output, integer attributes into float inputs, a
  point-list topology mismatch, interface mismatches (Location-06272, OpEntryPoint-08743).

## Frame rate

- **Any frame rate plays at normal speed** (user-checked at 120). The game steps by a duration from a table
  (`main+3c2fbb0` = table[present interval - 1]) and derives speed from it (`main+d9643c`); `fps.c` writes the entry
  every frame (page kept writable: re-protecting raced) and keeps the speed divisor at 30. Animation timer
  `main+b92784` fixed in every scene kind except 2. Frame update `main+2b79f60` gives each clock a duration from table
  `3c2fba8`; readers call vtbl[0](obj, 1) and read +8.
- Effects stepped VFX emitters in whole 60 Hz frames (`main+2039640`, `(int)(duration / 16666666.0)`): hook
  `fps_effect_frames` gives the fraction. Open: particles may look too fast uncapped.
- Map: Fl4sh's "60 FPS" / "Dynamic FPS" pchtxt (`mods-ref/`). Not ported: movement scaling at `cce37c` (lead for
  **Fletchinder flying too fast** at 60), `1526d64`, `2b7a874`. Not clocks: `170c408`, `20a8d3c`.
- Parked: cutscenes run fast above 30 (fine at 80-90, teleports at 120+); scene kind (`main+2936584`) stays 0 there.
- Day / night is real time (`e0b960`, 86400 s); `NX_TIME=<unix>` pins the clock.
- Switches: `NX_FRAMERATE=60|120|0` (default 60), F5 cycles; NX_NOPACE=1 uncapped.

## Performance

### Where things stand (2026-11-23)

- **Desktop (RX 7800 XT + 7800X3D), 1x:** CPU-bound. At the old bench spot ~4.3 ms (230 fps) after the lifter work
  (was 5.0); the bench save moved 2026-11-21 and the new spot runs ~2.0 ms, so compare within a day only. **The frame
  is main's own code** (NX_TRACE: main runs ~3.4 ms on CPU, ~97% of it on a host core; its blocks are waits for the
  workers' jobs, ~5 µs a hand-off). Lifted-code cost is per call, not per instruction (handport: loops compile as
  native; entries, call sites and exclusives pay).
- **Desktop, 2x:** GPU-bound, ~6.0 ms (GPU busy 5.7-6.0); ~2.5 ms of GPU work does not scale with the scale (shadows,
  unscaled passes, skinning, small draws), ~0.85 ms per 1x of pixels. Lighting draw ~1.0-1.4 ms at 2x. At 120 fps
  capped: some areas 97-110 fps, all GPU wait.
- **GTX 1650 Ti laptop:** ~11-12 ms (85-90 fps), GPU-bound; pool mirrors in VRAM there (`NX_POOL_MIRROR`; they lose on
  AMD, which runs with them off).
- Long sessions: the game's own full Lua collections (50-70 ms every 4-12 s) answer **failed allocations**
  (`main+eb071c`: collect, retry `eb0890`, abort if it still fails); which heap fills is open.

### How to measure

- **Stand-still bench**: `violetrecomp\bench.ps1 <name> [NX_X=v ...]` copies `build/save` to `build/save-idle-run`,
  NX_NOPACE=1, NX_TIME=1792494000, presses A through the title (needs the user's OK each round), averages `nvn:
  frame time` and NX_GPU_TIMELINE busy after the load (`build/bench_<name>.log`, `-Summary`, `-Rgp <frame>`). At 1x
  pass `NX_RES_SCALE=1 'NX_INPUT=300-9000/60:A'`. Spread ~0.2 ms: A/B in ABBA order, window medians, the same day;
  ask the user to pause video first (DWM). A fixed bench save would stop the scene moving with the user's play.
- Log lines every 600 frames: `nvn: frame time`, `gpu: per frame`, `cpu:` per thread. NX_GPU_PROF=1 per pass, =2 per
  shader; NX_GPU_TIMELINE=1; NX_HITCH_MS=<ms> splits slow frames; NX_PROFILE=<threads> samples slow frames.
- **NX_TRACE=<frame>[,<n>]**: a Chrome trace (`build\trace-<frame>.json`, ui.perfetto.dev) of every guest thread's
  running / waiting / blocked spans with guest callers, the runtime's work and GPU submissions;
  `py -3.12 nxrecomp\tools\trace_summary.py <json>` per frame.
- **RGP** on Adrenalin 26.10.2 (25.9.2 hung Windows): `.tools\rdts\...\RadeonDeveloperPanelCLI.exe -m profiling -p
  <name> --rgp-auto-capture=frame:N -o <file>.rgp`; NX_GPU_LABELS=1 names passes and draws. Offline ISA: `rga.exe -s
  vk-spv-offline -c gfx1101 --isa` on a `.spv`.
- `build/handport/`: one lifted function vs typed C++ from Hex-Rays, compiled and counted (`build.bat <off>`,
  `count.py`): recounts a function after a lifter change without a game run.

### Done (kept, with what each gave)

- Lifter: x0-x17 stored only when written / reloaded only when read (1x -6.6%); x19-x28 and x29 only for callees that
  read them (a few %, -2.2%); -O3 (~1.5%); sysv convention (-3.2%); fused exclusives (-1.7%, one crash in 5 runs,
  below); PGO opt-in (-3.8%); AAPCS calls without flags (4.41 -> 3.99 ms CPU-only, early).
- Frame pacing / stalls (2026-11-12, 2x 120 now steady where the GPU allows): Lua collector switched to incremental
  and stepped over the debt (`lua.c`, `NX_LUA_GC=0` off; was a 40-150 ms stall every 45-75 s; the switch must clear
  `lastatomic`); presents held to the frame's tick (`hold_present`, 7% -> 1% late frames); kernel `core_move` (saves
  no longer a 100 ms frame); shaders preloaded; host imports freed on their own thread.
- GPU: skinning draws store their own captures (`NX_XFB_STORE`) and `xfb_hazard` checks only used streams (1.0 ->
  0.3 ms a frame at 2x); the game's barriers inside a pass wait for the next draw (`barrier_due`); lighting shader
  2.19 -> 1.98 ms (laptop); frame submitted before the render thread's fence wait (`h_submit_ahead`, laptop 70.7 ->
  85-90 fps); per-draw shortcuts by generation counters (translation 7.2 -> 6.4 ms); early flushes only when the GPU
  is idle; narrower pass-end barriers; flushes every 250 draws; presents on their own thread; pool mirrors with hot
  pages (laptop).
- FSR 2 native AA, 2K shadows (Graphics).

### Ruled out (measured, no gain)

Deferring NVN barriers wholesale; skipping skinning at 1x (main-bound); no barrier at pass ends (corrupts lighting);
CPU_NO_ACCESS pools in VRAM; staging in ReBAR VRAM; stable clocks (-13%); NX_FLUSH_DRAWS=60; colour compression /
format lists; attachment-only barriers between passes on one target; dropping barriers around compute dispatches;
kspin; preserve_none (even, +10% code); guest cores pinned to physical cores (`NX_HOST_CORES=1`, +14%: the runtime's
and driver's threads share them); answering the fence late at 1x and more cores for the job join (the trace: main
never waits on the GPU, the joins are the workers' real work); rewriting single hot functions by hand (the profile is
flat: top function 1.3%); the lighting shader's instruction count (-28% instructions, same time).

### Open findings

- **Lazy pass-end barriers** (`NX_LAZY_BARRIER=1`, off): 6.11 -> 6.00 ms at 2x, sync validation clean stand-still,
  but in play flicker, giant squares like broken shadows and colour flashes. Something validation does not see (a
  view or alias of the shadow map, a texture-pool read, buffer device address) skips the owed barrier.
- **Fused exclusives crash** (1 in 5 b+c runs, 2026-11-20): access violation reading 0 at `main+b5ab44` in
  f_7100b5aa9c, a list walk next to a 16-bit refcount decrement (now a CAS loop); f_7102074494 returned [obj + 0x58] +
  idx * 8 with both 0. A poison run and three more runs were clean; whether fusion causes it is open. Fusion is on.
- **The game's dynamic resolution** (`main+293dc08`, twin `1eb0dec`): averages the GPU frame time
  `[[main+475a0a0]+0x130]` against 33.33 ms and steps `3DBufferWidthScale` / `HeightScale` through 1, 0.8, 0.65, 0.5
  (tables `main+3c5f120` / `3c5f138`); the time comes from timestamp counters we answer with CPU time, so it never
  steps. Only the 3D buffer follows it; its levels are not 16:9 (our scaler scales 16:9 only). Possible use: real GPU
  times and an 8.3 ms budget for a steady 120 at 2x.
- 2x GPU time (NX_GPU_PROF, 100 fps place): lighting draw 1.3-1.5 ms, skinning 0.3, outside passes ~0.65 (copies,
  clears, barriers), `vs d4ea93c1 / fs d4f2e432` 1,050 draws into an unscaled 1080p target 0.35. RGP: ~230 barriers
  a frame, all full stalls; no decompress or fast-clear passes.

### Next, ranked (estimates unless noted)

1. **Fractional render scale** (Z-A output_scale.c: a float scale in push constants): 2x renders 2.25x a 1440p
   window's pixels; also what FSR 2 upscaling presets need.
2. **XFB skinning into a device-local mirror** (Z-A exec.c 5851-5960; 3 ms on a 1660 Super): skinned vertices go
   over PCIe on the laptop. Needs "GPU-written" stamps so `mirror_sync` doesn't clobber, and a copy-back.
3. **Uploads / texture creation out of the pass** (TotK vk_texture.c 894-1010): later uploads still end the pass with
   full barriers; deswizzle / ASTC on workers, prefetch at nvnTexturePoolRegisterTexture (the 5-9 ms spikes at 2x).
4. **Fix the lazy pass-end barriers** (above; ~0.1-0.2 ms at 2x).
5. The fused-exclusive crash (above), then PGO by default (a profile per lift).
6. Lifter: inline small leaf callees (9-11% of 634,507 `bl` sites target <= 16-instruction leaves); host CRC32 / TBL /
   PMULL / AES; branch-free vector fmin / fmax / FCVTZS with a NaN guard; dispatch table as 32-bit offsets; giant cold
   functions at -O0, `/GS-`; STL as release instead of SEQ_CST.
7. GPU: graphics pipeline libraries + fast link (TotK, ssbu) then async shaders by default; a vkCmd* recorder thread
   (only once translation exceeds the GPU frame); UBOs as UNIFORM_BUFFER_DYNAMIC, push descriptor templates; the game's
   resolution controller fed real GPU times; real GPU timestamps for counters.
8. Kernel / I/O: overlapped RomFS / NCA reads (one synchronous handle serialises every thread; check with
   NX_FS_TRACE first); NCA AES-NI CTR in place (packages); MMCSS "Games"; release the core only around IPC that can
   block; kernel lists that don't scan every thread ever made.
9. Overlap CPU and GPU by answering SyncWait a frame late (Z-A): only helps where the guest waits on the GPU (not at
   1x here); large, high risk.

## Debugging tools

- `run.ps1 -DumpAfter N` (mutex owners, host stacks, audout state); `NX_DUMP_MEM=addr:len,*ptr:len` (also on crash);
  `NX_TRACE_CALLS=addr,...`; `NX_WATCH_MEM=addr:len@ms` (every write: writer, old value, host stack); `NX_ONE_CORE`.
- GPU: NX_PIPE_LOG, NX_SKIP_SHADERS=hash,..., NX_GPU_SKIP=1|2|4|8, NX_NO_DISCARD, NX_DUMP_SSBO=<hash>,
  NX_DUMP_DRAWS=<target> with F12, NX_DUMP=<frames> (the frame and every image to `build/frames`), NX_DEBUG_RT=<rt>:<bits>,
  NX_TRACE_VCB=<c>:<row>,..;.., NX_TRACE_FILTER='960x540 f41', NX_VIEW_LOG=1.
- Input: NX_INPUT_LOG, NX_INPUT_REPLAY=<log>. Unattended runs only, with the user's OK: `NX_INPUT='300-30000/60:A'`.
  New game: `--save <empty dir>`.
- Lua errors: patches.txt main+b5d8fc `after script_error` (lua.c) logs class.method and the error before the
  invoker's abort (main+b5d450 CallImpl).
- **Hex-Rays decompile of main** (IDA 9.3): `build/analysis/hexrays/main_0.c` .. `main_7.c` (326 MB, 163,935
  functions dealt round robin: grep all eight); `// ==== <offset hex> <name>`: `rg -n "^// ==== b5d8fc "
  build/analysis/hexrays`. Made by `nxrecomp/tools/ida_decomp.ps1 dump\elf\main.elf build\analysis\hexrays -Seeds
  build\analysis\main_ida.json` (17 + 17 min; `TVHEADLESS=1`). sdk / subsdk0 not done. Names are IDA's defaults.
  Functions reached only through vtables have no callers there: hook and log instead (`h_heap_oom`).
- Ghidra headless: `nxrecomp/tools/ghidra/decomp.ps1 <abs out> <off>...` inside violetrecomp (~10 s).

## Local play

- **Rooms**: Violet builds with NETPLAY. Two instances did Union Circle and full Tera raids. Test set-up:
  `NX_ROOM=127.0.0.1:24872`, `NX_NICK=A|B`, saves `save-net-a` / `-b`; a force-killed client keeps its nickname until
  ENet times out (~30 s). What it took: nifm accepts preset-1 requests; advertise data set after CreateNetwork; the
  link-code keyboard; the NG word filter needs system data `0100000000000823` (NgWord2, `build/sysdata/`); bsd eventfd.
- Hosting: `room_server.exe --port 24872 --name ... --game-id 01008F6008C5E000 --game "Pokemon Violet" [--password
  pw]`; playit.gg tunnels UDP 24872.
- **Real Switch** through `../ldn-beacon` (LDN is Wi-Fi action frames + a WPA2 network; Windows can't host it).
  **Link trades and raids with a real Switch work** (2026-11-11). A link-code search hosts and scans in turns: ldn_net.c
  sends DestroyNetwork to recent scanners and the beacon drops the network (else the Switch joined a torn-down one).
- Online play through Nintendo can't be redirected (NPLN is TLS against Nintendo's CA + account / NSO tokens).
- In Union Circle the world is simulated by one player at a time. Raids there are separate per player.

## Packaging

- Friends supply keys, firmware, base + update NSPs (DLC optional); nothing is extracted: the runtime reads NSPs
  directly (tickets, NCA3, AES-CTR, the update's BKTR, IVFC), ~600 MB/s. Module images built from the NSOs at first
  start (image.c).
- `violetrecomp_pkg` (NX_PACKAGE, `Pokemon Violet.exe`, no console): game files in `game\`; save, cache, log, `dumps\`
  under `%LOCALAPPDATA%\Pokemon Violet\`; translator from `translator\` (embeddable Python + translate.py +
  glslangValidator); first start "COMPILING SHADERS". The user's test folder has hard links to `rom\`'s NSPs, keys and
  NgWord2 in its `game\`: empty it before sending.
- Linux: Proton / Wine first; native port surveyed in `nxrecomp/docs/PLATFORMS.md`.

## Mods

### Framework

- nxrecomp `runtime/nxmod.h` + `mods.c`: DLLs in `<exe>\mods\` (NX_MODS=0 none) get window events, can hold input, draw
  over the swapchain; Dear ImGui via vcpkg (events on the window thread, drawing on the render thread).
- Settings: `host->setting_get / setting_set` keep values in `settings.ini` as `mod.<key>` (F8's all-shiny and
  no-battles use it).
- **Lua bridge**: `host->run_script(text, done, ud)` runs chunks at lua_pcallk's entry (patches.txt `after
  lua_pcall_entry` at b5ddd0). Lua C API in main: lua_load `232e0`, lua_gettop `b5d980`, lua_settop `b5db00`,
  lua_tolstring `b646d0`, lua_pushstring `b85bd0`, lua_rotate `b5dcd0`, lua_pushfstring `1a2cd10`, lua_pcallk `b5ddd0`;
  VM `278b0` / `4487c` / `26c60`; lua_gc `c8c520`; luaC_fullgc `2b690`.
- Native Lua classes: `luaL_requiref(L, "c<hash>", open, 1)` (callers of main+e7d200); the open function's ADRP+ADD
  refs are the luaL_Reg tables. Mon object `c113335A8`. Native -> script calls hash class and method (FNV-1a 64, game
  basis) via 15745bc / 18e4050.
- Tools: `tools/luabc.py`, `tools/xref.py` (ADRP+ADD refs), `tools/bfbs.py`, `tools/img.py`.

### F6: Tera raids (user-checked)

- Raid blocks found by scanning guest memory for den-shaped 0x20 entries (Paldea 72 after a 0x10 seed header, Kitakami
  100, Blueberry 80). Each den's raid from its seed; seeds written for a chosen species / shiny / Tera (our own
  Tera9RNG / Encounter9RNG); story progress picks the star roll.
- Data: `python mods/raids/raiddata.py` -> `build/mods/raids.dat`. Raid tables
  `world/data/raid/{,su1_,su2_}raid_enemy_0N/..._array.bin`. Area names `build/mods/raid_areas.txt` (**Kitakami /
  Blueberry numbers unchecked**).

### Raid crystals rebuilt on demand (`raidgem.c`)

- Entries are made by **e23ec8(M, force)** (manager vtable at file offset 3a65428): walks 3 x 100 dens (0x20 each at
  +0x50, +0xcd8, +0x1958: area, group, point, u64 seed, content, claimed) and adds an entry with the raid from the seed
  (e26fec). Setting entry+0x98 = 0, crystal+0xd1 = 1 **removes** a den. Groups: 0 own dens, 1-3 other players.
- raidgem.c (`main+2933398 after raidgem_update`; export `violet_raid_refresh`) marks group-0 entries the game's way,
  waits until erased, calls e23ec8(M, 0), sets M+0x1662. **Whether crystals reappear on screen: not reported.**
- Den entry: +0x20 float x, y, z, +0x40 0-17 (Tera?), +0x44 area, +0x48 group, +0x4c point, +0x64 region. North -z,
  east +x. Export `violet_raid_points`.

### Event raids

- A console gets them through BCAT (`raid/raid_enemy_array_3_0_0`, `fixed_reward_item_array`,
  `lottery_reward_item_array`, `raid_priority_array`; outbreaks `encountOutbreak/*_3_0_0`) stored in save blocks
  (KBCATRaidEnemyArray 0520A1B0 30,000 bytes, FixedRewardItemArray 7D6C2B82, LotteryRewardItemArray A52B4811,
  PriorityArray 095451E4, Identifier 37B99B4D). Den content 2 = event, 3 = 7-star. nxrecomp bcat.c is a stub.
- A friend's save's blocks copied in (`svedit copy`) gave a black crystal with a Rillaboom "(event)" after a restart
  (identifier 20230728).
- **How the game uses a delivery:** schemas in romfs (`world/data/raid/delivery_raid_enemy/...bfbs`,
  `delivery_raid_priority/...`). Enemy info fields: 0 romVer (0 both, 1 Scarlet, 2 Violet), 1 no, 2 deliveryGroupID,
  3 difficulty, 4 rate, 5 / 6 drop tables, 7 captureRate, 8 captureLv, 9 bossPokePara, 10 size, 11 bossDesc, 12
  raidTimeData. Setters (BcatClient 1d7c458): 1d93018 enemy, 1d933a0 fixed, 1d931dc lottery, 1d92c24 priority (copy
  into the save object at +0xe308.. and re-point the resource; a new identifier triggers RaidGemForceLottery). An event
  den rolls its star target, then walks the entries (skip other game, other group, difficulty out of range), sums
  rates, NextInt(sum): PKHeX's dist behaviour. Rewards look in romfs tables first, then delivered ones. **No entry
  cap** found (no verifier; only the 30,000-byte size).
- **Events mod (F4, built, not run yet):** delivers live through raidgem.c (`violet_raid_delivery`,
  `violet_raid_deliver`); `mods/common/delivery.h` is a small FlatBuffers writer sharing vtables and equal sub-tables
  (all 230 PKHeX raids fit one delivery). Dates: `mods/events/event_dates.csv` (Serebii, 214 of 230 dated). Game clock
  from `<cache dir>\clock.txt`. Entries: the mon from PKHeX, the rest from the first normal raid template of its star
  level (7 stars: 6's with hpCoef 3500), captureLv = level, group 1. **Guesses:** groupID_N = event dens per reroll,
  event dens are group 1 (+0x83a8 not read), fixed IVs draw nothing, the difficulty range from PKHeX's thresholds.
  Backups of the save's delivery in `build/events/backup-<identifier>/`.
- **Event outbreaks:** save blocks KBCATOutbreakPokeData 6C1A131B (0xe18), zones Paldea 3FDC5DFF / Kitakami F9F156A3 /
  Blueberry 1B45E41C (0x300), Enabled 61552076; active ones KOutbreakBC01..10*. Schemas
  `world/data/encount/delivery_outbreak/delivery_outbreak_<x>/delivery_outbreak_<x>_array.bfbs` (pokedata: id, devid,
  sex, form, levels, area bools, locationName, enable table, version table, bringItem, rare %, ribbon, scale; zones:
  zoneid, lotvalue, poke1..5 + lot values). Loader 0290d3b8(M, data); setters 029293dc / 4ec / 5fc / 70c point the
  resources at the data (no copy); refresh 029299b4. **Open:** what zoneid / pokeN index, where the outbreak struct
  lives in memory.
- Not wanted: Nintendo's servers. Community archives of real deliveries: the user picks, ask first.

### F7: items (user-checked)

- The bag is the save's KItem block: 3000 x 0x10 by item id {pocket, count, flags, 0}. Direct writes show only after a
  restart; F7 goes through Lua: bag `C14A8D35D72F56FE9.S90D73F1D715BCBF4()`, `:FB01A9B29CD4CC6CB(id, n)` add,
  `:FE027A297CCA14276(id, n)` remove. F7 has a Lua console.

### F8: Pokémon

- **Box writer** (user-checked): our PK9 writer (`mods/mons/pk9.h`) into the first empty slot of the live KBox block
  (990 x 0x158). Tables + trainer: svedit `monsdata`. Files: `mons.h`, `mons.cpp`, `egg.cpp`, `filtercombo.h`.
- **Eggs (PKHeX-legal offline, not tried in game):** level 1, egg location 30023 (picnic), egg date today, nickname =
  egg name, EVs 0, level-1 moves, OT friendship = hatch counter. PID fixed at creation, shininess checked against the
  hatcher ("Shiny for another trainer"). Trade: HT = receiver, met location 30002. Unchecked: whether SV turns a
  traded egg's egg location into Link Trade on hatching. Egg moves not offered. Trainers tab and
  `build/mods/mons_trainers_seen.txt`.
- **Wild spawns:** the spawner is Lua (native `12451d0` = `c113335A8.f7AC1FE6B(spec)`; script maker
  `C2B428A63695FD2C7.S988D7D0A4EEF693D`, called by the wild manager `C4EE52E49562F8277`); F8 wraps it (user-checked:
  Hisuian Arcanine in Paldea). Rewriting the encounter tables in memory changes nothing (read at save load).
  `violetrecomp/wild.c`: `after wild_create` at debf88 rewrites the spec (shiny, IVs, EVs, nature, ability, gender,
  level), `after wild_created` at debff8 sets moves and Tera; `NX_WILD_LOG=1`. All spawns shiny (`violet_all_shiny`).
- "Around you": wild manager W = `C4EE52E49562F8277.SF3075AB31C9E8AF4`, state W[17]; "Spawn on me" moves a spawn
  point to the player; encounter table W[26] (`F2F6C7BBE115AFBCB` fills, `FCEEA7F4134055567` picks; restore W[26]
  after wrapping). Union Circle: only on the simulating instance (untested).
- **No contact battles:** `main+1d0e5d4(component, enable)` zeroed by `h_encount_enable` (main+1d0e5e4). Unconfirmed;
  fallback event flag `FEVT_COMMON_0140_NO_ENCOUNT`.
- Version exclusives (4.0.0): of 955 table entries 889 both, 33 Scarlet-only, 33 Violet-only (versiontable field 28).
  Spawn-pool rules (wanted: exclusives in both, any time, equal weights, density): wrap the Lua filter
  (`F67A9C2A9373A0AC6`); density from `encounterCountMax` / `encounterCheckInterval` (loader main+1513f24).

### F9 / F10: weather (built, not tried in game)

`mods/weather/weather.cpp`: F9 rain, F10 any type. Lua class `CA71F0CDF966F3175`: set `A3EC462E45551FD6(type)`, reset
`ECAF51D6E7046362()`, span `8614855A6EF844D8(s)`. **Type numbers guessed** (1 normal .. 9 heavy fog, AreaWeather9
order); real ones in 1d56928 (ConditionCurrentWeather).

### Save edits (svedit)

`violetrecomp/tools/svedit` (PKHeX.Core 26.10.18, .NET 11; game closed, back up `build/save`): ride upgrades and raid
levels (user-checked), `addmon`, `dump` / `copy` blocks, `layout` / `check`, `find` flags, `monsdata`, `events`.
`KUnlocked*` = `FSYS_*_ENABLE` (key = (uint)FNV-1a-64 of the name). Save path
`build\save\1-000000000000000100006d6b38726563-0\main`.

## Reference projects compared

- **Pokémon Legends Z-A** (`../ref/PokeZ-ARecomp-Online`): taken: CPU-written texture fixes, passes kept over re-set
  targets, QPC waits, the FSR 2 approach. Ideas left: RomFS mods (fs.c 328-813), safe saves, crash explanations.
- **TotK** (`../ref/totk-pc-recomp`): taken: kspin (off), pipeline memo, no power throttling, PGO. Same GPU design.
- **ssbu-recomp**, **Savage Wind** (BotW, `../ref/SavageWindRecomp`): pipeline libraries, async submission,
  feedback-loop handling.
