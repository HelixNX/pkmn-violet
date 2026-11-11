/* PK9: the Scarlet / Violet stored Pokemon (0x148 bytes, 0x158 with the party part), as PKHeX reads it. Offsets checked
 * with violetrecomp\tools\svedit `layout`, our writer's output with `check`. Decrypted:
 *   0 EC, 6 checksum (u16 sum of 0x08..0x148), 8 species (the game's internal index), 0xA item, 0xC TID16, 0xE SID16
 *   (together ID32), 0x10 EXP, 0x14 ability, 0x16 ability number (1, 2, 4), 0x1C PID, 0x20 nature, 0x21 stat nature,
 *   0x22 fateful | gender << 1, 0x24 form, 0x26 EVs (hp atk def spe spa spd), 0x48 height, 0x49 weight, 0x4A scale,
 *   0x58 nickname (13 UTF-16), 0x72 moves, 0x7A PP, 0x7E PP ups, 0x82 relearn moves, 0x8A current HP, 0x8C IVs (5 bits
 *   each, hp atk def spe spa spd; bit 30 egg, 31 nicknamed), 0x94 Tera type, 0x95 Tera override (0x13 none), 0xCE
 *   version, 0xD4 affixed ribbon (0xFF none), 0xD5 language, 0xF8 OT name, 0x112 OT friendship (an egg: the hatch
 *   counter, egg cycles left), 0x119 egg date (y-2000, m, d), 0x11C met date, 0x11F obedience level, 0x120 egg
 *   location, 0x122 met location, 0x124 ball, 0x125 met level | OT gender << 7; party: 0x148 level, 0x14A max HP, then
 *   atk def spe spa spd.
 * Encrypted: the four 0x50 blocks from 0x08 shuffled by (EC >> 13) & 31, then 0x08..0x148 and 0x148..0x158 each XORed
 * with an LCRNG (x * 0x41C64E6D + 0x6073, high 16 bits) seeded with the EC.
 *
 * Trainer ids: the game shows the 6-digit "ID No." = ID32 % 1000000; the hidden part (SID7) = ID32 / 1000000. A mon is
 * shiny for a trainer when (PID >> 16) ^ (PID & 0xFFFF) ^ TID16 ^ SID16 < 16; an egg's shininess is checked against
 * whoever hatches it (a traded egg takes the hatcher as its OT). */
#pragma once
#include <stdint.h>
#include <string.h>
#include <string>
#include <windows.h>

namespace pk9 {

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

enum : int {
    SLOT = 0x158, STORED = 0x148,
    O_EC = 0x00, O_CHECKSUM = 0x06, O_SPECIES = 0x08, O_ITEM = 0x0A, O_TID = 0x0C, O_SID = 0x0E, O_EXP = 0x10,
    O_ABILITY = 0x14, O_ABILITY_NUM = 0x16, O_PID = 0x1C, O_NATURE = 0x20, O_STAT_NATURE = 0x21, O_GENDER = 0x22,
    O_FORM = 0x24, O_EVS = 0x26, O_HEIGHT = 0x48, O_WEIGHT = 0x49, O_SCALE = 0x4A, O_NICK = 0x58, O_MOVES = 0x72,
    O_PP = 0x7A, O_RELEARN = 0x82, O_HP = 0x8A, O_IV32 = 0x8C, O_TERA = 0x94, O_TERA_OVERRIDE = 0x95,
    O_VERSION = 0xCE, O_RIBBON = 0xD4, O_LANGUAGE = 0xD5, O_OT_NAME = 0xF8, O_OT_FRIENDSHIP = 0x112,
    O_EGG_DATE = 0x119, O_MET_DATE = 0x11C, O_OBEDIENCE = 0x11F, O_EGG_LOCATION = 0x120, O_MET_LOCATION = 0x122,
    O_BALL = 0x124, O_MET_LEVEL = 0x125, O_STAT_LEVEL = 0x148, O_STAT_HPMAX = 0x14A,
};
enum : u32 { IV_EGG = 1u << 30, IV_NICKNAMED = 1u << 31 };

inline u16 rd16(const u8 *p) { return (u16)(p[0] | p[1] << 8); }
inline u32 rd32(const u8 *p) { return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24; }
inline void wr16(u8 *p, u32 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); }
inline void wr32(u8 *p, u32 v) { wr16(p, v); wr16(p + 2, v >> 16); }

inline const char *block_order(u32 ec) {
    static const char *const shuffle[32] = {"ABCD", "ABDC", "ACBD", "ACDB", "ADBC", "ADCB", "BACD", "BADC", "BCAD", "BCDA",
                                            "BDAC", "BDCA", "CABD", "CADB", "CBAD", "CBDA", "CDAB", "CDBA", "DABC", "DACB",
                                            "DBAC", "DBCA", "DCAB", "DCBA", "ABCD", "ABDC", "ACBD", "ACDB", "ADBC", "ADCB",
                                            "BACD", "BADC"};
    return shuffle[(ec >> 13) & 31];
}

