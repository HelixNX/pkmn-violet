/* Tera raid crystals rebuilt on demand (the raids mod's F6, through the export violet_raid_refresh): seeds written
 * into the dens otherwise showed only after a restart.
 *
 * The crystals belong to a manager object M (4.0.0; Ghidra on main, 2026-11-07 and 2026-11-09):
 *   M + 0x2d       300 x 6 bytes: the own dens as sent to the other players of a session ({-1, 0}: none); not a cache
 *   M + 0x738..750 the area key of crystal group 0..3: 0 the player's own dens, 1-3 the other players' (29337b4
 *                  registers a player's list into a group; 29341d8(M, key) drops a group)
 *   M + 0x7d8      a generation counter, part of the crystal objects' names
 *   M + 0x7e8      23 vectors {begin, end, ...} (0x38 bytes each) of 0xa0-byte den entries: +0x10 group, +0x18 the
 *                  spawn point's object, +0x20 the den's position (float x, y, z; seen in a live game 2026-11-09),
 *                  +0x44 area (the vector's index too), +0x48 lottery group, +0x4c point, +0x64 region (0 Paldea,
 *                  1 Kitakami, 2 Blueberry), +0x70 the crystal object (+0xd1: remove it), +0x98 placed
 *   M + 0x1661     a reroll is due (e23ec8 then rerolls even without force)
 *   M + 0x1662     "reflect" flag: the per-frame update (2933374(M, dt)) posts "RaidGemForceReflection" (FNV-1a
 *                  event on M+8, 01ea821c) at its end and clears it
 * entry+0x98 = 0 with crystal+0xd1 = 1 (29341d8, 29343a0, 01ea82c0) only REMOVES a den: the update drops the crystal
 * once it is gone and then erases the entry. Nothing puts it back (the first version of this file did just that
 * for every den: all crystals vanished for good). The entries are made by e23ec8(M, force): without force, on a
 * save that has dens, it reads the 3 x 100 dens of the save (area, group, point, seed, content) and adds one entry
 * each through e26fec, the raid picked from the seed; with force (or M+0x1661) it rerolls the dens first.
 * A refresh here: remove the own entries (group 0) the game's way, wait until the update has erased them, call
 * e23ec8(M, 0) and set the reflect flag. Hooked after `mov x19, x0` in the update (main+2933398), on the game's
 * thread.
 *
 * Event deliveries (the events mod, exports violet_raid_delivery / violet_raid_deliver; NOTES "Event raids"): the raid
 * save object (the save list at *(*(main+46d4ca0)) +0x1c0..+0x1c8, the entry e280dc(obj + 0x28) says is the raid
 * one) holds the delivered files at +0xe308 enemies, +0xe340 fixed rewards, +0xe378 lottery rewards, +0xe3b0
 * priority, +0xe300 the identifier. A delivery goes in through the setters BcatClient uses, in its order, on this
 * thread: each copies the file into the save and points the manager's resource at it again. A new priority
 * versionNo makes the game reroll the dens with the new events by itself; otherwise the dens are rebuilt from the
 * save (the refresh above) so that event dens pick from the new entries. */
#include "hos.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#define MAIN 0x7100000000ull
#define GEM_PLACE_ALL (MAIN + 0xe23ec8)  /* (M, force reroll) */
#define GEM_CACHE 0x2d
#define GEM_GROUPS 0x738
#define GEM_VECS 0x7e8
#define GEM_VECS_END 0xcf0
#define GEM_REROLL_DUE 0x1661
#define GEM_REFLECT 0x1662
#define ENTRY_SIZE 0xa0
#define WAIT_MAX 300                    /* updates to wait for the old crystals to go */
#define SAVE_LIST_PP (MAIN + 0x46d4ca0) /* -> the save list singleton's slot */
#define SAVE_IS_RAID (MAIN + 0xe280dc)  /* (obj + 0x28): the raid save object? */
#define SAVE_IDENT 0xe300

