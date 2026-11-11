/* violet_mons.dll's shared parts (mons.cpp): the species / move tables, the save's trainer, the box block in guest memory.
 * egg.cpp (the Egg and Trainers tabs) builds on these. */
#pragma once
#include "filtercombo.h"
#include "modkit.h"
#include "pk9.h"
#include <atomic>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace mons {

using namespace pk9;

enum { MIN_RUN = 960, BOX_SLOTS = 30 };

struct Species {
    u16 national, form, internal;
    std::string name, form_name;
    u8 growth, gender;              /* gender: 0 male only, 254 female only, 255 genderless, else the female ratio */
    u16 ability[3];
    u8 type1, type2, friendship;
    u8 base[6];                     /* hp atk def spa spd spe */
    std::string label;
};
/* a mon's gender (0 male, 1 female, 2 genderless): fixed by the species, else the choice (0 male, 1 female,
 * GENDER_RANDOM: rolled on the species' ratio with r, as the game rolls it: female when 1 + r % 252 < ratio) */
enum { GENDER_RANDOM = 2 };
inline bool has_both_genders(const Species &s) { return s.gender != 0 && s.gender != 254 && s.gender != 255; }
inline int pick_gender(const Species &s, int choice, u32 r) {
    if (s.gender == 255) return 2;
    if (s.gender == 254) return 1;
    if (s.gender == 0) return 0;
    if (choice == GENDER_RANDOM) return (int)(1 + r % 252) < s.gender ? 1 : 0;
    return choice ? 1 : 0;
}
struct Move { u16 id; u8 pp; std::string name; };
struct Trainer { u16 tid = 0, sid = 0; u8 name[26] = {}; u8 gender = 0, language = 2, version = 51; u16 location = 6; bool ok = false; };

extern std::vector<Species> g_species;
extern std::vector<Move> g_moves;
extern std::map<u16, std::string> g_ability_names;
extern std::vector<std::pair<u8, std::string>> g_balls;
extern std::vector<u32> g_exp[6];
extern Trainer g_save_trainer;              /* mons_trainer.txt (svedit monsdata, from the save); else the boxes' */
extern std::string g_error;
extern const char *const natures[25];
extern const char *const types[18];
extern const char *const stat_names[6];     /* the UI's order: HP Atk Def SpA SpD Spe */
extern const int ev_slot[6];                /* UI order -> the PK9's (hp atk def spe spa spd) */

std::vector<std::vector<std::string>> read_tsv(const std::string &path);
void load_tables(const char *cache_dir);         /* <cache>\mods\mons_*.txt; problems in g_error */
const char *move_name(u16 id);
const char *species_label(int dev, int form);

/* the box block: found by a scan of guest memory (scan_start; the window starts one each time it opens) */
extern std::mutex g_lock;                   /* g_blocks */
extern std::vector<u64> g_blocks;
extern std::atomic<bool> g_scanning;
extern std::atomic<u64> g_done, g_total;
extern std::string g_scan_msg;
void scan_start();
bool block_at(u64 a, std::vector<u8> &buf);
bool find_trainer(const std::vector<u8> &blk, Trainer &out);
/* the trainer new mons get: mons_trainer.txt, else the boxes' most common; false and msg when neither */
bool current_trainer(const std::vector<u8> &blk, Trainer &out, std::string &msg);
/* a copy of the live box block (MIN_RUN slots, encrypted); false and msg if none */
bool box_snapshot(std::vector<u8> &blk, std::string &msg);
/* builds a decrypted PK9 for the trainer; false and msg to refuse */
typedef bool (*BuildFn)(const void *ctx, const Trainer &t, u8 *dec, std::string &msg);
/* into the first empty slot of every copy of the box block; the slot (-1: none) and msg */
int box_put(BuildFn build, const void *ctx, const char *label, std::string &msg);

}  // namespace mons

/* egg.cpp: tabs of the "Spawn a Pokemon" window */
void egg_init(const char *cache_dir);
void egg_ui(int &species_index);   /* the window's species choice (shared) */
void trainers_ui();
