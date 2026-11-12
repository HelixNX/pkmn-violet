/* Mods' host->run_script (nxmod.h) for Violet: source chunks run in the game's own Lua 5.4 VM.
 *
 * The game logic is Haxe compiled to Lua 5.4 (romfs script/lua/bin/release/main/main.blua, names obfuscated:
 * classes C<hash> are globals, statics S<hash>, methods F<hash>). Native code enters the VM through lua_pcallk
 * (~300 callers); patches.txt hooks its entry ("after" its first instruction, sub sp), where the caller's lua_State
 * is in x0 and the VM is between calls: a safe point to load and run a chunk of our own on top of the caller's
 * stack, with everything put back after (the stack top, every register).
 *
 * Main's Lua C API (4.0.0), found from luaL_loadfilex ("=stdin"):
 *   lua_load 232e0 (L, reader, data, chunkname, mode), getS 24b90 (lauxlib's string
 *   reader: data = {const char *s; size_t size}), lua_gettop b5d980, lua_settop b5db00, lua_tolstring b646d0,
 *   lua_pcallk b5ddd0 (L, nargs, nresults, errfunc, ctx, k).
 *
 * A chunk that returns NXMOD_SCRIPT_AGAIN ("\1again", e.g. a global it needs is not there: another VM, or the save
 * not loaded yet) is run again at a later safe point, up to AGAIN_MAX times, then reported as failed. */
#include "hos.h"
#include "mods.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#define MAIN 0x7100000000ull
#define LUA_LOAD (MAIN + 0x232e0)
#define LUA_GETS (MAIN + 0x24b90)
#define LUA_GETTOP (MAIN + 0xb5d980)
#define LUA_SETTOP (MAIN + 0xb5db00)
#define LUA_TOLSTRING (MAIN + 0xb646d0)
#define LUA_PCALLK (MAIN + 0xb5ddd0)

#define AGAIN NXMOD_SCRIPT_AGAIN
#define AGAIN_MAX 2000
#define MAX_JOBS 64

typedef struct Job { char *text; NxScriptDone done; void *ud; int tries; } Job;

static SRWLOCK lock = SRWLOCK_INIT;
static Job jobs[MAX_JOBS];
static int njobs;
static volatile LONG pending;                   /* njobs, read without the lock by the hook */
static __declspec(thread) int busy;             /* our own lua_pcallk enters the hook too */

static bool queue(const char *text, NxScriptDone done, void *ud) {
    char *t = _strdup(text);
    if (!t) return false;
    AcquireSRWLockExclusive(&lock);
    bool ok = njobs < MAX_JOBS;
    if (ok) { jobs[njobs++] = (Job){t, done, ud, 0}; pending = njobs; }
    ReleaseSRWLockExclusive(&lock);
    if (!ok) { free(t); hlog(0, "lua: %d chunks already queued, one dropped", MAX_JOBS); }
    return ok;
}

__attribute__((constructor)) static void lua_bridge_init(void) { mods_set_script_runner(queue); }

static u64 gcall(Ctx *c, u64 fn, u64 a0, u64 a1, u64 a2, u64 a3, u64 a4, u64 a5) {
    c->x[0] = a0; c->x[1] = a1; c->x[2] = a2; c->x[3] = a3; c->x[4] = a4; c->x[5] = a5;
    guest_call(c, fn);
    return c->x[0];
}

/* one chunk on L: true when it ran to the end (ok: without an error); out gets its first result or the error */
static bool run_one(Ctx *c, u64 L, const char *text, char *out, size_t outsz) {
    struct { const char *s; size_t size; } ls = {text, strlen(text)};
    static const char name[] = "=mod";
    s32 top = (s32)gcall(c, LUA_GETTOP, L, 0, 0, 0, 0, 0);
    s32 st = (s32)gcall(c, LUA_LOAD, L, LUA_GETS, (u64)(uintptr_t)&ls, (u64)(uintptr_t)name, 0, 0);
    if (st == 0) st = (s32)gcall(c, LUA_PCALLK, L, 0, 1, 0, 0, 0);
    u64 len = 0;
    u64 s = gcall(c, LUA_TOLSTRING, L, (u64)-1, (u64)(uintptr_t)&len, 0, 0, 0);
    if (s) {
        size_t n = len < outsz - 1 ? (size_t)len : outsz - 1;
        memcpy(out, (const void *)(uintptr_t)s, n);
        out[n] = 0;
    } else snprintf(out, outsz, st ? "(error without a message)" : "(no text result)");
    gcall(c, LUA_SETTOP, L, (u64)(s64)top, 0, 0, 0, 0);
    return st == 0;
}