static volatile LONG refresh_req;
static volatile LONG64 manager;
static volatile LONG logged;
static int waiting;                     /* game thread: updates since the own entries were marked; 0 = idle */

/* the delivered files: enemies, fixed rewards, lottery rewards, priority */
enum { DELIV_PARTS = 4 };
static const u32 deliv_off[DELIV_PARTS] = {0xe308, 0xe340, 0xe378, 0xe3b0};
static const u32 deliv_size[DELIV_PARTS] = {30000, 27456, 53464, 88};
static const u64 deliv_set[DELIV_PARTS] = {MAIN + 0x1d93018, MAIN + 0x1d933a0, MAIN + 0x1d931dc, MAIN + 0x1d92c24};
static volatile LONG64 raid_save;       /* the raid save object (0: not found yet) */
static SRWLOCK deliv_lock = SRWLOCK_INIT;
static u8 *deliv_new[DELIV_PARTS];      /* queued parts (NULL: keep) */
static volatile LONG deliv_req, deliv_done;

/* a delivered file into out (which 0-3 as above, 4: the 4-byte identifier); returns its size, 0 when the raid save
 * isn't known yet or out is too small */
__declspec(dllexport) int violet_raid_delivery(int which, void *out, int max) {
    u8 *s = (u8 *)(uintptr_t)raid_save;
    if (!s || which < 0 || which > DELIV_PARTS) return 0;
    if (which == DELIV_PARTS) {
        if (max < 4) return 0;
        memcpy(out, s + SAVE_IDENT, 4);
        return 4;
    }
    u8 *buf = *(u8 **)(s + deliv_off[which]);
    if (!buf || max < (int)deliv_size[which]) return 0;
    AcquireSRWLockShared(&deliv_lock);
    memcpy(out, buf, deliv_size[which]);
    ReleaseSRWLockShared(&deliv_lock);
    return (int)deliv_size[which];
}

/* queue a delivery (parts[i] NULL: keep that file; each as long as the file: 30000, 27456, 53464, 88 bytes); applied
 * at the crystal manager's next update. Returns how many deliveries were applied so far (parts NULL: only that),
 * -1 when the raid save isn't known yet (not in the field). */
__declspec(dllexport) int violet_raid_deliver(const void *const *parts) {
    if (!parts) return (int)deliv_done;
    if (!raid_save || !manager) return -1;
    AcquireSRWLockExclusive(&deliv_lock);
    for (int i = 0; i < DELIV_PARTS; i++) {
        free(deliv_new[i]);
        deliv_new[i] = NULL;
        if (parts[i] && (deliv_new[i] = (u8 *)malloc(deliv_size[i]))) memcpy(deliv_new[i], parts[i], deliv_size[i]);
    }
    ReleaseSRWLockExclusive(&deliv_lock);
    InterlockedExchange(&deliv_req, 1);
    return (int)deliv_done;
}

static u64 gcall2(Ctx *c, u64 fn, u64 a0, u64 a1) {
    Ctx saved = *c;
    c->x[0] = a0;
    c->x[1] = a1;
    guest_call(c, fn);
    u64 r = c->x[0];
    *c = saved;
    return r;
}

/* game thread: the raid save object from the save list (looked for until found, now and then) */
static void find_raid_save(Ctx *c) {
    static u32 tries;
    if (raid_save || tries++ % 60) return;
    u64 *slot = *(u64 **)(uintptr_t)SAVE_LIST_PP;
    u8 *list = slot ? (u8 *)(uintptr_t)*slot : NULL;
    if (!list) return;
    for (u64 *p = *(u64 **)(list + 0x1c0); p && p < *(u64 **)(list + 0x1c8); p++) {
        if (!*p || !(gcall2(c, SAVE_IS_RAID, *p + 0x28, 0) & 1)) continue;
        u8 *s = (u8 *)(uintptr_t)*p;
        InterlockedExchange64(&raid_save, (LONG64)(uintptr_t)s);
        u32 ident;
        memcpy(&ident, s + SAVE_IDENT, 4);
        hlog(0, "raidgem: raid save at %llx, delivery identifier %u, enemies at %llx", (unsigned long long)*p, ident,
             (unsigned long long)*(u64 *)(s + deliv_off[0]));
        return;
    }
}

