/* Tera raid event deliveries: the files Poke Portal News stores in the save (NOTES "Event raids"), read and written
 * as the game's FlatBuffers schemas describe them (romfs world/data/raid/delivery_raid_enemy/
 * delivery_raid_enemy_array.bfbs, delivery_raid_priority/delivery_raid_priority_array.bfbs). Used by the events mod
 * (builds and delivers them) and the raids mod (reads what is delivered, for event dens).
 *
 * The exe's exports (raidgem.c): violet_raid_delivery(which, out, max) copies a delivered file out (0 enemies, 1 fixed
 * rewards, 2 lottery rewards, 3 priority, 4 the identifier); violet_raid_deliver(parts[4]) hands new files to the
 * game's own setters. */
#pragma once
#include <stdint.h>
#include <string.h>
#include <deque>
#include <map>
#include <stdio.h>
#include <string>
#include <vector>
#include <windows.h>

namespace delivery {

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

enum { ENEMY, FIXED, LOTTERY, PRIORITY, PARTS, IDENT = PARTS };
static const u32 part_size[PARTS] = {30000, 27456, 53464, 88};
static const char *const part_file[PARTS] = {"raid_enemy_array_3_0_0", "fixed_reward_item_array_3_0_0",
                                             "lottery_reward_item_array_3_0_0", "raid_priority_array_3_0_0"};

/* RaidBossData's scalars by slot (0-9, 16-18; 10-15 are the extra actions) and the time data's */
enum { DESC_SCALARS = 19, EXTRAS = 6, TIME_FIELDS = 7 };

struct Entry {                      /* DeliveryRaidEnemyTable.raidEnemyInfo */
    int rom, no, group, difficulty, rate;
    u64 drop_fix, drop_rnd;
    int capture_rate, capture_lv;
    /* bossPokePara (PokeDataBattle) */
    int dev, form, sex, item, level, ball, waza_type, moves[4], move_pp[4], gem, seikaku, tokusei, talent_type,
        ivs[6], vnum, evs[6], rare, scale_type, scale;
    int size[6];                    /* bossPokeSize: height type / value, weight type / value, scale type / value */
    int desc[DESC_SCALARS];         /* bossDesc scalars by slot (10-15 unused here) */
    int extra[EXTRAS][4];           /* bossDesc extraAction1..6: timing, action, value, move */
    int time[TIME_FIELDS];          /* raidTimeData: isActive, gameLimit, clientLimit, commandLimit, revive, ai x2 */
};

/* ------------------------------------------------------------------------------------------------ reading */
struct Reader {
    const u8 *b;
    size_t n;
    bool ok(size_t p, size_t k) const { return p + k <= n; }
    u32 u32at(size_t p) const { u32 v = 0; if (ok(p, 4)) memcpy(&v, b + p, 4); return v; }
    /* a field's position in table t (0: absent) */
    size_t field(size_t t, int slot) const {
        if (!ok(t, 4)) return 0;
        int32_t so;
        memcpy(&so, b + t, 4);
        size_t vt = t - (size_t)(ptrdiff_t)so;
        if (!ok(vt, 4)) return 0;
        u16 vlen;
        memcpy(&vlen, b + vt, 2);
        if (4 + 2 * slot + 2 > vlen || !ok(vt + 4 + 2 * slot, 2)) return 0;
        u16 off;
        memcpy(&off, b + vt + 4 + 2 * slot, 2);
        return off ? t + off : 0;
    }
    int64_t num(size_t t, int slot, int size, bool sign) const {
        size_t p = field(t, slot);
        if (!p || !ok(p, size)) return 0;
        switch (size) {
        case 1: return sign ? (int64_t)(int8_t)b[p] : b[p];
        case 2: { u16 v; memcpy(&v, b + p, 2); return sign ? (int64_t)(int16_t)v : v; }
        case 4: { u32 v; memcpy(&v, b + p, 4); return sign ? (int64_t)(int32_t)v : v; }
        default: { u64 v; memcpy(&v, b + p, 8); return (int64_t)v; }
        }
    }
    size_t ref(size_t t, int slot) const {
        size_t p = field(t, slot);
        if (!p || !ok(p, 4)) return 0;
        size_t r = p + u32at(p);
        return ok(r, 4) ? r : 0;
    }
    size_t root() const { return n >= 8 && u32at(0) < n ? u32at(0) : 0; }
    /* a vector of tables at slot: count and the position of element i */
    u32 vcount(size_t t, int slot) const { size_t v = ref(t, slot); return v ? u32at(v) : 0; }
    size_t velem(size_t t, int slot, u32 i) const {
        size_t v = ref(t, slot);
        if (!v || i >= u32at(v)) return 0;
        size_t p = v + 4 + 4 * (size_t)i;
        return ok(p, 4) ? p + u32at(p) : 0;
    }
};

static const int desc_size[DESC_SCALARS] = {2, 1, 1, 2, 1, 2, 2, 1, 1, 1, 0, 0, 0, 0, 0, 0, 1, 1, 1};
static const bool desc_sign[DESC_SCALARS] = {1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 1, 1, 1};

/* the enemy array: every entry (placeholders too: rate 0) */
inline std::vector<Entry> parse_enemies(const u8 *buf, size_t n) {
    std::vector<Entry> out;
    Reader r{buf, n};
    size_t root = r.root();
    if (!root) return out;
    u32 count = r.vcount(root, 0);
    if (count > 1000) return out;
    for (u32 i = 0; i < count; i++) {
        size_t t = r.velem(root, 0, i), info = t ? r.ref(t, 0) : 0;
        if (!info) continue;
        Entry e = {};
        e.rom = (int)r.num(info, 0, 2, true);
        e.no = (int)r.num(info, 1, 4, true);
        e.group = (int)r.num(info, 2, 1, true);
        e.difficulty = (int)r.num(info, 3, 4, true);
        e.rate = (int)r.num(info, 4, 1, true);
        e.drop_fix = (u64)r.num(info, 5, 8, false);
        e.drop_rnd = (u64)r.num(info, 6, 8, false);
        e.capture_rate = (int)r.num(info, 7, 1, true);
        e.capture_lv = (int)r.num(info, 8, 1, true);
        if (size_t p = r.ref(info, 9)) {
            e.dev = (int)r.num(p, 0, 2, false);
            e.form = (int)r.num(p, 1, 2, true);
            e.sex = (int)r.num(p, 2, 4, true);
            e.item = (int)r.num(p, 3, 4, true);
            e.level = (int)r.num(p, 4, 4, true);
            e.ball = (int)r.num(p, 5, 4, true);
            e.waza_type = (int)r.num(p, 6, 4, true);
            for (int k = 0; k < 4; k++)
                if (size_t w = r.ref(p, 7 + k)) {
                    e.moves[k] = (int)r.num(w, 0, 2, false);
                    e.move_pp[k] = (int)r.num(w, 1, 1, true);
                }
            e.gem = (int)r.num(p, 11, 4, true);
            e.seikaku = (int)r.num(p, 12, 4, true);
            e.tokusei = (int)r.num(p, 13, 4, true);
            e.talent_type = (int)r.num(p, 14, 4, true);
            if (size_t q = r.ref(p, 15))
                for (int k = 0; k < 6; k++) e.ivs[k] = (int)r.num(q, k, 4, true);
            e.vnum = (int)r.num(p, 16, 1, true);
            if (size_t q = r.ref(p, 17))
                for (int k = 0; k < 6; k++) e.evs[k] = (int)r.num(q, k, 4, true);
            e.rare = (int)r.num(p, 18, 4, true);
            e.scale_type = (int)r.num(p, 19, 4, true);
            e.scale = (int)r.num(p, 20, 2, true);
        }
        if (size_t p = r.ref(info, 10))
            for (int k = 0; k < 6; k++) e.size[k] = (int)r.num(p, k, k & 1 ? 2 : 4, true);
        if (size_t p = r.ref(info, 11)) {
            for (int k = 0; k < DESC_SCALARS; k++)
                if (desc_size[k]) e.desc[k] = (int)r.num(p, k, desc_size[k], desc_sign[k]);
            for (int x = 0; x < EXTRAS; x++)
                if (size_t q = r.ref(p, 10 + x))
                    for (int k = 0; k < 4; k++) e.extra[x][k] = (int)r.num(q, k, 2, k < 3);
        }
        if (size_t p = r.ref(info, 12))
            for (int k = 0; k < TIME_FIELDS; k++) e.time[k] = (int)r.num(p, k, k ? 4 : 1, k != 0);
        out.push_back(e);
    }
    return out;
}

/* the priority file: versionNo and the 10 group counts (false: not a priority file) */
inline bool parse_priority(const u8 *buf, size_t n, int *version, int groups[10]) {
    Reader r{buf, n};
    size_t root = r.root(), p = root ? r.velem(root, 0, 0) : 0;
    if (!p) return false;
    *version = (int)r.num(p, 0, 4, true);
    size_t g = r.ref(p, 1);
    for (int k = 0; k < 10; k++) groups[k] = g ? (int)r.num(g, k, 1, true) : 0;
    return true;
}

/* ------------------------------------------------------------------------------------------------ writing */
/* A table to write: scalar fields (zero ones left out: their default), sub-tables, vectors of tables. Children are
 * written after their parent, so every offset points forward as FlatBuffers wants (uoffset_t is unsigned).
 * Equal sub-tables are written once: a table field is queued, and the queue is written after the tree (breadth
 * first); a copy written earlier is reused when it lies after the field that points at it. Raids built from one
 * star level's template share their boss data, size and time tables, and every all-zero EV table is one copy. */
struct Table {
    enum Kind { SCALAR, TABLE, VECTOR };
    struct F { int slot, size; Kind kind; u64 v; Table *t; std::vector<Table *> vec; };
    std::vector<F> f;
    std::string key;                /* its content with its children's (Writer::key_of), for sharing */
    Table &s(int slot, int size, int64_t v) { if (v) f.push_back({slot, size, SCALAR, (u64)v, nullptr, {}}); return *this; }
    Table &t(int slot, Table *c) { f.push_back({slot, 4, TABLE, 0, c, {}}); return *this; }
    Table &vec(int slot, std::vector<Table *> v) { f.push_back({slot, 4, VECTOR, 0, nullptr, std::move(v)}); return *this; }
};

struct Writer {
    std::vector<u8> b;
    std::deque<Table> arena;
    Table *make() { arena.emplace_back(); return &arena.back(); }
    void pad(size_t a) { while (b.size() % a) b.push_back(0); }
    void put(size_t at, const void *p, size_t n) { memcpy(&b[at], p, n); }
    size_t add(const void *p, size_t n) { size_t at = b.size(); b.insert(b.end(), (const u8 *)p, (const u8 *)p + n); return at; }

