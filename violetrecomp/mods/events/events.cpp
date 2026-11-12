/* Events: choose the event Tera raids the save has delivered (a mod of its own, violet_events.dll; window "Events",
 * F4 by default). Outbreaks come later (a tab of their own).
 *
 * A console gets event raids from Poke Portal News: four BCAT files the game copies into the save (enemies, fixed
 * and lottery rewards, priority; NOTES "Event raids"). This mod makes those files and hands them to the game's own
 * setters (the exe's export violet_raid_deliver, raidgem.c), so nothing waits for a restart:
 *   - from PKHeX.Core's lists of past events (<build>\mods\events.txt, `svedit events`): pick raids, and the mod
 *     writes an enemy file of them (battle settings and rewards from the game's own raid tables of that star level,
 *     <build>\mods\raid_templates.txt from raiddata.py) and a priority file;
 *   - from delivery files as BCAT has them (<build>\events\<name>\, the four *_3_0_0 files, also under raid\):
 *     written as they are. The delivery the save had when the mod first saw it is kept the same way
 *     (backup-<identifier>), so it can be put back.
 * A new identifier (priority versionNo) makes the game reroll the dens with the new events, as when a real delivery
 * comes in; keeping the identifier only rebuilds the dens, and event dens pick again from the new entries. The
 * raids mod (F6) then shows event dens' raids and finds seeds for them. */
#include "delivery.h"
#include "filtercombo.h"
#include "modkit.h"
#include <algorithm>
#include <ctype.h>
#include <atomic>
#include <stdio.h>
#include <string.h>
#include <string>
#include <time.h>
#include <vector>
#include <windows.h>

using namespace delivery;

/* ---------------------------------------------------------------------------------------------- data */
static std::string g_dir;                       /* <build> */
static std::string g_error;
static std::vector<std::string> g_species, g_moves, g_abilities, g_natures, g_types;   /* raids.dat: by DevID etc. */

struct Event {                                  /* a line of events.txt */
    char kind;                                  /* 'D' 1-6 stars, 'M' 7 stars */
    int index, species, dev, form, stars, level, rom, rate, gem, rare, tokusei, seikaku, sex, vnum, ivs[6], moves[4],
        scale_type, scale, stage_min, stage_max;
};
static std::vector<Event> g_events;

enum { TEMPLATE_NUMS = 54 };
static bool g_have_template[8];
static u64 g_template[8][TEMPLATE_NUMS];        /* by stars 1-6 (raiddata.py's templates()) */

static const char *name_of(const std::vector<std::string> &v, int i) {
    return i >= 0 && i < (int)v.size() && !v[i].empty() ? v[i].c_str() : "?";
}

static std::vector<u8> read_file(const std::string &path) {
    std::vector<u8> b;
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) return b;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n > 0) {
        b.resize((size_t)n);
        if (fread(b.data(), 1, b.size(), f) != b.size()) b.clear();
    }
    fclose(f);
    return b;
}

static bool write_file(const std::string &path, const std::vector<u8> &b) {
    FILE *f = fopen(path.c_str(), "wb");
    if (!f) return false;
    bool ok = fwrite(b.data(), 1, b.size(), f) == b.size();
    return fclose(f) == 0 && ok;
}

static void load_names() {
    std::vector<u8> b = read_file(g_dir + "\\mods\\raids.dat");
    if (b.size() < 8 || memcmp(b.data(), "NXRD", 4)) { g_error = "no build\\mods\\raids.dat: run mods\\raids\\raiddata.py"; return; }
    size_t p = 8;
    std::vector<std::string> *lists[] = {&g_species, &g_moves, &g_abilities, &g_natures, &g_types};
    for (auto *l : lists) {
        if (p + 4 > b.size()) return;
        u32 n;
        memcpy(&n, &b[p], 4);
        p += 4;
        for (u32 i = 0; i < n && p + 2 <= b.size(); i++) {
            u16 len = (u16)(b[p] | b[p + 1] << 8);
            p += 2;
            if (p + len > b.size()) return;
            l->emplace_back((const char *)&b[p], (size_t)len);
            p += len;
        }
    }
}