/* game thread: a queued delivery through the game's setters */
static void deliver(Ctx *c, u8 *m) {
    u8 *s = (u8 *)(uintptr_t)raid_save;
    u32 before, after;
    memcpy(&before, s + SAVE_IDENT, 4);
    AcquireSRWLockExclusive(&deliv_lock);
    int n = 0;
    for (int i = 0; i < DELIV_PARTS; i++) {
        if (!deliv_new[i]) continue;
        gcall2(c, deliv_set[i], (u64)(uintptr_t)m, (u64)(uintptr_t)deliv_new[i]);
        free(deliv_new[i]);
        deliv_new[i] = NULL;
        n++;
    }
    ReleaseSRWLockExclusive(&deliv_lock);
    memcpy(&after, s + SAVE_IDENT, 4);
    InterlockedIncrement(&deliv_done);
    hlog(0, "raidgem: delivery of %d file(s), identifier %u -> %u%s", n, before, after,
         after != before ? ": the game rerolls the dens" : ": dens rebuilt from the save");
    if (after == before) InterlockedExchange(&refresh_req, 1);
}

/* the mod's side: what != 0 asks for a rebuild; returns the manager's address (0: no update seen yet: not in the
 * field) */
__declspec(dllexport) unsigned long long violet_raid_refresh(int what) {
    if (what) InterlockedExchange(&refresh_req, 1);
    return (unsigned long long)manager;
}

/* Where the own dens are, for the mod's list (violet_raid_points): copied on the game's thread now and then. */
typedef struct VioletRaidPoint { u32 region, area, group, point; float x, y, z; u32 crystal; } VioletRaidPoint;
#define POINTS_MAX 320
static SRWLOCK points_lock = SRWLOCK_INIT;
static VioletRaidPoint points[POINTS_MAX];
static int npoints;

__declspec(dllexport) int violet_raid_points(VioletRaidPoint *out, int max) {
    AcquireSRWLockShared(&points_lock);
    int n = npoints < max ? npoints : max;
    memcpy(out, points, (size_t)n * sizeof *out);
    ReleaseSRWLockShared(&points_lock);
    return n;
}

static void points_copy(u8 *m) {
    static VioletRaidPoint tmp[POINTS_MAX];
    int n = 0;
    for (u64 *v = (u64 *)(m + GEM_VECS); (u8 *)v < m + GEM_VECS_END; v += 7)
        for (u64 a = v[0]; a < v[1] && n < POINTS_MAX; a += ENTRY_SIZE) {
            u8 *e = (u8 *)(uintptr_t)a;
            if (*(int *)(e + 0x10) != 0 || !e[0x98]) continue;
            VioletRaidPoint *p = &tmp[n++];
            p->region = *(u32 *)(e + 0x64); p->area = *(u32 *)(e + 0x44);
            p->group = *(u32 *)(e + 0x48); p->point = *(u32 *)(e + 0x4c);
            memcpy(&p->x, e + 0x20, 12);
            p->crystal = *(u64 *)(e + 0x70) != 0;
        }
    AcquireSRWLockExclusive(&points_lock);
    memcpy(points, tmp, (size_t)n * sizeof *tmp);
    npoints = n;
    ReleaseSRWLockExclusive(&points_lock);
}

