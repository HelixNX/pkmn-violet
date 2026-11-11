/* Eggs and trainer ids: the "Egg" and "Trainers" tabs of the "Spawn a Pokemon" window (violet_mons.dll).
 *
 * Egg: an unhatched egg written into the first free box slot, as PKHeX 26.10.18's legality check holds an unhatched SV
 * egg (checked 2026-11-11 against this save with svedit; the per-species facts come from PKHeX, see below):
 *   level 1 (EXP of level 1), met level 1, obedience 1; egg location 30023 (picnic), met location 0 (none until it
 *   hatches; a trade sets 30002 itself); egg date today and NO met date (a met date fails); egg flag (IV32 bit 30) and
 *   the nickname flag (must be set); nickname: the egg name written over the species name, the rest of which stays
 *   as the expected trash; EVs 0, no held item; the species' level-1 moves, relearn moves the same; OT friendship =
 *   the hatch counter (egg cycles left, 1 or more: 0 fails); Tera type one of the species' own types; height / weight
 *   / scale random.
 * Free choices, as breeding allows: IVs (any 0-31), nature, ability incl. hidden, gender, ball (those PKHeX allows for
 * the species: never Master or Cherish), Tera type of the species' two, shiny.
 *
 * Shiny: an egg's PID is fixed when it is made; whether it hatches shiny is checked against whoever hatches it (a traded
 * egg takes its hatcher as OT). "Shiny for" picks that trainer: its ids are read off a mon it traded you (Trainers
 * tab) or typed in (the 6-digit ID No. from the trainer card plus the hidden SID, or the 16-bit TID / SID). A shiny
 * PID gets a random xor 0-15 with those ids (as the game's rerolls give); a non-shiny one is kept >= 16.
 *
 * Per-species data: <build>\mods\mons_eggs.txt (svedit monsdata): one row per species / form PKHeX lets hatch from an
 * egg in SV (it builds each egg with these rules and keeps only those its legality check passes):
 *   national  form  hatch cycles  nickname flag  move1..4  relearn1..4  ball mask (hex, bit = ball id)  nickname
 *   buffer (26 bytes hex, in the save's language; another language gets its egg name over zeros: its trash unknown)
 * svedit `check` reads an egg we wrote (mons_egg_selftest, or dumped) back with PKHeX and its legality report. */
#include "mons.h"
#include <random>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

using namespace mons;

