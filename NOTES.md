# pkmn-violet: notes

Findings, addresses, measurements and rule-outs, by topic (reorganised 2026-11-11; the dated wording is in git
history). `CLAUDE.md` holds how to do things, state and what is open. Grep this before proposing a tool or an
experiment. Addresses are main.elf offsets (main = 0x7100000000) unless noted; `main+x` in host stacks is truncated
past 16 MB (`#line` is 24-bit in CodeView): main+416f28 means main+3416f28.

## The game (4.0.0 ExeFS)

| Module | text | ro | data+bss | Notes |
|---|---|---|---|---|
| main | 0x343fc90 (52.2 MB, ~13.7 M insns) | 0xf420a0 | 0x368868 + 0xe6e38 | game, engine ("Trinity"), all middleware static; 1,013 imports |
| sdk | 0x5d5660 (5.8 MB) | 0x6add7c | | NintendoSDK 13.3.13 (MK8: 4.4.0) |
| subsdk0 | 0x33e7d0 (3.2 MB) | 0x210194 | | `movie::` (android NuPlayer/ACodec), as MK8 |

- ~16 M instructions (3.4x MK8). **C++ exceptions used** (`__cxa_throw`, `_Unwind_Resume`, `__gxx_personality_v0`:
  std, gRPC, Havok, engine). libc musl-style from the sdk (`__nnmusl_init_dso`).
- Graphics: NintendoSDK_gfx 13.3.13 (`nn::gfx`) + NintendoWare G3d / Vfx / Ui2d / Font over NVN; `glslc*` weak, never
  called. Middleware in main: Havok AI 2021.1, Oodle, Wwise (`romfs/audio/wwise`), Bink 2 (`romfs/demo/movie/*.bk2`),
  Lua, FlatBuffers, nlib LZ4, Pia 6.20.1 + NPLN 1.8.3 + gRPC + OpenSSL + libcurl.
- Services in main.npdm (36): acc:u0 aoc:u appletOE audin:u audout:u audren:u banana bcat:u bsd:u bsdcfg btm:u
  caps:su caps:u csrng ectx:aw friend:u fsp-srv htc:tenv htcs hwopus ldn:u ldr:ro mii:u mm:u nfc:mf:u nfc:user
  nfp:user ngct:u nifm:u nim:eca notif:a nsd:u nvdrv pctl pl:u prepo:u sfdnsres time:u vi:u.
- Keys: every NCA decrypts with `../keys` (key gens 14, 15, 17). `title.keys` has no Violet entries: update and DLC
  carry tickets (`--tik`); base has none. DLC owned via `game.json` "dlc": [1]; its PublicData RomFS is one
  `keyfile.dat` (`dump/aoc/01008f6008c5f001`).
