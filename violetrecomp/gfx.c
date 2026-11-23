/* Graphics quality patches (patches.txt "if gfx" / "if shadow" / "if nodof": Fl4sh's Violet 4.0.0 "Improved Graphic
 * Quality" and "2K Shadows" pchtxt mods, as a map), each with a switch read once before the game starts:
 *   NX_QUALITY=0    the two quality values as the game has them ("if gfx")
 *   NX_SHADOWS2K=0  1024x1024 shadow maps as on the Switch. Default ("if shadow"): 2048x2048, and the GPU memory
 *                   pool they come from at the game's own 320 MB: the Switch setup (main+146e5f0) cuts the shadows
 *                   from 2048 to 1024 and that pool from 320 to 268 MB; with only the shadows back, opening the map
 *                   ran the pool out (2026-11-23: a 56 KB allocation, main+c28198 -> c28244 returned null, c281d0
 *                   read through it; h_heap_oom logs such failures)
 *   NX_DOF=1        keep the depth-of-field blur ("if nodof")
 *   NX_GFX=0        all of them off: the game's own settings */
#include "hos.h"
#include <stdlib.h>
#include <windows.h>

volatile u8 nx_flag_gfx = 1;
volatile u8 nx_flag_shadow = 1;
volatile u8 nx_flag_nodof = 1;

static bool env_is(const char *name, char v) {
    const char *e = getenv(name);
    return e && *e == v;
}

__attribute__((constructor)) static void gfx_init(void) {
    if (env_is("NX_QUALITY", '0')) nx_flag_gfx = 0;
    if (env_is("NX_SHADOWS2K", '0')) nx_flag_shadow = 0;
    if (env_is("NX_DOF", '1')) nx_flag_nodof = 0;
    if (env_is("NX_GFX", '0')) nx_flag_gfx = nx_flag_shadow = nx_flag_nodof = 0;
}

/* main+cd0e5c, the render thread's frame (main+ccfe14), once the frame's command buffers are recorded (by it and
 * the job threads, joined at cd0e0c). From here it waits for the last frame's two syncs (cd0e94, cd0eb0: the
 * acquired texture's and the queue fence taken at cd1c54), then submits the list at [x28 + [x28+d0] * 0x20 + 0x90]
 * (cd0edc -> cd1a74: nothing if the byte at x28+0x10 is 0). The runtime translates that list now, while the GPU
 * still draws the last frame (nvn_submit_ahead); the game's own submit of the same buffers then does nothing. */
void h_submit_ahead(Ctx *c) {
    u64 r = c->x[28];
    if (!*(u8 *)(uintptr_t)(r + 0x10)) return;
    u64 list = *(u64 *)(uintptr_t)(r + ((u64)*(u32 *)(uintptr_t)(r + 0xd0) << 5) + 0x90);
    u64 lo = *(u64 *)(uintptr_t)(list + 8), hi = *(u64 *)(uintptr_t)(list + 0x10);
    if (hi > lo) nvn_submit_ahead((u32)((hi - lo) >> 3), (const u64 *)(uintptr_t)lo);
}

/* main+c28230, reached only when main+c28198's allocator call (main+c28244) returned 0, out of memory (the caller
 * then reads through it at c281d0): x19 the size asked for, x21 the heap. Logs the first few failures with
 * the heap's first words and the guest frame chain, to tell which heap fills up. */
static bool readable(u64 a, u64 n) {
    MEMORY_BASIC_INFORMATION mi;
    if (!a || !VirtualQuery((void *)(uintptr_t)a, &mi, sizeof mi) || mi.State != MEM_COMMIT) return false;
    if (mi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return false;
    return a + n <= (u64)(uintptr_t)mi.BaseAddress + mi.RegionSize;
}

void h_heap_oom(Ctx *c) {
    static volatile LONG n;
    if (InterlockedIncrement(&n) > 4) return;
    u64 heap = c->x[21];
    hlog(0, "heap: out of memory: %llu bytes (%.1f MB) from heap %llx, thread %s", c->x[19], c->x[19] / 1048576.0,
         heap, cur_thread ? cur_thread->name : "?");
    for (int i = 0; i < 48 && readable(heap + i * 8, 8); i++) {
        u64 v = *(u64 *)(uintptr_t)(heap + i * 8);
        char str[48] = "";
        if (v >= 0x7100000000ull && v < 0x7110000000ull && readable(v, 32)) {      /* a name in the image? */
            const char *t = (const char *)(uintptr_t)v;
            int k = 0;
            while (k < 40 && t[k] >= 0x20 && t[k] < 0x7f) k++;
            if (k >= 4 && !t[k]) snprintf(str, sizeof str, "  \"%.40s\"", t);
        }
        hlog(0, "heap:   +%03x %016llx  (%lld MB)%s", i * 8, v, (long long)(v >> 20), str);
    }
    u64 fp = c->x[29];
    hlog(0, "heap:   lr %llx", c->x[30]);
    for (int i = 0; i < 24 && fp && !(fp & 7) && readable(fp, 16); i++) {
        u64 lr = *(u64 *)(uintptr_t)(fp + 8);
        hlog(0, "heap:   from %llx (main+%llx)", lr, lr - 0x7100000000ull);
        fp = *(u64 *)(uintptr_t)fp;
    }
}