/* main+b5ddd0, after sub sp, #0x40 */
void h_lua_pcall_entry(Ctx *c) {
    if (!pending || busy) return;
    Job mine[MAX_JOBS];
    AcquireSRWLockExclusive(&lock);
    int n = njobs;
    memcpy(mine, jobs, n * sizeof *jobs);
    njobs = 0;
    pending = 0;
    ReleaseSRWLockExclusive(&lock);

    busy = 1;
    Ctx saved = *c;
    u64 L = c->x[0];
    static bool logged;
    if (!logged) {
        logged = true;
        hlog(1, "lua: first chunk on thread %s, lua_State %llx (global state %llx)",
             cur_thread ? cur_thread->name : "?", L, *(u64 *)(uintptr_t)(L + 0x18));
    }
    char out[4096];
    for (int i = 0; i < n; i++) {
        Job *j = &mine[i];
        bool ok = run_one(c, L, j->text, out, sizeof out);
        *c = saved;
        if (ok && !strcmp(out, AGAIN)) {
            if (++j->tries < AGAIN_MAX) {
                AcquireSRWLockExclusive(&lock);
                if (njobs < MAX_JOBS) { jobs[njobs++] = *j; pending = njobs; j = NULL; }
                ReleaseSRWLockExclusive(&lock);
                if (!j) continue;
            }
            ok = false;
            snprintf(out, sizeof out, "gave up: the chunk asked to be run again %d times", j->tries);
        }
        if (!ok || strncmp(j->text, "--quiet", 7))  /* a chunk that polls (the raids mod's position) says so */
            hlog(ok ? 1 : 0, "lua: %s: %s", ok ? "ok" : "error", out);
        if (j->done) j->done(j->ud, ok, out);
        free(j->text);
    }
    busy = 0;
}

/* main+b5d8fc (mov x7, x0): the Haxe-Lua invoker (gflib3 haxe_lua/invoker.h, CallImpl) after a failed pcall of a
 * script method, x0 the error text, x23 / [x29-8] the class and method names it looked up (obfuscated). The game
 * aborts next (svcBreak with no text in our log), so say here what the script objected to. */
void h_script_error(Ctx *c) {
    const char *msg = (const char *)(uintptr_t)c->x[0];
    const char *cls = (const char *)(uintptr_t)c->x[23];
    const char *fn = (const char *)(uintptr_t)*(u64 *)(uintptr_t)(c->x[29] - 8);
    hlog(0, "lua: the game's own script call failed (it aborts now): %s.%s: %s", cls ? cls : "(global)", fn ? fn : "?",
         msg ? msg : "(no message)");
}

/* Lua's stop-the-world collections, logged (patches.txt): main held its core for 40-150 ms a few times a minute
 * with fullgen (main+2b840) on top of the profile. Lua 5.4.4+ layout: global_State totalbytes +10, GCdebt +18,
 * GCestimate +20, gckind +66 (1 generational), genminormul +68, genmajormul +69. x30 says who asked:
 * main+2afd0 is luaC_step (the heap grew past the major limit), main+2b6c0 luaC_fullgc (logged by its own hook) */
static double gc_ms(void) {
    LARGE_INTEGER f, t;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&t);
    return (double)t.QuadPart * 1000.0 / (double)f.QuadPart;
}

void h_gc_fullgen(Ctx *c) {
    static double last;
    const u8 *g = (const u8 *)(uintptr_t)c->x[1];
    double now = gc_ms();
    hlog(1, "lua: full collection (generational) from main+%llx, %.1f s after the last: heap %.1f MB, estimate "
            "%.1f MB, minor mul %u, major mul %u", c->x[30] - MAIN, last ? (now - last) / 1000 : 0,
         (double)(*(s64 *)(g + 0x10) + *(s64 *)(g + 0x18)) / 1048576, (double)*(u64 *)(g + 0x20) / 1048576, g[0x68],
         g[0x69] * 4);
    last = now;
}