    std::vector<std::pair<std::vector<u16>, size_t>> vtables;  /* written vtables, shared by equal tables */
    struct Pending { size_t at; Table *t; };                    /* a table field to point at its table */
    std::deque<Pending> queue;
    std::map<std::string, size_t> written;                      /* table content -> its last copy */
    bool share = true;

    const std::string &key_of(Table &t) {
        if (!t.key.empty()) return t.key;
        std::string k = "{";
        for (auto &x : t.f) {
            char h[48];
            snprintf(h, sizeof h, "%d:%d:%d:%llx", x.slot, x.size, (int)x.kind, (unsigned long long)x.v);
            k += h;
            if (x.kind == Table::TABLE) k += key_of(*x.t);
            for (Table *e : x.vec) k += key_of(*e);
            k += ',';
        }
        t.key = k + "}";
        return t.key;
    }

    /* A table starts at 4 mod 8 (its soffset, then 8-aligned fields), largest fields first, so its layout is known
     * before it is written and an equal vtable written earlier can be reused. */
    size_t table(Table &t) {
        int nslots = 0;
        for (auto &x : t.f) if (x.slot + 1 > nslots) nslots = x.slot + 1;
        std::vector<u16> v(2 + nslots, 0);
        v[0] = (u16)(4 + 2 * nslots);
        std::vector<std::pair<size_t, Table::F *>> fields;     /* offset in the table, field */
        size_t rel = 4;
        for (int size : {8, 4, 2, 1})
            for (auto &x : t.f) {
                if (x.size != size) continue;
                while ((4 + rel) % size) rel++;
                v[2 + x.slot] = (u16)rel;
                fields.push_back({rel, &x});
                rel += size;
            }
        v[1] = (u16)rel;
        size_t vt = 0;
        bool found = false;
        for (auto &k : vtables)
            if (k.first == v) { vt = k.second; found = true; break; }
        if (!found) {
            pad(2);
            vt = add(v.data(), v.size() * 2);
            vtables.push_back({v, vt});
        }
        while (b.size() % 8 != 4) b.push_back(0);
        size_t tp = b.size();
        b.resize(tp + rel, 0);
        int32_t so = (int32_t)(tp - vt);
        put(tp, &so, 4);
        std::vector<std::pair<size_t, Table::F *>> refs;
        for (auto &fx : fields) {
            Table::F &x = *fx.second;
            put(tp + fx.first, &x.v, x.size);
            if (x.kind != Table::SCALAR) refs.push_back({tp + fx.first, &x});
        }
        for (auto &r : refs) {
            Table::F &x = *r.second;
            size_t target;
            if (x.kind == Table::TABLE) {
                if (share) { queue.push_back({r.first, x.t}); continue; }
                target = table(*x.t);
            } else {
                pad(4);
                target = b.size();
                u32 cnt = (u32)x.vec.size();
                add(&cnt, 4);
                std::vector<u32> slots(cnt, 0);
                if (cnt) add(slots.data(), 4 * cnt);
                for (u32 i = 0; i < cnt; i++) {
                    size_t e = table(*x.vec[i]);
                    u32 o = (u32)(e - (target + 4 + 4 * i));
                    put(target + 4 + 4 * i, &o, 4);
                }
            }
            u32 o = (u32)(target - r.first);
            put(r.first, &o, 4);
        }
        return tp;
    }
    std::vector<u8> finish(Table &root) {
        b.assign(4, 0);
        u32 r = (u32)table(root);
        put(0, &r, 4);
        while (!queue.empty()) {
            Pending p = queue.front();
            queue.pop_front();
            const std::string &k = key_of(*p.t);
            auto it = written.find(k);
            size_t target;
            if (it != written.end() && it->second > p.at) target = it->second;
            else written[k] = target = table(*p.t);     /* its own table fields join the queue */
            u32 o = (u32)(target - p.at);
            put(p.at, &o, 4);
        }
        return b;
    }
};

inline Table *entry_table(Writer &w, const Entry &e) {
    Table *boss = w.make();
    boss->s(0, 2, e.dev).s(1, 2, e.form).s(2, 4, e.sex).s(3, 4, e.item).s(4, 4, e.level).s(5, 4, e.ball)
        .s(6, 4, e.waza_type);
    for (int k = 0; k < 4; k++) {
        Table *wz = w.make();
        wz->s(0, 2, e.moves[k]).s(1, 1, e.move_pp[k]);
        boss->t(7 + k, wz);
    }
    Table *iv = w.make(), *ev = w.make();
    for (int k = 0; k < 6; k++) { iv->s(k, 4, e.ivs[k]); ev->s(k, 4, e.evs[k]); }
    boss->s(11, 4, e.gem).s(12, 4, e.seikaku).s(13, 4, e.tokusei).s(14, 4, e.talent_type).t(15, iv).s(16, 1, e.vnum)
        .t(17, ev).s(18, 4, e.rare).s(19, 4, e.scale_type).s(20, 2, e.scale);
    Table *size = w.make();
    for (int k = 0; k < 6; k++) size->s(k, k & 1 ? 2 : 4, e.size[k]);
    Table *desc = w.make();
    for (int k = 0; k < DESC_SCALARS; k++) if (desc_size[k]) desc->s(k, desc_size[k], e.desc[k]);
    for (int x = 0; x < EXTRAS; x++) {
        Table *q = w.make();
        for (int k = 0; k < 4; k++) q->s(k, 2, e.extra[x][k]);
        desc->t(10 + x, q);
    }
    Table *time = w.make();
    for (int k = 0; k < TIME_FIELDS; k++) time->s(k, k ? 4 : 1, e.time[k]);
    Table *info = w.make();
    info->s(0, 2, e.rom).s(1, 4, e.no).s(2, 1, e.group).s(3, 4, e.difficulty).s(4, 1, e.rate).s(5, 8, (int64_t)e.drop_fix)
        .s(6, 8, (int64_t)e.drop_rnd).s(7, 1, e.capture_rate).s(8, 1, e.capture_lv).t(9, boss).t(10, size).t(11, desc)
        .t(12, time);
    Table *wrap = w.make();
    wrap->t(0, info);
    return wrap;
}

/* the enemy file (part_size[ENEMY] bytes, zero-padded); empty when the entries don't fit */
inline std::vector<u8> build_enemies(const std::vector<Entry> &es) {
    Writer w;
    std::vector<Table *> v;
    for (const Entry &e : es) v.push_back(entry_table(w, e));
    Table *root = w.make();
    root->vec(0, v);
    std::vector<u8> b = w.finish(*root);
    if (b.size() > part_size[ENEMY]) return {};
    b.resize(part_size[ENEMY], 0);
    return b;
}

/* the priority file (part_size[PRIORITY] bytes); empty when it doesn't fit */
inline std::vector<u8> build_priority(int version, const int groups[10]) {
    Writer w;
    Table *g = w.make();
    for (int k = 0; k < 10; k++) g->s(k, 1, groups[k]);
    Table *p = w.make();
    p->s(0, 4, version).t(1, g);
    Table *root = w.make();
    root->vec(0, {p});
    std::vector<u8> b = w.finish(*root);
    if (b.size() > part_size[PRIORITY]) return {};
    b.resize(part_size[PRIORITY], 0);
    return b;
}

/* ------------------------------------------------------------------------------------------------ the exe */
inline int read_part(int which, std::vector<u8> &out) {
    typedef int (*Fn)(int, void *, int);
    static Fn f = (Fn)(void *)GetProcAddress(GetModuleHandleW(nullptr), "violet_raid_delivery");
    if (!f) return -1;
    out.assign(which == IDENT ? 4 : part_size[which], 0);
    int n = f(which, out.data(), (int)out.size());
    if (n <= 0) out.clear();
    return n;
}

/* -2: this exe can't; -1: not in the field yet; else deliveries applied so far (parts NULL: only that) */
inline int deliver(const void *const *parts) {
    typedef int (*Fn)(const void *const *);
    static Fn f = (Fn)(void *)GetProcAddress(GetModuleHandleW(nullptr), "violet_raid_deliver");
    return f ? f(parts) : -2;
}

} // namespace delivery