static void load_events() {
    FILE *f = fopen((g_dir + "\\mods\\events.txt").c_str(), "r");
    if (!f) { g_error = "no build\\mods\\events.txt: run `dotnet run -- events ..\\..\\..\\build\\mods` in violetrecomp\\tools\\svedit"; return; }
    char line[1024];
    std::vector<std::string> cols;
    if (fgets(line, sizeof line, f))
        for (char *t = strtok(line, "\t\r\n"); t; t = strtok(nullptr, "\t\r\n")) cols.push_back(t);
    auto col = [&](const char *name) {
        for (size_t i = 0; i < cols.size(); i++) if (cols[i] == name) return (int)i;
        return -1;
    };
    static const char *const names[] = {"index", "species", "dev", "form", "stars", "level", "rom", "rate", "gem",
                                        "rare", "tokusei", "seikaku", "sex", "vnum", "iv_hp", "iv_atk", "iv_def",
                                        "iv_spa", "iv_spd", "iv_spe", "move1", "move2", "move3", "move4",
                                        "scale_type", "scale", "stage_min", "stage_max"};
    const int N = (int)(sizeof names / sizeof *names);
    int at[N];
    for (int i = 0; i < N; i++)
        if ((at[i] = col(names[i])) < 0) { g_error = std::string("events.txt has no column ") + names[i] + ": export it again"; fclose(f); return; }
    while (fgets(line, sizeof line, f)) {
        std::vector<std::string> v;
        for (char *t = strtok(line, "\t\r\n"); t; t = strtok(nullptr, "\t\r\n")) v.push_back(t);
        if (v.size() < cols.size()) continue;
        int x[N];
        for (int i = 0; i < N; i++) x[i] = atoi(v[at[i]].c_str());
        Event e = {v[0][0], x[0], x[1], x[2], x[3], x[4], x[5], x[6], x[7], x[8], x[9], x[10], x[11], x[12], x[13],
                   {x[14], x[15], x[16], x[17], x[18], x[19]}, {x[20], x[21], x[22], x[23]}, x[24], x[25], x[26], x[27]};
        g_events.push_back(e);
    }
    fclose(f);
}

static void load_templates() {
    FILE *f = fopen((g_dir + "\\mods\\raid_templates.txt").c_str(), "r");
    if (!f) { g_error = "no build\\mods\\raid_templates.txt: run mods\\raids\\raiddata.py"; return; }
    char line[2048];
    while (fgets(line, sizeof line, f)) {
        char *s = line, *end;
        long stars = strtol(s, &end, 10);
        if (end == s || stars < 1 || stars > 6) continue;
        s = end;
        int k = 0;
        for (; k < TEMPLATE_NUMS; k++) {
            unsigned long long v = strtoull(s, &end, 10);
            if (end == s) break;
            g_template[stars][k] = v;
            s = end;
        }
        g_have_template[stars] = k == TEMPLATE_NUMS;
    }
    fclose(f);
}

/* ---------------------------------------------------------------------------------------------- when events ran */
/* event_dates.csv (hand-made from Serebii's event list, next to the DLL in violet_events\): one row per run, Pokemon
 * and star level. Columns start, end (YYYY-MM-DD), event, species, form (empty: normal), stars, tera (empty: random
 * or not given), version (both / Scarlet / Violet), shiny (yes: always), pkhex (optional: D<index> / M<index>, the
 * kind and index of events.txt, shown in the Pokemon column's tooltip: only raids with that index). A row matches
 * every raid of the same species and stars whose form, Tera, game and shininess don't contradict it, so one raid can
 * show several runs (and spotlights of one species several raids). */
struct Run { std::string start, end, event, species, form, tera, version, shiny, pin; int stars; };
static std::vector<Run> g_runs;
static std::vector<std::vector<int>> g_ev_runs;  /* by event: indices into g_runs, by start date */
static std::string g_dates_note;

static std::string norm(const std::string &s) { /* lower case letters and digits only */
    std::string o;
    for (unsigned char c : s) if (isalnum(c)) o += (char)tolower(c);
    return o;
}

static std::vector<std::string> csv_fields(const std::string &line) {
    std::vector<std::string> v(1);
    bool q = false;
    for (size_t i = 0; i < line.size(); i++) {
        char c = line[i];
        if (q) {
            if (c == '"' && i + 1 < line.size() && line[i + 1] == '"') { v.back() += '"'; i++; }
            else if (c == '"') q = false;
            else v.back() += c;
        } else if (c == '"') q = true;
        else if (c == ',') v.emplace_back();
        else if (c != '\r' && c != '\n') v.back() += c;
    }
    return v;
}

static std::string tera_name(int gem);

static bool run_matches(const Run &r, const Event &ev) {
    if (!r.pin.empty()) {
        char want[16];
        snprintf(want, sizeof want, "%c%d", ev.kind, ev.index);
        if (norm(r.pin) != norm(want)) return false;
    }
    if (r.stars != ev.stars || norm(r.species) != norm(name_of(g_species, ev.dev))) return false;
    if (r.form.empty() != (ev.form == 0)) return false;
    if (!r.tera.empty() && (ev.gem < 2 || norm(r.tera) != norm(tera_name(ev.gem)))) return false;
    std::string v = norm(r.version);
    if (ev.rom && ((v == "scarlet" && ev.rom != 1) || (v == "violet" && ev.rom != 2))) return false;
    return (norm(r.shiny) == "yes") == (ev.rare == 2);
}

