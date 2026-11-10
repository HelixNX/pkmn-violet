/* Weather: a mod of its own (violet_weather.dll, optional). F9 toggles rain; window "Weather" (F10) picks any
 * weather type, the transition time, or gives the weather back to the game. Both keys rebindable (Options >
 * Controls > Hotkeys).
 *
 * It goes through the cutscene timeline's weather commands in the game's script (host->run_script, the runner in
 * violetrecomp/lua.c), the class main.tml.TimeLineCommand = global CA71F0CDF966F3175 (Haxe -> Lua 5.4, names are
 * FNV-1a 64 with the game's basis 0xcbf29ce484222645; statics S<hash>, methods F<hash>: both are tried):
 *   - the timeline event "Title/Weather/WeatherChange/WeatherChange" (main+202f928) reads its WeatherType (1 when
 *     absent), calls CFunc_Tml_SetEnvChangeSpan(seconds) (8614855A6EF844D8), then main+202fa74, which calls this
 *     class's method A3EC462E45551FD6 (name stripped from the binary) with the type;
 *   - "WeatherReset" (main+202ff54) calls SetEnvChangeSpan, then CFunc_Tml_ResetWeather (ECAF51D6E7046362), and at
 *     its end CFunc_Tml_ResetChangeSpan (58CE0E7D1599C50E).
 * The type numbers are a guess, not read from the script yet: 1 is the default (normal), and the order follows the
 * encounter tables' weather flags (PKHeX AreaWeather9: normal, overcast, raining, thunderstorm, intense sun, snowing,
 * snowstorm, sandstorm, heavy fog = 1 << (type - 1)). The window has a raw number box to check them. */
#include "modkit.h"
#include <atomic>
#include <mutex>
#include <stdio.h>
#include <string.h>
#include <string>
#include <thread>
#include <windows.h>

