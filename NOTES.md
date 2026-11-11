# pkmn-violet: notes and history

Findings, addresses, rule-outs and experiments, oldest first (condensed 2026-11-11; the full wording is in git
history). `CLAUDE.md` holds how to start things, state and what is open. Grep this before proposing a tool or an
experiment. Paths as they were at the time; since moved: test saves to `build/saves/`, the room server to `../room/`
(`room/build/room_server.exe`, was `nxrecomp/tools/room`), the mods zip to `mods-ref/`.

## What the game is (4.0.0 ExeFS, 2026-10-17)

| Module | text | ro | data+bss | Notes |
|---|---|---|---|---|
| main | 0x343fc90 (52.2 MB, ~13.7 M insns) | 0xf420a0 | 0x368868 + 0xe6e38 | game, engine ("Trinity"), all middleware static; 1,013 imports |
| sdk | 0x5d5660 (5.8 MB) | 0x6add7c | | NintendoSDK 13.3.13 (MK8: 4.4.0) |
| subsdk0 | 0x33e7d0 (3.2 MB) | 0x210194 | | `movie::` (android NuPlayer/ACodec), as MK8 |

~16 M instructions (3.4x MK8). Unlike MK8:
- **C++ exceptions used** (`__cxa_throw`, `_Unwind_Resume`, `__gxx_personality_v0`...: std, gRPC, Havok, engine).
- Graphics through NintendoSDK_gfx 13.3.13 (`nn::gfx`) + NintendoWare G3d / Vfx / Ui2d / Font over NVN; `glslc*` weak, never called.
- Middleware in main (guest code, no work of its own): Havok AI 2021.1, Oodle, Wwise (`romfs/audio/wwise`), Bink 2
  (`romfs/demo/movie/*.bk2`), Lua, FlatBuffers, nlib LZ4, Pia 6.20.1 + NPLN 1.8.3 + gRPC + OpenSSL + libcurl.
- libc musl-style from the sdk (`__nnmusl_init_dso`).
- Services in main.npdm (36): acc:u0 aoc:u appletOE audin:u audout:u audren:u banana bcat:u bsd:u bsdcfg btm:u
  caps:su caps:u csrng ectx:aw friend:u fsp-srv htc:tenv htcs hwopus ldn:u ldr:ro mii:u mm:u nfc:mf:u nfc:user
  nfp:user ngct:u nifm:u nim:eca notif:a nsd:u nvdrv pctl pl:u prepo:u sfdnsres time:u vi:u.
- RomFS (30 files, 9.24 GB): almost all `arc/data.trpfs` (8.0 GB, Trinity pack FS, index `arc/data.trpfd`); Wwise
  `.pck`/`.bnk`, two Bink movies, `event.trevt`, prefetch lists.

## Plan (V0-V8; V0-V3 done 2026-10-18, V4-V6 largely done)