void h_gc_fullgc(Ctx *c) {
    const u8 *g = (const u8 *)(uintptr_t)*(u64 *)(uintptr_t)(c->x[0] + 0x18);
    /* x29 is still the caller's frame: its own return address is at +8 (lua_gc's caller, when it is lua_gc) */
    hlog(1, "lua: luaC_fullgc(emergency %llu) from main+%llx (called from main+%llx), kind %u, heap %.1f MB", c->x[1],
         c->x[30] - MAIN, *(u64 *)(uintptr_t)(c->x[29] + 8) - MAIN, g[0x66],
         (double)(*(s64 *)(g + 0x10) + *(s64 *)(g + 0x18)) / 1048576);
}

/* Lua's collector, paced for frame rates above 30 (NX_LUA_GC=0: the game's own).
 *
 * The game keeps the collector stopped and drives it once a frame (main+c8c36c): generational mode, one young
 * collection per frame (lua_gc(L, LUA_GCSTEP, 0)), a full one after 300 s without one. What survives two young
 * collections is old and stays until a full collection, and at 60-120 fps that is anything alive for 17-33 ms
 * (67 ms at the game's 30): the heap went from ~57 MB to its allocator's limit (~70 MB) every 45-60 s, and the
 * emergency collection there stopped main for 60-150 ms. The routine has an incremental branch too (kind != 1:
 * LUA_GCSTEP with 3000 KB), so the scripts run under either mode.
 *
 * Here: incremental mode, a step a frame of 1 KB over the debt, so the work follows what the frame allocated
 * (step multiplier 12: Lua does multiplier / 16 units of work per byte allocated, so the ~3.5 M units of marking
 * a 57 MB heap spread over ~5 MB of allocation, well inside the 13 MB of room; pause 100: the next cycle starts
 * at once), and no 300 s full collection. A cycle that falls behind still ends in the
 * allocator's emergency collection, as before. Every 3600 steps a line says what it cost.
 *
 * The game also asks for full collections itself: main+25ff7d0's lua_gc(L, LUA_GCCOLLECT), reached through the
 * function pointer at [main+46d3928] from 14 places that let go of script objects (c50e14, c31b54, ff172c ...).
 * In the field that was one every 4-12 s, each 50-70 ms of main holding its core (a 57 MB heap marked in one go).
 * Here a request only hurries the paced collector: the steps grow to GC_HURRY_KB a frame until two cycles have
 * ended (the one under way may already have marked what was just let go; the next one frees it), about half a
 * second instead of a stall. NX_LUA_GC=2 keeps the paced steps and the game's full collections */
#define LUA_GC (MAIN + 0xc8c520)                /* lua_gc(L, what, ...) */
#define GC_COLLECT 2
#define GC_STEP 5
#define GC_INC 11
#define GC_HURRY_KB 200                         /* ~2 ms a step (0.01 ms a KB measured) */
#define GCS_PAUSE 8                             /* gcstate between cycles */
static int gc_mode = 1;

__attribute__((constructor)) static void gc_init(void) {
    const char *e = getenv("NX_LUA_GC");
    if (e) gc_mode = atoi(e);
}

static double gc_heap(const u8 *g) { return (double)(*(s64 *)(g + 0x10) + *(s64 *)(g + 0x18)) / 1048576; }

/* per collector (the game has more than one VM); the step being timed is the calling thread's */
typedef struct GcStat { const u8 *g; double after, alloc, ms, longest, lo, hi, since; u32 n; } GcStat;
static GcStat gc_stat[4];
static volatile LONG gc_hurry[4];               /* cycles still to finish at the hurried pace */
static volatile LONG gc_asked[4];               /* the game's requests since the last line */
static __declspec(thread) GcStat *gc_cur;
static __declspec(thread) double gc_t0, gc_h0;

/* before the game's lua_gc(L, LUA_GCSTEP, x2) (the lifter patches no branches: hooks sit on the instructions
 * around the calls and change their arguments) */
