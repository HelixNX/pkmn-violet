// svedit events <dir>: PKHeX.Core's lists of past event Tera raids (Encounters9.Dist: 1-6 stars, Encounters9.Might:
// 7 stars) to <dir>\events.txt for the events mod (violetrecomp\mods\events). No save needed.
//
// events.txt: a header line of column names, then one raid a line, tab-separated, numbers in decimal. The values are
// already in the game's delivery terms (delivery_raid_enemy_array.bfbs, NOTES "Event raids"):
//   kind D / M, index (PKHeX's event slot), species (national), dev (the game's DevID), form, stars, level, rom (0 both, 1 Scarlet, 2 Violet),
//   rate (PKHeX's RandRate), gem (GemType: 0 the species' types, 1 random, 2 + type), rare (0 random, 1 never,
//   2 always), tokusei (0 1/2, 1 1/2/H, 2-4 fixed 1/2/H), seikaku (0 random, else nature + 1), sex (0 random, 1 male,
//   2 female), vnum (flawless IVs), iv_hp .. iv_spe (-1: rolled), move1..4, scale_type (0 random .. 6 value),
//   scale, stage_min, stage_max (PKHeX's progress stages: 0-3 for 1-5 star raids, 3-4 the postgame).
using System.Reflection;
using PKHeX.Core;

static class Events
{
    static readonly string[] Cols =
    {
        "kind", "index", "species", "dev", "form", "stars", "level", "rom", "rate", "gem", "rare", "tokusei", "seikaku", "sex",
        "vnum", "iv_hp", "iv_atk", "iv_def", "iv_spa", "iv_spd", "iv_spe", "move1", "move2", "move3", "move4",
        "scale_type", "scale", "stage_min", "stage_max",
    };

    static object? Get(object o, string name) =>
        o.GetType().GetProperty(name, BindingFlags.Public | BindingFlags.Instance)?.GetValue(o)
        ?? o.GetType().GetField(name, BindingFlags.Public | BindingFlags.Instance)?.GetValue(o);

    static long Num(object o, string name) => Get(o, name) is { } v ? Convert.ToInt64(v) : 0;

    public static int Write(string dir)
    {
        var t = typeof(EncounterDist9).Assembly.GetType("PKHeX.Core.Encounters9")!;
        const BindingFlags S = BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Static;
        var lists = new (string kind, Array arr)[]
        {
            ("D", (Array)t.GetField("Dist", S)!.GetValue(null)!),
            ("M", (Array)t.GetField("Might", S)!.GetValue(null)!),
        };
        Directory.CreateDirectory(dir);
        var outPath = Path.Combine(dir, "events.txt");
        using var w = new StreamWriter(outPath);
        w.WriteLine(string.Join('\t', Cols));
        int n = 0;
        foreach (var (kind, arr) in lists)
            foreach (var e in arr)
            {
                w.WriteLine(kind + "\t" + string.Join('\t', Row(e!)));
                n++;
            }
        Console.WriteLine($"{outPath}: {n} event raids ({lists[0].arr.Length} 1-6 stars, {lists[1].arr.Length} 7 stars)");
        return 0;
    }

    static IEnumerable<long> Row(object e)
    {
        long rom = Get(e, "Version") is GameVersion v ? v switch { GameVersion.SL => 1, GameVersion.VL => 2, _ => 0 } : 0;
        long rare = Get(e, "Shiny") is Shiny sh ? sh switch { Shiny.Random => 0, Shiny.Never => 1, _ => 2 } : 0;
        long tokusei = Get(e, "Ability") is AbilityPermission ap ? ap switch
        {
            AbilityPermission.Any12 => 0,
            AbilityPermission.Any12H => 1,
            AbilityPermission.OnlyFirst => 2,
            AbilityPermission.OnlySecond => 3,
            AbilityPermission.OnlyHidden => 4,
            _ => 0,
        } : 0;
        long nature = Num(e, "Nature");
        long seikaku = nature is >= 0 and < 25 ? nature + 1 : 0;
        long gender = Num(e, "Gender");
        long sex = gender switch { 0 => 1, 1 => 2, _ => 0 };
        var ivs = new long[] { -1, -1, -1, -1, -1, -1 };
        if (Get(e, "IVs") is { } iv && Get(iv, "IsSpecified") is true)
        {
            string[] names = { "HP", "ATK", "DEF", "SPA", "SPD", "SPE" };
            for (int i = 0; i < 6; i++) ivs[i] = Num(iv, names[i]);
        }
        var moves = new long[4];
        if (Get(e, "Moves") is { } mv)
            for (int i = 0; i < 4; i++) moves[i] = Num(mv, "Move" + (i + 1));
        yield return Num(e, "Index");
        yield return Num(e, "Species");
        yield return SpeciesConverter.GetInternal9((ushort)Num(e, "Species"));
        yield return Num(e, "Form");
        yield return Num(e, "Stars");
        yield return Num(e, "Level");
        yield return rom;
        yield return Num(e, "RandRate");
        yield return Num(e, "TeraType");
        yield return rare;
        yield return tokusei;
        yield return seikaku;
        yield return sex;
        yield return Num(e, "FlawlessIVCount");
        foreach (var x in ivs) yield return x;
        foreach (var x in moves) yield return x;
        yield return Num(e, "ScaleType");
        yield return Num(e, "Scale");
        yield return Num(e, "ProgressStageMin");
        yield return Num(e, "ProgressStageMax");
    }
}