/* entries of the player's own dens; mark: remove them as the game does */
static int own_entries(u8 *m, int mark, int *gems) {
    int n = 0;
    *gems = 0;
    for (u64 *v = (u64 *)(m + GEM_VECS); (u8 *)v < m + GEM_VECS_END; v += 7)
        for (u64 a = v[0]; a < v[1]; a += ENTRY_SIZE) {
            u8 *e = (u8 *)(uintptr_t)a;
            if (*(int *)(e + 0x10) != 0) continue;
            u64 gem = *(u64 *)(e + 0x70);
            n++;
            if (gem) ++*gems;
            if (!mark) continue;
            e[0x98] = 0;
            if (gem) *(u8 *)(uintptr_t)(gem + 0xd1) = 1;
        }
    return n;
}

static void log_state(u8 *m, const char *why) {
    char line[512];
    int n = 0;
    for (int g = 0; g < 4; g++) n += snprintf(line + n, sizeof line - n, " %llx", *(unsigned long long *)(m + GEM_GROUPS + 8 * g));
    hlog(0, "raidgem: %s manager %llx, groups%s", why, (unsigned long long)(uintptr_t)m, line);
    n = 0;
    for (int i = 0; i < 16 && n < (int)sizeof line - 32; i++) {
        u8 *e = m + GEM_CACHE + 6 * i;
        n += snprintf(line + n, sizeof line - n, " %d:%02x%02x", *(int *)e, e[4], e[5]);
    }
    hlog(0, "raidgem: list for other players%s", line);
    int shown = 0, total = 0;
    for (u64 *v = (u64 *)(m + GEM_VECS); (u8 *)v < m + GEM_VECS_END; v += 7)
        for (u64 a = v[0]; a < v[1]; a += ENTRY_SIZE) {
            u8 *e = (u8 *)(uintptr_t)a;
            total++;
            if (shown++ < 6)
                hlog(0, "raidgem:  vec %d entry group %d, 44 %d 48 %d 4c %d 64 %d, placed %d, crystal %llx",
                     (int)((u8 *)v - (m + GEM_VECS)) / 0x38, *(int *)(e + 0x10), *(int *)(e + 0x44), *(int *)(e + 0x48),
                     *(int *)(e + 0x4c), *(int *)(e + 0x64), e[0x98], *(unsigned long long *)(e + 0x70));
        }
    hlog(0, "raidgem: %d crystal entries", total);
}

void h_raidgem_update(Ctx *c) {
    u8 *m = (u8 *)(uintptr_t)c->x[19];
    if ((u64)manager != (u64)(uintptr_t)m) {
        InterlockedExchange64(&manager, (LONG64)(uintptr_t)m);
        waiting = 0;
        if (!InterlockedExchange(&logged, 1)) log_state(m, "first update,");
    }
    int gems;
    static u32 tick;
    find_raid_save(c);
    if (!waiting && raid_save && InterlockedExchange(&deliv_req, 0)) deliver(c, m);
    if (!waiting && tick++ % 30 == 0) points_copy(m);
    if (!waiting) {
        if (!InterlockedExchange(&refresh_req, 0)) return;
        log_state(m, "refresh,");
        if (m[GEM_REROLL_DUE]) {
            hlog(0, "raidgem: a reroll is due, nothing rebuilt (it would replace the written seeds)");
            return;
        }
        int n = own_entries(m, 1, &gems);
        hlog(0, "raidgem: %d own dens marked for removal (%d with a crystal)", n, gems);
        waiting = 1;
        return;                         /* this update erases the ones without a crystal */
    }
    int left = own_entries(m, 0, &gems);
    if (left && waiting++ < WAIT_MAX) return;
    if (left) hlog(0, "raidgem: %d old dens (%d crystals) still there after %d updates, placing anyway", left, gems, WAIT_MAX);
    int after = waiting;
    waiting = 0;
    Ctx saved = *c;
    c->x[0] = (u64)(uintptr_t)m;
    c->x[1] = 0;
    guest_call(c, GEM_PLACE_ALL);
    *c = saved;
    m[GEM_REFLECT] = 1;
    int n = own_entries(m, 0, &gems);
    hlog(0, "raidgem: dens placed again from the save after %d update(s): %d own entries", after, n - left);
    log_state(m, "placed,");
}