- V0 shared framework `../nxrecomp` + thin per-game parts. V1 lifter at scale (IDA export, 100% cover, Unicorn
  tests, split output, C++ exceptions: measure first; if needed lift landing pads from `.eh_frame`/LSDA as
  secondary entries and resume like `GUEST_SETJMP` / `h_longjmp`). V2 boot to `nnMain` (SDK 13 SVCs, TIPC sm,
  musl init). V3 headless main loop (offline services fail cleanly; implement sub-interface openers for real, never
  the null-object stub: MK8's Time Trials lesson). V4 graphics (title, opening, overworld). V5 audio, movies,
  input, saves. V6 playable (overworld, battles, raids, DLC, long sessions).
- **V7 performance and PC features.** Measure first; unlock frame rate only where the game's timing allows.
  Quality options (user, 2026-10-19): render scale, anisotropy, shadow map size (1024 cascades) are ours; LOD,
  draw distance, grass density are the game's data (look where `nn::oe::GetOperationMode` feeds render settings).
  Our own overlay menu first; the game's own Options menu (Trinity UI, Lua, every language) maybe later.
- **V8 packaging for owners.** One launcher next to their NSPs / keys / firmware: checks, unpacks (own NSP/NCA
  reader), lifts, compiles, translates shaders, caches. Ships only our code (runtime, bundled LLVM, import libs,
  embedded Python or ported tools, function list as data). **Never ship anything built from the game**, not even to
  owners. Needs up-front shader extraction and a faster lift / compile (first run 20-40 min).

## Bring-up (2026-10-18): V0-V4

- NSPs identified; every NCA decrypts with `../keys` (key gens 14, 15, 17). `title.keys` has no Violet entries:
  update and DLC carry tickets (`--tik`); base has none (standard crypto). Extracted update ExeFS (`dump/exefs`),
  ELFs (`dump/elf`, nso2elf.py), RomFS merged over base (`dump/romfs`, 3 min).
- **V0.** Bases: main 0x7100000000, subsdk0 0x7104800000, sdk 0x7105000000 (sizes 0x47d3000, 0x69a000, 0xda8000,
  2 MB-aligned). Run nxrecomp's tools from inside `violetrecomp/`.
- **V1.** IDA export (main 37 min, 165,000 functions; sdk 100 s, subsdk0 48 s). All 15.86 M instructions lift (6
  header words in main stay traps). Added SQDMULH/SQRDMULH, SQXTUN, SRSHR/SRSRA, SHSUB, S/UABDL, S/UABAL,
  SQSHL/SQRSHL, SQABS, scalar saturating ops / narrows; fixed LD3/LD4/ST3/ST4 single-lane decoding and USRA
  precedence (latent in MK8). Unicorn 43,812 + 35,632 pair cases pass. Jump tables IDA missed recovered by pattern
  (`Mod.recover_switches`, bounded by AND/UBFX/CMP: 34 sdk, 1 subsdk0, 14 main); BL-target function recovery;
  fall-through into IDA-split functions; weak imports nobody defines get trap stubs (gRPC TLS, `getentropy`);
  analysis cached in `build/modcache` (keyed on a64lift.py's mtime). Lift 4.8 min, 830 files, 1.0 GB C; build 25
  min from scratch, exe 407 MB, PDB 304 MB, lifted.lib 748 MB.
- **V2 / V3.** `nnMain` runs, 1,200 frames in 60 s, DLC owned (`game.json` "dlc": [1]; PublicData RomFS =
  one `keyfile.dat`, in `dump/aoc/01008f6008c5f001`). nxrecomp: SDK 13 start-up (`nn::init::Start` with DT_FINI),
  TIPC (sm:), MapMemory from .bss / Normal memory, GetInfo 16 = NPDM system resource size, MapPhysicalMemory (the
  game's 12 GB allocator), GetInfo 20, time shared memory, CreateManagedDisplaySeparableLayer,
  nvnProgramSetShadersExt, fsp 53 / other titles' saves = not found (SV probes 9 older Pokémon games), bcat
  (empty), csrng, btm, audout:u (Wwise's output, 48 kHz stereo), hwopus, bsd:u (ENETDOWN), ngct:u; unknown commands
  answer with a stub object.
- **V4: first-boot screens and opening render.** Language select, look select, name keyboard (Enter + "Player", or
  `NX_PLAYER_NAME`), the opening cutscene, Uva Academy in 3D.
  - Translator added: BRX jump tables (from c[1]), PBK/BRK, loops (blocks rerun in a `while`), FCMP, IMNMX, BFI,
    TLDS, SHFL / FSWZADD (quad subgroup ops), HADD2, HSETP2, ICMP, R2P, POPC, FLO, ISCADD.CC, ISET.CC, LDG.128,
    storage images (SUST.P, SULD.P, SURED as image atomics; bound or bindless, NVN keeps image handles in c[2] at
    0x120 + 8k), VMNMX, VOTE, global ATOM (U32/S32), LEA; compute code starts at control word 0x708. Runtime:
    descriptor set 3 = texture pool entries as storage images (one level, no swizzle, sRGB as UNORM; storage-bound
    images count as GPU-written).
  - Fixes: `OP_PROGRAM` binds per NVN stage bits (compute apart from graphics); lost wake-up in `condvar_wait` (the
    key's waiter flag must be set before the mutex is released: the SDK's Signal skips the SVC while the key is 0;
    froze the job system); audout GetReleasedAudioOutBuffers must zero the rest of the output array (a stale tag
    kept Wwise draining forever under its global lock); audout releases buffers on a playback clock; library applet
    ids (0x11 keyboard; web 0x13, 0x14, 0x17-0x1a).
  - **Crash at ~4,300 frames (Bink movie with audio):** Bink's sound backend (main+20c50) mallocs its renderer work
    buffer + AudioRendererConfig from a 192 KB sub-heap made current for main (per-thread allocator at
    TPIDR_EL0+0x30). Our audren GetWorkBufferSize said 0x29000: overran into a shader file's pages. Now follows the
    console's sum (0x1d000 for 32 voices). Found with `NX_WATCH_MEM` on the nlib heap's span tags
    (`TlsHeapCentral`, base 0x1040000000, span table +0xa48, 8 bytes/page); the overlap was deterministic.
  - A Bink decode thread jumped into an unrecovered `adr; ldrb; add lsl #2; br` table: the slice took `ldr w19,
    [x7], #4` as writing Rt2. `_writes` in a64lift.py is now exact (base only with writeback, Rt2 only for pairs).
  - **Black screen from ~frame 4,600:** the tonemap's 32x32x32 colour-grading LUT (3D, f61) is drawn one slice per
    instance (gl_Layer); we rendered layerCount 1. Targets now render all layers / depth slices of their view
    (`view_layers`; giving 3D textures depth-many layers in view_resolve lost the device). The LUT shader writes
    layer = instance + c0[0x644] + 1 (c0[0x640/0x644] = base vertex / instance, solved 10-19).
  - Lua's luaD_throw "longjmp to a jmp_buf no lifted setjmp call set": host jump buffer table 16 -> 64 per thread,
    returned-frame slots reused first (later 256, non-stack entries evicted first, 2026-11-05).
  - Bedroom at ~10 fps: 86% of the render thread in GetWriteWatch over every GPU pool page each frame; now only the
    ranges of textures uploaded from guest memory: 58 fps (`NX_REFRESH_POOLS=1`: the old whole-pool refresh).
  - Workflow then: each run dumps new shaders (their draws skipped), `translate.py --spv`, run again. SPIR-V lives in
    `build/shaders/spv/<translator version>` (FNV-1a of translate.py): a translator change retranslates everything.
  - Debugging added: `run.ps1 -DumpAfter N` (mutex owners, host stacks, audout state), `NX_DUMP_MEM=addr:len,*ptr:len`
    (also on crash), `NX_TRACE_CALLS=addr,...`, `NX_WATCH_MEM=addr:len@ms` (every write: writer, old value, host
    stack of `NX_WATCH_DEPTH` frames; GPU writes not seen), `NX_ONE_CORE` (races vs logic). Guest offsets in host
    stacks are truncated past 16 MB (`#line` is 24-bit in CodeView): main+416f28 means main+3416f28. idalib on the
    saved `main.elf.i64`: 15 s a batch.
  - Keyboard: ESDF left stick, IJKL right stick, arrows = D-pad only, R = R. Unattended from a save:
    `NX_INPUT='300-30000/60:A;330-30000/60:B'` (B closes tutorials). New game: `--save <empty dir>`.
  - The "player doesn't move" issue (camera moved, mesh stale) went away with the fixes below.
- **Portability pass (AMD RX 7800 XT; nxrecomp `e705520`..`234d70d`).** Every bug relied on NVIDIA's handling of
  undefined behaviour; rule since: spec-correct SPIR-V / Vulkan, checked with NX_VK_VALIDATION.
  - Device losses, flickering grass, missing trees: GPU-culling compute `fba20ffd…` assumes 32-lane warps; AMD ran
    wave64 and a loop spun. Compute pipelines require 32-wide subgroups (EXT_subgroup_size_control, `vk.warp32`;
    AMD allows it for compute only). Found with VK_LAYER_LUNARG_crash_diagnostic (dumps in %USERPROFILE%\cdl) and
    NX_PIPE_LOG.
  - Sky in four quadrants: unwritten vertex outputs must read (0,0,0,1); translate.py initialises every output.
  - TLDS: 2D ARRAY.LZ takes x, y from Rb, Rb+1 (layer in Ra); encoding 8 is 2D ARRAY.LZ, 3D.LZ is 7 (1,099 shaders
    read through the 3D array: black grass, no terrain albedo; starters lost the device).
  - Single-sampled alpha to coverage -> alpha >= 0.5 test (AMD dithers A2C), only when colour target 0 exists.
  - Integer vertex formats: 32-bit fetched as same-size float; 8/16-bit get vertex shader variants with uvec4/ivec4
    inputs (`translate.py --only=<hash>:<mask>`, 2 bits per location; runtime `shader_variant`). Used by the XFB
    skinning pass (bone indices).
  - AMD (amdvlk aborts with fail-fast 7 on device loss; 5 is the CRT's invalid parameter): no D24 (fallback D32S8 /
    D32), no dynamic logic op (in the pipeline key), set 1 has one texture array per view type (TB_*,
    TEX_BINDING), 32-bit integer textures get same-bits float views, c[16] goes in a free slot and the runtime reads
    slots from the SPIR-V (`cb_of_slot`). robustBufferAccess2 / robustImageAccess2 on where supported. Blits the
    formats can't do are skipped and logged (AMD: no depth format is a blit destination). Don't trust runs under a
    Win32 debugger for GPU work.
  - GPU-assisted validation (gpuav_enable) caused TDR resets on the AMD machine: don't use it; plain
    NX_VK_VALIDATION is fine.
  - Objects near the camera invisible (trees, Gimmighoul rock): GPU-culled indirect draws with large base instances;
    vertex shaders add base instance / vertex from c0[0x644] / c0[0x640], which we left unbound. translate.py reads
    them as gl_BaseInstance / gl_BaseVertex, vertex id = gl_VertexIndex - gl_BaseVertex. Found with F12's per-draw
    occlusion queries.
  - Render thread 64% -> 28% (a getenv per storage buffer per draw; gpu.c's switches are now read once in env_init).
  - Input drift (menu cursor, camera): hid now writes all 7 lifos of all 10 npads (main reads Handheld too).
    Six-axis lifos are at NPAD +0x1758, 0x708 each. WASD = left stick, mouse = right stick (click captures, Esc
    releases, NX_MOUSE_SENS).
  - Shader JIT: nvn.c queues a shader with no SPIR-V at creation; a host thread runs `translate.py --spv
    --only=@build/shaders/jit_list.txt` (a file: ~2,300 shaders at boot exceed the command line); draws wait for it.
    NX_SHADER_JIT=0 off, NX_PYTHON picks the interpreter.
  - Second machine set-up from NSPs alone with Ghidra (`tools/ghidra/ghidra_export.ps1 -Seeds`, Ghidra 12.1.3,
    `GHIDRA_HOME`/`-Ghidra`): writes the same `<module>_ida.json`. Keep "Non-Returning Functions - Discovered" off
    (it cut 9,706 sdk functions short). main: 19 min + 5 min seeding, 156,946 functions, 4,289 switch tables.
    a64lift repeats function / jump-table recovery to a fixed point.
  - Debugging added: NX_PIPE_LOG, NX_SKIP_SHADERS=hash,..., NX_GPU_SKIP=1|2|4|8 (all / global-memory / textured /
    non-2D), NX_INPUT_LOG, NX_INPUT_REPLAY=<log> (`build/input_walkout.log`, `input_door.log`), NX_NO_DISCARD,
    NX_DUMP_SSBO=<hash>, NX_DUMP_DRAWS=<target> with F12 (target after every draw), per-draw sample counts and
    indirect args in traced frames, FPS overlay F3 (NX_FPS=0).
  - Open from then (some later fixed, see below): the Let's Go glow too bright; huge coloured glows from `?` / `!`
    markers near the camera (frame 10268; maybe the same cause; suspect blend factor mappings or the feedback case
    fs 3614c5d0); the "…" thought bubble shows only grey corners (draws `de9e66ba…` / `f3fbc253…`, BC3 sRGB atlas);
    storage images (set 3) share one array across dimensions; validation still reports the shadow pass's R8_UINT
    target sampled as float, A2C without alpha output, integer attributes into float inputs, a point-list topology
    mismatch, interface mismatches (RuntimeSpirv-Location-06272, OpEntryPoint-08743).
  - Commits of that day: nxrecomp `e122065` (translator), `abb28ce` (runtime), `dda831d` (storage images),
    `8024860` (lifter jump tables), `576381c` (audren work buffer), `f408ac0` (debugging tools).

## Frame rate and performance (2026-10-19 / 20)

- **Any frame rate plays at normal speed.** `NX_FRAMERATE=60|120|0` (unset: the game's 30; default later 60 via
  `game.json` "framerate"); F5 cycles; F3 shows cap, TICK (30 = normal), SPEED. The game steps by a duration from a
  table (`main+3c2fbb0` = table[present interval - 1]) and derives speed from it (`main+d9643c`); `fps.c` writes
  the entry every frame (rodata via VirtualProtect, page kept writable: re-protecting raced) and keeps the speed
  divisor at 30. Instructions are patched through `patches.txt` (lifter: "if" alternates at run time, "call" /
  "after" host hooks; re-lift main after changing it). Animation timer `main+b92784` (h_fps_timer_step) fixed in
  every scene kind except 2, as the Dynamic FPS patch does (user-checked at 120).
  - Map: Fl4sh's "60 FPS" / "Dynamic FPS (ASM)" pchtxt (gamebanana) in `mods-ref/`; pchtxt addresses = our
    main.elf addresses, same build id (709BFD66...). Not ported: its movement scaling at `cce37c` (stutter walking;
    suspect for the too-fast Fletchinder). Its NOP at `1526d64` is its code cave's housekeeping (not needed). Not
    clocks: 60.0 at `170c408`, 100000.0 at `20a8d3c`. Its `2b7a874` (whole ticks per animation update) not needed.
  - Parked: cutscenes run fast above 30 (fine at 80-90, teleports at 120+). Scene kind (`main+2936584`, setter
    `29364c8`) stays 0 in cutscenes; `b926f0` posts one global step (timer 2, from `main+27b04`) forced to 33.3 ms in
    kind 0. Also seen: cyan-white streaks in the air in Mesagoza (frames/frame_14699.ppm).
- **RX 7800 XT, Mesagoza, uncapped: ~85 -> 103-120 fps -> 150-195.** Submit and present give the emulated core up
  (main shared core 0 with the render thread and waited 59%); flush every 250 draws (`NX_FLUSH_DRAWS`); CPU-written
  texture check on its own thread; fewer pass breaks after XFB; shaders interned via hash maps; stage_ubos reuses
  the last c[0]/c[2] staging when unchanged; draw() records only state that changed (`rec`, DynState: AMD driver
  share was 23% from ~40 vkCmdSet a draw); stage_ubos pushes only the slots a shader declares (`slots_used`);
  pool_find tries the last hit first; descset_use skips unchanged draws; DMB/DSB: load- or store-only = compiler
  barrier, full = `lock or (%rsp)` (CPU-only 287-296 -> 297-304 fps); FRSQRTE estimate memoised (was 2.5-6.5% of
  main); wakes sent after klk is released. Uncapped: NX_NOPACE=1 (= NX_FRAMERATE=0; IMMEDIATE above 60 without
  MAILBOX).
- Log lines every 600 frames: `nvn: frame time` (avg, 1% low, worst), `gpu: per frame` (render thread translating
  / presenting / waiting for GPU), `cpu:` per thread (running, waiting for a core, asleep). NX_GPU_PROF=1 per pass
  (its timestamps drain the GPU: inflates around barriers; use NX_GPU_TIMELINE for totals), =2 per shader.
- The game pins main + render thread (5c) to core 0, workers 5a/5b to 1/2: more emulated cores won't help. It keeps
  one frame in flight: 5c waits (`main+cd0eb4`, nvnSyncWait) for frame N-1's fence (`cd1c54`, fenced after
  submitting at `cd1a74`) before submitting N. Job-system condvar `main+b47d18`: wake latency 5.4 µs to klk, 0.2 µs
  to a core: blocked time is idle workers, not latency. 5a is also ~20% blocked at `main+d64f14`.
- Measured, not worth it (then): deferring NVN barriers (mostly 0x12 before new targets), skipping skinning `44f2ec77…`
  (no change), format lists (DCC), no barrier at pass ends (also corrupts lighting), CPU_NO_ACCESS pools in VRAM,
  staging in ReBAR VRAM, stable clocks (-13%). NX_GPU_SKIP=1: ~290 fps, translation 0.6 ms.
- Pools (Mesagoza): 3,314 CPU_NO_ACCESS (367 MB), one 350 MB CPU_UNCACHED, a 268 MB compressible (targets).
- **RGP:** on Adrenalin 25.9.2 any capture lost the device / hung Windows. On **26.10.2** (installed 2026-10-31:
  vkcube, bench, validation all fine) it works: stay on it. CLI first, then the program:
  `.tools\rdts\RadeonDeveloperToolSuite-2026-10-02-1872\RadeonDeveloperPanelCLI.exe -m profiling -p <name>
  --rgp-auto-capture=frame:N -o <file>.rgp` (N >= 5; `--rgp-sqtt-buffer-size high`; wait for the .rgp before
  stopping either: a fixed cut gave "trace failed with result: -2"). Windows' GPU counters show 0% for us.
- **GTX 1650 Ti laptop: 23-29 -> 75-78 fps.** The 367 MB CPU_UNCACHED|GPU_CACHED pool (every mesh / index / UBO)
  was read over PCIe 3.0 x8 (G-buffer 19-24 ms). gpu.c mirrors pools >= 8 MB that are GPU_CACHED and CPU-visible in
  VRAM (NX_POOL_MIRROR=0 off): CPU-written pages (write watch, scanned on a thread per guest submit, ~0.5 ms) copied
  in by a `pre` command buffer; GPU copies / clears go to both; ranges written by storage buffers or XFB read from
  host memory from then on (Mirror.bad). G-buffer 1.0 ms. **On AMD the mirror loses** (137-167 vs 155-205 fps; +1
  ms translating, unexplained; title spikes): AMD runs use NX_POOL_MIRROR=0; turn it on only where it pays (open).
  Presents run on their own thread (NVIDIA's vkQueuePresentKHR blocked ~4-5 ms under Optimus): second queue where
  there is one, else vk.queue under queue_lock; uncapped, a frame arriving mid-present is dropped (mailbox-like).
- Benchmark save `build/save-bench` (user-supplied, game data, never committed): starts a ~5 min scripted scene;
  copy to a throwaway folder first; `NX_TIME=1792494000` pins the clock (day/night moved GPU cost 19 -> 24 ms); the
  scene runs on game time: compare whole runs. Lows there come with new pipelines / images (1% lows 6.7-10.3 ms
  without, 9.9-48.9 with). NX_HITCH_MS=<ms> logs each slow frame split into translating / presenting / GPU wait /
  rest, with shader loads, JIT waits, pipeline builds, image creation, uploads, slot waits, mirror copy-in.
- Alpha test is a specialization constant (ALPHA_FUNC): no discard without one.
- Lift / build: a64lift lifts in worker processes (NX_LIFT_PROCS, ~3 GB each: 129 -> 59 s); LIFT_JOBS=0 = min(cores,
  RAM / 2 GB): full build ~4.4 min on the 16-thread machine.

## Performance work (2026-10-22 .. 10-31, RX 7800 XT + Ryzen 7 7800X3D)

Baseline 2026-10-22: ~170 fps uncapped, 5.8-6.0 ms, 1% lows ~9 ms. Scaling from a docked Switch (Violet uses its
whole 33 ms): CPU ~12-15x per thread, GPU ~50x, bandwidth ~24x, so game logic should fit ~2.2-2.8 ms and the GPU's
work ~0.7 ms. Measured CPU ~4.1-4.3 ms (normal for static recomp), GPU ~5.5 ms (**~8x off: the outlier**).

- **Stand-still benchmark** (frame-rate work; the bench save is for hitches): `violetrecomp\bench.ps1 <name>
  [NX_X=v ...]` copies `build/save` to `build/save-idle-run`, runs with NX_NOPACE=1, NX_TIME=1792494000, scripted A
  through the title, hands off; averages `nvn: frame time` and NX_GPU_TIMELINE busy over the windows after the
  load hitches (log `build/bench_<name>.log`; `-Summary` re-reads; `-Rgp <frame>` captures, only `<name>` is
  positional). Spread ~0.2 ms. Run A/B pairs in both orders (ABBA). To compare exes, swap
  `build\cmake\violetrecomp.exe` (retry the copy, check the hash; the first run after a swap is ~0.1 ms slow: don't
  count it). Clear an env switch before the next run in the same shell (two runs were void).
- What a frame was (10-22): 5c translates ~3.4 ms (~2,000 draws, ~1.7 us each; Savage Wind's 2.4) and waits 1.6-2.8
  ms for the GPU; main runs ~40%. CPU-only (NX_GPU_SKIP=1): 4.1-4.3 ms, main 73% running, ~21% blocked at
  `main+b47d18` (job join, ~11 waits a frame); lifted code ~60% of main's samples, flat (hottest 2.3%).
- **Lifted calls** (nxrecomp `cf5f543`): NX_PROFILE splits lifted samples by instruction kind (calls ~23%,
  loads/stores 27%, integer 29%). Calls follow AAPCS64 (SAVEC / LOADC: no flags, callee-saved not reloaded):
  CPU-only **4.41 -> 3.99 ms**. Inline flat indirect-call table (`call_ind_fast`): runtime share 9.6 -> 3.1%, no
  frame change. SP-based loads/stores as `LDS`/`STS` (volatile unless `-DNX_STACK_PLAIN=ON`): no change.
  a64test passes (18,163 + 17,355); run it with `py -3.12` (capstone + unicorn).
- CPU-only stays ~250 fps because the frame is a chain of hand-offs between main and workers 5a/5b (job condvar),
  not one thread's speed. With rendering main ~45%: game code is not the limit.
- Ruled out: lifted code at -O2 (`NX_LIFT_OPT`; ~3%, noise; default stays -O1); decompiling the game to recompile
  natively (not worth it); hand overrides only once a profile has hot spots (none). C vs C++ for generated code: no
  difference.
- Feedback loops (Savage Wind needed them): `NX_FEEDBACK_LOG=1` (gpu.c `feedback_check`). Only two undefined cases:
  fs `3614c5d014241256` reads and writes the same 1x1 f46 target (exposure adaptation? **lead for the too-bright
  glows**, untested) and fs `8f2c86b8511ac03b` samples 1080p depth with depth writes on. Fix: copy the target first.
- **Early flushes** (gpu.c `head_flush`, NX_FLUSH_FIRST default 32, 0 off; doubling until NX_FLUSH_DRAWS):
  NX_GPU_TIMELINE=1 (`tl_*`; GPU timestamps per submission, busy / idle split every 600 frames) showed 0.93 ms idle at
  each guest submission's start, 0.63 of it until the first flush. **6.60 -> 6.09 ms.** Not run on the 1650 Ti
  (with mirrors each flush waits for the scan: compare NX_FLUSH_FIRST=0 there).
- Ruled out vs clean baselines: skipping skinning `vs 44f2ec77`, NX_FLUSH_DRAWS=60 (=1 doubles translation), no
  pass-end barrier (~0.2, noise).
- **Why is the GPU 8x off?** Tried, each no change:
  1. Colour compression: images are MUTABLE_FORMAT only when a view can need another format, with a format list
     (nxrecomp `abd7a93`, `format_list`; NX_MUTABLE_ALL=1 old way; NX_VIEW_LOG=1 logs foreign-format views: only one). Kept. RGP shows
     no DCC decompress anywhere.
  2. Not tried yet: fast clears / load-store ops (every pass LOAD/STORE in GENERAL; clears are ClearAttachments
     inside the pass; a clear at pass start could be loadOp CLEAR; discarded targets DONT_CARE).
  3. Pass splits: ~72 a frame from NVN barriers (`OP_BARRIER` -> end_rendering) + ~21 target changes + ~25 XFB.
     Eden ends a pass only when the framebuffer changes. Narrower pass-end barriers (nxrecomp `471f5c7`, `pass_att_writes`,
     `ACCESS_NO_ATTACHMENTS`; NX_FULL_BARRIERS=1 old): fewer flushes in RGP (234 -> 180), no frame change, kept.
     The cost is the stall (~230 FULL barriers a frame), not the flushes.
  4. Shader quality vs Eden's shader_recompiler (SSA IR + passes; we emit GLSL without passes). Done
     (nxrecomp `3f6dbc1`): no `nonuniformEXT` on handles read from c[2] at a fixed offset; c[1] inlined as literals
     (`cbuf()`). No change on NVIDIA or AMD. Not done: XMAD triples -> one multiply (FoldXmadMultiplyAdd), FSWZADD
     0x99/0xA5 -> dFdxFine/dFdyFine, `precise` on FADD/FMUL/FFMA (correctness, costs speed; try if z-fighting).
     Not worth it: Eden's texture specialization (conflicts with offline translation), global -> SSBO, its
     structured control flow (only 8 shaders loop).
- **RGP on the stand-still scene** (`build/standstill*.rgp`; NX_GPU_LABELS=1 names passes `pass WxH c<i>:<fmt>
  ds:<fmt> @<addr>`, draws `draw vs <hash> fs <hash>`, `barrier L<gpu.c line>`, `end pass L<line>`, `nvn barrier`):
  no decompress / fast-clear eliminate / FMask at any barrier; ~230 barriers a frame, all FULL stalls; driver adds
  CS colour clears. The frame opens with the XFB skinning chain on `1920x1080 c0:37 @12fad3a000` (vs
  44f2ec77be3a3fd3), split every 1-5 draws by NVN barriers, draws 5-20 us never overlapping. Most expensive draw:
  full-screen triangle on the lit scene buffer (`@12f5ee2000`, vs 5250e040fae92a2c fs fd87a85cf5d0fd96, 236 us); then
  the G-buffer (`@12f80a2000`, 4 colour + depth). Next there: keep the pass open across NVN barriers in passes with
  no colour / depth writes (behind a switch), time the skinning chain.
- **Render thread translation** (what it is: nvn.c records our encoding (nvn.h) into the game's command memory;
  at nvnQueueSubmitCommands gpu.c replays it into Vulkan on 5c: state, pipelines, UBO staging / push descriptors,
  binds, targets, barriers, image cache, vkCmd*). Sits just under the GPU's pace: cutting 3.42 -> 3.13 ms (nxrecomp
  `c427890`: vk_format memo, word-wise pipe_bucket, set_vertex_input skipped when unchanged...) only grew the GPU
  wait. 5c profile: AMD driver 18%, run 6.5%, draw 5%, stage_ubos 4%, memmove 3.5%.
  - Ideas (design only): Eden's Scheduler records vkCmd* into 32 KB chunks replayed on a worker; dirty-flag
    dynamic state (ours rebuilds and memcmps every draw). Savage Wind's async submission queue
    (`ref/SavageWindRecomp/runtime/nvn/objects.c`): guest returns at once, waits only at nvnSyncWait / QueueFinish.
    For Violet a replay thread helps only once translation exceeds the GPU frame (5c waits for N-1's fence). Check
    first everything that reads GPU results or gpu.c state synchronously, and gpu_cs.
- **CPU hand-offs: parked** until GPU work is done (main ~2.9 ms vs ~2.2-2.8 native estimate; the rest is the
  game's job hand-offs). Register-argument design checked on clang-cl 19.1.5 (scratch, not committed):
  `__attribute__((preserve_none))` passes Ctx + 11 u64 in registers on Win x64, musttail and pointer calls work, a
  16-byte struct return goes through memory (return x0 only). Signature `NXCC u64 f_X(Ctx *c, u64 x0..x8, u64 sp,
  u64 x30)`; at bl store only written x19-x29 and X1; functions reading x9-x18 before writing keep full SAVE; host
  functions keep `void (Ctx *)` behind thunks. Files: a64lift.py, a64rt.h, dispatch.c, hos.h, nvn.c, a64test.py.
- **For the 1650 Ti laptop** (CPU changes that gave nothing on the 7800X3D's 96 MB L3): measure with drawing off
  (`bench.ps1 <name> NX_GPU_SKIP=1`, ABBA, one throwaway run first): `-DNX_STACK_PLAIN=ON` (no re-lift; configure in
  a vcenv shell), `-DNX_LIFT_OPT=-O2`.
- Spawn / load dips (~80 fps for a second): pipeline_get and JIT waits polled Sleep(1) (pipeline wait fixed
  2026-11-10). Pipeline cache kept between sessions (`build/pipelines.bin`, or `pipelines.bins` with
  VK_KHR_pipeline_binary); `pipelines.list` prebuilt on workers at start; effect on hitches not measured.

## Local play, stalls, translator fixes (2026-11-04)

- **Real-Switch local play** (LDN, `ldn:u`) is Wi-Fi: action frames (OUI 00:22:AA) on channels 1/6/11, then a WPA2
  network keyed from a console key, Pia's UDP over it. Windows drivers can't send action frames or host such an AP
  (the desktop's RZ616 = MT7922: managed mode only). Became its own project `../ldn-beacon/` (ESP32-S3; Linux
  mt76 / Pi routes parked). Redirecting DNS doesn't work: NPLN is TLS against Nintendo's CA + account / NSO tokens.
- **Rooms** (nxrecomp `8b5de5a`, violetrecomp `9ab9a71`): Violet builds with NETPLAY; two instances did Union Circle
  and a full Tera raid. Test set-up: clients with `NX_ROOM=127.0.0.1:24872`, `NX_NICK=A|B`, saves `save-net-a` /
  `-b` (the user set music volume in them: don't overwrite); ~5.3 GB private each; a force-killed client keeps its
  nickname until ENet times out (~30 s): restart the server too. What it took: nifm accepts preset-1 requests
  (`SetLocalNetworkMode(true)`); advertise data set before CreateNetwork was wiped by it; the software keyboard
  returned "Player" as link code (now a Win32 input box, swkbd.c); the NG word filter aborts without system data
  `0100000000000823` (NgWord2, `build/sysdata/`); bsd eventfd; ConnectPrivate (layout from memory, unverified).
  Room client sends at once (was next ENet tick, up to 15.6 ms a hop).
  Hosting: `room_server.exe --port 24872 --name ... --game-id 01008F6008C5E000 --game "Pokemon Violet"
  [--password pw]` (without the game id it says MK8 and the browser's game filter hides it); playit.gg tunnels UDP
  24872 (ngrok has no UDP); the host joins 127.0.0.1:24872.
  - ldn:u's own local communication id is the NACP's first LocalCommunicationId = Scarlet's 0100A3D008C5C000
    (both games meet on it; real Switch and Eden filter on it). Dev builds read `dump/control/control.nacp`
    (`nstool -x /0` of the update's Control NCA). Log `ldn: local communication id`.
- **Frame stalls** (nxrecomp `3e6100f`; 1% lows were 20-47 ms at 60): new pipelines compiled in the draw (~100 ms
  on amdvlk) now built by 2 workers with the draw skipped (NX_ASYNC_SHADERS, on in netplay builds only); main waited
  for core 0 held by 5c blocked in `gpu_fence` on gpu_cs, which loader threads held while importing pools: import
  now outside gpu_cs, nvnQueueFenceSync gives up the core; svcSleepThread spins its last 0.25 ms. Slow frames
  after loading 255 -> 12, locked 60. `gpu:   threads this frame:` gives each thread's core hold and core wait;
  NX_PROFILE=<up to 4 threads> samples slow frames only; its own report hitches every 600 frames (ignore 600k,
  600k+1; read 1% lows from runs without it).
- **Item ball light pillars / raid den beacons fixed** (nxrecomp `dfe3ea7`, user-checked). Soft particles in the
  half-res effects layer (960x540 f41 `12f4f6a000`, composited by `acbcacd1…`) passed 0 samples: (1) translate.py
  dropped IPA.MULTIPLY's multiplier assuming w; now `in * gl_FragCoord.w * R(multiplier)` (pillar vs
  `7a1f81cac22350a1` fs `df61a3468379ebd4`); (2) their depth (`960x540 f53 @12f10ac600`) is a scaled copy of 1080p
  depth that AMD can't blit: gpu.c `depth_blit` draws it (`runtime/shaders/depth_blit.*`; regenerate
  depth_blit_spv.h with glslangValidator). Debug: NX_DEBUG_RT=<rt0>:<bits> (1 discards off, 2 depth test off, 4
  depth clamp on), NX_TRACE_VCB=<c>:<row>,..;.. (vertex uniform rows, first element of each stream). Trick: swap a
  hand-edited SPIR-V into `build/shaders/spv/<version>/` (move `pipelines.bin*` aside).
- **Translator scan** vs hardware (Eden as reference; 6,009 shaders). Fixed (user: "looks way better", bricks on a
  grey tower): NaN handling (`.SAT` -> `sat()`, FMNMX -> `nmin`/`nmax`, F2I NaN -> 0x80000000; DenormFlushToZero 32
  and SignedZeroInfNanPreserve 32 declared, may cost speed); I2I.CC flags; TLD4S y from Rb; txq() for integer
  textures; IADD3.RS carry; STS/LDS shared memory sized from control 0x754; TMML `uint(max(lod * 256, 0))`;
  FMUL.RZ (`fmul_rz`), I2F.RZ (`i2f_rz`). Every FMUL/FFMA is .FTZ. Not taken: `precise` (measure first).
  Latent (wrong but unused): HSETP2 compare bits / H_AND, F2F f16 rounding, I2F.U32.NEG, FMUL scale 7, PSETP 2nd
  dest, ISCADD .PO, SHFL UP/IDX masks, BFE.S offset >= 32, MOV lane mask, IPA centroid / offset, mixed interpolation
  in one vec4, S2R Y direction +1, SPH mrt_enable 0, depth-compare with LL/bias/1D, AOFFI on non-integer fetches,
  LDL/STL / LDC out of bounds. Raised as Unsupported: IMAD, IMUL, PRMT, SHF, P2R, CSET, doubles, most f16, TLD4 PTP,
  multisample TLDS.
- **Effects froze above ~60 fps: fixed** (user-checked). The effect manager (`main+2039640`) steps VFX emitter sets
  (`b85c44` -> `7e5924`) by `(int)(duration / 16666666.0)`: 0 below 16.7 ms. Hook `fps_effect_frames`
  (patches.txt `main+20397d0`, fps.c) gives the fraction. Game clocks: frame update `main+2b79f60` gives each clock a
  duration from table `3c2fba8` (16.7 / 33.3 / 50 / 66.7 / 83.3 ms); readers call vtbl[0](obj, 1) and read +8 (100
  sites). Open (user, uncapped): particles may look too fast (per-update behaviour in the VFX library; then step in
  whole 60 Hz frames carrying the remainder).
- Found with Ghidra headless: `nxrecomp/tools/ghidra/decomp.ps1 <abs out> <off>...` inside violetrecomp (`<off>!`
  makes a function Ghidra lacks, `x<off>` callers only; ~10 s; read-only on build/analysis/ghidra).

## Profile photo, frames, shaders, package, mods (2026-11-05)

- **Black profile photo / player icon fixed** (user-checked). The screen capture (`main+187bf80` UpdateCapture)
  renders into a pitch-linear 1920x1080 RGBA8 texture that the CPU BC1-encodes: gpu.c `linear_wrote` /
  `linear_writeback` copy GPU-written linear textures back to guest memory at submission end. Old pictures stay black.
- **Where a frame went (stand-still):** 4.8-5.3 ms (187-207 fps), GPU busy 3.1 ms (5.5 on 10-31: the translator
  fixes), translation 2.2; **main is the limit** (~71% running). Main blocked 16% at job join `main+b47d18`, ~6% on
  the game's own mutex `main+ccc41c` between main and 5a/5b in a parallel loop (`2a78764` -> `ccada8` -> `ccc00c`
  -> `ccdeac`, a physics step). Logs `build/bench_prof1105.log`, `bench_profmain1105.log`, `build/ghidra_lock*.txt`.
  - **Present stalls fixed** (nxrecomp `f715b4c`): presents no longer wait for the CPU-written texture scan
  (GetWriteWatch, up to 14.9 ms; NX_REFRESH_WAIT=1 old way). Load windows 85-112 fps -> ~205.
  - Twice a run ~60 ms frames with main holding its core (~60 s apart): not chased (NX_PROFILE=main, NX_HITCH_MS=40).
- **Day / night: no frame-rate link.** Clouds right at higher rates; our clocks are real time. `e0b960` is an
  86400 s day; `GlobalTimeSpeed` is a render setting; `timeOfDayHours` / `dayTime` / `nightTime` come from a
  FlatBuffers parameter file (loader `1513f24`); `CFunc_Tml_ResetGameTime` is a Lua command. Map clock is static.
- **Shaders from the game files: all extracted and translated.** `nxrecomp/tools/shader/extract/` (inside
  violetrecomp: `build.bat ..\build\ooz`, then `extract.py`, 80 s -> `build/shaders_all/`, 23,083 .bin) and
  `translate.py --spv ..\build\shaders_all` (340 s): 23,081 translated (`455b72ede1cc5e2b` TLD and
  `baacde0ac4bf184d` fail, never used in play); SPIR-V 3.0 GB loose.
  - `data.trpfs`: `ONEPACK\0`, u64 offset of a FlatBuffers table {pack hashes, pack offsets} (23,589 packs). Pack
    hash = FNV-1a 64 with basis `0xcbf29ce484222645` (not ...2325) of the name as `data.trpfd` stores it (slashes
    dropped). Pack = {file hashes, files[]}; file {field 1 = 3 / field 2 = 4 when Oodle, u64 size, data}. Oodle
    Kraken (`8C 06`) via ooz (`deps/ooz`, GPLv3+) in `oozbatch.exe` (separate process). 408,641 files, 34 GB.
  - Shaders are BNSH: standalone (`.trsha`, `.bnsh`), in VFXB effects (`.ptcl`, `.trsot`, `.tracp`), SARC UI
    archives, and 2 in `main.elf` (file offsets 0x43ec000, 0x43f4000: 40 engine shaders). BNSH: u64 {control,
    code} offset pairs then sizes. GLSLC control starts `34 12 76 98`, graphics code `78 56 34 12`. nvn.c's shader
    hash = FNV-1a (standard basis) of code[0 : control+0x704 (else 0x30 + control+0x6f8)]; all 6,032 seen in play
    are in the set. Control sections of the same code differ only in unread bytes.
  - Vertex-input variants: only mask `01000000` ever needed (location 12 unsigned, XFB skinning bone indices);
    plan: translate.py finds integer inputs at build time and writes those variants (missing ones fall back to float).
  - Still open for V8: a one-folder build kit (extract, nso2elf, Ghidra analysis, lift, build, translate, package;
    open choices: our own NSP extractor instead of nstool, Ghidra + JDK required).
  - Later (nxrecomp `6a6a9d0`): `package.ps1 -Out <dir>` translates every shader and builds `shaders.pak` itself.
- **Package for testers** (nxrecomp `nca.c` / `image.c` / `tools/package.ps1`): friends supply keys, firmware,
  base + update NSPs (DLC optional); nothing extracted.
  - Runtime reads NSPs directly when `--game <dir>` has `.nsp` files: prod.keys / title.keys there or in
    `%USERPROFILE%\.switch`, tickets, NCA3 header XTS, AES-CTR, the update's BKTR over the base RomFS, IVFC level 6;
    DLC and NgWord2 by data id (`game\firmware\*.nca`); ExeFS, control.nacp, icon. Byte-identical with dump/romfs,
    ~600 MB/s (each thread its own CNG key copy). Module images built from the NSOs at first start (image.c, 156 ms;
    a different game version stops with a message).
  - `violetrecomp_pkg` (NX_PACKAGE, `Pokemon Violet.exe`, no console, -O3 `lifted_o3`): game files in `game\`;
    save, cache, log.txt, `dumps\` under `%LOCALAPPDATA%\Pokemon Violet\`; F3 off, F5 / F11 off, F12 kept. Translator
    from `translator\` (embeddable Python 3.12.10 + translate.py + maxwell.py + glslangValidator). -O3's speed not
    measured. User's test folder `H:\ns-porting\violet-package\Pokemon Violet` (its game\ has hard links to rom\'s
    NSPs, prod.keys, NgWord2: empty it before sending).
  - First start: "COMPILING SHADERS: done/total", input held until the burst ends (`gpu_shaders_starting`).
  - Graphics patches (gfx.c, patches.txt): Improved Graphic Quality (`if gfx`; NX_QUALITY=0) and no DOF (`if nodof`;
    NX_DOF=1) on. **2K shadows (`if shadow`, `main+146e80c` mov w8 #0x400 -> #0x800) off: opening the map crashes**
    (null read at main+c281d0; the patched code compares a size against 0x800 first). Plan for a 1K/2K/4K toggle:
    find what the map sizes from the shadow size. main+x in `build/image/main.img` is at file offset 96 + nsym * 16 +
    strtab size + x (header in a64image.py).
- **Mods framework**: nxrecomp `runtime/nxmod.h` + `mods.c`: DLLs in `<exe>\mods\` (NX_MODS=0 none) get window
  events, can hold input, draw over the swapchain; `modkit/` = Dear ImGui (vcpkg `imgui[vulkan-binding]`; not its
  SDL3 backend: events on the window thread, drawing on the render thread). `nx_mod()` in nxrecomp.cmake.
- **Tera raid editor (F6)**: finds raid blocks by scanning guest memory for den-shaped 0x20 entries (PKHeX
  RaidSpawnList9 layout: Paldea 72 after a 0x10 seed header, Kitakami 100, Blueberry 80); shows each den's raid from
  its seed; writes seeds for a chosen species / shiny / Tera (Tera9RNG / Encounter9RNG behaviour, our code); story
  progress picks the 1-5 star roll (black dens have none). Data: `python mods/raids/raiddata.py` ->
  `build/mods/raids.dat` (needs `build/ooz`); `nxrecomp/tools/trinity.py` reads any romfs file by path. Raid tables
  `world/data/raid/{,su1_,su2_}raid_enemy_0N/..._array.bin` (ROM type 1 Scarlet, 2 Violet);
  `avalon/data/personal_array.bin` field 0 = {DevID, form, national, ?}; names `message/dat/English/common/*.dat`.
- **Audio / UI fixes:** audren REV7+ (Bink's renderer is REV10) starts the mix section with a 0x20 header {magic
  0xCAFEBABE, count}, each mix's id at +0x10 (we read the magic as the final mix volume: movie sound was noise).
  Keyboard config: texts and limit 4 bytes earlier than thought (header 0x34, sub 0xb6, guide 0x1b8, limit 0x3bc).
  Direct Connect asks for a password when the room needs one. Quiet music: hwopus was silent; now libopus (vcpkg
  `opus`, `nx_opus_lib`; multistream layout unverified).
- **Render scale 1-4x** (nxrecomp `ab7f7c5`, user-checked, switchable live: gpu.c `rescale_all` at a present):
  16:9 targets from 120x68 up are made N times larger when first drawn into; translate.py takes bit 18 of a handle
  and pc.scale (FragCoord, texelFetch, TXQ, image coords). Violet draws 360x360 targets with the 1080p depth bound
  (that pass stays 1x). Downscale to the window by a box filter (vk.c `downscale`, n x n bilinear taps, n up to 8;
  nxrecomp `29f4e94`, "noticeably smoother"). AA: render scale is supersampling; FSR 2/3 / DLSS need motion vectors
  and jitter Violet lacks; SMAA / FSR 1 not done.
- **Performance at 2x:** 60 fps capped: 26 of 29 windows locked, slow frames have our translation at 5-9 ms
  instead of ~2.5. Uncapped: **146 fps, GPU busy 5.73 ms: GPU-bound**; lows 90-100 fps; target 120 (user). Next
  then: NX_HITCH_MS=10 + NX_GPU_PROF=1 to split GPU passes (blur / bloom chains need not be scaled?) from
  translation spikes.
- **Fletchinder flying too fast** at 60 fps (some, maybe while fleeing; never Hoppip): one movement steps per frame.
  Lead: Dynamic FPS's scaling at `main+cce37c`. F12 frame `build/frames/frame_4272*`.
- Linux: Proton / Wine first; native port surveyed in `nxrecomp/docs/PLATFORMS.md`.

## Open rendering issues: rain, missing letters

- **Rain:** splashes, distant sheets and wet surfaces show, but no streaks near the camera. Frame 26027: the half-res
  effects layer (`960x540 f41 @12f4f6a000`) has 7 lit pixels (beacon frames 12k-210k): streaks never drawn.
  2026-11-11 (1650 Ti, weather mod at Poco Path; logs `build/frames/rain-*`, `rain2-*`, `rain3-*`): forced rain /
  thunderstorm leave sky blue, ground dry; rain adds no draws; thunderstorm adds white falling dots (full res) and 8
  tall quads (soft particles vs 7a1f81cac22350a1 fs df61a3468379ebd4) with 0 samples even with depth test off. Ruled
  out: skipped draws (no DrawTexture / XFB / multi-draw), particle clocks (cb10[2].x all alive), the 1080p ->
  960x540 depth copy. Light shafts (vs 6e7faccfa4d7de47) in that layer: 0 samples with depth test on, 326k-590k off:
  check the half-res depth test (ds `@12f541a000`, written by d4ea93c14e5c47f0). The opening area may have no weather
  of its own: test natural rain after the prologue. Tools: `NX_TRACE_VCB=10:2,3`, `NX_TRACE_FILTER='960x540 f41'`,
  `NX_DEBUG_RT=<rt>:bits`.
- **Missing letters:** whole glyphs blank wherever they appear ("Mareep" as " a eep", "Maschiff" -> "as hiff",
  battle labels "oundoom"; whole again in later frames). Not skipped shaders. Suspect the run-time glyph atlas
  (CPU-written) losing uploads; the CPU-texture scan took 248 ms as one battle loaded. Likely fixed by the
  2026-11-10 texture fixes (ZA section): check text after a long session. F12 captures 2026-11-06 14:32 / 14:33:
  `build/frames/frame_49186*`, `frame_55343*` (one text, one rain).
- A move's effect drawn partly once: ~530 draws skipped while pipelines built (NX_ASYNC_SHADERS); cached after.

## Mods, save edits, Lua bridge, wild spawns (2026-11-06)

- `run.ps1` has no time limit unless `-Timeout <s>` (the old 60 s default killed play sessions, once mid-save).
- **F6 raid scan** (user-checked): Violet keeps its data in the alias region (svcMapPhysicalMemory,
  `NXMOD_ALIAS_BASE`) as well as the heap: both scanned (3.5 GB). A run counts dens with a seed (`den_live`),
  allows all-zero slots (`den_slot`), needs 90% distinct seeds. Names by DevID (`e.dev`; national up to Gen 8 only:
  a "shiny Koraidon" was a Baxcalibur). Kitakami / Blueberry tables classed by live den count (<= 72 = Paldea):
  check area numbers in the DLC.
- **Save edits** (`violetrecomp/tools/svedit`, PKHeX.Core; game closed, back up `build/save` first): ride upgrades
  (dash / swim / high jump / glide / climb / fly + flight controls; user-checked) and raid levels 3-6. PKHeX's
  `KUnlocked*` = the game's `FSYS_*_ENABLE` / `FSYS_RAID_DIFFICTLTY*_RELEASE` (key = (uint)FNV-1a-64 of the name).
  In-memory flags not found by hash: flags stay closed-game edits. Fly points (`FSYS_YMAP_FLY_*`, `svedit find`)
  not done. "Story completed" not feasible. `svedit addmon` (full IVs, the save's OT). The user's pre-2026-11-05
  save: `build/saves/save-backup-2026-11-05/`.
- **Item giver (F7):** the bag is the save's KItem block, live in memory: 3000 x 0x10 by item id {pocket, count,
  flags 1 new / 2 favourite / 4 obtained, 0}; pockets 0 medicine, 1 balls, 2 battle, 3 berries, 4 other, 5 TMs, 6
  treasures, 7 picnic, 8 key, 9 materials. Found by the Rotom Phone (1829) entry plus every held item in its own
  pocket. A direct write shows only after save + restart (the bag keeps lists built at load).
- `host->watch_mem(addr, len)` (NXMOD_VERSION 2; `diag_watch`): NX_WATCH_MEM armed at run time.
- **Lua bridge** (instant items). A shop purchase writes through `main+1488d18(id, lo, hi)` -> `1488dd4`, called
  from Lua binding `1488b90`. The game logic is **Haxe compiled to Lua 5.4** (`haxe_lua/invoker.h`, CallImpl);
  parser linked. Lua C API in main: lua_load `232e0`, getS `24b90` (luaL_loadbufferx inlined), lua_gettop
  `b5d980`, lua_settop `b5db00`, lua_tolstring `b646d0`, lua_pushstring `b85bd0`, lua_rotate `b5dcd0`,
  lua_pushfstring `1a2cd10`, **lua_pcallk `b5ddd0`** (~300 callers); VM `278b0` / `4487c` / `26c60`.
  `violetrecomp/lua.c` + patches.txt `after lua_pcall_entry` at b5ddd0; nxmod.h **version 3**:
  `host->run_script(text, done, ud)` (`mods_set_script_runner`); a chunk returning `NXMOD_SCRIPT_AGAIN` retries
  later. F7 has a Lua console. A chunk starting `--quiet` isn't logged on success.
  - Scripts: romfs `script/lua/bin/release/{main,main_dynamic,dll_*}/*.blua`, Lua 5.4 bytecode with debug info,
    obfuscated names (classes `C<hash>` globals, statics `S<hash>`, methods `F<hash>`, natives `c<hash>`/`f<hash>`);
    in `build/lua/` via trinity.py.
  - Bag: `C14A8D35D72F56FE9.S90D73F1D715BCBF4()` = bag; `:FB01A9B29CD4CC6CB(id, n)` add (clamped),
    `:FE027A297CCA14276(id, n)` remove, `:F13D72EF77582090D(id)` index, `b[6][i].num`. Native store binding 1488b90
    = `cACA3F5A6.f445BD6CD` (luaL_Reg at main+4420320).
  - Native Lua classes: each `luaL_requiref(L, "c<hash>", open, 1)` (callers of main+e7d200) names a class; its
    open function's ADRP+ADD refs are the luaL_Reg tables. Mon object `c113335A8` (172 methods), makers `cA20E9FCA`
    (f80B8DD7C from PokeData = native 160dd10; f6644EFDC = 90d010).
  - Tools (kept): `tools/luabc.py` (find / dis / callers), `tools/xref.py` (ADRP+ADD refs), `tools/bfbs.py`
    (FlatBuffers schemas, also from romfs), `tools/img.py` (`ptr <off>` finds luaL_Reg entries in main.img).
- **F8 box writer** (user-checked): our own PK9 writer (layout and crypto in the header; checked with svedit
  `layout` / `check`), into the first empty slot of the live KBox block (990 x 0x158 encrypted; empty = encrypted
  blank), found by 960+ consecutive slots with valid checksums. Species stored as internal index (Espathra 956 ->
  927). Tables + trainer: `svedit monsdata <main> <build>\mods` -> `mons_species / moves / abilities / exp / balls /
  trainer.txt` (rerun after changing saves). Game-side box get `c1A1CBE3B.f6AA17B5F(box, slot)`, set
  `CEC4BF82A35D1E447.S0B3F69C4549A0284():F518978B87DAB37D6(box, slot, mon)` (party: `c1A1CBE3B.fFCE06E04():
  f36E12C71(pos, mon)`), mon `:f0BD5134F()` dev id, `:f5D94E897()` form. NX_MONS_TEST="<national>,<form>,<level>"
  (unattended; its one run logged nothing to stderr, not chased). Dropdowns filter as you type (`filtercombo.h`).
- **Wild spawns.** Rewriting the encounter tables in memory (`world/data/encount/pokedata/pokedata{,_su1,_su2}/
  ..._array.bin`: Paldea 449, Kitakami 252, Blueberry 254; fields 21 bandrate, 22 bandtype, 23 bandpoke, 25
  bandFormno, 18 enabletable) **changes nothing**: the spawner reads them into its own lists at save load (code kept
  for scan / self-test; `pokedata_lc` fails trinity.py's Oodle decode). `main+10cd434` is not the spawner (makes ~26
  mons at every load).
  - **The wild spawner is Lua**: chain `1245528 <- 1245224 <- Lua`; native `12451d0` = `c113335A8.f7AC1FE6B(spec)` (luaL_Reg at main+443a538, class registered at 140ce30).
    Script maker `C2B428A63695FD2C7.S988D7D0A4EEF693D(?, DevID, form, sex, level, ...)` (spec from
    `cC99399C3.f101D811F()`: devId f2247607A, form f81738FA1, sex f88C47BAC, level f02983AD3, `rarePercentage`
    -> shiny), called only by the wild manager `C4EE52E49562F8277` (F71A61170646F68AF, F61D1ABB7697071EB,
    F316E31E09886ADC9). F8 wraps it (original kept as `C.__nx_orig`): **user-checked** (Hisuian Arcanine in Paldea).
  - **The rest of the spec: `violetrecomp/wild.c`.** Every mon param is built by constructor `debf7c(obj, spec)`
    (callers: `PokemonParam::Create` debe4c, 24 callers, and inlined Create `1245474`). patches.txt `after
    wild_create` at debf88 (x1 spec) rewrites the spec on DevID + form match (shiny mode, IVs, EVs, nature, ability,
    gender, level); `after wild_created` at debff8 (x19 param) sets moves / PP and Tera at param+0x70. InitialSpec
    layout in wild.c (from SetupInitialSpec `160ffd0`, caller `160df74`). Mod calls the exe export
    `violet_wild_set`. `NX_WILD_LOG=1` logs every create with 5 return addresses. Any other mon of that species made
    while on (raids, trades) gets the spec too.
  - **"Around you"** (mons.cpp, map above `FIELD_PRELUDE`): wild manager instance
    `C4EE52E49562F8277.SF3075AB31C9E8AF4` (W); update `F648B6B775BCC730B` is a state machine on W[17] (0 spawning:
    `FA9A2EC1E99B58D55`; 1 waiting for field objects). Player position `K.SBF5E0EFFB821427F:f7360ED03()`; spawn
    point `cFB7F03A4.f42A4ADE3` / `f35A39D75(x, y, z, 0, radius, 0)`; info `W:F654580D678C85C0E(pt)` (pos, levels,
    biome, area, enable_table); group into W[23] by `W:F61D1ABB7697071EB(info)`, placed by `W:F9B7FBB66FA56765E`.
    "Spawn on me" moves info.pos to the player (offset slider), only while W[17] == 0. Encounter table W[26] (class
    `CE1BA7320ED1C2331`): `F2F6C7BBE115AFBCB(info, ctx, flag, list, weights)` fills entries + weights, returns the
    total; `FCEEA7F4134055567` picks; per-entry rules in `F67A9C2A9373A0AC6` (CheckArea, CheckLocation, a ground ray,
    four more); ctx = `W[28]:FDCCDAF4C1D58AECA(pos)`, ctx[4] / ctx[5] force a DevID / form. Entry getters f2BF366A2
    DevID, fE15E9D0A form, f5B8A3FE2 sex, f4646F546 / f61FD9277 levels. Weights override (K.__nx_w) is lost when the
    table is remade at save load. A row spawn finds its entry again by DevID / form at spawn time (returning a kept
    entry crashed the game).
  - **No contact battles**: behaviour-tree nodes SetEnableEncount(Blackboard) (type names main+2995db0 / 29954e0,
    vtables 4474d48 / 4474a00) call `main+1d0e5d4(component, enable)`; wild.c `h_encount_enable` (patches.txt
    main+1d0e5e4) zeroes it while on (export `violet_no_battles`). Unconfirmed; fallback lead: event flag
    `FEVT_COMMON_0140_NO_ENCOUNT` (`C10578806AC30DCA3.SBA6FF574C1C9AA09.h[name] = v` + native
    `c37452BA0.f4BEF3427(name, v)`).
  - In Union Circle the world is simulated by one player at a time (it moves): "Spawn on me" acts only on that
    instance; spawned groups become network objects (netOwnerIndex / netSerialId, c2CA78EB7.*); F47FB4E501540CBF7 keeps spawns
    off other players' mons. Untested.
  - NPC trade lead for making mons from script: `P2882` `FA1D3529002C3A75C(tray, pos, receivePoke)` makes the mon
    with `CF7739C6C4D25CC45.S470187B7637D37DC(spec)` (PokeData from eventTradeList) and stores it with
    `CD081B219DEE96EEF.S644D748FB7CE5439`. Static mons: Lua PokeData maker `cA20E9FCA.f80B8DD7C` (path P14484).
- Plans from that evening still open: confirm the wild spec on a caught mon (if the overworld model isn't shiny,
  look at `rarePercentage` `r0:f52C8AF50(...)` and the spec's `f82BDE968`); fallback for spawning: start a wild
  battle from script (`EncountWild` users in `CA5A5606298DDCB29`).
- **Spawn-pool rules** (user: "mess with the normal spawning rules"): wrap the Lua filter (`F2F6C7BBE115AFBCB` /
  `F67A9C2A9373A0AC6`), or patch the tables and reload the save, or patch the file as it's read. Ideas the user
  liked: version exclusives in both, any time of day, ignore story flags, equal weights, every species everywhere.
  Density: parameters `encounterCountMax`, `encounterCheckInterval`, `encounterUnregisterFrameCount` (loader
  main+1513f24; refs 1514048 / 151415c / 15142cc); Lua `SpawnLimit` / `SpawnTimer` / `radiusSpawnArea` near
  main.blua line 259k. Mass outbreaks are separate (BCAT `encountOutbreak/*_3_0_0`, save `KBCATOutbreak*`).

## Event raids (2026-11-07; parked by the user: "later")

- How a console gets them: Poke Portal News syncs the BCAT delivery cache, `BcatClient::ParseAndSave`
  (bcat_client.cpp; "BcatLoad Raid", "BcatLoad EncountOutbreak") stores the files in save blocks, size-checked
  against `RaidSaveDataAccessor::kDeliveryRaid*DataSize`. Files (strings at main.elf ~0x3c5c4e4-0x3c5c5a4):
  `raid/raid_enemy_array_3_0_0`, `raid/fixed_reward_item_array_3_0_0`, `raid/lottery_reward_item_array_3_0_0`,
  `raid/raid_priority_array_3_0_0`; outbreaks kEncountOutbreakPokedataPath, kEncountOutbreakZone{Main,Su1,Su2}Path
  (`world/data/encount/point_data/outbreak_point_data/outbreak_point_{main,su2}.bin|bfbs`). nn::bcat API:
  MountDeliveryCacheStorage, DeliveryCacheDirectory Open/Read, DeliveryCacheFile Open/GetSize/Read,
  RequestSyncDeliveryCache. nxrecomp `services/bcat.c` is an empty stub.
- Save blocks (PKHeX SaveBlockAccessor9SV): KBCATRaidEnemyArray 0520A1B0 (30,000 bytes), FixedRewardItemArray
  7D6C2B82 (27,456), LotteryRewardItemArray A52B4811 (53,464), PriorityArray 095451E4 (88), Identifier 37B99B4D (4),
  plus KBCATOutbreak*. Den content 2 = event, 3 = 7-star. `svedit dump <main> <dir> <key>...` writes blocks out.
- **A friend's save** (`build/saves/friend-2026-11-07/main`; dumps in `build/bcat/friend/`): raid blocks filled
  (identifier 20230728 = a 2023-07-28 delivery). `svedit copy <main> <from> "^KBCAT|^KOutbreakBC"` copied the 5
  KBCATRaid*, KBCATOutbreak* and 336 KOutbreakBC* blocks into `build/save` (backup
  `build/saves/save-backup-2026-11-07-preevent`). No change in game: dens take delivered raids only at the new-day
  reroll. To force: start with `NX_TIME` a day ahead (does new-day handler 01ea82c0 run on load?), or post
  RaidGemForceLottery (01ea821c on M+8), or e23ec8(M, 1) (the reroll).
- Other route (agreed, not started): past / custom events from PKHeX.Core's embedded lists (EncounterDist9 /
  EncounterMight9); serve `build/bcat/delivery/raid/*_3_0_0` from bcat.c (most faithful) or write the blocks. Find
  the delivery schema (.bfbs near the raid tables?). Room broadcast (`--event <file>` to joiners) maybe later.
  Not wanted: Nintendo's servers. Community archives of past events: ask first. Joiners likely get the boss from
  the host. Species without a model in Violet won't load.

## Raid crystals rebuilt on demand (2026-11-07, redone 2026-11-09)

Seeds written with F6 showed only after a restart. Found with Ghidra (`build/ghidra_gem1..5.txt`):
- Events `RaidGemForceLottery` / `RaidGemForceReflection` are posted by `01ea821c(target, name)` (FNV-1a 64 under
  key 0xdcbfd50a277e546d, via 015745bc). Lottery (new-day reroll) from 01d93564 (day counter M+0x16a8) and 01ea82c0
  (new-day reset); Reflection from the manager's update 2933374(M, dt) when M+0x1662 is set.
- The first "rebuild" (entry+0x98 = 0, crystal+0xd1 = 1) is the game's way to **remove** a den: the update drops a
  crystal once its own update (2936d94) says gone, then erases entries with +0x98 = 0 and no crystal. Hence crystals
  vanished for good (user, 11-07).
- Groups: 0 = the player's own dens; 1-3 per other player of a session (29337b4 registers a player's 300 x 6-byte
  list, 29341d8 drops one, 01ea82c0 all). M+0x2d is our own list sent to others, not a cache. But the user says raids
  in a Union Circle are separate per player (others' raids only joined from the menu), so what groups 1-3 are is
  open. The 6 bytes (area, group, point, +0x40, black / event bits, region) carry no seed; a Switch raid's boss must
  come with the lobby data (not researched: needs a session with the Switch through ldn-beacon).
- Entries are made by **e23ec8(M, force)** (manager method table at main.elf file offset 3a65428): bumps generation
  M+0x7d8; without force, with M+0x1661 clear and dens in the save (e2913c), walks 3 x 100 dens (0x20 each at +0x50,
  +0xcd8, +0x1958: area, group, point, u64 seed, content, claimed), finds the spawn point (e25b3c), adds the entry
  with the raid from the seed (e26fec(M, point, rng, seed, event index, 0); 1 also writes the save: the reroll).
  Otherwise resets (e29340) and rerolls. An entry keeps its raid (+0x20..+0x68).
- `violetrecomp/raidgem.c` (patches.txt `main+2933398 after raidgem_update`; export `violet_raid_refresh`): marks
  group-0 entries the game's way, waits until erased (up to 300 updates), `guest_call` e23ec8(M, 0), sets M+0x1662.
  Refused when M+0x1661 is set. User run 2026-11-09: `119 own dens marked (2 with a crystal)`, `dens placed again
  ... after 32 update(s): 119 own entries`: placing works; whether crystals reappear on screen: not said. If not:
  the Reflection / Lottery receiver (M+8) and where the crystal object (+0x70) is made.
- Den entry layout (confirmed live via ReadProcessMemory): +0x20 float x, y, z, +0x40 value 0-17 (Tera type?),
  +0x44 area (= vector index; 23 vectors, regions mixed), +0x48 group, +0x4c point, +0x64 region 0/1/2. North -z,
  east +x. raidgem.c copies placed dens every 30 updates; export `violet_raid_points(out, max)`.
- F6 (2026-11-09): unused slots hidden (`den_unused`); list with Den, Active, Area, From you, Position, Stars,
  Pokemon, Tera, Shiny, Seed (sortable); player position polled via Lua twice a second. Area names
  `build/mods/raid_areas.txt` (Paldea = place_name.dat line 2 * area + 4; **Kitakami / Blueberry numbers from
  memory, unchecked**).
- **F8 "Spawn on me" crash** (2026-11-09): spawn_on_me ended with `T.FCEEA7F4134055567 = nil`, but W[26] holds the
  picker itself (no metatable), so the next spawn failed. Now kept and restored (`pick0`). Found via patches.txt
  main+b5d8fc `after script_error` (lua.c): logs class.method and the Lua error before the Haxe-Lua invoker's abort
  (main+b5d450 CallImpl, svcBreak out of main+b5d930; error text also at [main+46d4238]).
- F8 tabs: Pokemon / Wild spawns (All spawns shiny: `violet_all_shiny`, mode 3 rolled -> 2 always, for every create
  that rolls; `wild: all shiny: first rolled create made shiny` confirms) / Around you.
- Lift note: `py -3.12 ../../nxrecomp/tools/lifter/a64lift.py lift ../build/lifted` from violetrecomp (plain
  `python` there was 3.13 without capstone). build.bat does not lift. A build tree configured from the game's own
  folder didn't find SDL3 (vcenv replaces VCPKG_ROOT): configured once with `-DNX_SDL3_INCLUDE/_LIB/_DLL`.

## ZA Recomp as a reference (2026-11-10; compiled, not run)

`../ref/PokeZ-ARecomp-Online` compared with nxrecomp. Changed (to try in game):
- **Glyphs**, gpu.c: three ways CPU-written contents stayed blank: `image_find`'s container takeover counted as
  valid with non-image layers / levels zero; `image_get` grown to more mips left new levels zero; partial
  `do_copy_buffer_to_texture` / WriteTexels into a never-uploaded image. Now the container uploads guest memory and
  copies in only GPU-written parts; a grown never-GPU-written image uploads whole; partial writes upload first. Z-A
  notes the same symptom ("P ease se e t a i e", 1024x2048 R8 linear glyph cache).
- **Passes kept over a re-set of the same render targets** (OP_RENDER_TARGETS; `NX_SAME_RT=0` off; `gpu:` line
  counts "passes kept"). On the 1650 Ti: no change.
- **Guest timed waits**: kernel.c `block` used GetTickCount64 (15.6 ms steps); now QPC µs; `timeBeginPeriod(1)` in
  every build.
- **Pipeline waits**: condition variable instead of Sleep(1); a worker that gave the pipeline back (P_LISTED) no
  longer leaves the waiter stuck.

Not done, ranked: (1) ignore NVN barriers inside passes with no attachment hazard (Z-A exec.c, no CMD_BARRIER;
our two feedback cases need copies first); (2) uploads in their own command buffer, deswizzled on workers,
prefetched at nvnTexturePoolRegisterTexture (the 5-9 ms spikes at 2x); (3) UBOs as UNIFORM_BUFFER_DYNAMIC with the
set kept; (4) rendering suspects: OP_MULTI_DRAW_INDIRECT / OP_DRAW_XFB / OP_DRAW_TEXTURE skipped (log level 1),
indirect quads not expanded, depth clamp on in every pass (Z-A only in depth-only / no-depth / square-depth passes:
clamp disables near clipping: glows near the camera?), AMD quad lanes from pixel position; (5) RomFS mods (Z-A
fs.c 328-813; trpfd unlisting); (6) saves via `<file>.pending` + MoveFileEx on Commit (ours write in place: a crash
mid-save can corrupt `main`); (7) libc HLE (~3% of Z-A's CPU); (8) dirty-register stores before calls; (9) older
GPUs: static vertex input in the key, uint8 indices widened on CPU, wave64 by a 32-lane prelude (for
`nxrecomp/docs/GPUS.md`); (10) crash explanations, P-cores / EcoQoS, fibers if ever used.

## Weather mod (2026-11-11; built, not yet tried in game)

`mods/weather/weather.cpp` -> `violet_weather.dll`: F9 toggles rain, F10 window sets any type, transition time,
or "Game's own". Through run_script only.
- Found with IDA batch (`idat -A -S<script>` on a copy of `main.elf.i64`). Timeline event
  "Title/Weather/WeatherChange/WeatherChange" set up by 1521448: start callback 202f928 reads `WeatherType` (1 when
  absent), calls `CFunc_Tml_SetEnvChangeSpan(seconds)` (202fa30), then 202fa74 -> 202fb44 = the static call of
  `main.tml.TimeLineCommand` method hash `A3EC462E45551FD6` with the type. "WeatherReset": 202ff54, then
  `CFunc_Tml_ResetWeather`, at its end 20301c0 `CFunc_Tml_ResetChangeSpan`. Native -> script: 202f80c(class,
  method) / 18e3f2c(class, method, &arg) hash both names (FNV-1a 64, game basis) via 15745bc / 18e4050.
- Lua class `CA71F0CDF966F3175`; the mod tries `S<hash>` then `F<hash>`: set `A3EC462E45551FD6(type)`, reset
  `ECAF51D6E7046362()`, span `8614855A6EF844D8(s)`, span reset `58CE0E7D1599C50E()` (~1 s after).
- **Type numbers are a guess** (AreaWeather9 order): 1 normal, 2 overcast, 3 rain, 4 thunderstorm, 5 intense sun, 6
  snow, 7 snowstorm, 8 sandstorm, 9 heavy fog; "Raw type number" box to check. Real ones: the script's bytecode, or
  1d56928 (ConditionCurrentWeather). Open: does the game's schedule override a forced type; does the static take
  `self`.
- First try on the 1650 Ti: no chunk ever ran: that machine's `build/lifted` predated every patches.txt hook.
  **Lifted code is per machine: pulling a patches.txt change needs a re-lift** (`python
  ..\..\nxrecomp\tools\lifter\a64lift.py lift ..\build\lifted`, 12 min, then a 20 min build); the build doesn't
  warn.

## Eggs and trainer ids in F8 (2026-11-11; PKHeX-legal offline, not yet tried in game)

- Tabs "Egg" and "Trainers" in violet_mons.dll (share its tables and box scan). Split: `mods/mons/pk9.h` (PK9
  offsets, crypto, UTF-8 <-> UTF-16, egg names per language, ID32 <-> 6-digit ID No. + SID, shiny xor / TSV),
  `mods/mons/mons.h` (tables, trainer, `box_snapshot`, `current_trainer`, `box_put`; namespace `mons`),
  `mods/mons/egg.cpp` (builder, both tabs, `mons_egg_selftest`).
- The egg: level 1, met level 1, obedience 1, egg location 30023 (picnic), met location 0 (a trade sets 30002),
  egg date today, nickname = egg name in the OT's language, egg bit, EVs 0, no item, Tera one of the species' two,
  OT friendship = hatch counter. Choices: IVs, nature, ability (hidden too), gender, ball (not Cherish), Tera,
  shiny, cycles left, custom OT, "Shiny for another trainer".
- PID fixed at creation; shininess checked against whoever hatches it. Shiny: random low half, high = low ^ TID ^ SID
  ^ (0-15); not shiny: rerolled until xor >= 16 for the hatcher.
- Trainers tab: "Read the boxes" lists OT, ID No. / SID, game, shiny (box only, not party); per row "Hatcher" / "OT".
  Use: a friend trades any mon, read their ids, make the egg, trade it back, they hatch it shiny.
- PKHeX 26.10.18 legality rules found: relearn moves = egg moves; nickname flag set; unhatched egg has NO met date;
  nickname buffer = egg name over the species name (leftovers are expected trash); 0 cycles fails, 1 legal; Master /
  Cherish fail for eggs; Tera one of the species' types; hidden ability, any IVs / size, square or star shiny pass.
  Babies (Pichu, Happiny, Riolu) are Undiscovered yet hatch: no egg-group filter. `monsdata` writes 329 egg species
  / forms (balls as a mask). A custom OT in another language than the save's may fail the trash check.
- svedit on PKHeX 26 (.NET 11): `StatNature` -> `StatAlignment`, `EncryptedPartyData` -> `WriteEncryptedDataParty`,
  IVs via `GetIVs` / `SetIVs`, bag `.Pouches`. Save path `build\save\1-000000000000000100006d6b38726563-0\main`.

## TotK Recomp as a reference (2026-11-11, GTX 1650 Ti laptop)

`../ref/totk-pc-recomp` compared with nxrecomp. Taken (compiled, run):
- kernel.c `kspin`: a thread about to sleep in `block` / `core_acquire` polls ~20 us with klk released
  (`NX_KSPIN_US`; off below 6 logical processors). The first version hung the load (took a second core after the
  last poll); now it leaves when it holds a core. 135 s ABBA on the laptop: off 14.42 / 14.09 ms, on 14.47 / 15.79:
  no gain (GPU-bound; CPU and GPU share the power budget). **Off by default** (`NX_KSPIN_US=20` on); try on the RX
  7800 XT.
- gpu_pipe.c `pipeline_get`: one-entry memo of the last ready pipeline.
- main.c: opts out of power throttling (EcoQoS, ignored timer resolution).

Laptop stand-still: ~14 ms (70-80 fps), GPU busy 9.4, idle 4.5 (1.8 at submission start, 2.3-3.0 between flushes;
mirrors on, PCIe 15.8 GB/s); translate 7.4 + GPU 9.6 in series (`bench_kspin0a.log`). bench.ps1 needs ~135 s (scene
starts 30-100 s in); its A presses run to frame 4000 (this laptop's title takes input at ~1,800-2,400).

Not done, ranked: (1) skip `set_dynamic_state` / `set_vertex_input` with a state generation bumped by every
setter (TotK `bind_sig`); cache descriptor lookups / `gpu_buffer_ro` per slot; (2) PGO (TotK +6-12%;
docs/pgo.md: clang-cl /clang:-fprofile-generate, profraw on a timer and at exit); (3) a vkCmd* recorder thread
(TotK vk_rec.c, -17% on replay; AMD hangs fixed once the recorder also begins / ends / submits); (4) dirty-register
stores, flag-kind dataflow; (5) TotK's GPU design equals ours (GENERAL, LOAD/STORE, full barriers): barrier dedup
is cheap; (6) priority inheritance, no async preemption inside SDK spin locks (TLR+0x100).

## Second reference sweep: TotK, Z-A, ssbu-recomp (2026-11-11; read-only, nothing measured)

Checked in our code: no PGO yet; `NX_ASYNC_SHADERS` off outside netplay builds (gpu.c 796-798); `rs_scale` u32 from
1; romfs.c 248 opens a synchronous handle; `crc_bits` bit-serial (a64rt.c 15); STL is SEQ_CST (a64rt.h 97);
OP_REPORT_COUNTER answers a running total and translation-time `now_ns()` (gpu.c 506); no `/GS-`. Already ahead:
registers in locals with lazy flags, flat dispatch + musttail, bindless pools + push descriptors, dynamic state,
pipeline binaries / list prebuild, write-watch texture checks. Gains are estimates.

GPU:
1. **Overlap CPU and GPU** (Z-A exec.c 97-160): answer SyncWait one frame late (keyed per frame; the engine waits
   twice a frame). Up to ~3-4.5 ms on the 1650 Ti. Hazards: vertex / index data read in place from guest pools,
   compute / XFB outputs; Z-A needed uniform copies, storage snapshots, and is off on AMD. Large, high risk.
2. **XFB skinning into a device-local mirror** (Z-A exec.c 5851-5960; 3 ms on a 1660 Super): skinned vertices go
   over PCIe now (gpu_draw.c 656-717 `xfb_begin`). Needs "GPU-written" stamps so `mirror_sync` doesn't clobber, and
   a copy-back. Time XFB passes first. Medium.
3. **Render scale below 1x / fractional / dynamic** (Z-A output_scale.c): float scale in push constants and
   translate.py. Medium, low risk.
4. **Real GPU timestamps for counters** (Z-A exec.c 288-300): Z-A's engine runs dynamic resolution from them;
   check what reads counter type 0 in Violet. Small.
5. **Graphics pipeline libraries + fast link** (TotK vk_pipeline.c 1476-1830; ssbu nvn_vk_m4.c 360-600) with kill
   switch / driver blocklist (RADV RDNA4 hang); or a loading-screen barrier (ssbu `m4_barrier`: frames with <= 60
   draws not presented until queues drain, 20 s cap): then async shaders by default. Medium-large.
6. **Texture creation / upload out of the pass** (TotK vk_texture.c 894-1010): `image_vk_create` and uploads end the
   pass with 2-3 full barriers each (gpu_image.c 480, 501, 666, 669, 806): record into `slots[cur].pre`. Medium.
7. **ASTC / deswizzle off the render thread** (TotK tex_astc_mt.c, vk_astc.c, vk_deswizzle.c; ssbu budgeted
   pre-upload nvn_vk_draw.c 640-700). Count Violet's ASTC textures first.
8. `descset_sync` rescans the whole pool every submission (gpu_desc.c 358-381, up to 65,536): dirty range from
   `write_desc`. Small.
9. Device-memory headroom (TotK vk_headroom.c): `vk_alloc` makes 64 MB blocks on the render thread (vk.c 561-595;
   8-19 ms each); eviction at a fixed 55%, not VK_EXT_memory_budget. Small.
10. Smaller: pipeline workers by CPU count, demand-first queue; push descriptor update templates; `stage_ubos` memo
    (Z-A exec.c 7329); robustImageAccess2 A/B (vk.c 380); anisotropy cap; spirv-opt (translate.py 2273); real
    occlusion queries (TotK vk_exec_xfer.c 112-260); FIFO pacing (TotK gfx_present.c).

CPU / lifter:
1. **Fewer SAVEC stores**: 19.4 registers per call (5,415 sites). x9-x17 dead across a call; x19-x28 need no store
   before a plain call (Z-A regpromote.py) except with landing pads / secondary entries, setjmp, patch / hook sites.
   ~55% fewer stores without dataflow.
2. **`preserve_none` on lifted functions** (Win64 callee-saved pushes and xmm6-15 spills for nothing; else
   `sysv_abi`); preemption / longjmp stubs save everything. Maybe 3-10% on call-heavy code.
3. Inline small leaf callees at lift time (9-11% of 634,507 `bl` sites target <= 8-16 instruction leaves).
4. Plain guest loads / stores with a compiler barrier on back-edges (TotK cpu.h 77-84): heap path never tried.
5. Host CRC32 / TBL / PMULL / AES (a64rt.c 15-33, 202, 212-228): sse4.2, pshufb, pclmul, aesenc.
6. Branch-free vector fmin / fmax / FCVTZS / FRINTA with a NaN guard (a64rt.h 208-312).
7. LDAXR / op / STLXR / CBNZ fused into one atomic (71.8k sites, 3.5% of lifted samples); STLR as release store
   where provably safe.
8. Dispatch table as 32-bit offsets (TotK dispatch.c 145-161); `host_cntpct` / `kwait_now_us` multiply not divide.
9. Build: giant cold functions at -O0 (Z-A gamebuild.c 354-373; we have 95 functions >= 3,000 instructions, largest
   f_710127f998 82,861), cost-balanced chunks (TotK gen.py 172-210), `/GS-`. PGO: record a digest of a64rt.h (inline
   changes silently invalidate a profile).

Kernel / I/O:
1. **Overlapped RomFS / NCA reads** (romfs.c 248, nca.c, services/fs.c 213 serialise every thread on one handle):
   `FILE_FLAG_OVERLAPPED` + per-call event (TotK win32.c 1103-1135). Check overlap with NX_FS_TRACE first.
2. NCA read path (nca.c 297-320): in-place AES-NI CTR instead of malloc + copy + BCrypt ECB (~600 MB/s). Packages only.
3. One logical processor per physical core, behind a switch (TotK perf.c 247-266; Savage Wind +4-10%).
4. MMCSS "Games" / above-normal for guest threads.
5. Release / retake the core only around IPC that can block (svc.c 203-204).
6. Deferred async preemption (TotK kernel.c 644-700), only if `acq` waits show.
7. Kernel lists: `threads` append-only; `best_waiter` / `next_for_core` scan every thread ever made (kernel.c 56,
   230, 530, 752).
8. Smaller: PrefetchVirtualMemory over lifted text; audout clock steered to the SDL queue (audout.c 44-75); an
   in-process A/B switch (TotK `rt_ab`); a no-core-gating switch to measure the 6-10% core waits.

First steps (cheap, measurable): time XFB / compute passes and `descset_sync`; count ASTC textures and counter-0
readers; then overlapped reads, `/GS-`, CRC / TBL lowering, SAVEC trimming, sub-1x scale.