static void load_dates() {
    char dll[MAX_PATH] = "";
    HMODULE self = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCSTR)&load_dates, &self);
    GetModuleFileNameA(self, dll, sizeof dll);
    std::string path = dll;
    path = path.substr(0, path.find_last_of('\\') + 1) + "violet_events\\event_dates.csv";
    FILE *f = fopen(path.c_str(), "r");
    if (!f) { g_dates_note = "no " + path + ": no dates"; return; }
    char line[1024];
    std::vector<std::string> cols;
    auto col = [&](const char *name) {
        for (size_t i = 0; i < cols.size(); i++) if (norm(cols[i]) == name) return (int)i;
        return -1;
    };
    int at[10];
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#' || line[0] == '\r' || line[0] == '\n') continue;
        std::vector<std::string> v = csv_fields(line);
        if (cols.empty()) {
            cols = v;
            const char *names[10] = {"start", "end", "event", "species", "form", "stars", "tera", "version", "shiny", "pkhex"};
            for (int i = 0; i < 10; i++) at[i] = col(names[i]);
            if (at[0] < 0 || at[3] < 0 || at[5] < 0) { g_dates_note = path + ": no start, species or stars column"; break; }
            continue;
        }
        auto get = [&](int i) { return at[i] >= 0 && at[i] < (int)v.size() ? v[at[i]] : std::string(); };
        Run r = {get(0), get(1), get(2), get(3), get(4), get(6), get(7), get(8), get(9), atoi(get(5).c_str())};
        if (r.start.size() >= 10 && r.stars) g_runs.push_back(r);
    }
    fclose(f);
    std::sort(g_runs.begin(), g_runs.end(), [](const Run &a, const Run &b) { return a.start < b.start; });

    g_ev_runs.assign(g_events.size(), {});
    std::vector<bool> used(g_runs.size());
    int dated = 0;
    for (size_t e = 0; e < g_events.size(); e++) {
        auto &l = g_ev_runs[e];
        for (size_t k = 0; k < g_runs.size(); k++) {
            if (!run_matches(g_runs[k], g_events[e])) continue;
            used[k] = true;
            bool dup = false;                   /* the same run listed twice (random and fixed Tera rows) */
            for (int j : l) dup |= g_runs[j].start == g_runs[k].start && g_runs[j].event == g_runs[k].event;
            if (!dup) l.push_back((int)k);
        }
        dated += !l.empty();
    }
    int unused = 0;
    for (size_t k = 0; k < g_runs.size(); k++)
        if (!used[k]) {
            if (unused++ < 20)
                modkit::host()->log(1, "events: no raid of PKHeX's list for %s %s %d* %s (%s)", g_runs[k].start.c_str(),
                                    g_runs[k].species.c_str(), g_runs[k].stars, g_runs[k].tera.c_str(), g_runs[k].event.c_str());
        }
    char note[160];
    snprintf(note, sizeof note, "%d of %zu raids have dates; %d of %zu date rows match no raid", dated, g_events.size(),
             unused, g_runs.size());
    g_dates_note = note;
    modkit::host()->log(0, "events: %s", note);
}

/* the game's clock at the next start: <build>\clock.txt (the runtime's time service reads it at boot; NX_TIME wins) */
static std::string clock_path() { return g_dir + "\\clock.txt"; }

static long long clock_next() {                 /* 0: real time */
    FILE *f = fopen(clock_path().c_str(), "r");
    if (!f) return 0;
    char b[32] = "";
    long long t = fgets(b, sizeof b, f) ? strtoll(b, nullptr, 10) : 0;
    fclose(f);
    return t;
}

/* ---------------------------------------------------------------------------------------------- deliveries */
static int g_rom = 2;                           /* 1 Scarlet, 2 Violet */
static const int HP_7_STARS = 3500;             /* bossDesc hpCoef of 7-star events (the 2023 Rillaboom's) */

/* an event from PKHeX's list as a delivered entry: the mon from the list, the rest from the game's own raid of that
 * star level */