- **RomFS** (30 files, 9.24 GB): almost all `arc/data.trpfs` (8.0 GB). `data.trpfs`: `ONEPACK\0`, u64 offset of a
  FlatBuffers table {pack hashes, pack offsets} (23,589 packs). Pack hash = FNV-1a 64 with basis
  `0xcbf29ce484222645` (not ...2325) of the name as `data.trpfd` stores it (slashes dropped). Pack = {file hashes,
  files[]}; file {field 1 = 3 / field 2 = 4 when Oodle, u64 size, data}. Oodle Kraken (`8C 06`) via ooz (`deps/ooz`,
  GPLv3+) in `oozbatch.exe` (separate process). 408,641 files, 34 GB. `nxrecomp/tools/trinity.py` reads any file by
  path (needs numpy; run inside `violetrecomp\`); `pokedata_lc` fails its Oodle decode.
- Game logic is **Haxe compiled to Lua 5.4** (`haxe_lua/invoker.h`, CallImpl); scripts in romfs
  `script/lua/bin/release/{main,main_dynamic,dll_*}/*.blua` with debug info and obfuscated names (classes `C<hash>`
  globals, statics `S<hash>`, methods `F<hash>`, natives `c<hash>` / `f<hash>`); extracted to `build/lua/`.
- Data files are FlatBuffers with `.bfbs` schemas next to them (`violetrecomp/tools/bfbs.py`). Names:
  `message/dat/English/common/*.dat`. Personal table `avalon/data/personal_array.bin` field 0 = {DevID, form,
  national, ?}. Species are stored by DevID (internal index; Espathra national 956 -> 927).
- ldn:u's local communication id is the NACP's first LocalCommunicationId = Scarlet's 0100A3D008C5C000 (both games
  meet on it; a real Switch and Eden filter on it). Dev builds read `dump/control/control.nacp`.

## Plan (V0-V8)

V0 shared framework `../nxrecomp` + thin per-game parts. V1 lifter at scale. V2 boot to `nnMain`. V3 headless main
loop (offline services fail cleanly; implement sub-interface openers for real, never the null-object stub: MK8's
Time Trials lesson). V4 graphics. V5 audio, movies, input, saves. V6 playable. All done.
- **V7 performance and PC features.** Measure first. Quality options (user, 2026-10-19): render scale, anisotropy,
  shadow map size (1024 cascades) are ours; LOD, draw distance, grass density are the game's data (look where
  `nn::oe::GetOperationMode` feeds render settings). Our own menu first; the game's own Options menu maybe later.
- **V8 packaging for owners**: done as `package.ps1` (Packaging). Still open: a one-folder build kit (extract,
  nso2elf, Ghidra analysis, lift, build, translate, package; our own NSP extractor instead of nstool?).
  **Never ship anything built from the game**, not even to owners.

## Lifter and build

- Bases: main 0x7100000000, subsdk0 0x7104800000, sdk 0x7105000000 (sizes 0x47d3000, 0x69a000, 0xda8000, 2 MB-aligned).
- Function export: IDA (main 37 min, 165,000 functions; sdk 100 s, subsdk0 48 s) or Ghidra 12.1.3
  (`tools/ghidra/ghidra_export.ps1 -Seeds`, `GHIDRA_HOME` / `-Ghidra`; main 19 min + 5 min seeding, 156,946
  functions, 4,289 switch tables; same `<module>_ida.json`). Keep Ghidra's "Non-Returning Functions - Discovered"
  off (it cut 9,706 sdk functions short). a64lift repeats function / jump-table recovery to a fixed point.
- All 15.86 M instructions lift (6 header words in main stay traps). Added for Violet: SQDMULH/SQRDMULH, SQXTUN,
  SRSHR/SRSRA, SHSUB, S/UABDL, S/UABAL, SQSHL/SQRSHL, SQABS, scalar saturating ops / narrows; fixed LD3/LD4/ST3/ST4
  single-lane decoding and USRA precedence. Jump tables IDA missed recovered by pattern (`Mod.recover_switches`,
  bounded by AND/UBFX/CMP: 34 sdk, 1 subsdk0, 14 main); BL-target function recovery; fall-through into IDA-split
  functions; weak imports nobody defines get trap stubs (gRPC TLS, `getentropy`); analysis cached in
  `build/modcache` (keyed on a64lift.py's mtime). `_writes` is exact (base only with writeback, Rt2 only for pairs):
  a Bink thread once jumped into an unrecovered `adr; ldrb; add lsl #2; br` table because `ldr w19, [x7], #4` counted
  as writing Rt2.
- Lua's luaD_throw needs setjmp / longjmp: host jump buffer table 256 per thread, returned-frame slots reused first,
  non-stack entries evicted first.
- Lift: 830 files, 1.0 GB of C; worker processes (NX_LIFT_PROCS, ~3 GB each) cut one step 129 -> 59 s; a re-lift
  takes ~12 min on the laptop. Build: LIFT_JOBS=0 = min(cores, RAM / 2 GB); full build ~4.4 min on the 16-thread
  desktop, ~20 min on the laptop, 25 min before the parallel compile; exe 407 MB, PDB 304 MB. Tests: `a64test` with `py -3.12` (capstone + unicorn; 43,812 + 35,632 pair cases).
- **`patches.txt`**: "if" alternates switched at run time, "call" / "after" host hooks. **Lifted code is per
  machine: pulling a patches.txt change needs a re-lift; the build doesn't warn** (the weather mod first failed on
  the laptop for that).
- Calls follow AAPCS64 (SAVEC / LOADC: no flags, callee-saved not reloaded): CPU-only 4.41 -> 3.99 ms. Inline flat
  indirect-call table (`call_ind_fast`). SP-based loads / stores as `LDS` / `STS` (volatile unless
  `-DNX_STACK_PLAIN=ON`): no change. Lifted code at -O2 (`NX_LIFT_OPT`): ~3%, noise; default -O1. C vs C++ for
  generated code: no difference. Decompiling the game to recompile natively: not worth it.
- Register-argument design checked on clang-cl 19.1.5 (not committed): `__attribute__((preserve_none))` passes Ctx +
  11 u64 in registers on Win x64, musttail and pointer calls work, a 16-byte struct return goes through memory.
  Signature `NXCC u64 f_X(Ctx *c, u64 x0..x8, u64 sp, u64 x30)`; at bl store only written x19-x29 and X1; functions
  reading x9-x18 before writing keep full SAVE; host functions keep `void (Ctx *)` behind thunks. Files: a64lift.py,
  a64rt.h, dispatch.c, hos.h, nvn.c, a64test.py.

## Runtime fixes (kernel, services, audio)

- Start-up: SDK 13 `nn::init::Start` with DT_FINI, TIPC (sm:), MapMemory from .bss / Normal memory, GetInfo 16 =
  NPDM system resource size, MapPhysicalMemory (the game's 12 GB allocator; it also keeps data in the alias region,
  `NXMOD_ALIAS_BASE`), GetInfo 20, time shared memory, CreateManagedDisplaySeparableLayer, nvnProgramSetShadersExt,
  fsp 53 / other titles' saves = not found (SV probes 9 older Pokémon games), bcat (empty), csrng, btm, audout:u
  (Wwise's output, 48 kHz stereo), hwopus, bsd:u (ENETDOWN), ngct:u; unknown commands answer with a stub object.
  Library applet ids: 0x11 keyboard; web 0x13, 0x14, 0x17-0x1a. First boot: language, look, name (Enter + "Player",
  or `NX_PLAYER_NAME`).
- Lost wake-up in `condvar_wait`: the key's waiter flag must be set before the mutex is released (the SDK's Signal
  skips the SVC while the key is 0; froze the job system).
- audout: GetReleasedAudioOutBuffers zeroes the rest of the output array (a stale tag kept Wwise draining forever
  under its global lock); buffers released on a playback clock.
- **Crash at ~4,300 frames** (Bink movie with audio): Bink's sound backend (main+20c50) mallocs its renderer work
  buffer + AudioRendererConfig from a 192 KB sub-heap (per-thread allocator at TPIDR_EL0+0x30). Our audren
  GetWorkBufferSize said 0x29000 and overran a shader file's pages; now the console's sum (0x1d000 for 32 voices).
  Found with `NX_WATCH_MEM` on the nlib heap's span tags (`TlsHeapCentral`, base 0x1040000000, span table +0xa48, 8
  bytes / page).
- audren REV7+ (Bink's renderer is REV10) starts the mix section with a 0x20 header {magic 0xCAFEBABE, count}, each
  mix's id at +0x10 (read as volume, movie sound was noise). Quiet music: hwopus was silent; now libopus (vcpkg
  `opus`; multistream layout unverified).
- Keyboard applet config: texts and limit 4 bytes earlier than thought (header 0x34, sub 0xb6, guide 0x1b8, limit
  0x3bc). The link-code keyboard is a Win32 input box (swkbd.c).
- hid writes all 7 lifos of all 10 npads (main reads Handheld too; missing ones drifted the cursor / camera).
  Six-axis lifos at NPAD +0x1758, 0x708 each.
- Guest timed waits use QPC µs (was GetTickCount64, 15.6 ms steps); `timeBeginPeriod(1)` always; power throttling
  (EcoQoS) opted out. svcSleepThread spins its last 0.25 ms.
- Threads: the game pins main + render thread (5c) to core 0, workers 5a / 5b to 1 / 2: more emulated cores won't
  help. Job-system condvar `main+b47d18` (job join): wake latency 5.4 µs to klk, 0.2 µs to a core.
- kernel.c `kspin` (from TotK): a thread about to sleep in `block` / `core_acquire` polls ~20 µs with klk released
  (`NX_KSPIN_US`; off below 6 logical processors). The first version hung the load (took a second core after the
  last poll). Laptop ABBA: off 14.42 / 14.09 ms, on 14.47 / 15.79: no gain (GPU-bound, shared power budget). **Off by
  default**; try on the RX 7800 XT.
- Saves are written in place (a crash mid-save can corrupt `main`); Z-A writes `<file>.pending` + MoveFileEx.

## Graphics

### Translator (Maxwell -> GLSL -> SPIR-V)

- Bring-up added: BRX jump tables (from c[1]), PBK/BRK, loops (blocks rerun in a `while`), FCMP, IMNMX, BFI, TLDS,
  SHFL / FSWZADD (quad subgroup ops), HADD2, HSETP2, ICMP, R2P, POPC, FLO, ISCADD.CC, ISET.CC, LDG.128, storage images
  (SUST.P, SULD.P, SURED as image atomics; bound or bindless, NVN keeps image handles in c[2] at 0x120 + 8k), VMNMX,
  VOTE, global ATOM (U32/S32), LEA; compute code starts at control word 0x708. c0[0x640] / c0[0x644] = base vertex /
  instance (read as gl_BaseVertex / gl_BaseInstance; vertex id = gl_VertexIndex - gl_BaseVertex).
- Scan vs hardware (Eden as reference; 6,009 shaders), fixed: NaN handling (`.SAT` -> `sat()`, FMNMX -> `nmin` /
  `nmax`, F2I NaN -> 0x80000000; DenormFlushToZero 32 and SignedZeroInfNanPreserve 32 declared), I2I.CC flags, TLD4S
  y from Rb, txq() for integer textures, IADD3.RS carry, STS/LDS shared memory sized from control 0x754, TMML
  `uint(max(lod * 256, 0))`, FMUL.RZ (`fmul_rz`), I2F.RZ (`i2f_rz`), every FMUL/FFMA .FTZ, IPA.MULTIPLY's multiplier
  (`in * gl_FragCoord.w * R(multiplier)`; soft particles passed 0 samples). Not taken: `precise` (measure first).
  Latent (wrong but unused): HSETP2 compare bits / H_AND, F2F f16 rounding, I2F.U32.NEG, FMUL scale 7, PSETP 2nd
  dest, ISCADD .PO, SHFL UP/IDX masks, BFE.S offset >= 32, MOV lane mask, IPA centroid / offset, mixed interpolation
  in one vec4, S2R Y direction +1, SPH mrt_enable 0, depth-compare with LL/bias/1D, AOFFI on non-integer fetches,
  LDL/STL / LDC out of bounds. Raised as Unsupported: IMAD, IMUL, PRMT, SHF, P2R, CSET, doubles, most f16, TLD4 PTP,
  multisample TLDS.
- Vertex-input variants: integer formats of 8/16 bits get shader variants with uvec4 / ivec4 inputs
  (`translate.py --only=<hash>:<mask>`, 2 bits per location; runtime `shader_variant`). Only mask `01000000` is ever
  needed (location 12 unsigned: XFB skinning bone indices). Plan: find integer inputs at build time.
- Quality vs Eden's shader_recompiler: no `nonuniformEXT` on handles read from c[2] at a fixed offset; c[1] inlined
  as literals (`cbuf()`): no frame change. Not done: XMAD triples -> one multiply, FSWZADD 0x99 / 0xA5 ->
  dFdxFine / dFdyFine, `precise` (try if z-fighting). Not worth it: texture specialization (conflicts with offline
  translation), global -> SSBO, structured control flow (only 8 shaders loop).
- Alpha test is a specialization constant (ALPHA_FUNC): no discard without one.

### Shaders from the game files

- `nxrecomp/tools/shader/extract/` (inside violetrecomp: `build.bat ..\build\ooz`, then `extract.py`, 80 s ->
  `build/shaders_all/`, 23,083 .bin); `translate.py --spv ..\build\shaders_all` (340 s): 23,081 translated
  (`455b72ede1cc5e2b` TLD and `baacde0ac4bf184d` fail, never used in play); SPIR-V 3.0 GB loose.
  `package.ps1 -Out <dir>` translates everything and builds `shaders.pak` itself.
- Shaders are BNSH: standalone (`.trsha`, `.bnsh`), in VFXB effects (`.ptcl`, `.trsot`, `.tracp`), SARC UI archives,
  and 2 in `main.elf` (file offsets 0x43ec000, 0x43f4000: 40 engine shaders). BNSH: u64 {control, code} offset pairs
  then sizes. GLSLC control starts `34 12 76 98`, graphics code `78 56 34 12`. nvn.c's shader hash = FNV-1a
  (standard basis) of code[0 : control+0x704 (else 0x30 + control+0x6f8)]; all 6,032 seen in play are in the set.
- Shader JIT: nvn.c queues a shader without SPIR-V; a host thread runs `translate.py --spv
  --only=@build/shaders/jit_list.txt` (~2,300 at boot exceed a command line); draws wait. NX_SHADER_JIT=0 off,
  NX_PYTHON picks the interpreter. SPIR-V in `build/shaders/spv/<translator version>` (FNV-1a of translate.py): a
  translator change retranslates everything. Trick: swap a hand-edited SPIR-V in (move `pipelines.bin*` aside).

### Vulkan side (gpu.c, vk.c)

- **Spec-correct only** (rule since the AMD port): every early bug relied on NVIDIA's handling of undefined
  behaviour; check with NX_VK_VALIDATION. GPU-assisted validation (gpuav_enable) caused TDRs on AMD: don't. Don't
  trust GPU runs under a Win32 debugger. amdvlk aborts with fail-fast 7 on device loss (5 is the CRT's invalid
  parameter). Device-loss hunting: VK_LAYER_LUNARG_crash_diagnostic (dumps in %USERPROFILE%\cdl) + NX_PIPE_LOG.
- AMD fixes: GPU-culling compute `fba20ffd…` assumes 32-lane warps (compute pipelines require 32-wide subgroups,
  EXT_subgroup_size_control, `vk.warp32`); unwritten vertex outputs read (0,0,0,1) (sky in quadrants); TLDS 2D
  ARRAY.LZ takes x, y from Rb, Rb+1 (encoding 8 is 2D ARRAY.LZ, 3D.LZ is 7); single-sampled alpha to coverage ->
  alpha >= 0.5 test (only with colour target 0); no D24 (D32S8 / D32), no dynamic logic op (in the pipeline key),
  set 1 has one texture array per view type (TB_*, TEX_BINDING), 32-bit integer textures get same-bits float views,
  c[16] in a free slot (`cb_of_slot`), robustBufferAccess2 / robustImageAccess2 where supported; blits the formats
  can't do are skipped and logged (AMD: no depth format is a blit destination).
- Targets render all layers / depth slices of their view (`view_layers`): the tonemap's 32x32x32 colour-grading LUT
  (3D, f61) is drawn one slice per instance (layer = instance + c0[0x644] + 1); layerCount 1 gave a black screen.
- Descriptor set 3 = texture pool entries as storage images (one level, no swizzle, sRGB as UNORM; storage-bound
  images count as GPU-written).
- CPU-written textures: only ranges of textures uploaded from guest memory are watched (GetWriteWatch over every pool
  page was 86% of the render thread; `NX_REFRESH_POOLS=1` old way), scanned on their own thread; presents don't
  wait for the scan (`NX_REFRESH_WAIT=1` old way). Three ways CPU-written contents stayed blank (from Z-A; the
  missing-letters suspect): `image_find`'s container takeover counted as valid with non-image layers / levels zero;
  `image_get` grown to more mips left new levels zero; partial `do_copy_buffer_to_texture` / WriteTexels into a
  never-uploaded image. Now: containers upload guest memory and copy in only GPU-written parts, a grown
  never-GPU-written image uploads whole, partial writes upload first.
- GPU-written linear textures are copied back to guest memory at submission end (`linear_wrote` /
  `linear_writeback`): the profile photo (`main+187bf80` UpdateCapture renders a pitch-linear 1920x1080 RGBA8 the
  CPU BC1-encodes) was black.
- Half-res depth (`960x540 f53 @12f10ac600`) is a scaled copy of 1080p depth AMD can't blit: `depth_blit` draws it
  (`runtime/shaders/depth_blit.*`; regenerate depth_blit_spv.h with glslangValidator). Item ball pillars and raid den
  beacons (soft particles in the 960x540 f41 effects layer `12f4f6a000`, composited by `acbcacd1…`) depended on it.
- Feedback loops (`NX_FEEDBACK_LOG=1`, `feedback_check`): fs `3614c5d014241256` reads and writes the same 1x1 f46
  target (exposure? lead for too-bright glows, untested); fs `8f2c86b8511ac03b` samples 1080p depth with depth writes
  on. Fix would be copying the target first.
- **Render scale 1-4x** (switchable live, `rescale_all` at a present): 16:9 targets from 120x68 up are made N times
  larger when first drawn into; translate.py takes bit 18 of a handle and pc.scale (FragCoord, texelFetch, TXQ,
  image coords). The menus draw the 3D player into a 400x800 target and mons into 360x360 ones with the 1080p depth
  still bound; a pass runs at its smallest target's scale, so they stayed 1x (user: "720p upscaled", F12 frame 26444).
  `image_promote` (2026-11-11, user-checked): a colour target drawn while a scaled depth is bound joins the scale (2D,
  not linear, >= 64x64). Downscale by a box filter (vk.c
  `downscale`, n x n bilinear taps, n up to 8). Render scale is supersampling; FSR 2/3 / DLSS need motion vectors and
  jitter Violet lacks; SMAA / FSR 1 not done.
- Graphics patches (gfx.c, patches.txt): Improved Graphic Quality (`if gfx`; NX_QUALITY=0) and no DOF (`if nodof`;
  NX_DOF=1) on. **2K shadows (`if shadow`, `main+146e80c` mov w8 #0x400 -> #0x800) off: opening the map crashes**
  (null read at main+c281d0; the patched code compares a size against 0x800 first). For a 1K / 2K / 4K toggle: find
  what the map sizes from the shadow size. main+x in `build/image/main.img` is at file offset 96 + nsym * 16 +
  strtab size + x (header in a64image.py).
- Pipelines: built on 2 workers with the draw skipped (NX_ASYNC_SHADERS, netplay builds only; ~530 draws of a move's
  effect skipped once); waits on a condition variable; cache kept between sessions (`build/pipelines.bin`, or
  `pipelines.bins` with VK_KHR_pipeline_binary); `pipelines.list` prebuilt at start; one-entry memo of the last ready
  pipeline.

### Open rendering issues

- **Far house lights and the moon flaring over the screen, not always** (the user, 2026-11-13): the samples-passed
  counter (NVN counter 1). The game resets it, draws, and has it reported (one pair a frame at the title; the
  report there is 1,036,800 = 1920 x 540). Ours answered a made-up total that grew by 65,536 with every report of
  any counter (83 million by the title), never reset. **Fix (nxrecomp `gpu_query.c`, `NX_OCCLUSION=0` the old
  answers; built 2026-11-13, booted to the title with validation: only the two known messages; not played):**
  real occlusion queries, one open in every pass once the game uses the counter, cut at each report and reset;
  the sums are made on the CPU when the submission is done, before a guest thread can see that it is
  (`gpu_completed`, `gpu_wait`), in the game's pixels (divided by the render scale squared). Log: `gpu: samples
  passed: a submission with ...` (the first four). To check in play: the lights and the moon; trees and other
  occlusion-tested objects near the camera (culling is real now, a frame late as on the console: popping?);
  GPU time with a query open in every pass. `SetRenderEnableConditional` is still ignored.
- **Rain:** splashes, distant sheets and wet surfaces show; no streaks near the camera. The half-res effects layer
  (`960x540 f41 @12f4f6a000`) has 7 lit pixels: streaks never drawn. Laptop, weather mod at Poco Path (logs
  `build/frames/rain-*`, `rain2-*`, `rain3-*`): forced rain / thunderstorm leave the sky blue and the ground dry; rain
  adds no draws; thunderstorm adds white falling dots (full res) and 8 tall quads (soft particles vs
  7a1f81cac22350a1 fs df61a3468379ebd4) with 0 samples even with depth test off. Ruled out: skipped draws (no
  DrawTexture / XFB / multi-draw), particle clocks (cb10[2].x all alive), the depth copy. Light shafts (vs
  6e7faccfa4d7de47) there: 0 samples with depth test on, 326k-590k off: check the half-res depth test (ds
  `@12f541a000`, written by d4ea93c14e5c47f0). The opening area may have no weather of its own: test natural rain
  after the prologue.
- **Missing letters:** whole glyphs blank wherever they appear ("Mareep" as " a eep", battle labels "oundoom"),
  whole again later. Suspect the run-time glyph atlas (CPU-written); likely fixed by the CPU-written texture fixes
  above: check text after a long session. F12 captures `build/frames/frame_49186*`, `frame_55343*`.
- Earlier, unconfirmed whether still there: the Let's Go glow too bright; huge coloured glows from `?` / `!` markers
  near the camera (suspects: blend factors, the feedback case fs 3614c5d0, depth clamp on in every pass); the "…"
  thought bubble shows only grey corners (draws `de9e66ba…` / `f3fbc253…`, BC3 sRGB atlas); cyan-white streaks in the
  air in Mesagoza (`frames/frame_14699.ppm`). Validation still reported: shadow pass R8_UINT target sampled as float,
  A2C without alpha output, integer attributes into float inputs, a point-list topology mismatch, interface
  mismatches (RuntimeSpirv-Location-06272, OpEntryPoint-08743).

## Frame rate

- **Any frame rate plays at normal speed** (user-checked at 120). The game steps by a duration from a table
  (`main+3c2fbb0` = table[present interval - 1]) and derives speed from it (`main+d9643c`); `fps.c` writes the entry
  every frame (rodata via VirtualProtect, page kept writable: re-protecting raced) and keeps the speed divisor at 30.
  Animation timer `main+b92784` (h_fps_timer_step) fixed in every scene kind except 2. Frame update `main+2b79f60`
  gives each clock a duration from table `3c2fba8` (16.7 / 33.3 / 50 / 66.7 / 83.3 ms); readers call vtbl[0](obj, 1)
  and read +8 (100 sites).
- Effects froze above ~60 fps: the effect manager (`main+2039640`) steps VFX emitter sets (`b85c44` -> `7e5924`) by
  `(int)(duration / 16666666.0)`; hook `fps_effect_frames` (patches.txt `main+20397d0`) gives the fraction. Open:
  particles may look too fast uncapped (then step in whole 60 Hz frames carrying the remainder).
- Map to Fl4sh's "60 FPS" / "Dynamic FPS (ASM)" pchtxt (`mods-ref/`; pchtxt addresses = main.elf addresses, build id
  709BFD66...). Not ported: its movement scaling at `cce37c` (stutter walking; lead for **Fletchinder flying too
  fast** at 60, F12 frame `build/frames/frame_4272*`), its code-cave NOP at `1526d64`, its `2b7a874`. Not clocks:
  60.0 at `170c408`, 100000.0 at `20a8d3c`.
- Parked: cutscenes run fast above 30 (fine at 80-90, teleports at 120+). Scene kind (`main+2936584`, setter
  `29364c8`) stays 0 in cutscenes; `b926f0` posts one global step (timer 2, from `main+27b04`) forced to 33.3 ms in
  kind 0.
- Day / night has no frame-rate link: clocks are real time. `e0b960` is an 86400 s day; `GlobalTimeSpeed` is a render
  setting; `timeOfDayHours` / `dayTime` / `nightTime` come from a FlatBuffers parameter file (loader `1513f24`);
  `CFunc_Tml_ResetGameTime` is a Lua command. `NX_TIME=<unix>` pins the clock.
- Switches: `NX_FRAMERATE=60|120|0` (default 60 via `game.json`), F5 cycles; NX_NOPACE=1 uncapped (IMMEDIATE above 60
  without MAILBOX).

## Performance

### Where things stand

- **RX 7800 XT + 7800X3D**, stand-still: 4.8-5.3 ms (187-207 fps), GPU busy 3.1 ms, translation 2.2; **main is the
  limit** (~71% running; blocked 16% at job join `main+b47d18`, ~6% on the game's mutex `main+ccc41c` between main
  and 5a / 5b in a physics step: `2a78764` -> `ccada8` -> `ccc00c` -> `ccdeac`). Mesagoza uncapped 150-195. CPU-only
  (NX_GPU_SKIP=1) ~250 fps: the frame is a chain of hand-offs between main and the workers, not one thread's speed.
- **At 2x scale**: 146 fps uncapped, GPU busy 5.73 ms (GPU-bound), lows 90-100; capped 60: 26 of 29 windows locked,
  slow frames have translation at 5-9 ms instead of ~2.5 (target 120, user).
- **GTX 1650 Ti laptop**: ~14 ms (70-80 fps), GPU busy 9.4, idle 4.5 (1.8 at submission start, 2.3-3.0 between
  flushes); translate 7.4 + GPU 9.6 in series. Was 23-29 fps: the 367 MB CPU_UNCACHED | GPU_CACHED pool (every mesh,
  index, UBO) was read over PCIe 3.0 x8. Pools >= 8 MB that are GPU_CACHED and CPU-visible are mirrored in VRAM
  (NX_POOL_MIRROR=0 off): CPU-written pages (write watch, scanned per guest submit ~0.5 ms) copied in by a `pre`
  command buffer, GPU copies / clears go to both, ranges written by storage buffers or XFB read from host memory from
  then on. G-buffer 19-24 -> 1.0 ms. **On AMD the mirror loses** (137-167 vs 155-205 fps): AMD runs use
  NX_POOL_MIRROR=0; turn it on only where it pays (open).
- Native estimate (docked Switch uses its whole 33 ms; CPU ~12-15x per thread, GPU ~50x, bandwidth ~24x): game logic
  ~2.2-2.8 ms, GPU work ~0.7 ms. Measured GPU was ~8x off before the translator fixes.

### How to measure

- **Stand-still benchmark**: `violetrecomp\bench.ps1 <name> [NX_X=v ...]` copies `build/save` to
  `build/save-idle-run`, runs with NX_NOPACE=1, NX_TIME=1792494000, presses A through the title (to frame 4000),
  hands off; averages `nvn: frame time` and NX_GPU_TIMELINE busy after the load hitches (log
  `build/bench_<name>.log`; `-Summary`; `-Rgp <frame>`). ~135 s (the scene starts 30-100 s in). Spread ~0.2 ms: run
  A/B pairs ABBA; two 600-frame windows are too noisy. To compare exes swap `build\cmake\violetrecomp.exe` (check the
  hash; the first run after a swap is ~0.1 ms slow). Clear env switches between runs in one shell.
- Benchmark save `build/save-bench` (for hitches; copy first): a ~5 min scripted scene on game time; compare whole
  runs. Day / night moved GPU cost 19 -> 24 ms there: pin NX_TIME.
- Log lines every 600 frames: `nvn: frame time` (avg, 1% low, worst), `gpu: per frame` (translating / presenting /
  waiting), `cpu:` per thread, `gpu: threads this frame:` (core hold / wait). NX_GPU_PROF=1 per pass (inflates
  around barriers), =2 per shader; NX_GPU_TIMELINE=1 GPU timestamps per submission (busy / idle); NX_HITCH_MS=<ms>
  splits slow frames (translating / presenting / GPU wait / shader loads / JIT / pipeline builds / images / uploads /
  slot waits / mirror copy-in); NX_PROFILE=<up to 4 threads> samples slow frames (its own report hitches every 600
  frames: read lows from runs without it), and splits lifted samples by instruction kind (calls ~23%, loads / stores
  27%, integer 29%).
- **RGP** works on Adrenalin **26.10.2** (25.9.2 lost the device / hung Windows on any capture): `.tools\rdts\
  RadeonDeveloperToolSuite-2026-10-02-1872\RadeonDeveloperPanelCLI.exe -m profiling -p <name>
  --rgp-auto-capture=frame:N -o <file>.rgp` (N >= 5; `--rgp-sqtt-buffer-size high`; wait for the .rgp). NX_GPU_LABELS=1
  names passes `pass WxH c<i>:<fmt> ds:<fmt> @<addr>`, draws `draw vs <hash> fs <hash>`, barriers by gpu.c line.
  Windows' GPU counters show 0% for us.

### Done (kept)

Submit and present give the emulated core up; flush every 250 draws (`NX_FLUSH_DRAWS`) with early flushes doubling
from 32 (`NX_FLUSH_FIRST`, 6.60 -> 6.09 ms; not tried on the laptop with mirrors); presents on their own thread
(NVIDIA's vkQueuePresentKHR blocked ~4-5 ms under Optimus; second queue or `queue_lock`; uncapped, a frame arriving
mid-present is dropped); shaders interned via hash maps; stage_ubos reuses unchanged c[0] / c[2] staging and pushes
only declared slots; draw() records only changed state; pool_find tries the last hit; descset_use skips unchanged
draws; DMB / DSB load- or store-only = compiler barrier, full = `lock or (%rsp)`; FRSQRTE memoised; wakes sent after
klk is released; translation 3.42 -> 3.13 ms (vk_format memo, word-wise pipe_bucket, set_vertex_input skipped when
unchanged: only grew the GPU wait); narrower pass-end barriers (`pass_att_writes`, NX_FULL_BARRIERS=1 old); images
MUTABLE_FORMAT only with a format list (NX_MUTABLE_ALL=1 old); passes kept over a re-set of the same targets
(NX_SAME_RT=0 off); frame stalls (1% lows 20-47 ms at 60 -> locked 60): new pipelines off the draw, pool imports
outside gpu_cs, nvnQueueFenceSync gives up the core; a getenv per storage buffer per draw removed (switches read once).

### Frame stalls at 60 / 120 capped, 2x (2026-11-12, user sessions with NX_HITCH_MS, RX 7800 XT)

- **Lua's collector was the 40-150 ms main-thread stall** (main holds its core, nothing else busy; one every 45-75
  s). The game keeps the collector stopped and drives it once a frame (`main+c8c36c`: `lua_gc` = `main+c8c520`,
  variadic; STEP then STOP; COLLECT after 300 s without a full one, timer at `7104759d08`, flag `[71046cb850]` set
  by any full collection). Generational mode (the VM starts incremental with `lastatomic` set, i.e. generational
  after a bad major collection, and turns generational within the title): young collections only, old garbage
  grows from ~57 MB to the allocator's limit (~70 MB), then `luaM_` calls `luaC_fullgc(L, 1)` (`main+2b690`;
  `fullgen` `main+2b840`, `luaC_step` `main+2ac30`, `luaS_newlstr` `main+3be00`). global_State: totalbytes +10,
  GCdebt +18, GCestimate +20, lastatomic +28, currentwhite +64, gcstate +65, gckind +66, genminormul +68,
  genmajormul +69, gcrunning +6a, gcemergency +6b. **Fix (lua.c, patches.txt; `NX_LUA_GC=0` off):** hooks beside
  the routine's STEP call switch the VM to incremental (pause 100, step multiplier 12) and step 1 KB over the
  debt; the 300 s COLLECT becomes COUNT. Field, 120 fps: 2.7-3.5 MB/s allocated, steps 0.27-0.33 ms a frame
  (generational at the title: 0.14), longest 6-8 ms (once a cycle, about every 2 s: open, probably the atomic
  phase), heap 58-65 MB, no emergency collection in 9 minutes. `lua: ` lines every 3600 steps and at each full
  collection. Forcing STEP data 1 without leaving "declared generational" made every step a whole mark and the
  heap sit at 60+ MB: the switch must clear `lastatomic` (lua_gc INC does).
- **Present jitter:** a frame was presented when its work was done, so 7% of frames at 60 were 2-8 ms late on
  screen while `nvn: frame time` (measured after the pacing sleep) said 16.67. `hold_present` (nvn.c) holds a
  finished frame until the longest recent work + 0.3 ms after its tick (at most 3/4 of the period;
  `NX_PRESENT_HOLD=0` off): 7% -> ~1%. The `gpu: slow frame` line measures present to present, `nvn: frame time`
  tick to tick: read both.
- **Host imports freed on their own thread** (gpu.c `junk_loop`): vkFreeMemory of an import (FreeGpuVirtualAddress /
  DestroyAllocation2) was 17% of the render thread's samples in slow frames. Effect on the 20-125 ms translation
  spikes with nothing attributed: not measured apart yet.
- **The game's own full collections were the 50-70 ms hitch every 4-12 s** (the 60 fps session of 2026-11-12,
  `build/run-session-1112.log`: 214 of the ~560 frames over 25 ms, main holding its core, a `luaC_fullgc(emergency
  0) from main+c8c5dc` line before each). `main+25ff7d0` takes the VM's lock and calls `lua_gc(L, LUA_GCCOLLECT)`
  (`25ff818` tail call / `25ff83c`); it is reached through the function pointer at `[main+46d3928]` (`if (p->fn)
  p->fn(p + 0x20)`) from 14 places that let go of script objects (most often `c50e14`, `c31b54`, `ff172c`). Its
  neighbour `main+1107154` is `lua_gc(L, LUA_GCGEN, 0, 0)`. **Fix (lua.c `h_gc_request`, patches.txt; built
  2026-11-12, booted to the title, not played yet; **withdrawn the same day: it crashes, see the crash entry below**):** the request becomes LUA_GCCOUNT and the paced collector
  hurries (200 KB a step, ~2 ms a frame) until two cycles have ended, so what was let go is freed within about
  half a second. `NX_LUA_GC=2` keeps the paced steps with the game's full collections. The `lua: collector` line
  now counts the requests. To check in play: no `luaC_fullgc` lines from `c8c5dc`, `longest` under ~3 ms, heap
  still 58-65 MB, and nothing that depended on the collection being done at once (objects' finalisers run up to
  half a second later).
- Rest of that session's frames over 25 ms (after the title): 158 with all threads busy (loading), 85 presenting,
  60 shader loads, 24 GPU wait, 7 pipelines, 7 uploads. **Presenting 20-37 ms** comes in runs of 10-40 frames and
  is inside `vk_present` between the acquire and the submit (fence, acquire, submit all ~0): recording the
  present's commands, i.e. the downscale pass, the overlay or the menu / mods' ImGui draw. Not split yet: time
  `ui_draw` / `mods_draw` apart in `vk_present_times` and note whether a mod window was open.
- **A save was a ~100 ms frame** (manual or automatic, not told apart: the user saved by hand once or twice in
  these sessions; identified by `main`'s write time matching one of them; 3 in a 3-minute session, `build/run-session-1112b.log`; the same signature
  in the session before): after two `prepo:u` 10105 calls main starts a thread that starts another (priority 44,
  cores 0-2), which took the free core 1 and kept it ~65 ms; job worker 5a (core 1 only, same priority) waited
  behind it while main waited for 5a on an idle core 0. The console's scheduler moves such a running thread to the
  idle core; ours did not. **Fix (nxrecomp kernel.c, `core_move`; `NX_CORE_MIGRATE=0` off):** a core's owner that
  may use an idle core moves there when a thread is ready behind it (checked when a core goes idle and when a
  thread finds its cores taken). User session after it (`run-session-1112c.log`, 60 fps, 2x): its one save frame
  44.5 ms (main's own 40 ms of work), every window after loading at 60.0 with the worst frame 17-47 ms, one frame
  over 50 ms in the field (a 58 ms shader load); 3-7 moves a frame in ordinary play (`cpu:` line).
- The presents' recording time is split in the slow-frame line (`recording`, `menu and mod windows`, `mods'
  draws`). With the menu open: 7-9 ms a frame in `menu and mod windows` (ImGui), once 15.9 ms `recording` on the
  first frame. The 20-37 ms runs of the earlier session are probably a mod window's ImGui; not reproduced yet.
- The game's full-collection requests did not happen at all in those two sessions (`0 full collections asked`):
  what triggers them is not known, and the hurry path is still unplayed.
- **Shader loads in the field** (the 58 ms frame: 3 loads, 45 ms) are not translations: every shader the game
  has is translated already (`JIT waits 0`). The time is `shader_load` on the render thread at a shader's first
  draw: reading its loose `.spv` (8,111 files), scanning it and vkCreateShaderModule. **Fix (gpu_shader.c,
  `preload_thread`; `NX_SHADER_PRELOAD=0` off; built 2026-11-12, booted, not played yet):** `gpu_shader_created`
  queues a shader that has a translation for a thread that loads it then (the game makes shader objects while it
  loads); `ShaderCode.load_state` settles who loads when a draw gets there first. 4,000+ loaded ahead in the first
  45 s. To check in play: `shaders N ms (n)` in field slow-frame lines near 0. A new pipeline for a new shader is
  still built off the draw (its draws skipped until done), as before.
- **120 fps at 2x after the fixes above** (user session, `build/run-session-1112d.log`, NX_HITCH_MS=10,
  NX_GPU_TIMELINE=1, window 2560x1440): the stalls are gone (15 frames over 16 ms and 3 over 25 ms in ~3 minutes
  of play after loading, none over 50; no shader load in any slow frame, so the preload works; no full-collection
  request again). What is left is the GPU: 1,883 of 2,016 frames over 10 ms are GPU wait. GPU busy 6.7 ms a frame
  where 120 holds (1% low 9-12 ms) and 8.1-9.2 ms where it does not (97-110 fps for 5 to 8 windows in a row);
  translating 2.1-3.6 ms. Seven frames of 16-30 ms were translation with nothing attributed (no upload, image,
  pipeline or shader time). Lua's longest step 6-15 ms per 30 s. Next: per-pass GPU time at 2x in one of the
  97-110 fps places (NX_GPU_PROF=1 or an RGP capture), and a fractional render scale (2x renders 3840x2160 for a
  1440p window; "To do" GPU 5).
- **Where the 2x frame's GPU time goes** (user sessions with NX_GPU_PROF, `run-session-1112e.log` by pass,
  `-1112f.log` by shader; 7.9-8.4 ms a frame in a 100 fps place):
  - 1.3-1.5 ms: one draw, the deferred lighting triangle (vs `5250e040fae92a2c` fs `fd87a85cf5d0fd96`, 3840x2160;
    236 us at 1x). Its translation is 2,395 lines (`build/shaders_look/`): G-buffer texelFetches through `rs_c2`,
    each with the integer / float texture choice made at run time, NaN-safe `nmin` / `nmax` / `sat`, bounds-checked
    `cbr`.
  - **1.0-1.15 ms: the skinning draws** (vs `44f2ec77be3a3fd3`, no fragment shader; 137-174 a frame, 6.6 us each
    where other small draws are 0.4-0.8 us). Each has its own BeginTransformFeedback / End, and 38 passes a frame
    end on `xfb_hazard` (gpu_draw.c `draw`). This cost does not scale with the render scale (a third of the 1x
    frame's 3.1 ms). "Skipping skinning: no gain" in "Ruled out" was measured main-bound at 1x.
    `xfb_hazard` treats a vertex buffer bound without a size as reaching to the end of memory and checks every
    slot, used or not: likely false hazards. NX_GPU_PROF now logs how the captures are laid out (`gpu prof: xfb
    draws`: same buffers / binding as the draw before, starting where it ended): not read in the field yet.
  - 0.63-0.65 ms outside passes (155-162 copies, clears, barriers a frame).
  - By pass at 3840x2160: lit buffer + depth 1.96 ms (10 passes), `c0:37` 1.66 (46 passes: the skinning chain and
    others share the label), G-buffer 1.56 (3 passes).
- **Skinning draws 1.0 -> 0.30 ms a frame** (2026-11-12, user sessions `run-session-1112g..j.log`, NX_GPU_PROF=2).
  Every skinning draw captures into its own, non-adjacent place of one pool (3 buffers, strides 12 / 4 / 4), so
  spans cannot be shared. (1) The vertex shader stores its captures itself (translate.py `PC_DECL_XFB`: device
  addresses in push constants after the 20 common bytes; gpu_draw.c `xfb_begin`, point lists drawn without
  indices; `NX_XFB_STORE=0` off): 6.6 -> 3.3 us a draw. (2) `xfb_hazard` looks only at the vertex streams the
  attributes name and the program's stages, and at the captures one by one instead of their hull (`xfb_ranges`):
  passes ended for it 46 -> 0.5 a frame, 1.8 us a draw, "outside passes" 0.8 -> 0.67 ms. Validation run
  (`-1112k.log`): only the two known messages (Location-06272, OpEntryPoint-08743). Not confirmed by the user
  yet: that characters and Pokemon animate as before. The translator changed: `build/shaders/spv/27a75578`.
  Left there: the game's own barriers between skinning draws (88 a frame end a pass at gpu.c OP_BARRIER).
- **Crash 2026-11-12 17:32 (a long session, audren frame 188,000; log `build/crash-1112.log`): the hurry was
  wrong.** `nn::diag` Abort on thread 5b from `main+eb0784`. The code there (`eb071c`-`eb0780`): call the
  function pointer at `[main+46d3928]` (the full collection), try an allocation again (`bl eb0890`), abort (line
  0x69) if it still fails. So the 14 callers of the collection are **allocation-failure paths**, and the
  collection has to be finished when it returns. `h_gc_request` now only counts the requests and leaves them
  full (rebuilt 2026-11-12, not run); `NX_LUA_GC=3` is the hurry, for experiments. An emergency collection
  (`luaC_fullgc(emergency 1)`, heap 68.6 MB) came just before the abort.
  So the 50-70 ms hitches every 4-12 s of the earlier long session are the game running out of room in some
  heap and collecting to get by, and they will be back in long sessions. Next: which allocator fails and why it
  is full after a long session (`eb0890`'s heap; `c97840` is the allocation before it; do we leak, or is the
  heap sized for something we size differently?), or collect ahead of the failure in small steps when that heap
  runs low. The first minutes of a session never ask (`0 full collections asked`).
- **Broken text again** (the user, 2026-11-12 17:23): F12 capture `build/frames/frame_23259*` (the frame, and
  targets `240x135 f63 @12f1c36000`, `1920x1080 f63 @12f6752000`, `30x16 f63 @12f1bd1800`). So "likely fixed
  2026-11-10" in "Open rendering issues" is wrong. Not looked at yet.
- The skinning change is user-checked: characters and Pokemon looked right. Old translations
  deleted: only `build/shaders/spv/27a75578` is left (11 GB -> 1 GB).
- Still open at 120, 2x: GPU busy 7-9 ms in some areas (GPU-bound there; 1,132 of 1,538 slow frames were GPU
  wait); translation spikes with no cause in the slow-frame line (~25 a session over 15 ms); presenting 15-25 ms
  (53 frames); single shader loads of 34-36 ms. NX_PROFILE's report is a ~200 ms hitch every 600 frames.
- **The lighting draw's translation, 2026-11-13** (fs `fd87a85cf5d0fd96`). **No gain in play:** the draw is still
  1.435 ms at 2x (user session, `build/run-session-1113a.log`), so the instructions removed below were not its
  cost (fetches / memory? open). It is the
  sun, one local light, fog and the half-res buffers' upsample in one pass: no loop over lights. Compiled for the
  7800 XT offline (`rga.exe -s vk-spv-offline -c gfx1101 --isa ...` from `.tools/rdts`, on the `.spv`; work files
  `build/shaders_look/lit/`): 4,175 instructions, of which ~1,150 were 20 software integer divisions:
  `rs_sub()`'s `ivec2(gl_FragCoord.xy) % r`, once per scaled texelFetch (the driver has no integer divide), run
  only above 1x: the draw cost 5.5-6.4x at 4x the pixels. translate.py now works the sub-pixel offset out once
  in main (`rs_init`, `rs_s`) from a float quotient: 3,024 instructions (-28%); 1,971 of 8,134 shaders use it.
  Translations are in `build/shaders/spv/ef98d08b` (`27a75578` is the old set, can go). Tried on top, each ~5%
  and not taken: no float controls, plain min / max for `nmin` / `nmax`, no integer / float texture choice.
  Left in it: ~300 scalar mask instructions from the block flags (`b1`..`b63`).
- **The game's barriers inside a pass wait for the next draw, 2026-11-13** (gpu.c OP_BARRIER, gpu_draw.c
  `barrier_due`; `NX_BARRIER_SKIP=0` off; not measured in play yet). The pass ends only if that draw samples one
  of the pass's targets, or a shader of the pass could store to memory or images (captures are checked on every
  draw anyway, `xfb_hazard`). Aimed at the 64 pass ends a frame after skinning draws (`-1112j.log`). Title boot
  with validation at 2x: the two known messages; there the pass ends are target changes (71 a frame) and one
  barrier is kept. To check in play with NX_GPU_PROF: `passes/frame ended at`, the new line `the game's barriers
  inside passes`, "outside passes" (was 0.67 ms), and that nothing flickers. "Deferring NVN barriers" in "Ruled
  out" was before the capture fixes, when the captures ended those passes anyway.
- `ref/PokeZ-ARecomp-Online` and `ref/totk-pc-recomp` are not on the desktop (only on the laptop?): the line
  numbers under "To do, ranked" cannot be read here.
- The lifter patches no branch instructions (`bl`): hook the instruction before (`after`) and change the arguments.

### Ruled out (measured, no gain)

Deferring NVN barriers; skipping skinning `vs 44f2ec77`; no barrier at pass ends (also corrupts lighting); CPU_NO_ACCESS
pools in VRAM; staging in ReBAR VRAM; stable clocks (-13%); NX_FLUSH_DRAWS=60; colour compression / format lists (no
DCC decompress in RGP); shader-quality changes above; kspin on the laptop.

### What a frame looks like (RGP, stand-still)

~230 barriers a frame, all FULL stalls (the cost is the stall, not the flushes); no decompress / fast-clear eliminate /
FMask; ~72 pass splits from NVN barriers + ~21 target changes + ~25 XFB (Eden ends a pass only when the framebuffer
changes). The frame opens with the XFB skinning chain on `1920x1080 c0:37 @12fad3a000` (vs 44f2ec77be3a3fd3), split
every 1-5 draws by NVN barriers, draws 5-20 µs never overlapping. Most expensive draw: full-screen triangle on the lit
scene buffer (`@12f5ee2000`, vs 5250e040fae92a2c fs fd87a85cf5d0fd96, 236 µs); then the G-buffer (`@12f80a2000`, 4
colour + depth). Pools (Mesagoza): 3,314 CPU_NO_ACCESS (367 MB), one 350 MB CPU_UNCACHED, a 268 MB compressible.
Render thread profile: AMD driver 18%, run 6.5%, draw 5%, stage_ubos 4%, memmove 3.5%. Twice a run ~60 ms frames with
main holding its core (~60 s apart): not chased.

### To do, ranked (from the Z-A, TotK and ssbu-recomp sweeps; estimates, nothing measured)

First steps (cheap, measurable): time XFB / compute passes and `descset_sync`; count ASTC textures and counter-0
readers; then overlapped reads, `/GS-`, CRC / TBL lowering, SAVEC trimming, sub-1x scale.

GPU:
1. **Overlap CPU and GPU** (Z-A exec.c 97-160): answer SyncWait one frame late (5c now waits at `main+cd0eb4` for frame
   N-1's fence, fenced at `cd1c54` after submitting at `cd1a74`). Up to ~3-4.5 ms on the laptop. Hazards: vertex /
   index data read in place from guest pools, compute / XFB outputs; Z-A needed uniform copies, storage snapshots,
   and is off on AMD. Large, high risk.
2. **Ignore NVN barriers inside passes with no attachment hazard** (Z-A, no CMD_BARRIER; our two feedback cases need
   copies first; TotK's barrier design equals ours: dedup is cheap).
3. **XFB skinning into a device-local mirror** (Z-A exec.c 5851-5960; 3 ms on a 1660 Super): skinned vertices go over
   PCIe now (gpu_draw.c `xfb_begin`). Needs "GPU-written" stamps so `mirror_sync` doesn't clobber, and a copy-back.
4. **Uploads / texture creation out of the pass** (TotK vk_texture.c 894-1010; gpu_image.c 480, 501, 666, 669, 806
   end the pass with 2-3 full barriers each): record into `slots[cur].pre`, deswizzle / ASTC on workers, prefetch at
   nvnTexturePoolRegisterTexture (the 5-9 ms spikes at 2x).
5. **Render scale below 1x / fractional / dynamic** (Z-A output_scale.c): float scale in push constants.
6. Skip `set_dynamic_state` / `set_vertex_input` with a generation bumped by every setter (TotK `bind_sig`); cache
   descriptor lookups; UBOs as UNIFORM_BUFFER_DYNAMIC; push descriptor update templates; `stage_ubos` memo.
7. A vkCmd* recorder thread (TotK vk_rec.c, -17% on replay; Eden's 32 KB chunks; Savage Wind's async submission
   `ref/SavageWindRecomp/runtime/nvn/objects.c`). Helps only once translation exceeds the GPU frame.
8. Graphics pipeline libraries + fast link (TotK vk_pipeline.c 1476-1830; ssbu nvn_vk_m4.c 360-600) with a driver
   blocklist (RADV RDNA4 hang), or a loading-screen barrier (ssbu `m4_barrier`): then async shaders by default.
9. Smaller: real GPU timestamps for counters (Z-A exec.c 288-300; `OP_REPORT_COUNTER` answers a running total);
   `descset_sync` dirty range (gpu_desc.c 358-381); device-memory headroom via VK_EXT_memory_budget (`vk_alloc` 64 MB
   blocks on the render thread, 8-19 ms each); pipeline workers by CPU count; anisotropy cap; spirv-opt; real
   occlusion queries; FIFO pacing; rendering suspects (OP_MULTI_DRAW_INDIRECT / OP_DRAW_XFB / OP_DRAW_TEXTURE
   skipped at log level 1, indirect quads not expanded, depth clamp on in every pass, AMD quad lanes from pixel
   position).

CPU / lifter:
1. **Fewer SAVEC stores**: 19.4 registers per call (5,415 sites); x9-x17 dead across a call; x19-x28 need no store
   before a plain call (Z-A regpromote.py) except with landing pads, secondary entries, setjmp, hook sites. ~55% fewer.
2. **`preserve_none` on lifted functions** (Win64 callee-saved pushes and xmm6-15 spills for nothing): maybe 3-10%.
3. **PGO** (TotK +6-12%, `docs/pgo.md`: clang-cl `/clang:-fprofile-generate`, profraw on a timer and at exit; record a
   digest of a64rt.h).
4. Inline small leaf callees (9-11% of 634,507 `bl` sites target <= 8-16 instruction leaves); plain guest loads /
   stores with a compiler barrier on back-edges (TotK cpu.h 77-84); LDAXR / op / STLXR / CBNZ fused into one atomic
   (71.8k sites, 3.5% of lifted samples); STL is SEQ_CST now (a64rt.h 97).
5. Host CRC32 / TBL / PMULL / AES (a64rt.c 15-33, 202, 212-228); branch-free vector fmin / fmax / FCVTZS / FRINTA
   with a NaN guard (a64rt.h 208-312); dispatch table as 32-bit offsets; `host_cntpct` multiply not divide.
6. Build: giant cold functions at -O0 (95 functions >= 3,000 instructions, largest f_710127f998 82,861), cost-balanced
   chunks, `/GS-`. On the laptop try `-DNX_STACK_PLAIN=ON` and `-DNX_LIFT_OPT=-O2` with drawing off (ABBA).

Kernel / I/O:
1. **Overlapped RomFS / NCA reads** (romfs.c 248, nca.c, services/fs.c 213 serialise every thread on one handle):
   `FILE_FLAG_OVERLAPPED` + per-call event. Check overlap with NX_FS_TRACE first.
2. NCA reads with in-place AES-NI CTR (packages only; ~600 MB/s now).
3. One logical processor per physical core behind a switch (Savage Wind +4-10%); MMCSS "Games"; release the core only
   around IPC that can block (svc.c 203-204); deferred async preemption; priority inheritance, no async preemption
   inside SDK spin locks (TLR+0x100); kernel lists that don't scan every thread ever made (kernel.c 56, 230, 530,
   752); PrefetchVirtualMemory over lifted text; libc HLE (~3% of Z-A's CPU).

## Debugging tools

- `run.ps1 -DumpAfter N` (mutex owners, host stacks, audout state); `NX_DUMP_MEM=addr:len,*ptr:len` (also on
  crash); `NX_TRACE_CALLS=addr,...`; `NX_WATCH_MEM=addr:len@ms` (every write: writer, old value, host stack of
  `NX_WATCH_DEPTH` frames; GPU writes not seen; mods arm it with `host->watch_mem`); `NX_ONE_CORE` (races vs logic).
- GPU: NX_PIPE_LOG, NX_SKIP_SHADERS=hash,..., NX_GPU_SKIP=1|2|4|8 (all / global-memory / textured / non-2D),
  NX_NO_DISCARD, NX_DUMP_SSBO=<hash>, NX_DUMP_DRAWS=<target> with F12 (target after every draw), per-draw sample
  counts and indirect args in traced frames, NX_DEBUG_RT=<rt>:<bits> (1 discards off, 2 depth test off, 4 depth clamp
  on), NX_TRACE_VCB=<c>:<row>,..;.. (vertex uniform rows), NX_TRACE_FILTER='960x540 f41', NX_VIEW_LOG=1.
- Input: NX_INPUT_LOG, NX_INPUT_REPLAY=<log> (`build/input_walkout.log`, `input_door.log`). Unattended runs only:
  `NX_INPUT='300-30000/60:A;330-30000/60:B'`. New game: `--save <empty dir>`.
- Lua errors: patches.txt main+b5d8fc `after script_error` (lua.c) logs class.method and the error before the
  Haxe-Lua invoker's abort (main+b5d450 CallImpl, svcBreak out of main+b5d930; text also at [main+46d4238]).
- Ghidra headless: `nxrecomp/tools/ghidra/decomp.ps1 <abs out> <off>...` inside violetrecomp (`<off>!` makes a
  function Ghidra lacks, `x<off>` callers only; ~10 s). idalib on `main.elf.i64`: 15 s a batch; IDA batch `idat -A
  -S<script>` on a copy.
- **Whole-game Hex-Rays decompile** (2026-11-12, IDA 9.3): `build/analysis/hexrays/main_0.c` .. `main_7.c` (326 MB,
  163,935 functions, blocks of 256 dealt round robin, so grep all eight). Each function follows a line
  `// ==== <ELF offset hex> <name>` (offset = main+off): `rg -n "^// ==== b5d8fc " build/analysis/hexrays`.
  1,095 failed (`FAILED: <why>`): 964 are import stubs ("special segments"), 27 call analysis, 2 too big. Made by
  `nxrecomp/tools/ida_decomp.ps1 dump\elf\main.elf build\analysis\hexrays -Seeds build\analysis\main_ida.json`:
  analysis 17 min (162,375 functions + 1,557 from the lifter's export) writes `dump/elf/main.elf.i64` (815 MB), then
  8 idat processes on copies, 17 min. idat needs `TVHEADLESS=1` to run without a console (the script sets it).
  Not done: sdk, subsdk0 (same command on their ELFs). The script's decompile step ran as written; its analysis
  step was started by hand that day (same idat arguments), so the script's own analysis branch is untested. The
  earlier "idat batch failed to start" did not recur and its cause was never found. Shard speed 10-40 functions/s,
  idat about 0.5-1 GB each. Hex-Rays names are IDA's defaults (`sub_<off>`): no symbols or types were imported.
  The .i64 opens in the IDA GUI; batch scripts should work on a copy.

## Local play

- **Rooms**: Violet builds with NETPLAY (`G_NETPLAY`). Two instances did Union Circle and full Tera raids. Test set-up:
  `NX_ROOM=127.0.0.1:24872`, `NX_NICK=A|B`, saves `save-net-a` / `-b`; ~5.3 GB private each; a force-killed client
  keeps its nickname until ENet times out (~30 s): restart the server too. What it took: nifm accepts preset-1
  requests (`SetLocalNetworkMode(true)`); advertise data set before CreateNetwork was wiped by it; the link-code
  keyboard (swkbd.c); the NG word filter aborts without system data `0100000000000823` (NgWord2, `build/sysdata/`);
  bsd eventfd; ConnectPrivate (layout from memory, unverified). The room client sends at once.
- Hosting: `room_server.exe --port 24872 --name ... --game-id 01008F6008C5E000 --game "Pokemon Violet" [--password
  pw]` (without the game id the browser's game filter hides it); playit.gg tunnels UDP 24872 (ngrok has no UDP).
- **Real Switch**: LDN is Wi-Fi (action frames OUI 00:22:AA on channels 1 / 6 / 11, then a WPA2 network keyed from a
  console key, Pia over UDP); Windows drivers can't send action frames or host such an AP. Hence `../ldn-beacon`
  (ESP32-S3). Redirecting DNS to play online doesn't work: NPLN is TLS against Nintendo's CA + account / NSO tokens.
- A recomp host leaves the security parameter (server random) zero; a trade opens its network twice (2026-11-11:
  `network of 192.168.1.2` at :34 and :37), the Switch joined 2 s after the second; first attempt dropped after 1.7 s
  (deauthentication reason 3), second stayed. **Link trades with a real Switch work** (user, 2026-11-11).
- **Why joins dropped:** a link-code search hosts and scans in turns (log: "created a network", "scan found 0",
  created again). The beacon kept a torn-down network on the air until 5 s without a ScanResp; the Switch joined
  it, the PC (scanning, not hosting) ignored the Connect, and the Switch left ~515 ms after the auth response.
  Fixed: ldn_net.c sends DestroyNetwork to whoever scanned in the last 10 s (`watchers`; not broadcast, a room
  member connected elsewhere might take it as its own), and the beacon drops the network on it (fallback: scan
  every 0.5 s, off after 2.5 s). Next trade: first join stayed, 1 s after the network went up (user: the rest of
  the wait is the search itself, slow online too).
- In Union Circle the world is simulated by one player at a time (it moves). Raids there are separate per player
  (others' raids only joined from the menu).

## Packaging

- Friends supply keys, firmware, base + update NSPs (DLC optional); nothing is extracted. With `.nsp` files in
  `--game <dir>` the runtime reads them directly: prod.keys / title.keys there or in `%USERPROFILE%\.switch`,
  tickets, NCA3 header XTS, AES-CTR, the update's BKTR over the base RomFS, IVFC level 6; DLC and NgWord2 by data id
  (`game\firmware\*.nca`); ExeFS, control.nacp, icon. Byte-identical with dump/romfs, ~600 MB/s (each thread its own
  CNG key copy). Module images built from the NSOs at first start (image.c, 156 ms; another game version stops with a
  message).
- `violetrecomp_pkg` (NX_PACKAGE, `Pokemon Violet.exe`, no console, -O3 `lifted_o3`, speed not measured): game files
  in `game\`; save, cache, log.txt, `dumps\` under `%LOCALAPPDATA%\Pokemon Violet\`; F3 off, F5 / F11 off, F12 kept.
  Translator from `translator\` (embeddable Python 3.12.10 + translate.py + maxwell.py + glslangValidator). First
  start: "COMPILING SHADERS: done/total", input held until the burst ends. The user's test folder has hard links to
  `rom\`'s NSPs, prod.keys and NgWord2 in its `game\`: empty it before sending.
- Linux: Proton / Wine first; native port surveyed in `nxrecomp/docs/PLATFORMS.md`.

## Mods

### Framework

- nxrecomp `runtime/nxmod.h` + `mods.c`: DLLs in `<exe>\mods\` (NX_MODS=0 none) get window events, can hold input, draw
  over the swapchain; `modkit/` = Dear ImGui (vcpkg `imgui[vulkan-binding]`; not its SDL3 backend: events on the
  window thread, drawing on the render thread). `nx_mod()` in nxrecomp.cmake; the game exe depends on every mod.
- **Mods' settings** (nxmod.h version 5, 2026-11-12): `host->setting_get / setting_set (+ _int)` keep values in
  `settings.ini` as `mod.<key>` (options.c's store; written within a frame or two). First user: F8's "All spawns
  shiny" and "No contact battles" (`mod.mons.all_shiny`, `mod.mons.no_battles`), put back in `nx_mod_init`.
  Checked: a seeded `mod.mons.all_shiny=1` logs `wild: all spawns shiny on` at start and all five mods load.
  Ticking the box and restarting is the user's check.
- **Lua bridge** (nxmod.h version 3): `host->run_script(text, done, ud)`; a chunk returning `NXMOD_SCRIPT_AGAIN` retries
  later; `--quiet` chunks aren't logged on success. `violetrecomp/lua.c` + patches.txt `after lua_pcall_entry` at
  b5ddd0. Lua C API in main: lua_load `232e0`, getS `24b90`, lua_gettop `b5d980`, lua_settop `b5db00`,
  lua_tolstring `b646d0`, lua_pushstring `b85bd0`, lua_rotate `b5dcd0`, lua_pushfstring `1a2cd10`, **lua_pcallk
  `b5ddd0`** (~300 callers); VM `278b0` / `4487c` / `26c60`.
- Native Lua classes: each `luaL_requiref(L, "c<hash>", open, 1)` (callers of main+e7d200) names a class; its open
  function's ADRP+ADD refs are the luaL_Reg tables. Mon object `c113335A8` (172 methods), makers `cA20E9FCA`
  (f80B8DD7C from PokeData = native 160dd10; f6644EFDC = 90d010). Native -> script calls hash class and method (FNV-1a
  64, game basis) via 15745bc / 18e4050 (202f80c(class, method), 18e3f2c(class, method, &arg)).
- Tools: `tools/luabc.py` (find / dis / callers), `tools/xref.py` (ADRP+ADD refs), `tools/bfbs.py` (FlatBuffers
  schemas, also from romfs), `tools/img.py` (`ptr <off>` finds luaL_Reg entries in main.img).

### F6: Tera raids (user-checked)

- Raid blocks found by scanning guest memory (heap and alias region, 3.5 GB) for den-shaped 0x20 entries (PKHeX
  RaidSpawnList9 layout: Paldea 72 after a 0x10 seed header, Kitakami 100, Blueberry 80); a run counts dens with a
  seed (`den_live`), allows all-zero slots (`den_slot`), needs 90% distinct seeds; unused slots hidden. Each den's
  raid from its seed; seeds written for a chosen species / shiny / Tera (Tera9RNG / Encounter9RNG behaviour, our
  code); story progress picks the 1-5 star roll (black dens have none). Names by DevID (`e.dev`).
- Data: `python mods/raids/raiddata.py` -> `build/mods/raids.dat` (needs `build/ooz`). Raid tables
  `world/data/raid/{,su1_,su2_}raid_enemy_0N/..._array.bin` (ROM type 1 Scarlet, 2 Violet). Area names
  `build/mods/raid_areas.txt` (Paldea = place_name.dat line 2 * area + 4; **Kitakami / Blueberry numbers from memory,
  unchecked**). List columns: Den, Active, Area, From you, Position, Stars, Pokemon, Tera, Shiny, Seed (sortable);
  player position polled via Lua twice a second.

### Raid crystals rebuilt on demand

Seeds written with F6 showed only after a restart. Found with Ghidra (`build/ghidra_gem1..5.txt`):
- Events `RaidGemForceLottery` / `RaidGemForceReflection` are posted by `01ea821c(target, name)` (FNV-1a 64 under key
  0xdcbfd50a277e546d, via 015745bc). Lottery (new-day reroll) from 01d93564 (day counter M+0x16a8) and 01ea82c0
  (new-day reset); Reflection from the manager's update 2933374(M, dt) when M+0x1662 is set.
- Setting entry+0x98 = 0, crystal+0xd1 = 1 is the game's way to **remove** a den (the update drops a crystal once its
  own update 2936d94 says gone, then erases entries with +0x98 = 0 and no crystal): the first attempt made crystals
  vanish for good.
- Groups: 0 = the player's own dens; 1-3 per other player of a session (29337b4 registers a player's 300 x 6-byte
  list, 29341d8 drops one, 01ea82c0 all). M+0x2d is our own list sent to others. The 6 bytes (area, group, point,
  +0x40, black / event bits, region) carry no seed: a Switch raid's boss comes with the lobby data (not researched).
- Entries are made by **e23ec8(M, force)** (manager method table at main.elf file offset 3a65428): bumps generation
  M+0x7d8; without force, with M+0x1661 clear and dens in the save (e2913c), walks 3 x 100 dens (0x20 each at +0x50,
  +0xcd8, +0x1958: area, group, point, u64 seed, content, claimed), finds the spawn point (e25b3c), adds the entry
  with the raid from the seed (e26fec(M, point, rng, seed, event index, 0); 1 also writes the save: the reroll).
  Otherwise resets (e29340) and rerolls. An entry keeps its raid (+0x20..+0x68).
- `violetrecomp/raidgem.c` (patches.txt `main+2933398 after raidgem_update`; export `violet_raid_refresh`): marks
  group-0 entries the game's way, waits until erased (up to 300 updates), `guest_call` e23ec8(M, 0), sets M+0x1662;
  refused when M+0x1661 is set. User run 2026-11-09: 119 own dens marked, placed again after 32 updates. **Whether
  crystals reappear on screen: not reported.** If not: the Reflection / Lottery receiver (M+8) and where the crystal
  object (+0x70) is made.
- Den entry layout (confirmed live): +0x20 float x, y, z, +0x40 value 0-17 (Tera type?), +0x44 area (= vector index;
  23 vectors, regions mixed), +0x48 group, +0x4c point, +0x64 region 0/1/2. North -z, east +x. raidgem.c copies
  placed dens every 30 updates; export `violet_raid_points(out, max)`.

### Event raids

- How a console gets them: Poké Portal News syncs the BCAT delivery cache; `BcatClient::ParseAndSave` (bcat_client.cpp;
  "BcatLoad Raid", "BcatLoad EncountOutbreak") stores the files in save blocks, size-checked against
  `RaidSaveDataAccessor::kDeliveryRaid*DataSize`. Files (strings at main.elf ~0x3c5c4e4-0x3c5c5a4):
  `raid/raid_enemy_array_3_0_0`, `raid/fixed_reward_item_array_3_0_0`, `raid/lottery_reward_item_array_3_0_0`,
  `raid/raid_priority_array_3_0_0`; outbreaks kEncountOutbreakPokedataPath, kEncountOutbreakZone{Main,Su1,Su2}Path
  (`world/data/encount/point_data/outbreak_point_data/outbreak_point_{main,su2}.bin|bfbs`). nn::bcat API:
  MountDeliveryCacheStorage, DeliveryCacheDirectory Open/Read, DeliveryCacheFile Open/GetSize/Read,
  RequestSyncDeliveryCache. nxrecomp `services/bcat.c` is an empty stub.
- Save blocks (PKHeX SaveBlockAccessor9SV): KBCATRaidEnemyArray 0520A1B0 (30,000 bytes), FixedRewardItemArray 7D6C2B82
  (27,456), LotteryRewardItemArray A52B4811 (53,464), PriorityArray 095451E4 (88), Identifier 37B99B4D (4), plus
  KBCATOutbreak*. Den content 2 = event, 3 = 7-star. `svedit dump <main> <dir> <key>...` writes blocks out.
- **A friend's save** (`build/saves/friend-2026-11-07/main`; dumps in `build/bcat/friend/`): `svedit copy <main>
  <from> "^KBCAT|^KOutbreakBC"` copied the 5 KBCATRaid*, KBCATOutbreak* and 336 KOutbreakBC* blocks into `build/save`
  (backup `build/saves/save-backup-2026-11-07-preevent`). Dens take delivered raids only at the new-day reroll:
  **after a restart a black crystal showed a Rillaboom "(event)"** (user, by 2026-11-11; not fought). The identifier
  20230728 is the event's date: the 7-star Mightiest Mark Rillaboom (Normal Tera) first ran 2023-07-28 00:00 UTC to
  07-30 and again 08-04..06; a rerun from 2026-06-04 (Nintendo Wire) likely reused the delivery.
- Plan for a Switch catch (talked through, untested): the Switch joins the PC's raid through ldn-beacon with its
  clock set inside the event window (a mon's met date is the catching console's date; no time stored); 7-star bosses
  are one per save. PKHeX's EncounterDist9 / EncounterMight9 carry no dates (only an event index), so it doesn't check
  the met date against the event.
- **How the game uses a delivery** (Ghidra 2026-11-11, `build/ghidra_ev*.txt` copies of the scratch runs):
  - **Schemas** are in the romfs: `world/data/raid/delivery_raid_enemy/delivery_raid_enemy_array.bfbs` and
    `delivery_raid_priority/delivery_raid_priority_array.bfbs`; rewards use `raid_{fixed,lottery}_reward_item/*.bfbs`.
    Read them with `tools/bfbs.py x <path>`.
  - **Enemy array:** `{values: [{raidEnemyInfo}]}`. Info fields: 0 romVer (0 both, 1 Scarlet, 2 Violet), 1 no, 2
    deliveryGroupID (byte), 3 difficulty (int), 4 rate (byte, signed in the sum), 5 / 6 dropTableFix / Random (u64
    table_name), 7 captureRate, 8 captureLv, 9 bossPokePara (PokeDataBattle, same slots as the romfs raid tables),
    10 bossPokeSize, 11 bossDesc (RaidBossData: shield / extra actions), 12 raidTimeData.
  - **Priority array:** `{values: [{versionNo int, deliveryGroupID {groupID_01..10 bytes}}]}`.
  - The friend's enemy array is 72 entries: entry 0 is the Rillaboom, the other 71 are placeholders with every field
    written.
  - **Raid save object** (the one with dens at +0x50 / +0xcd8 / +0x1958) holds pointers to the delivery buffers:
    +0xe308 enemy (0x7530 bytes), +0xe340 fixed rewards, +0xe378 lottery rewards, +0xe3b0 priority. +0xe300 is the
    identifier. +0x83a8 is an int per den: its event group (below 1 = any).
  - **Setters** (BcatClient 1d7c458 calls them in this order, M = the crystal manager of raidgem.c):
    1d93018(M, data) enemy, 1d933a0 fixed, 1d931dc lottery, 1d92c24 priority. Each one copies `data` into the save
    buffer, then re-points the resource via 01eaa4e0(res, &buffer): res+0x120/0x128/0x130 become the FlatBuffer view
    (root table at +0x128). The resources are M+0x1668 enemy, +0x1678 fixed, +0x1688 lottery, +0x1698 priority;
    01189558(obj + 0xe0) looks one up, +0x11a = loaded.
  - The priority setter stores the old identifier in M+0x16a8 and writes versionNo as the new one (01eaa618). The
    update (01d93564) then sees the change and posts RaidGemForceLottery: the dens are rerolled with the new events.
  - **A den's raid** (e23ec8 -> e26fec -> e28ab0): standard dens take `Xoro(seed)`, then 012457e4 rolls the star
    target for the story progress (e2826c: 0-4 from FSYS_RAID_DIFFICTLTY3..6_RELEASE). Event dens (content 2 / 3,
    group from +0x83a8) roll the same star target first (only to burn the RNG and get the progress' difficulty
    range [first target's, highest target's]; the max becomes 7 once 6 stars are out). Then 01ea8504 -> 01ea86d4
    walks the delivered entries in order:
    - skips an entry whose romVer is for the other game;
    - with a group, skips an entry whose deliveryGroupID differs or whose difficulty is outside the range;
    - sums the rates, takes NextInt(sum) on the same generator, and picks the entry where the running sum passes it.
    This is PKHeX's dist behaviour.
  - Rewards: 01ea9788 looks a table_name up in the romfs reward tables (M+0x1650) first, then the delivered ones
    (M+0x1688). A made-up delivery can point its drop tables at the romfs ones.
- **Events mod (F4, `mods/events/events.cpp`, 2026-11-11, built, not run yet).** It delivers live through
  raidgem.c's exports. `violet_raid_delivery(which, out, max)` reads a file out of the raid save object, which
  raidgem.c finds through the save list and e280dc. `violet_raid_deliver(parts[4])` queues new files for the setters
  on the game thread. With an unchanged identifier, the dens are rebuilt afterwards.
  - Shared reader / writer: `mods/common/delivery.h`. It is a small FlatBuffers writer that leaves zeros out and
    shares vtables and equal sub-tables (2026-11-11). Table fields are queued and written after the tree,
    breadth first, and a copy is reused only when it lies after the field pointing at it (uoffset_t is unsigned).
    Check (a scratch program on delivery.h): the friend's 72 entries rebuild field for field, in 28,264 bytes
    unshared and 1,816 shared (its 71 placeholders are equal). All 230 PKHeX raids built as the mod builds them
    (templates) round-trip in 28,362 of the 30,000 bytes, ~123 a raid (unshared: 105 fit, ~298 a raid).
  - **No entry cap** in what the game does with a delivery: 027bab80 (re-pointing the resource) only builds the
    view (root table, schema object), with no verifier and no size or count check. 01ea86d4 walks the whole `values`
    vector through the reflection accessors. BcatClient checks only the file size (30,000). Other readers of the
    enemy resource (if any) not checked.
  - **When events ran:** `mods/events/event_dates.csv` (committed, hand-made from Serebii's event list and event
    pages through the web-fetch agent, 2026-11-11; 241 rows). Columns start, end, event, species, form, stars, tera,
    version, shiny, pkhex. A row matches every PKHeX raid of its species and stars whose form, Tera, game and
    shininess don't contradict it. pkhex (`D2` / `M1`: events.txt's kind and index, shown in the Pokemon cell's
    tooltip) narrows it further. The index isn't unique per delivery (Salamence D1 and Rayquaza D1 share it).
    Built to `build/cmake/mods/violet_events/` (game.cmake POST_BUILD); package.ps1 copies a mod's own folder.
    Matching (scratch replica): 214 of 230 raids dated. Undated: one group of 4-5 stars (Gengar, Gyarados,
    Bronzong, Lucario, Florges, Mimikyu, D2) not on Serebii's list. Rows without a raid: the 2025 shiny Ruinous
    legends (not in PKHeX 26.10.18's lists). Gaps in the CSV: second-week Blissey of the 2024 Mighty events; some
    reruns the index leaves out (only some 2026 ones filled in).
  - **Game clock:** `<cache dir>\clock.txt` (Unix seconds on the first line) starts the game's clock there at every
    start (nxrecomp time.c, after NX_TIME). The Events window writes it ("Game clock": a date from the Ran column,
    noon local) and removes it ("Back to real time").
  - Data:
    - `svedit events <dir>` writes `events.txt`: PKHeX's Encounters9.Dist (174) and .Might (56) in the game's
      terms, DevID through SpeciesConverter.GetInternal9.
    - raiddata.py writes `raid_templates.txt`: Paldea's first both-games raid per star level, giving drop tables,
      capture rate, size, bossDesc (WAZA extra actions and the charge penalty move dropped) and time data.
    - raids.dat is version 2 and adds a personal table (DevID, form → national, types, gender, abilities).
  - A built entry:
    - the mon comes from PKHeX: talentType VALUE with fixed IVs, else V_NUM;
    - the rest comes from the template of its star level; 7 stars use 6's template with hpCoef 3500 (the
      Rillaboom's);
    - captureLv = level, deliveryGroupID 1, no = identifier * 100 + n;
    - the priority is {identifier, groupID_01 = "Event dens"}.
  - **Unverified guesses:**
    - groupID_N is the number of dens a reroll makes group N (the friend's 7-star delivery has 1);
    - F6 assumes event dens are group 1 and doesn't read +0x83a8;
    - fixed IVs draw nothing from the RNG;
    - the progress' difficulty range (raids.cpp event_range) is taken from PKHeX's star thresholds.
  - F6 shows delivered raids in its den list and picker ("(event)") and finds seeds for them. Den content 2 / 3 is in
    its write combo.
  - Backups: the save's delivery when the mod first reads it goes to `build/events/backup-<identifier>/`, as BCAT
    file names. Any such folder (or one with `raid/`) can be delivered from the window.
- **Event outbreaks** (Ghidra 12.1.3 on the data drive, `-Ghidra <dir>`; 2026-11-11):
  - BCAT files `encountOutbreak/{pokedata,zone_main,zone_su1,zone_su2}_array_3_0_0`. Save blocks
    KBCATOutbreakPokeData 6C1A131B (3,608 = 0xe18), KBCATOutbreakZonesPaldea 3FDC5DFF / Kitakami F9F156A3 /
    Blueberry 1B45E41C (768 = 0x300 each), KBCATOutbreakEnabled 61552076 (Bool). Active ones: KOutbreakBC01..10
    {Main,DLC1,DLC2}{CenterPos, DummyPos, Species, Form, Found, NumKOed, TotalSpawns, Materials, DeliveryID (u64),
    DeliveryZoneID (byte), DeliveryPokeID (byte)}, KOutbreakBC{Main,DLC1,DLC2}NumActive.
  - **Schemas** (romfs; found by checking guessed paths' FNV hashes against data.trpfd's list, as trinity.py's
    `has()` does):
    `world/data/encount/delivery_outbreak/delivery_outbreak_{pokedata,zone_main,zone_su1,zone_su2}/
    delivery_outbreak_<x>_array.bfbs`.
    - pokedata `{values: [DeliveryOutbreakPokeData]}`: 0 id u64, 1 devid u16, 2 sex int, 3 formno byte, 4 / 5 min /
      maxlevel short, 6 area (table AreaNo_DO: 23 bools _1.._23), 7 locationName u64 (hash), 8 enabletable (struct
      5 bools land / up_water / underwater / air1 / air2), 9 versiontable (table A, B), 10 bringItem (struct itemID
      int, bringRate byte), 11 enableRarePercentage bool, 12 rarePercentage float, 13 addRibbonType int (RibbonType),
      14 addRibbonPercentage float, 15 enableScaleRange bool, 16 / 17 min / maxScale short.
    - each zone file `{values: [{0 zoneid u64, 1 lotvalue short, 2 poke1 u64, 3 poke1lotvalue short, ... 10 poke5,
      11 poke5lotvalue}]}`.
  - The friend's blocks read cleanly with these: 30 pokedata and 10 zones per region, all placeholders
    (locationName = the FNV basis, the hash of ""; versiontable A and B set). No real outbreak to copy.
  - **Loader** 0290d3b8(M, data) (no direct callers; M = *DAT_046d4ed0): data is one struct [u32 at +0 (stored at
    M+0x50; identifier?)][pokedata 0xe18][main 0x300][su1 0x300][su2 0x300]. It calls the setters 029293dc /
    029294ec / 029295fc / 0292970c(M, part), which point the resources at `part` directly (no copy):
    *(M+0x220)+0x30 (pokedata; the others likely +0x38 / 0x40 / 0x48), via 01189558 / 01eaa4e0 as for raids. Then
    029299b4(M, 0), the refresh (also called from 8c6da4, 29288bc, 292939c); it reads the pokedata fields
    (addRibbonType, enableRarePercentage at main_565.c's lifted lines).
  - 0292bd38 (callers 292af5c / 292b024 / 292b0ec, one a region) copies a zone file into 10 slots of {zoneid,
    lotvalue, poke1..5 + lot values}. **Open:** what zoneid indexes (outbreak_point_{main,su1,su2} entries? the
    byte DeliveryZoneID suggests an index), whether pokeN is a pokedata `id`, and where the outbreak save struct
    lives in memory (for a live export like raidgem.c's). Then the Outbreaks tab (PKHeX Encounters9.Outbreak).
  - Lookups: strings are file offset - 0x1000 (main.elf's first segment at 0x1000 -> 0); lifted code loads them as
    `X = 0x710<page>ull; X = X + 0x<off>ull`.
- Other route (agreed, not started): past / custom events from PKHeX.Core's lists served as deliveries from bcat.c
  (`build/bcat/delivery/raid/*_3_0_0`) or written as blocks; find the delivery schema (.bfbs near the raid tables?).
  Version: 174 event raid entries in PKHeX, 23 Scarlet-only, 23 Violet-only (Scarlet-only bosses need a delivery
  with a Violet rate or a Scarlet-rolling host). Not wanted: Nintendo's servers. Community archives of past events:
  ask first.

### F7: items (user-checked)

- The bag is the save's KItem block, live in memory: 3000 x 0x10 by item id {pocket, count, flags 1 new / 2 favourite
  / 4 obtained, 0}; pockets 0 medicine, 1 balls, 2 battle, 3 berries, 4 other, 5 TMs, 6 treasures, 7 picnic, 8 key, 9
  materials. Found by the Rotom Phone (1829) entry plus every held item in its own pocket. A direct write shows only
  after save + restart (the bag keeps lists built at load), hence the Lua route.
- A shop purchase writes through `main+1488d18(id, lo, hi)` -> `1488dd4`, from Lua binding `1488b90` =
  `cACA3F5A6.f445BD6CD` (luaL_Reg at main+4420320). Bag: `C14A8D35D72F56FE9.S90D73F1D715BCBF4()`;
  `:FB01A9B29CD4CC6CB(id, n)` add (clamped), `:FE027A297CCA14276(id, n)` remove, `:F13D72EF77582090D(id)` index,
  `b[6][i].num`. F7 has a Lua console.

### F8: Pokémon

- **Box writer** (user-checked): our own PK9 writer (`mods/mons/pk9.h`: layout, crypto, UTF-8 <-> UTF-16, egg names
  per language, ID32 <-> 6-digit ID No. + SID, shiny xor / TSV; checked with svedit `layout` / `check`), into the
  first empty slot of the live KBox block (990 x 0x158 encrypted; empty = encrypted blank), found by 960+ consecutive
  slots with valid checksums. Tables + trainer: svedit `monsdata` -> `build/mods/mons_species / moves / abilities /
  exp / balls / trainer / eggs.txt` (rerun after changing saves; the trainer comes from the save's own trainer data).
  Game-side box get `c1A1CBE3B.f6AA17B5F(box, slot)`, set `CEC4BF82A35D1E447.S0B3F69C4549A0284():
  F518978B87DAB37D6(box, slot, mon)` (party: `c1A1CBE3B.fFCE06E04():f36E12C71(pos, mon)`), mon `:f0BD5134F()` dev id,
  `:f5D94E897()` form. NX_MONS_TEST="<national>,<form>,<level>" (unattended; its one run logged nothing).
- Files: `mods/mons/mons.h` (tables, trainer, `box_snapshot`, `current_trainer`, `box_put`; namespace `mons`),
  `mons.cpp` (Pokemon / Wild spawns / Around you tabs), `egg.cpp` (Egg and Trainers tabs, `mons_egg_selftest`).
  Dropdowns filter as you type (`filtercombo.h`).
- **Gender** "Random" (Egg tab, box writer, wild spec): rolled on the species' ratio per mon (female when
  1 + r % 252 < ratio); the wild spec leaves the game's own roll.

### Eggs and trainer ids (PKHeX-legal offline, not yet tried in game)

- The egg: level 1, met level 1, obedience 1, egg location 30023 (picnic), met location 0, egg date today, no met
  date, nickname flag set, nickname = egg name written over the species name (the rest is the expected trash), egg
  bit, EVs 0, no item, level-1 moves with relearn moves the same, Tera one of the species' two types, OT friendship =
  hatch counter. Choices: IVs, nature, ability (hidden too), gender, ball (PKHeX's per-species mask; never Master /
  Cherish), Tera, shiny, cycles left (1 legal, 0 fails), custom OT, "Shiny for another trainer".
- PID fixed at creation; shininess is checked against whoever hatches it (a traded egg takes its hatcher as OT).
  Shiny: random low half, high = low ^ TID ^ SID ^ (0-15); not shiny: rerolled until xor >= 16 for the hatcher.
- A trade (PKHeX UpdateHandler): HT = receiver, met location 30002 (Link Trade), met date = trade date, egg location /
  date unchanged. On hatching the console sets OT to the hatcher, clears HT, and sets met location / date to the
  hatch. Unchecked: whether SV turns a traded egg's egg location into Link Trade on hatching.
- PKHeX 26.10.18 rules found: babies (Pichu, Happiny, Riolu) are Undiscovered yet hatch (no egg-group filter);
  `monsdata` writes 329 egg species / forms; a custom OT in another language than the save's may fail the trash
  check. Egg moves (inherited from parents) are not offered; PKHeX has SV's lists (`LearnSource9SV.GetEggMoves`).
- Trainers tab: "Read the boxes" lists OT, ID No. / SID, game, shiny (boxes only, not the party); per row "Hatcher" /
  "OT". Each other trainer found (OT of a non-egg mon, not ours) is kept in `build/mods/mons_trainers_seen.txt` (id32,
  gender, version, first seen, name): "Trainers seen before" table (Hatcher / OT / Forget) and a picker under "Shiny
  for another trainer".
- svedit on PKHeX 26 (.NET 11): `StatNature` -> `StatAlignment`, `EncryptedPartyData` -> `WriteEncryptedDataParty`,
  IVs via `GetIVs` / `SetIVs`, bag `.Pouches`. Save path `build\save\1-000000000000000100006d6b38726563-0\main`.

### Wild spawns

- Rewriting the encounter tables in memory (`world/data/encount/pokedata/pokedata{,_su1,_su2}/..._array.bin`: Paldea
  449, Kitakami 252, Blueberry 254) **changes nothing**: the spawner reads them into its own lists at save load.
  `main+10cd434` is not the spawner (makes ~26 mons at every load).
- Table schema (EncountPokeData): 0 devid, 1 sex, 2 formno, 3-4 min / max level, 5 lotvalue, 6-13 biome1-4 with
  lotvalues, 14 area, 15 locationName, 16-17 min / max height, 18 enabletable (land, up_water, underwater, air1,
  air2), 19 timetable, 20 flagName, 21 bandrate, 22 bandtype, 23 bandpoke, 24 bandSex, 25 bandFormno, 26
  outbreakLotvalue, 27 pokeVoiceClassification, 28 **versiontable** (sub-table: slot 0 Scarlet, 1 Violet), 29
  bringItem.
- Version exclusives (4.0.0): of 955 entries 889 both, 33 Scarlet-only (Paldea: Blaze Tauros, Larvitar, Pupitar,
  Drifloon, Drifblim, Stunky, Skuntank, Deino, Zweilous, Skrelp, Dragalge, Oranguru, Stonjourner, the 7 ancient
  Paradoxes; Kitakami: Gligar, Volbeat, Illumise, Cramorant; Blueberry: Alolan Vulpix, Plusle, Minun, Cranidos), 33
  Violet-only (the mirror: Aqua Tauros, Misdreavus, Gulpin, Bagon, Clauncher, Passimian, Eiscue, Dreepy, the Iron
  Paradoxes; Aipom, Morpeko; Alolan Sandshrew, Shieldon). Volbeat / Illumise / Plusle / Minun are in both at different
  spots. PKHeX doesn't split SV's wild slots by version.
- **The spawner is Lua**: chain `1245528 <- 1245224 <- Lua`; native `12451d0` = `c113335A8.f7AC1FE6B(spec)` (luaL_Reg
  at main+443a538, class registered at 140ce30). Script maker `C2B428A63695FD2C7.S988D7D0A4EEF693D(?, DevID, form,
  sex, level, ...)` (spec from `cC99399C3.f101D811F()`: devId f2247607A, form f81738FA1, sex f88C47BAC, level
  f02983AD3, `rarePercentage` -> shiny), called only by the wild manager `C4EE52E49562F8277` (F71A61170646F68AF,
  F61D1ABB7697071EB, F316E31E09886ADC9). F8 wraps it (original kept as `C.__nx_orig`): **user-checked** (Hisuian
  Arcanine in Paldea).
- **The rest of the spec: `violetrecomp/wild.c`.** Every mon param is built by constructor `debf7c(obj, spec)`
  (`PokemonParam::Create` debe4c, 24 callers, and inlined Create `1245474`). patches.txt `after wild_create` at
  debf88 (x1 spec) rewrites the spec on DevID + form match (shiny mode, IVs, EVs, nature, ability, gender, level);
  `after wild_created` at debff8 (x19 param) sets moves / PP and Tera at param+0x70. InitialSpec layout in wild.c
  (from SetupInitialSpec `160ffd0`, caller `160df74`). Mod calls the exe export `violet_wild_set`. `NX_WILD_LOG=1`
  logs every create with 5 return addresses. Any other mon of that species made while on (raids, trades) gets the
  spec too. All spawns shiny: `violet_all_shiny` (mode 3 rolled -> 2 always; log `wild: all shiny: first rolled
  create made shiny`). Unconfirmed: the wild spec on a caught mon (if the overworld model isn't shiny, look at
  `rarePercentage` `r0:f52C8AF50(...)` and the spec's `f82BDE968`).
- **"Around you"**: wild manager instance `C4EE52E49562F8277.SF3075AB31C9E8AF4` (W); update `F648B6B775BCC730B` is a
  state machine on W[17] (0 spawning: `FA9A2EC1E99B58D55`; 1 waiting for field objects). Player position
  `K.SBF5E0EFFB821427F:f7360ED03()`; spawn point `cFB7F03A4.f42A4ADE3` / `f35A39D75(x, y, z, 0, radius, 0)`; info
  `W:F654580D678C85C0E(pt)` (pos, levels, biome, area, enable_table); group into W[23] by
  `W:F61D1ABB7697071EB(info)`, placed by `W:F9B7FBB66FA56765E`. "Spawn on me" moves info.pos to the player (offset
  slider), only while W[17] == 0. Encounter table W[26] (class `CE1BA7320ED1C2331`): `F2F6C7BBE115AFBCB(info, ctx,
  flag, list, weights)` fills entries + weights, returns the total; `FCEEA7F4134055567` picks (W[26] holds the
  picker itself, no metatable: restore it after wrapping, or the next spawn fails); per-entry rules in
  `F67A9C2A9373A0AC6` (CheckArea, CheckLocation, a ground ray, four more); ctx = `W[28]:FDCCDAF4C1D58AECA(pos)`,
  ctx[4] / ctx[5] force a DevID / form. Entry getters f2BF366A2 DevID, fE15E9D0A form, f5B8A3FE2 sex, f4646F546 /
  f61FD9277 levels. A weights override (K.__nx_w) is lost when the table is remade at save load. A row spawn finds
  its entry again by DevID / form at spawn time (returning a kept entry crashed the game).
- In Union Circle "Spawn on me" acts only on the simulating instance; spawned groups become network objects
  (netOwnerIndex / netSerialId, c2CA78EB7.*); F47FB4E501540CBF7 keeps spawns off other players' mons. Untested.
- **No contact battles**: behaviour-tree nodes SetEnableEncount(Blackboard) (type names main+2995db0 / 29954e0,
  vtables 4474d48 / 4474a00) call `main+1d0e5d4(component, enable)`; wild.c `h_encount_enable` (patches.txt
  main+1d0e5e4) zeroes it while on (export `violet_no_battles`). Unconfirmed; fallback: event flag
  `FEVT_COMMON_0140_NO_ENCOUNT` (`C10578806AC30DCA3.SBA6FF574C1C9AA09.h[name] = v` + native `c37452BA0.f4BEF3427(name,
  v)`).
- **Spawn-pool rules** (user: "mess with the normal spawning rules"; ideas liked: version exclusives in both, any time
  of day, ignore story flags, equal weights, every species everywhere): wrap the Lua filter (`F2F6C7BBE115AFBCB` /
  `F67A9C2A9373A0AC6`), or patch the tables and reload the save, or patch the file as it's read. Density:
  `encounterCountMax`, `encounterCheckInterval`, `encounterUnregisterFrameCount` (loader main+1513f24; refs 1514048 /
  151415c / 15142cc); Lua `SpawnLimit` / `SpawnTimer` / `radiusSpawnArea` near main.blua line 259k. Mass outbreaks
  are separate (BCAT `encountOutbreak/*_3_0_0`, save `KBCATOutbreak*`).
- Leads for making mons from script: NPC trade `P2882` `FA1D3529002C3A75C(tray, pos, receivePoke)` makes the mon with
  `CF7739C6C4D25CC45.S470187B7637D37DC(spec)` (PokeData from eventTradeList) and stores it with
  `CD081B219DEE96EEF.S644D748FB7CE5439`; static mons: `cA20E9FCA.f80B8DD7C` (path P14484); a wild battle from script:
  `EncountWild` users in `CA5A5606298DDCB29`.

### F9 / F10: weather (built, not yet tried in game)

`mods/weather/weather.cpp` -> `violet_weather.dll`: F9 toggles rain, F10 window sets any type, transition time, or
"Game's own". Through run_script only.
- Timeline event "Title/Weather/WeatherChange/WeatherChange" set up by 1521448: start callback 202f928 reads
  `WeatherType` (1 when absent), calls `CFunc_Tml_SetEnvChangeSpan(seconds)` (202fa30), then 202fa74 -> 202fb44 = the
  static call of `main.tml.TimeLineCommand` method hash `A3EC462E45551FD6` with the type. "WeatherReset": 202ff54,
  then `CFunc_Tml_ResetWeather`, at its end 20301c0 `CFunc_Tml_ResetChangeSpan`.
- Lua class `CA71F0CDF966F3175`; the mod tries `S<hash>` then `F<hash>`: set `A3EC462E45551FD6(type)`, reset
  `ECAF51D6E7046362()`, span `8614855A6EF844D8(s)`, span reset `58CE0E7D1599C50E()` (~1 s after).
- **Type numbers are a guess** (AreaWeather9 order): 1 normal, 2 overcast, 3 rain, 4 thunderstorm, 5 intense sun, 6
  snow, 7 snowstorm, 8 sandstorm, 9 heavy fog; a "Raw type number" box to check. Real ones: the script's bytecode, or
  1d56928 (ConditionCurrentWeather). Open: does the game's schedule override a forced type; does the static take
  `self`.

### Save edits (svedit)

`violetrecomp/tools/svedit` (PKHeX.Core; game closed, back up `build/save` first): ride upgrades (dash / swim / high
jump / glide / climb / fly + flight controls; user-checked) and raid levels 3-6; `addmon` (full IVs, the save's OT);
`dump` / `copy` blocks; `layout` / `check` for our PK9s; `find` flags; `monsdata`. PKHeX's `KUnlocked*` = the game's
`FSYS_*_ENABLE` / `FSYS_RAID_DIFFICTLTY*_RELEASE` (key = (uint)FNV-1a-64 of the name). In-memory flags not found by
hash: flags stay closed-game edits. Fly points (`FSYS_YMAP_FLY_*`) not done; "story completed" not feasible. Old
saves: `build/saves/save-backup-2026-11-05/`.

## Reference projects compared

- **Pokémon Legends Z-A recomp** (`../ref/PokeZ-ARecomp-Online`, 2026-11-10): taken: the CPU-written texture fixes,
  passes kept over re-set targets, QPC guest waits, pipeline waits on a condition variable. Ideas in the ranked
  lists above; also RomFS mods (Z-A fs.c 328-813, trpfd unlisting), safe saves, crash explanations.
- **TotK recomp** (`../ref/totk-pc-recomp`, 2026-11-11): taken: kspin (off by default), pipeline memo, no power
  throttling. Its GPU design equals ours (GENERAL layouts, LOAD / STORE, full barriers).
- **Second sweep** (TotK, Z-A, ssbu-recomp; read-only): we're already ahead on registers in locals with lazy flags,
  flat dispatch + musttail, bindless pools + push descriptors, dynamic state, pipeline binaries / list prebuild,
  write-watch texture checks. Checked in our code: no PGO; `NX_ASYNC_SHADERS` off outside netplay builds;
  `rs_scale` u32 from 1; romfs.c opens a synchronous handle; `crc_bits` bit-serial; STL SEQ_CST; no `/GS-`.
- **Savage Wind** (BotW, `../ref/SavageWindRecomp`): feedback-loop handling, async submission, one LP per core.