namespace {

struct EggRow { u16 national, form; u8 cycles; bool nickflag; u16 moves[4], relearn[4]; u64 balls; u8 nick[26]; bool has_nick; int species = -1; };
std::vector<EggRow> g_eggs;
std::string g_egg_error;

const EggRow *egg_row(int species) {
    for (auto &r : g_eggs) if (r.species == species) return &r;
    return nullptr;
}

/* who an egg is made for */
struct EggSpec {
    int nature = 3;
    int ability_slot = 0;           /* 0, 1, 2 hidden */
    int gender = 0;
    int iv[6] = {31, 31, 31, 31, 31, 31};   /* UI order */
    u8 ball = 4;
    int tera = 0;                   /* 0 the species' first type, 1 its second */
    bool shiny = true;
    int cycles = 1;                 /* hatch counter; -1: the species' full count */
    /* OT: the save's trainer unless custom */
    bool custom_ot = false;
    char ot_name[40] = "";
    u32 ot_id = 0;
    int ot_gender = 0;
    /* shiny for: the OT unless another hatcher */
    bool other_hatcher = false;
    u32 hatcher_id = 0;
    char hatcher_name[40] = "";     /* only shown */
};
EggSpec g_egg;
std::string g_egg_status;
std::mutex g_egg_status_lock;

void egg_status(const std::string &s) { std::lock_guard<std::mutex> l(g_egg_status_lock); g_egg_status = s; }

u32 level1_stat(const Species &s, const EggSpec &e, int i) {    /* i: UI order; level 1, no EVs */
    int b = s.base[i], iv = e.iv[i];
    if (i == 0) return s.national == 292 ? 1 : (u32)((2 * b + iv) / 100 + 1 + 10);
    u32 v = (u32)((2 * b + iv) / 100 + 5);
    int up = e.nature / 5, down = e.nature % 5;
    int me = i == 1 ? 0 : i == 2 ? 1 : i == 3 ? 3 : i == 4 ? 4 : 2;
    if (up != down) { if (me == up) v = v * 110 / 100; else if (me == down) v = v * 90 / 100; }
    return v;
}

struct EggJob { int species; EggSpec e; };

/* the decrypted PK9 of an unhatched egg */
bool build_egg(const void *ctx, const Trainer &save_t, u8 *d, std::string &msg) {
    const EggJob &job = *(const EggJob *)ctx;
    const EggSpec &e = job.e;
    const Species &s = g_species[job.species];
    const EggRow *row = egg_row(job.species);
    if (!row) { msg = s.label + " does not hatch from an egg."; return false; }
    if (!(row->balls >> e.ball & 1)) { msg = "No " + s.name + " egg comes in that ball."; return false; }
    Trainer t = save_t;
    if (e.custom_ot) {
        if (!e.ot_name[0]) { msg = "Give the custom OT a name."; return false; }
        put_name(t.name, e.ot_name);
        t.tid = (u16)e.ot_id; t.sid = (u16)(e.ot_id >> 16);
        t.gender = (u8)e.ot_gender;
    }
    u16 htid = t.tid, hsid = t.sid;                 /* whoever hatches it */
    if (e.other_hatcher) { htid = (u16)e.hatcher_id; hsid = (u16)(e.hatcher_id >> 16); }

    static std::mt19937 rng((u32)GetTickCount64() ^ (u32)time(nullptr) ^ 0x5EED0E66u);
    memset(d, 0, SLOT);
    u32 ec = rng(), pid;
    if (e.shiny) {
        u32 lo = rng() & 0xFFFF, x = rng() & 15;
        pid = lo | (u32)((lo ^ htid ^ hsid ^ x) & 0xFFFF) << 16;
    } else
        do pid = rng(); while (shiny_xor(pid, htid, hsid) < 16);
    wr32(d + O_EC, ec);
    wr16(d + O_SPECIES, s.internal);
    wr16(d + O_TID, t.tid);
    wr16(d + O_SID, t.sid);
    wr32(d + O_EXP, g_exp[s.growth < 6 ? s.growth : 0][0]);
    wr16(d + O_ABILITY, s.ability[e.ability_slot]);
    d[O_ABILITY_NUM] = (u8)(1 << e.ability_slot);
    wr32(d + O_PID, pid);
    d[O_NATURE] = d[O_STAT_NATURE] = (u8)e.nature;
    int gender = s.gender == 255 ? 2 : s.gender == 254 ? 1 : s.gender == 0 ? 0 : e.gender;
    d[O_GENDER] = (u8)(gender << 1);
    wr16(d + O_FORM, s.form);
    d[O_HEIGHT] = (u8)rng(); d[O_WEIGHT] = (u8)rng(); d[O_SCALE] = (u8)rng();
    if (row->has_nick && (!g_save_trainer.ok || t.language == g_save_trainer.language)) memcpy(d + O_NICK, row->nick, 26);
    else put_name16(d + O_NICK, egg_name(t.language));
    for (int i = 0; i < 4; i++) {
        wr16(d + O_MOVES + 2 * i, row->moves[i]);
        u8 pp = 0;
        for (auto &m : g_moves) if (m.id == row->moves[i]) pp = m.pp;
        d[O_PP + i] = pp;
        wr16(d + O_RELEARN + 2 * i, row->relearn[i]);
    }
    u32 iv32 = IV_EGG | (row->nickflag ? IV_NICKNAMED : 0);
    for (int i = 0; i < 6; i++) iv32 |= (u32)(e.iv[i] & 31) << (5 * ev_slot[i]);
    wr32(d + O_IV32, iv32);
    d[O_TERA] = e.tera == 1 && s.type2 != s.type1 ? s.type2 : s.type1;
    d[O_TERA_OVERRIDE] = 0x13;
    d[O_VERSION] = t.version;
    d[O_RIBBON] = 0xFF;
    d[O_LANGUAGE] = t.language;
    memcpy(d + O_OT_NAME, t.name, 26);
    int cyc = e.cycles < 0 || e.cycles > row->cycles ? row->cycles : e.cycles;     /* 0 only when asked (popup) */
    d[O_OT_FRIENDSHIP] = (u8)cyc;
    time_t now = time(nullptr);
    struct tm tm;
    localtime_s(&tm, &now);
    u8 date[3] = {(u8)(tm.tm_year - 100), (u8)(tm.tm_mon + 1), (u8)tm.tm_mday};
    memcpy(d + O_EGG_DATE, date, 3);                /* the met date stays empty until it hatches */
    d[O_OBEDIENCE] = 1;
    wr16(d + O_EGG_LOCATION, 30023);               /* picnic */
    wr16(d + O_MET_LOCATION, 0);
    d[O_BALL] = e.ball;
    d[O_MET_LEVEL] = (u8)(1 | t.gender << 7);
    u32 hp = level1_stat(s, e, 0);
    wr16(d + O_HP, hp);
    d[O_STAT_LEVEL] = 1;
    wr16(d + O_STAT_HPMAX, hp);
    wr16(d + 0x14C, level1_stat(s, e, 1)); wr16(d + 0x14E, level1_stat(s, e, 2)); wr16(d + 0x150, level1_stat(s, e, 5));
    wr16(d + 0x152, level1_stat(s, e, 3)); wr16(d + 0x154, level1_stat(s, e, 4));
    wr16(d + O_CHECKSUM, checksum(d));
    return true;
}

/* ---------------------------------------------------------------------------------------------- trainer ids UI */
/* an id editor: the game's display (6-digit ID No. + hidden SID) or the 16-bit halves */
int g_id_mode = 0;

bool id_edit(const char *label, u32 &id) {
    ImGui::PushID(label);
    bool changed = false;
    if (g_id_mode == 0) {
        u32 t = tid7(id), s = sid7(id);
        ImGui::SetNextItemWidth(90);
        bool a = ImGui::InputScalar("ID No.", ImGuiDataType_U32, &t);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(70);
        bool b = ImGui::InputScalar("SID", ImGuiDataType_U32, &s);
        u32 v;
        if ((a || b) && id32_from7(t > 999999 ? 999999 : t, s, v)) { id = v; changed = true; }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("The hidden secret ID: 0-4294 (ID No. + SID x 1000000 fits 32 bits)");
    } else {
        u16 t = (u16)id, s = (u16)(id >> 16);
        ImGui::SetNextItemWidth(80);
        bool a = ImGui::InputScalar("TID16", ImGuiDataType_U16, &t);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80);
        bool b = ImGui::InputScalar("SID16", ImGuiDataType_U16, &s);
        if (a || b) { id = id32(t, s); changed = true; }
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(TSV %u)", tsv((u16)id, (u16)(id >> 16)));
    ImGui::PopID();
    return changed;
}

std::string id_text(u32 id) {
    char b[64];
    snprintf(b, sizeof b, "%06u / %04u", tid7(id), sid7(id));
    return b;
}

FilterCombo fc_egg_species, fc_egg_nature, fc_egg_ability, fc_egg_ball;

/* ---------------------------------------------------------------------------------------------- Trainers tab data */
struct BoxMon {
    int slot;
    std::string species, nick, ot;
    u32 id;
    u8 ot_gender, version;
    bool egg, shiny;
};
std::vector<BoxMon> g_box_mons;
Trainer g_me;
bool g_me_ok = false;
std::string g_trainers_msg;

void read_box_mons() {
    std::vector<u8> blk;
    std::string msg;
    g_box_mons.clear();
    if (!box_snapshot(blk, msg)) { g_trainers_msg = msg; return; }
    g_me_ok = current_trainer(blk, g_me, msg);
    u8 dec[SLOT];
    for (int i = 0; i < MIN_RUN; i++) {
        decrypt(&blk[(size_t)i * SLOT], dec);
        u16 dev = rd16(dec + O_SPECIES);
        if (!dev) continue;
        BoxMon m;
        m.slot = i;
        const char *sl = species_label(dev, rd16(dec + O_FORM));
        m.species = sl ? sl : "DevID " + std::to_string(dev);
        m.nick = get_name(dec + O_NICK);
        m.ot = get_name(dec + O_OT_NAME);
        m.id = rd32(dec + O_TID);
        m.ot_gender = dec[O_MET_LEVEL] >> 7;
        m.version = dec[O_VERSION];
        m.egg = (rd32(dec + O_IV32) & IV_EGG) != 0;
        m.shiny = shiny_xor(rd32(dec + O_PID), (u16)m.id, (u16)(m.id >> 16)) < 16;
        g_box_mons.push_back(m);
    }
    char b[128];
    snprintf(b, sizeof b, "%zu mons in the boxes.", g_box_mons.size());
    g_trainers_msg = b;
}

const char *version_name(u8 v) { return v == 50 ? "Scarlet" : v == 51 ? "Violet" : v == 0 ? "-" : "other game"; }

}  // namespace