static Entry entry_of(const Event &ev, int version, int n, bool any_game) {
    Entry e = {};
    int ts = ev.stars < 6 ? ev.stars : 6;
    const u64 *t = g_template[ts];
    e.rom = any_game ? 0 : ev.rom;
    e.no = version * 100 + n;
    e.group = 1;
    e.difficulty = ev.stars;
    e.rate = ev.rate < 127 ? ev.rate : 127;
    e.drop_fix = t[0];
    e.drop_rnd = t[1];
    e.capture_rate = (int)t[2];
    e.capture_lv = ev.level;
    for (int k = 0; k < 6; k++) e.size[k] = (int)t[4 + k];
    for (int k = 0; k < 10; k++) e.desc[k] = (int)t[10 + k];
    for (int x = 0; x < EXTRAS; x++)
        for (int k = 0; k < 4; k++) e.extra[x][k] = (int)t[20 + 4 * x + k];
    for (int k = 0; k < 3; k++) e.desc[16 + k] = (int)t[44 + k];
    for (int k = 0; k < TIME_FIELDS; k++) e.time[k] = (int)t[47 + k];
    if (ev.stars >= 7 && e.desc[0] < HP_7_STARS) e.desc[0] = HP_7_STARS;
    e.dev = ev.dev;
    e.form = ev.form;
    e.sex = ev.sex;
    e.level = ev.level;
    bool moves = false;
    for (int k = 0; k < 4; k++) { e.moves[k] = ev.moves[k]; moves |= ev.moves[k] != 0; }
    e.waza_type = moves ? 1 : 0;
    e.gem = ev.gem;
    e.seikaku = ev.seikaku;
    e.tokusei = ev.tokusei;
    if (ev.ivs[0] >= 0) {
        e.talent_type = 2;
        for (int k = 0; k < 6; k++) e.ivs[k] = ev.ivs[k];
    } else if (ev.vnum > 0) {
        e.talent_type = 1;
        e.vnum = ev.vnum;
    }
    e.rare = ev.rare;
    e.scale_type = ev.scale_type;
    e.scale = ev.scale;
    return e;
}

static std::string g_status;
static int g_pending = -1;                      /* deliveries applied count we wait to go past (-1: none) */

static void deliver_parts(const std::vector<u8> parts[PARTS], const char *what) {
    const void *p[PARTS];
    for (int i = 0; i < PARTS; i++) p[i] = parts[i].empty() ? nullptr : parts[i].data();
    int r = deliver(p);
    if (r == -2) { g_status = "This build of the game has no delivery export (raidgem.c): rebuild it."; return; }
    if (r < 0) { g_status = "Not in the field yet: load the save and walk around first."; return; }
    g_pending = r;
    g_status = std::string(what) + ": handed to the game, applied at its next raid update.";
    modkit::host()->log(0, "events: %s", what);
}

/* the current delivery (read from the save in memory) */
struct Current {
    bool have;
    u32 ident;
    int version, groups[10];
    std::vector<Entry> entries;
    std::vector<u8> parts[PARTS];
};
static Current g_cur;
static ULONGLONG g_cur_read;

static void read_current(bool force) {
    ULONGLONG now = GetTickCount64();
    if (!force && now - g_cur_read < 2000) return;
    g_cur_read = now;
    Current c = {};
    std::vector<u8> id;
    if (read_part(IDENT, id) != 4) { g_cur.have = false; return; }
    memcpy(&c.ident, id.data(), 4);
    for (int i = 0; i < PARTS; i++) read_part(i, c.parts[i]);
    c.entries = parse_enemies(c.parts[ENEMY].data(), c.parts[ENEMY].size());
    if (!parse_priority(c.parts[PRIORITY].data(), c.parts[PRIORITY].size(), &c.version, c.groups)) c.version = 0;
    c.have = !c.parts[ENEMY].empty();
    g_cur = std::move(c);
}

/* the delivery the save had when first seen, kept as BCAT files (once per identifier) */
static void backup_current() {
    static bool done;
    if (done || !g_cur.have) return;
    done = true;
    char name[64];
    snprintf(name, sizeof name, "backup-%u", g_cur.ident);
    std::string dir = g_dir + "\\events";
    CreateDirectoryA(dir.c_str(), nullptr);
    dir += "\\";
    dir += name;
    if (GetFileAttributesA(dir.c_str()) != INVALID_FILE_ATTRIBUTES) return;
    CreateDirectoryA(dir.c_str(), nullptr);
    int n = 0;
    for (int i = 0; i < PARTS; i++)
        if (!g_cur.parts[i].empty() && write_file(dir + "\\" + part_file[i], g_cur.parts[i])) n++;
    modkit::host()->log(0, "events: the save's delivery (identifier %u) kept in %s (%d files)", g_cur.ident, dir.c_str(), n);
}

/* packages: <build>\events\<name>\ with the BCAT files (directly or in raid\) */
struct Package { std::string name, dir; };
static std::vector<Package> g_packages;