namespace {

struct Kind { int type; const char *name; };
const Kind kinds[] = {{1, "Normal"}, {2, "Overcast"}, {3, "Rain"}, {4, "Thunderstorm"}, {5, "Intense sun"},
                      {6, "Snow"}, {7, "Snowstorm"}, {8, "Sandstorm"}, {9, "Heavy fog"}};
const int N_KINDS = (int)(sizeof kinds / sizeof *kinds);
const int RAIN = 3;

std::mutex g_status_lock;
std::string g_status = "The game's own weather.";
std::atomic<int> g_forced{0};               /* the type we last set; 0: the game's own weather */
std::atomic<int> g_gen{0};                  /* bumps on every change: a stale span reset is skipped */
std::atomic<bool> g_open;
int g_sel = 2;                              /* index into kinds: Rain */
int g_raw = RAIN;
float g_span = 2.0f;                        /* transition, seconds */

void set_status(const std::string &s) {
    modkit::host()->log(0, "weather: %s", s.c_str());
    std::lock_guard<std::mutex> l(g_status_lock);
    g_status = s;
}

bool have_runner() { return modkit::host()->version >= 3 && modkit::host()->run_script; }

/* a chunk with f(hash) = the class's static or method of that hash */
const char *const PRELUDE =
    "local c = CA71F0CDF966F3175\n"
    "if c == nil then return '" NXMOD_SCRIPT_AGAIN "' end\n"
    "local function f(h) return c['S' .. h] or c['F' .. h] end\n";

struct Job { std::string what; int forced; };

void job_done(void *ud, bool ok, const char *result) {
    Job *j = (Job *)ud;
    if (ok && !strcmp(result, "ok")) {
        g_forced = j->forced;
        set_status(j->what);
    } else {
        set_status(std::string("The game refused: ") + result);
    }
    delete j;
}

/* CFunc_Tml_ResetChangeSpan once the transition is over, unless something else changed the weather meanwhile */
void reset_span_later(float secs) {
    int gen = g_gen;
    std::thread([secs, gen] {
        Sleep((DWORD)(secs * 1000) + 1000);
        if (g_gen != gen) return;
        std::string text = std::string("--quiet\n") + PRELUDE +
                           "local r = f('58CE0E7D1599C50E') if r then r() end return 'ok'\n";
        modkit::host()->run_script(text.c_str(), nullptr, nullptr);
    }).detach();
}

bool send(const char *body, Job *j) {
    if (!have_runner()) { set_status("No script runner in this build (needs violetrecomp's lua.c)."); delete j; return false; }
    g_gen++;
    char head[128];
    snprintf(head, sizeof head, "local span = %.3f\n", g_span);
    std::string text = std::string(head) + PRELUDE + body;
    if (!modkit::host()->run_script(text.c_str(), job_done, j)) { set_status("The script queue is full."); delete j; return false; }
    set_status("Sent to the game...");
    reset_span_later(g_span);
    return true;
}

const char *kind_name(int type) {
    for (const Kind &k : kinds) if (k.type == type) return k.name;
    return nullptr;
}

void set_weather(int type) {
    char body[512];
    snprintf(body, sizeof body,
             "local set, sp = f('A3EC462E45551FD6'), f('8614855A6EF844D8')\n"
             "if set == nil then return 'no weather function in main.tml.TimeLineCommand' end\n"
             "if sp then sp(span) end\n"
             "set(%d)\n"
             "return 'ok'\n",
             type);
    const char *n = kind_name(type);
    std::string what = n ? std::string("Weather: ") + n + "." : "Weather: type " + std::to_string(type) + ".";
    send(body, new Job{what, type});
}

void reset_weather() {
    send("local r, sp = f('ECAF51D6E7046362'), f('8614855A6EF844D8')\n"
         "if r == nil then return 'no CFunc_Tml_ResetWeather' end\n"
         "if sp then sp(span) end\n"
         "r()\n"
         "return 'ok'\n",
         new Job{"The game's own weather.", 0});
}

/* F9: rain on, or back to the game's weather. Its menu entry never reports "open": an open window frees the mouse
 * (runtime/mods.c), and the rain is not a window. */
bool rain_on() { return g_forced == RAIN; }
bool rain_entry_open() { return false; }
void rain_toggle() {
    modkit::host()->log(0, "weather: rain toggle (rain %s now)", rain_on() ? "on" : "off");
    if (rain_on()) reset_weather();
    else set_weather(RAIN);
}

bool weather_open() { return g_open; }
void weather_toggle() { g_open = !g_open; }

void weather_ui() {
    if (!g_open) return;
    ImGui::SetNextWindowSize(ImVec2(360, 0), ImGuiCond_FirstUseEver);
    bool open = true;
    if (!ImGui::Begin("Weather", &open, ImGuiWindowFlags_AlwaysAutoResize)) { ImGui::End(); if (!open) g_open = false; return; }
    if (!open) g_open = false;
    if (!have_runner()) ImGui::TextWrapped("This build has no script runner: the mod cannot reach the game.");

    bool rain = rain_on();
    if (ImGui::Checkbox("Rain", &rain)) rain_toggle();
    ImGui::SameLine();
    ImGui::TextDisabled("(F9)");

    ImGui::SetNextItemWidth(160);
    if (ImGui::BeginCombo("##kind", kinds[g_sel].name)) {
        for (int i = 0; i < N_KINDS; i++)
            if (ImGui::Selectable(kinds[i].name, g_sel == i)) g_sel = i;
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("Set")) set_weather(kinds[g_sel].type);
    ImGui::SameLine();
    if (ImGui::Button("Game's own")) reset_weather();

    ImGui::SetNextItemWidth(160);
    ImGui::SliderFloat("Transition (s)", &g_span, 0.0f, 10.0f, "%.1f");

    if (ImGui::CollapsingHeader("Raw type number")) {
        ImGui::TextWrapped("The names above are a guess at the game's numbering; try numbers here and tell which is which.");
        ImGui::SetNextItemWidth(120);
        ImGui::InputInt("##raw", &g_raw);
        if (g_raw < 0) g_raw = 0;
        ImGui::SameLine();
        if (ImGui::Button("Set number")) set_weather(g_raw);
    }

    int f = g_forced;
    if (f) {
        const char *n = kind_name(f);
        if (n) ImGui::Text("Forced: %s (%d)", n, f);
        else ImGui::Text("Forced: type %d", f);
    }
    {
        std::lock_guard<std::mutex> l(g_status_lock);
        ImGui::TextWrapped("%s", g_status.c_str());
    }
    ImGui::End();
}

} // namespace

/* ---------------------------------------------------------------------------------------------- the mod */
static void mod_ui() { weather_ui(); }

static const NxMod mod = {"Weather", nullptr, nullptr, nullptr, "1", "Toggle rain (F9) or force any weather (window, F10) through the game's own cutscene weather commands; \"Game's own\" gives it back.", mod_ui};

extern "C" __declspec(dllexport) const NxMod *nx_mod_init(const NxModHost *host) {
    if (host->version != NXMOD_VERSION) return NULL;
    if (strcmp(host->title_id, "01008F6008C5E000") && strcmp(host->title_id, "0100A3D008C5C000")) return NULL;
    if (!modkit::init(host, "weather")) return NULL;
    modkit::window("Rain (toggle)", "F9", rain_entry_open, rain_toggle);
    modkit::window("Weather", "F10", weather_open, weather_toggle);
    return &mod;
}