/* ---------------------------------------------------------------------------------------------- init */
void egg_init(const char *cache_dir) {
    std::string keep = g_error;
    auto rows = read_tsv(std::string(cache_dir) + "\\mods\\mons_eggs.txt");
    g_error = keep;
    for (auto &r : rows) {
        if (r.size() < 13 || r[0].empty() || r[0][0] == '#') continue;
        EggRow e = {};
        e.national = (u16)atoi(r[0].c_str()); e.form = (u16)atoi(r[1].c_str());
        e.cycles = (u8)atoi(r[2].c_str()); e.nickflag = atoi(r[3].c_str()) != 0;
        for (int i = 0; i < 4; i++) { e.moves[i] = (u16)atoi(r[4 + i].c_str()); e.relearn[i] = (u16)atoi(r[8 + i].c_str()); }
        e.balls = strtoull(r[12].c_str(), nullptr, 16);
        if (r.size() > 13 && r[13].size() == 52) {
            e.has_nick = true;
            for (int i = 0; i < 26; i++) e.nick[i] = (u8)strtoul(r[13].substr(2 * i, 2).c_str(), nullptr, 16);
        }
        for (size_t i = 0; i < g_species.size(); i++)
            if (g_species[i].national == e.national && g_species[i].form == e.form) e.species = (int)i;
        if (e.species >= 0) g_eggs.push_back(e);
    }
    if (g_eggs.empty()) g_egg_error = "No egg list: run svedit monsdata again (it writes build\\mods\\mons_eggs.txt).";
    if (g_save_trainer.ok) { g_egg.ot_id = id32(g_save_trainer.tid, g_save_trainer.sid); g_egg.hatcher_id = g_egg.ot_id; }
    if (modkit::host()) modkit::host()->log(1, "mons: %zu egg species / forms", g_eggs.size());
}