static void scan_packages() {
    g_packages.clear();
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((g_dir + "\\events\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == '.') continue;
        std::string d = g_dir + "\\events\\" + fd.cFileName;
        for (const char *sub : {"", "\\raid"})
            if (GetFileAttributesA((d + sub + "\\" + part_file[ENEMY]).c_str()) != INVALID_FILE_ATTRIBUTES) {
                g_packages.push_back({fd.cFileName, d + sub});
                break;
            }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    std::sort(g_packages.begin(), g_packages.end(), [](const Package &a, const Package &b) { return a.name < b.name; });
}

static void deliver_package(const Package &p) {
    std::vector<u8> parts[PARTS];
    for (int i = 0; i < PARTS; i++) {
        std::vector<u8> b = read_file(p.dir + "\\" + part_file[i]);
        if (b.empty()) continue;            /* missing: the save keeps that file */
        if (b.size() > part_size[i]) {
            g_status = std::string(part_file[i]) + " of " + p.name + " is larger than the save's room for it";
            return;
        }
        b.resize(part_size[i], 0);
        parts[i] = std::move(b);
    }
    if (parse_enemies(parts[ENEMY].data(), parts[ENEMY].size()).empty()) {
        g_status = p.name + ": its enemy file doesn't read as a delivery";
        return;
    }
    deliver_parts(parts, (std::string("delivery ") + p.name).c_str());
}

/* ---------------------------------------------------------------------------------------------- UI */
static std::atomic<bool> g_open;
static std::vector<int> g_picked;               /* indices into g_events, in delivery order */
static bool g_any_game, g_new_ident = true;
static int g_event_dens = 5;

static const char *rare_name(int r) { return r == 1 ? "never" : r == 2 ? "always" : ""; }

static std::string tera_name(int gem) {
    if (gem == 0) return "species";
    if (gem == 1) return "random";
    if (gem == 101) return "Stellar";
    return name_of(g_types, gem - 2);
}

static const char *game_name(int rom) { return rom == 1 ? "Scarlet" : rom == 2 ? "Violet" : ""; }

static void current_ui() {
    read_current(false);
    backup_current();
    if (!g_cur.have) { ImGui::TextDisabled("No delivery read yet: load the save and walk around (the raid crystals need to be loaded)."); return; }
    int real = 0;
    for (auto &e : g_cur.entries) real += e.rate > 0;
    ImGui::Text("Identifier %u, %d raid(s) delivered; event dens per reroll: group 1 %d", g_cur.ident, real, g_cur.groups[0]);
    if (ImGui::BeginTable("cur", 7, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV |
                                        ImGuiTableFlags_SizingFixedFit, ImVec2(0, 140))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        for (const char *h : {"Stars", "Pokemon", "Lv.", "Tera", "Shiny", "Rate", "Game"}) ImGui::TableSetupColumn(h);
        ImGui::TableHeadersRow();
        for (auto &e : g_cur.entries) {
            if (e.rate <= 0) continue;
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::Text("%d", e.difficulty);
            ImGui::TableNextColumn(); ImGui::TextUnformatted(name_of(g_species, e.dev));
            ImGui::TableNextColumn(); ImGui::Text("%d", e.level);
            ImGui::TableNextColumn(); ImGui::TextUnformatted(tera_name(e.gem).c_str());
            ImGui::TableNextColumn(); ImGui::TextUnformatted(rare_name(e.rare));
            ImGui::TableNextColumn(); ImGui::Text("%d", e.rate);
            ImGui::TableNextColumn(); ImGui::TextUnformatted(game_name(e.rom));
        }
        ImGui::EndTable();
    }
}

static char g_clock[24];                         /* "YYYY-MM-DD HH:MM", local time */

static void set_clock_from(const Run &r) { snprintf(g_clock, sizeof g_clock, "%s 12:00", r.start.substr(0, 10).c_str()); }

static std::string run_text(const Run &r) {
    std::string end = r.end.size() >= 10 && r.end.compare(0, 4, r.start, 0, 4) == 0 ? r.end.substr(5) : r.end;
    return r.start + (r.end.empty() || r.end == r.start ? "" : " to " + end) + "  " + r.event;
}

/* the Ran cell: the first run (+ how many more); click: pick a run for the clock */
static void ran_cell(int i) {
    const auto &l = g_ev_runs.empty() ? std::vector<int>() : g_ev_runs[i];
    if (l.empty()) { ImGui::TextDisabled("-"); return; }
    char label[48];
    if (l.size() > 1) snprintf(label, sizeof label, "%s +%zu##ran", g_runs[l[0]].start.c_str(), l.size() - 1);
    else snprintf(label, sizeof label, "%s##ran", g_runs[l[0]].start.c_str());
    ImGui::PushID(i);
    if (ImGui::Selectable(label, false, ImGuiSelectableFlags_None, ImVec2(ImGui::CalcTextSize(label, nullptr, true).x, 0))) {
        if (l.size() == 1) set_clock_from(g_runs[l[0]]);
        else ImGui::OpenPopup("runs");
    }
    if (ImGui::IsItemHovered()) {
        std::string t;
        for (int k : l) t += run_text(g_runs[k]) + "\n";
        ImGui::SetTooltip("%s", t.c_str());
    }
    if (ImGui::BeginPopup("runs")) {
        ImGui::TextDisabled("Set the clock to:");
        for (int k : l)
            if (ImGui::Selectable(run_text(g_runs[k]).c_str())) set_clock_from(g_runs[k]);
        ImGui::EndPopup();
    }
    ImGui::PopID();
}

static void clock_ui() {
    long long next = clock_next();
    if (next > 0) {
        time_t t = (time_t)next;
        char b[32];
        strftime(b, sizeof b, "%Y-%m-%d %H:%M", localtime(&t));
        ImGui::TextColored(ImVec4(1, 0.8f, 0.3f, 1), "Every start: the game's clock starts at %s (clock.txt).", b);
        ImGui::SameLine();
        if (ImGui::SmallButton("Back to real time")) DeleteFileA(clock_path().c_str());
    } else ImGui::TextDisabled("The game's clock is the real one.");
    ImGui::SetNextItemWidth(160);
    ImGui::InputTextWithHint("##clock", "YYYY-MM-DD HH:MM", g_clock, sizeof g_clock);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Your time zone. A date in the Ran column fills this in (noon on the first day).");
    ImGui::SameLine();
    struct tm tm = {};
    int n = sscanf(g_clock, "%d-%d-%d %d:%d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday, &tm.tm_hour, &tm.tm_min);
    bool ok = n >= 3 && tm.tm_year >= 2000 && tm.tm_mon >= 1 && tm.tm_mon <= 12 && tm.tm_mday >= 1 && tm.tm_mday <= 31;
    ImGui::BeginDisabled(!ok);
    if (ImGui::Button("Start the clock there from the next start")) {
        tm.tm_year -= 1900;
        tm.tm_mon -= 1;
        tm.tm_isdst = -1;
        long long t = (long long)mktime(&tm);
        FILE *f = fopen(clock_path().c_str(), "w");
        if (f && t > 0) {
            fprintf(f, "%lld\n# The game's clock starts here at every start (the events mod's \"Start the clock there\").\n"
                       "# Delete this file, or \"Back to real time\" in the Events window, for the real clock.\n", t);
            g_status = "Restart the game: its clock then starts at " + std::string(g_clock) + " (every start, until you go back to real time).";
        } else g_status = "Couldn't write " + clock_path();
        if (f) fclose(f);
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("A caught Pokemon's met date is the game's date. The clock runs on from there, and it starts at the\n"
                          "same moment at every start until you go back to real time. A new day rerolls the raid dens.");
}

static void picker_ui() {
    static char filter[48];
    static int stars_filter = 0;                /* 0 all, 1-7 */
    ImGui::SetNextItemWidth(200);
    ImGui::InputTextWithHint("##evfilter", "Pokemon name", filter, sizeof filter);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120);
    ImGui::Combo("Stars", &stars_filter, "all\0" "1\0" "2\0" "3\0" "4\0" "5\0" "6\0" "7\0");
    ImGui::SameLine();
    ImGui::TextDisabled("(%zu event raids from PKHeX)", g_events.size());
    if (!g_dates_note.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", g_dates_note.c_str());
    ImGui::SameLine();
    bool tick_all = ImGui::SmallButton("Tick all shown");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Ticks every raid the filter shows, in the table's order (all 230 fit in one delivery).");
    enum { C_PICK, C_STARS, C_MON, C_LV, C_TERA, C_SHINY, C_RATE, C_GAME, C_RAN, C_MOVES, C_COUNT };
    if (ImGui::BeginTable("ev", C_COUNT, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV |
                                             ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_Sortable |
                                             ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable |
                                             ImGuiTableFlags_Hideable, ImVec2(0, 220))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        const char *heads[C_COUNT] = {"Deliver", "Stars", "Pokemon", "Lv.", "Tera", "Shiny", "Rate", "Game", "Ran", "Moves"};
        for (int c = 0; c < C_COUNT; c++)
            ImGui::TableSetupColumn(heads[c], c == C_STARS ? ImGuiTableColumnFlags_DefaultSort
                                              : c == C_MOVES ? ImGuiTableColumnFlags_NoSort : 0);
        static const char pick_tip[] = "Ticked raids go into the next \"Deliver the picked raids\" (below the table),\n"
                                       "in the order you ticked them.";
        ImGui::TableNextRow(ImGuiTableRowFlags_Headers);       /* TableHeadersRow, plus a tooltip on "Deliver" */
        for (int c = 0; c < C_COUNT; c++) {
            if (!ImGui::TableSetColumnIndex(c)) continue;
            ImGui::TableHeader(ImGui::TableGetColumnName(c));
            if (c == C_PICK && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", pick_tip);
            if (c == C_RAN && ImGui::IsItemHovered())
                ImGui::SetTooltip("When the event ran (Serebii; event_dates.csv). Click a date to set the game's clock to it.");
        }

        std::vector<int> rows;
        for (int i = 0; i < (int)g_events.size(); i++) {
            const Event &ev = g_events[i];
            if (stars_filter && ev.stars != stars_filter) continue;
            if (!filter_match(name_of(g_species, ev.dev), filter)) continue;
            rows.push_back(i);
        }
        if (ImGuiTableSortSpecs *ss = ImGui::TableGetSortSpecs(); ss && ss->SpecsCount > 0) {
            int col = ss->Specs[0].ColumnIndex;
            bool desc = ss->Specs[0].SortDirection == ImGuiSortDirection_Descending;
            auto picked = [](int i) { return std::find(g_picked.begin(), g_picked.end(), i) != g_picked.end(); };
            auto key = [&](int ia, int ib) -> int {
                const Event &a = g_events[ia], &b = g_events[ib];
                auto cmp = [](int x, int y) { return x < y ? -1 : x > y ? 1 : 0; };
                switch (col) {
                case C_PICK: return cmp(picked(ib), picked(ia));
                case C_STARS: return cmp(a.stars, b.stars);
                case C_MON: { int c = strcmp(name_of(g_species, a.dev), name_of(g_species, b.dev)); return c ? c : cmp(a.form, b.form); }
                case C_LV: return cmp(a.level, b.level);
                case C_TERA: return tera_name(a.gem).compare(tera_name(b.gem));
                case C_SHINY: return cmp(b.rare == 2, a.rare == 2);
                case C_RATE: return cmp(a.rate, b.rate);
                case C_GAME: return cmp(a.rom, b.rom);
                case C_RAN: {                   /* by first run; undated last */
                    const auto &ra = g_ev_runs[ia], &rb = g_ev_runs[ib];
                    if (ra.empty() || rb.empty()) return cmp(ra.empty(), rb.empty());
                    return g_runs[ra[0]].start.compare(g_runs[rb[0]].start);
                }
                }
                return 0;
            };
            std::stable_sort(rows.begin(), rows.end(), [&](int a, int b) {
                int c = key(a, b);
                if (!c) return a < b;
                return desc ? c > 0 : c < 0;
            });
        }
        if (tick_all)
            for (int i : rows)
                if (std::find(g_picked.begin(), g_picked.end(), i) == g_picked.end()) g_picked.push_back(i);
        for (int i : rows) {
            const Event &ev = g_events[i];
            const char *nm = name_of(g_species, ev.dev);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            bool on = std::find(g_picked.begin(), g_picked.end(), i) != g_picked.end();
            ImGui::PushID(i);
            if (ImGui::Checkbox("##pick", &on)) {
                if (on) g_picked.push_back(i);
                else g_picked.erase(std::find(g_picked.begin(), g_picked.end(), i));
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", pick_tip);
            ImGui::PopID();
            ImGui::TableNextColumn(); ImGui::Text("%d", ev.stars);
            ImGui::TableNextColumn(); ImGui::Text("%s%s", nm, ev.form ? " (form)" : "");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("PKHeX: %c%d (event_dates.csv's pkhex column narrows a date row to this index)", ev.kind, ev.index);
            ImGui::TableNextColumn(); ImGui::Text("%d", ev.level);
            ImGui::TableNextColumn(); ImGui::TextUnformatted(tera_name(ev.gem).c_str());
            ImGui::TableNextColumn(); ImGui::TextUnformatted(rare_name(ev.rare));
            ImGui::TableNextColumn(); ImGui::Text("%d", ev.rate);
            ImGui::TableNextColumn();
            if (ev.rom && ev.rom != g_rom) ImGui::TextColored(ImVec4(1, 0.6f, 0.3f, 1), "%s", game_name(ev.rom));
            else ImGui::TextUnformatted(game_name(ev.rom));
            ImGui::TableNextColumn();
            ran_cell(i);
            ImGui::TableNextColumn();
            ImGui::Text("%s, %s, %s, %s", name_of(g_moves, ev.moves[0]), name_of(g_moves, ev.moves[1]),
                        name_of(g_moves, ev.moves[2]), name_of(g_moves, ev.moves[3]));
        }
        ImGui::EndTable();
    }

    ImGui::Text("Picked: %zu", g_picked.size());
    if (!g_picked.empty()) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear")) g_picked.clear();
    }
    for (size_t k = 0; k < g_picked.size() && k < 8; k++) {
        const Event &ev = g_events[g_picked[k]];
        ImGui::SameLine();
        ImGui::TextDisabled("%s%s %d*", k ? "," : "", name_of(g_species, ev.dev), ev.stars);
    }
    if (g_picked.size() > 8) { ImGui::SameLine(); ImGui::TextDisabled("and %zu more", g_picked.size() - 8); }
    ImGui::Checkbox("Reroll the dens now (new identifier)", &g_new_ident);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("On: the game rerolls today's dens as when a delivery arrives, and some become event dens.\n"
                          "Off: only dens that already are event dens pick again from the new raids.");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100);
    ImGui::SliderInt("Event dens", &g_event_dens, 1, 40);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("The priority file's count for group 1 (all raids here are group 1): how many dens a reroll makes event dens.");
    ImGui::Checkbox("Let the other game's exclusives appear here", &g_any_game);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Marks every raid for both games. PKHeX would call a Scarlet-only raid caught in Violet illegal.");

    bool can = !g_picked.empty() && g_cur.have;
    for (int s = 1; s <= 6; s++) can &= g_have_template[s];
    ImGui::BeginDisabled(!can);
    if (ImGui::Button("Deliver the picked raids")) {
        int version = g_new_ident ? (int)(g_cur.ident % 2000000000u) + 1 : (int)g_cur.ident;
        if (version <= 0 || version > 21474835) version = 20990001;       /* no fits an int: version * 100 + n */
        if (g_new_ident && (u32)version == g_cur.ident) version++;
        std::vector<Entry> es;
        for (size_t k = 0; k < g_picked.size(); k++) es.push_back(entry_of(g_events[g_picked[k]], version, (int)k + 1, g_any_game));
        std::vector<u8> parts[PARTS];
        parts[ENEMY] = build_enemies(es);
        int groups[10] = {g_event_dens};
        parts[PRIORITY] = build_priority(version, groups);
        if (parts[ENEMY].empty() || parts[PRIORITY].empty()) g_status = "Too many raids for the save's room: pick fewer.";
        else {
            char what[96];
            snprintf(what, sizeof what, "%zu raid(s), identifier %d%s", es.size(), version, g_new_ident ? " (reroll)" : "");
            deliver_parts(parts, what);
        }
    }
    ImGui::EndDisabled();
    if (!g_cur.have && !g_picked.empty()) { ImGui::SameLine(); ImGui::TextDisabled("(not in the field yet)"); }
}

static void packages_ui() {
    static bool scanned;
    if (!scanned || ImGui::SmallButton("Look again")) { scan_packages(); scanned = true; }
    ImGui::SameLine();
    ImGui::TextDisabled("BCAT delivery folders in build\\events (backup-* are the save's own)");
    for (auto &p : g_packages) {
        ImGui::PushID(p.name.c_str());
        if (ImGui::SmallButton("Deliver")) deliver_package(p);
        ImGui::SameLine();
        ImGui::TextUnformatted(p.name.c_str());
        ImGui::PopID();
    }
    if (g_packages.empty()) ImGui::TextDisabled("none");
}

static void ui() {
    ImGui::SetNextWindowSize(ImVec2(860, 640), ImGuiCond_FirstUseEver);
    bool open = true;
    if (!ImGui::Begin("Events", &open)) { ImGui::End(); if (!open) g_open = false; return; }
    if (!open) g_open = false;
    if (!g_error.empty()) ImGui::TextColored(ImVec4(1, 0.5f, 0.3f, 1), "%s", g_error.c_str());
    if (g_pending >= 0 && deliver(nullptr) > g_pending) {
        g_pending = -1;
        read_current(true);
        g_status += " Applied.";
    }
    if (ImGui::BeginTabBar("tabs")) {
        if (ImGui::BeginTabItem("Tera raids")) {
            ImGui::SeparatorText("Delivered now");
            current_ui();
            ImGui::SeparatorText("Past events (PKHeX)");
            picker_ui();
            ImGui::SeparatorText("Game clock");
            clock_ui();
            ImGui::SeparatorText("Delivery files");
            packages_ui();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Outbreaks")) {
            ImGui::TextDisabled("Not yet: event outbreaks come next.");
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    if (!g_status.empty()) ImGui::TextWrapped("%s", g_status.c_str());
    ImGui::End();
}

/* ---------------------------------------------------------------------------------------------- the mod */
static bool is_open() { return g_open; }
static void toggle() { g_open = !g_open; if (g_open) read_current(true); }
static void mod_ui() { if (g_open) ui(); }

static const NxMod mod = {"Events", nullptr, nullptr, nullptr, "1",
                          "Choose the event Tera raids delivered to the save: past events (PKHeX's lists) or delivery "
                          "files. Needs build\\mods\\events.txt (svedit events) and raid_templates.txt (raiddata.py).",
                          mod_ui};

extern "C" __declspec(dllexport) const NxMod *nx_mod_init(const NxModHost *host) {
    if (host->version != NXMOD_VERSION) return NULL;
    if (strcmp(host->title_id, "01008F6008C5E000") && strcmp(host->title_id, "0100A3D008C5C000")) return NULL;
    g_rom = strcmp(host->title_id, "0100A3D008C5C000") ? 2 : 1;
    if (!modkit::init(host, "events")) return NULL;
    g_dir = host->cache_dir;
    load_names();
    if (g_error.empty()) load_events();
    if (g_error.empty()) load_templates();
    if (g_error.empty()) load_dates();
    modkit::window("Events", "F4", is_open, toggle);
    if (!g_error.empty()) host->log(0, "events: %s", g_error.c_str());
    else host->log(1, "events: %zu event raids, %zu species names", g_events.size(), g_species.size());
    return &mod;
}