inline void xor_stream(u8 *d, u32 seed, int from, int to) {
    for (int i = from; i < to; i += 2) {
        seed = seed * 0x41C64E6Du + 0x6073u;
        wr16(d + i, rd16(d + i) ^ (seed >> 16));
    }
}

inline u16 checksum(const u8 *dec) {
    u32 s = 0;
    for (int i = 8; i < STORED; i += 2) s += rd16(dec + i);
    return (u16)s;
}

inline void encrypt(const u8 *dec, u8 *enc) {
    const char *order = block_order(rd32(dec));
    memcpy(enc, dec, SLOT);
    for (int pos = 0; pos < 4; pos++) memcpy(enc + 8 + 0x50 * pos, dec + 8 + 0x50 * (order[pos] - 'A'), 0x50);
    xor_stream(enc, rd32(dec), 8, STORED);
    xor_stream(enc, rd32(dec), STORED, SLOT);
}

inline void decrypt(const u8 *enc, u8 *dec) {
    u32 ec = rd32(enc);
    u8 t[SLOT];
    memcpy(t, enc, SLOT);
    xor_stream(t, ec, 8, STORED);
    xor_stream(t, ec, STORED, SLOT);
    const char *order = block_order(ec);
    memcpy(dec, t, SLOT);
    for (int pos = 0; pos < 4; pos++) memcpy(dec + 8 + 0x50 * (order[pos] - 'A'), t + 8 + 0x50 * pos, 0x50);
}

/* an encrypted slot's checksum holds (the block shuffle does not change the sum) */
inline bool slot_valid(const u8 *enc) {
    if (rd16(enc + 4)) return false;
    u32 seed = rd32(enc), s = 0;
    for (int i = 8; i < STORED; i += 2) {
        seed = seed * 0x41C64E6Du + 0x6073u;
        s += rd16(enc + i) ^ (seed >> 16);
    }
    return (u16)s == rd16(enc + 6);
}

/* names: 12 UTF-16 characters + a terminator (26 bytes) */
inline void put_name16(u8 *dst, const wchar_t *s) {
    memset(dst, 0, 26);
    for (int i = 0; i < 12 && s[i]; i++) wr16(dst + 2 * i, (u16)s[i]);
}
inline void put_name(u8 *dst, const char *utf8) {
    wchar_t w[32] = {};
    if (!MultiByteToWideChar(CP_UTF8, 0, utf8, -1, w, 31))
        for (int i = 0; i < 31 && utf8[i]; i++) w[i] = (u8)utf8[i];
    put_name16(dst, w);
}
inline std::string get_name(const u8 *src) {
    wchar_t w[13] = {};
    for (int i = 0; i < 12 && rd16(src + 2 * i); i++) w[i] = (wchar_t)rd16(src + 2 * i);
    char out[64] = {};
    WideCharToMultiByte(CP_UTF8, 0, w, -1, out, sizeof out - 1, nullptr, nullptr);
    return out;
}

/* what the game calls an egg, in the OT's language (PKHeX's SpeciesName egg names) */
inline const wchar_t *egg_name(int language) {
    switch (language) {
    case 1: return L"タマゴ";      /* タマゴ */
    case 3: return L"Œuf";                 /* Œuf */
    case 4: return L"Uovo";
    case 5: return L"Ei";
    case 7: return L"Huevo";
    case 8: return L"알";                   /* 알 */
    case 9: case 10: return L"蛋";          /* 蛋 */
    default: return L"Egg";
    }
}

/* trainer ids: 16-bit halves <-> the game's display (6-digit ID No., 4-digit hidden SID) */
inline u32 id32(u16 tid, u16 sid) { return (u32)tid | (u32)sid << 16; }
inline u32 tid7(u32 id) { return id % 1000000; }
inline u32 sid7(u32 id) { return id / 1000000; }
inline bool id32_from7(u32 t7, u32 s7, u32 &out) {
    u64 v = (u64)s7 * 1000000 + t7;
    if (t7 > 999999 || v > 0xFFFFFFFFull) return false;
    out = (u32)v;
    return true;
}

inline u32 shiny_xor(u32 pid, u16 tid, u16 sid) { return (pid >> 16) ^ (pid & 0xFFFF) ^ tid ^ sid; }
/* the trainer shiny value (TSV): a PID whose PSV ((hi ^ lo) >> 4) equals it is shiny for that trainer */
inline u32 tsv(u16 tid, u16 sid) { return (u32)(tid ^ sid) >> 4; }

}  // namespace pk9
