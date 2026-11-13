/* Graphics quality patches (patches.txt "if gfx" / "if shadow" / "if nodof": Fl4sh's Violet 4.0.0 "Improved Graphic
 * Quality" and "2K Shadows" pchtxt mods, as a map), each with a switch read once before the game starts:
 *   NX_QUALITY=0    the two quality values as the game has them ("if gfx")
 *   NX_SHADOWS2K=1  2048x2048 shadow maps ("if shadow"): OFF by default, opening the map crashes with them
 *                   (null read at main+c281d0, 2026-11-05; not looked into)
 *   NX_DOF=1        keep the depth-of-field blur ("if nodof")
 *   NX_GFX=0        all of them off: the game's own settings */
#include "hos.h"
#include <stdlib.h>

volatile u8 nx_flag_gfx = 1;
volatile u8 nx_flag_shadow = 0;
volatile u8 nx_flag_nodof = 1;

static bool env_is(const char *name, char v) {
    const char *e = getenv(name);
    return e && *e == v;
}

__attribute__((constructor)) static void gfx_init(void) {
    if (env_is("NX_QUALITY", '0')) nx_flag_gfx = 0;
    if (env_is("NX_SHADOWS2K", '1')) nx_flag_shadow = 1;
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