void h_gc_step(Ctx *c) {
    u64 L = c->x[0];
    const u8 *g = (const u8 *)(uintptr_t)*(u64 *)(uintptr_t)(L + 0x18);
    int k = 0;
    while (k < 3 && gc_stat[k].g && gc_stat[k].g != g) k++;
    GcStat *st = &gc_stat[k];
    if (st->g != g) { memset(st, 0, sizeof *st); st->g = g; st->since = gc_ms(); }
    static u8 seen[4] = {9, 9, 9, 9};
    if (seen[k] != g[0x66]) {
        hlog(1, "lua: collector %p: kind %u at the game's step (was %u), step data %llu, state %u, heap %.1f MB",
             (void *)g, g[0x66], seen[k], c->x[2], g[0x65], gc_heap(g));
        seen[k] = g[0x66];
    }
    if (gc_mode >= 1) {
        /* generational as Lua declares it: that kind, or incremental after a major collection that freed too
         * little (lastatomic, +28), where every step is a whole mark until one frees enough */
        if (g[0x66] == 1 || *(u64 *)(g + 0x28)) {
            hlog(1, "lua: collector %p set to incremental (was kind %u, last atomic %llu, heap %.1f MB)", (void *)g,
                 g[0x66], *(u64 *)(g + 0x28), gc_heap(g));
            gcall(c, LUA_GC, L, GC_INC, 100, 12, 0, 0);
        }
        c->x[2] = gc_hurry[k] > 0 ? GC_HURRY_KB : 1;
    }
    c->x[0] = L;
    c->x[1] = GC_STEP;
    gc_cur = st;
    gc_h0 = gc_heap(g);
    gc_t0 = gc_ms();
}

/* after that call */
void h_gc_stepped(Ctx *c) {
    (void)c;
    GcStat *st = gc_cur;
    if (!st) return;
    gc_cur = NULL;
    const u8 *g = st->g;
    double ms = gc_ms() - gc_t0, h0 = gc_h0, h1 = gc_heap(g);
    if (st->n && h0 > st->after) st->alloc += h0 - st->after;
    st->after = h1;
    st->ms += ms;
    if (ms > st->longest) st->longest = ms;
    if (!st->n || h1 < st->lo) st->lo = h1;
    if (h0 > st->hi) st->hi = h0;
    LONG k = (LONG)(st - gc_stat);
    if (gc_hurry[k] > 0 && g[0x65] == GCS_PAUSE) InterlockedDecrement(&gc_hurry[k]);
    if (++st->n == 3600) {
        double s = (gc_ms() - st->since) / 1000;
        hlog(1, "lua: collector %p (%s): %.2f MB/s allocated, steps %.3f ms average, longest %.1f ms, heap %.1f-%.1f MB "
                "over %.0f s, %ld full collections asked", (void *)g, g[0x66] == 1 ? "generational" : "incremental",
             st->alloc / s, st->ms / st->n, st->longest, st->lo, st->hi, s, InterlockedExchange(&gc_asked[k], 0));
        st->n = 0; st->alloc = st->ms = st->longest = st->hi = 0; st->since = gc_ms();
    }
}

/* before the game's lua_gc(L, LUA_GCCOLLECT), its full collection after 300 s without one: the cycles above do
 * that work, so it becomes LUA_GCCOUNT, which changes nothing */
void h_gc_collect(Ctx *c) {
    if (gc_mode == 1) c->x[1] = 3;
}

/* before the game's own request for a full collection (main+25ff808 / 25ff838: mov w1, #LUA_GCCOLLECT, x0 = L):
 * LUA_GCCOUNT instead, and the paced collector hurries. A VM the per-frame routine doesn't step keeps the request */
void h_gc_request(Ctx *c) {
    if (gc_mode != 1) return;
    const u8 *g = (const u8 *)(uintptr_t)*(u64 *)(uintptr_t)(c->x[0] + 0x18);
    for (int k = 0; k < 4; k++)
        if (gc_stat[k].g == g) {
            InterlockedExchange(&gc_hurry[k], 2);
            InterlockedIncrement(&gc_asked[k]);
            c->x[1] = 3;
            return;
        }
}