/* ---------------------------------------------------------------------------------------------- Egg tab */
void egg_ui(int &species_index) {
    if (!g_egg_error.empty()) { ImGui::TextWrapped("%s", g_egg_error.c_str()); return; }
    EggSpec &e = g_egg;
    const EggRow *row = species_index >= 0 ? egg_row(species_index) : nullptr;
    ImGui::SetNextItemWidth(320);
    if (fc_egg_species.begin("Egg of", row ? g_species[species_index].label.c_str() : "(one that hatches from an egg)")) {
        for (auto &r : g_eggs)
            if (fc_egg_species.item(g_species[r.species].label.c_str(), r.species == species_index)) {
                species_index = r.species;
                e.ability_slot = 0;
                e.tera = 0;
            }
        fc_egg_species.end();
    }
    if (!row) {
        if (species_index >= 0)
            ImGui::TextColored(ImVec4(1, 0.7f, 0.3f, 1), "%s does not hatch from an egg in this game: pick its first stage.",
                               g_species[species_index].name.c_str());
        return;
    }
    const Species &s = g_species[species_index];

    ImGui::SeparatorText("The Pokemon inside");
    ImGui::Checkbox("Shiny", &e.shiny);
    if (s.gender != 0 && s.gender != 254 && s.gender != 255) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100);
        ImGui::Combo("Gender", &e.gender, "Male\0Female\0");
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(160);
    if (fc_egg_nature.begin("Nature", natures[e.nature])) {
        for (int k = 0; k < 25; k++) if (fc_egg_nature.item(natures[k], e.nature == k)) e.nature = k;
        fc_egg_nature.end();
    }
    ImGui::SetNextItemWidth(200);
    {
        auto an = [&](int k) { auto it = g_ability_names.find(s.ability[k]); return it != g_ability_names.end() ? it->second : std::string("?"); };
        std::string cur = an(e.ability_slot) + (e.ability_slot == 2 ? " (H)" : "");
        if (fc_egg_ability.begin("Ability", cur.c_str(), 0)) {
            for (int k = 0; k < 3; k++) {
                std::string t = an(k) + (k == 2 ? " (H)" : ""), l = t + "##" + std::to_string(k);
                if (fc_egg_ability.item(t.c_str(), e.ability_slot == k, l.c_str())) e.ability_slot = k;
            }
            fc_egg_ability.end();
        }
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(160);
    {
        if (!(row->balls >> e.ball & 1)) e.ball = 4;            /* this species: not that ball */
        const char *bn = "?";
        for (auto &b : g_balls) if (b.first == e.ball) bn = b.second.c_str();
        if (fc_egg_ball.begin("Ball", bn)) {
            for (auto &b : g_balls)
                if ((row->balls >> b.first & 1) && fc_egg_ball.item(b.second.c_str(), b.first == e.ball)) e.ball = b.first;
            fc_egg_ball.end();
        }
    }
    if (s.type2 != s.type1) {
        ImGui::SetNextItemWidth(160);
        char items[64];
        snprintf(items, sizeof items, "%s%c%s%c", types[s.type1 < 18 ? s.type1 : 0], 0, types[s.type2 < 18 ? s.type2 : 0], 0);
        ImGui::Combo("Tera type", &e.tera, items);
    } else
        ImGui::TextDisabled("Tera type: %s (an egg gets one of its species' types)", types[s.type1 < 18 ? s.type1 : 0]);

    ImGui::TextUnformatted("IVs");
    for (int i = 0; i < 6; i++) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(44);
        ImGui::InputInt((std::string(stat_names[i]) + "##eiv").c_str(), &e.iv[i], 0, 0);
        e.iv[i] = e.iv[i] < 0 ? 0 : e.iv[i] > 31 ? 31 : e.iv[i];
    }
    if (ImGui::SmallButton("All 31")) for (int &v : e.iv) v = 31;
    ImGui::SameLine();
    if (ImGui::SmallButton("0 Atk")) e.iv[1] = 0;
    ImGui::SameLine();
    if (ImGui::SmallButton("0 Spe")) e.iv[5] = 0;
    ImGui::SameLine();
    ImGui::TextDisabled("Moves: its level-1 moves. No EVs or item on an egg.");

    ImGui::SeparatorText("Hatching");
    {
        int full = row->cycles;
        int c = e.cycles < 0 ? full : e.cycles > full ? full : e.cycles;
        ImGui::SetNextItemWidth(200);
        if (ImGui::SliderInt("Egg cycles left", &c, 1, full)) e.cycles = c;
        ImGui::SameLine();
        if (ImGui::SmallButton("Almost ready")) e.cycles = 1;
        ImGui::SameLine();
        if (ImGui::SmallButton("Fresh")) e.cycles = -1;
        ImGui::SameLine();
        if (ImGui::SmallButton("0")) e.cycles = 0;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Asks first: PKHeX calls 0 illegal");
        ImGui::TextDisabled("Fresh: %d cycles. At 1 it hatches after a short walk (the trade keeps the count).", full);
    }

    ImGui::SeparatorText("Trainers");
    ImGui::RadioButton("ID No. + SID", &g_id_mode, 0);
    ImGui::SameLine();
    ImGui::RadioButton("16-bit TID / SID", &g_id_mode, 1);
    {
        char me[96] = "the save's trainer";
        if (g_save_trainer.ok)
            snprintf(me, sizeof me, "%s (%s)", get_name(g_save_trainer.name).c_str(),
                     id_text(id32(g_save_trainer.tid, g_save_trainer.sid)).c_str());
        ImGui::Checkbox("Custom OT", &e.custom_ot);
        ImGui::SameLine();
        if (e.custom_ot) ImGui::TextDisabled("(the egg's trainer until someone else hatches it)");
        else ImGui::TextDisabled("OT: %s", me);
        if (e.custom_ot) {
            ImGui::Indent();
            ImGui::SetNextItemWidth(160);
            ImGui::InputText("Name##ot", e.ot_name, 13 * 3);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(90);
            ImGui::Combo("##otg", &e.ot_gender, "Male\0Female\0");
            id_edit("ot", e.ot_id);
            ImGui::Unindent();
        }
        ImGui::Checkbox("Shiny for another trainer (who hatches it)", &e.other_hatcher);
        if (e.other_hatcher) {
            ImGui::Indent();
            id_edit("hatcher", e.hatcher_id);
            if (e.hatcher_name[0]) { ImGui::SameLine(); ImGui::TextDisabled("%s", e.hatcher_name); }
            ImGui::TextDisabled("Their ids: Trainers tab, on a mon they traded you; or their trainer card's ID No.");
            ImGui::Unindent();
        }
    }

    ImGui::Separator();
    static bool ask_zero = false;
    if (ImGui::Button("Make the egg (first free box slot)")) {
        if (e.cycles == 0) ask_zero = true;
        else {
            EggJob job = {species_index, e};
            std::string msg;
            box_put(build_egg, &job, ("Egg of " + s.label).c_str(), msg);
            egg_status(msg);
        }
    }
    if (ask_zero) { ImGui::OpenPopup("Egg cycles 0"); ask_zero = false; }
    if (ImGui::BeginPopupModal("Egg cycles 0", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("0 cycles left: PKHeX flags this egg as illegal (\"Invalid Egg hatch cycles\").\n"
                               "1 is legal, looks like an egg walked nearly to the end, and hatches almost as soon.");
        if (ImGui::Button("Make it with 1")) {
            e.cycles = 1;
            EggJob job = {species_index, e};
            std::string msg;
            box_put(build_egg, &job, ("Egg of " + s.label).c_str(), msg);
            egg_status(msg);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Make it with 0")) {
            EggJob job = {species_index, e};
            std::string msg;
            box_put(build_egg, &job, ("Egg of " + s.label).c_str(), msg);
            egg_status(msg);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (g_scanning) {
        ImGui::SameLine();
        ImGui::ProgressBar((float)((double)g_done / (double)g_total), ImVec2(200, 0), "scanning memory");
    }
    std::lock_guard<std::mutex> l(g_egg_status_lock);
    if (!g_egg_status.empty()) ImGui::TextWrapped("%s", g_egg_status.c_str());
}

/* ---------------------------------------------------------------------------------------------- Trainers tab */
void trainers_ui() {
    if (ImGui::Button("Read the boxes")) read_box_mons();
    ImGui::SameLine();
    static bool others_only = true;
    ImGui::Checkbox("Only other trainers' mons", &others_only);
    ImGui::SameLine();
    ImGui::RadioButton("ID No. + SID##t", &g_id_mode, 0);
    ImGui::SameLine();
    ImGui::RadioButton("16-bit##t", &g_id_mode, 1);
    if (g_me_ok) {
        u32 me = id32(g_me.tid, g_me.sid);
        ImGui::Text("You: %s, ID No. %06u, SID %04u (TID16 %u, SID16 %u, TSV %u)", get_name(g_me.name).c_str(), tid7(me),
                    sid7(me), g_me.tid, g_me.sid, tsv(g_me.tid, g_me.sid));
    }
    if (!g_trainers_msg.empty()) ImGui::TextDisabled("%s A traded mon in your party: move it to a box first.", g_trainers_msg.c_str());
    u32 me = g_me_ok ? id32(g_me.tid, g_me.sid) : 0xFFFFFFFFu;
    if (ImGui::BeginTable("##trainers", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY |
                                               ImGuiTableFlags_SizingFixedFit, ImVec2(0, 380))) {
        ImGui::TableSetupColumn("Box");
        ImGui::TableSetupColumn("Pokemon", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("OT");
        ImGui::TableSetupColumn(g_id_mode == 0 ? "ID No. / SID" : "TID16 / SID16");
        ImGui::TableSetupColumn("Game");
        ImGui::TableSetupColumn("Use as");
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();
        for (auto &m : g_box_mons) {
            if (others_only && g_me_ok && m.id == me) continue;
            ImGui::PushID(m.slot);
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::Text("%d/%d", m.slot / BOX_SLOTS + 1, m.slot % BOX_SLOTS + 1);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(m.egg ? ("Egg (" + m.species + ")").c_str() : m.species.c_str());
            if (!m.egg && m.nick != m.species && !m.nick.empty()) { ImGui::SameLine(); ImGui::TextDisabled("\"%s\"", m.nick.c_str()); }
            if (m.shiny && !m.egg) { ImGui::SameLine(); ImGui::TextColored(ImVec4(1, 0.85f, 0.3f, 1), "*"); }
            ImGui::TableNextColumn(); ImGui::Text("%s %s", m.ot.c_str(), m.ot_gender ? "(F)" : "(M)");
            ImGui::TableNextColumn();
            if (g_id_mode == 0) ImGui::TextUnformatted(id_text(m.id).c_str());
            else ImGui::Text("%5u / %5u", (u16)m.id, (u16)(m.id >> 16));
            ImGui::TableNextColumn(); ImGui::TextUnformatted(version_name(m.version));
            ImGui::TableNextColumn();
            if (ImGui::SmallButton("Hatcher")) {
                g_egg.other_hatcher = true;
                g_egg.hatcher_id = m.id;
                snprintf(g_egg.hatcher_name, sizeof g_egg.hatcher_name, "%s", m.ot.c_str());
                egg_status("Eggs from the Egg tab are now shiny for " + m.ot + " (" + id_text(m.id) + ") when shiny is on.");
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Make the Egg tab's eggs shiny for this trainer when they hatch them");
            ImGui::SameLine();
            if (ImGui::SmallButton("OT")) {
                g_egg.custom_ot = true;
                g_egg.ot_id = m.id;
                g_egg.ot_gender = m.ot_gender;
                snprintf(g_egg.ot_name, sizeof g_egg.ot_name, "%s", m.ot.c_str());
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Make the Egg tab's eggs with this trainer as OT");
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

/* for svedit `check`: an egg these settings make, encrypted, to a file (no game needed). hatcher_id 0: shiny for the OT */
extern "C" __declspec(dllexport) int mons_egg_selftest(const char *cache_dir, const char *out, int national, int form,
                                                       unsigned ot_id, unsigned hatcher_id, int cycles) {
    if (g_species.empty()) load_tables(cache_dir);
    if (g_eggs.empty()) egg_init(cache_dir);
    int sp = -1;
    for (size_t i = 0; i < g_species.size(); i++)
        if (g_species[i].national == national && g_species[i].form == form) sp = (int)i;
    if (sp < 0) return 1;
    EggJob job = {sp, EggSpec()};
    job.e.shiny = true;
    job.e.ability_slot = 2;
    job.e.cycles = cycles;
    job.e.other_hatcher = hatcher_id != 0;
    job.e.hatcher_id = hatcher_id;
    Trainer t;
    put_name(t.name, "Tester");
    t.tid = (u16)ot_id; t.sid = (u16)(ot_id >> 16);
    if (g_save_trainer.ok) { t.language = g_save_trainer.language; t.version = g_save_trainer.version; }
    t.ok = true;
    u8 dec[SLOT], enc[SLOT], back[SLOT];
    std::string msg;
    if (!build_egg(&job, t, dec, msg)) return 2;
    encrypt(dec, enc);
    decrypt(enc, back);
    if (memcmp(dec, back, SLOT) || !slot_valid(enc)) return 3;
    FILE *f = fopen(out, "wb");
    if (!f) return 4;
    fwrite(enc, 1, SLOT, f);
    fclose(f);
    return 0;
}
