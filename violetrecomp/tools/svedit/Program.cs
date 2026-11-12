// svedit: Scarlet / Violet save edits through PKHeX.Core (NuGet 26.10.18 on .NET 11; was 25.12.21 on .NET 9 until 2026-11-11). The game must
// be closed: it saves over the file otherwise. Back the save folder up first.
//
//   dotnet run -- flags <save>\main <key hex>...   turn boolean save flags on (SCBlock keys, PKHeX's
//                                                  SaveBlockAccessor9SV names; = (uint)FnvHash.HashFnv1a_64(name))
//   dotnet run -- items <save>\main <out>          write items.txt for the item mod: "id \t pocket \t name"
//   dotnet run -- find <save>\main <regex>         list PKHeX's block names matching regex, their keys and state
//   dotnet run -- dump <save>\main <dir> <key>...   blocks' raw data to <dir>\<key>.bin
//   dotnet run -- copy <save>\main <other>\main <regex>  blocks named like regex from another save (event data:
//                                                  "^KBCAT|^KOutbreakBC": delivered raids / outbreaks, active BCAT outbreaks)
//   dotnet run -- monsdata <save>\main <dir>        the F8 mod's tables, <dir>\mons_*.txt (mons_eggs.txt: PKHeX-checked eggs)
//   dotnet run -- events <dir>                      PKHeX's past event raids for the events mod, <dir>\events.txt (Events.cs)
//   dotnet run -- check <save>\main <pk9> [<ID No.> <SID>]  what PKHeX reads from a PK9 the mod made, legality; with a
//                                                  trainer's ids, whether it is (hatches) shiny for them
//   dotnet run -- addmon<save>\main <species> <level> <nature> <ability 0-2> <nickname> <m1,m2,m3,m4> <hp/atk/def/spa/spd/spe EVs>
//                                                  a made-up mon (full IVs, the save's trainer), party or first free box slot
//
// Keys used so far: ride upgrades dash 0D707154, swim 76FF4AF0, high jump 54B8E8E1, glide 9DDF953F, climb 6ABB01F7,
// fly 3A9255B2, flight controls option E1F8DEBA; raid levels 3-6 EC95D8EF A9428DFE 9535F471 6E7F8220. Bag: KItem
// 21C9BD44 = 3000 x {u32 pocket, u32 count, u32 flags (1 new, 2 favourite, 4 obtained), u32 0}, by item id.
using System.Reflection;
using System.Text.RegularExpressions;
using PKHeX.Core;

if (args.Length < 2) { Console.Error.WriteLine("usage: flags <main> <key>... | items <main> <out> | find <main> <regex> | addmon <main> <species> <level> <nature> <ability> <nick> <moves> <evs> | events <dir>"); return 1; }
if (args[0] == "events") return Events.Write(args[1]);
var path = args[1];
var sav = (SAV9SV)SaveUtil.GetSaveFile(path)!;
switch (args[0])
{
    case "flags":
    {
        var keys = args.Skip(2).Select(k => Convert.ToUInt32(k, 16)).ToArray();
        foreach (var key in keys)
        {
            var b = sav.Accessor.GetBlock(key);
            var was = b.Type;
            if (b.Type == SCTypeCode.Bool1) b.ChangeBooleanType(SCTypeCode.Bool2);
            Console.WriteLine($"{key:X8}: {was} -> {b.Type}");
        }
        File.WriteAllBytes(path, sav.Write().ToArray());
        var check = (SAV9SV)SaveUtil.GetSaveFile(path)!;
        foreach (var key in keys) Console.WriteLine($"reloaded {key:X8}: {check.Accessor.GetBlock(key).Type}");
        return 0;
    }
    case "items":
    {
        var pocket = new Dictionary<InventoryType, int> {
            [InventoryType.Medicine] = 0, [InventoryType.Balls] = 1, [InventoryType.BattleItems] = 2, [InventoryType.Berries] = 3,
            [InventoryType.Items] = 4, [InventoryType.TMHMs] = 5, [InventoryType.Treasure] = 6, [InventoryType.Ingredients] = 7,
            [InventoryType.KeyItems] = 8, [InventoryType.Candy] = 9 };
        var lines = new List<string>();
        foreach (var p in sav.Inventory.Pouches)
            foreach (var id in p.GetAllItems())
                lines.Add($"{id}\t{pocket[p.Type]}\t{GameInfo.Strings.Item[id]}");
        File.WriteAllLines(args[2], lines);
        Console.WriteLine($"{lines.Count} items -> {args[2]}");
        return 0;
    }
    case "addmon":
    {
        // addmon <main> <species> <level> <nature> <ability index 0-2> <nickname> <move,move,move,move> <evs hp/atk/def/spa/spd/spe>
        // full IVs, the save's trainer as OT, Poke Ball; into the party, else the first free box slot
        var s = GameInfo.Strings;
        int Find(IReadOnlyList<string> list, string name, string what)
        {
            for (int i = 0; i < list.Count; i++)
                if (string.Equals(list[i], name, StringComparison.OrdinalIgnoreCase)) return i;
            throw new Exception($"no {what} named {name}");
        }
        var species = (ushort)Find(s.Species, args[2], "species");
        var pk = new PK9 { Species = species, Form = 0 };
        var pi = pk.PersonalInfo;
        pk.PID = Util.Rand32();
        pk.EncryptionConstant = Util.Rand32();
        pk.Version = GameVersion.VL;
        pk.Language = sav.Language;
        pk.OriginalTrainerName = sav.OT;
        pk.OriginalTrainerGender = sav.Gender;
        pk.ID32 = sav.ID32;
        pk.Ball = (byte)Ball.Poke;
        pk.MetLocation = 6;                 // South Province (Area One)
        pk.MetLevel = 1;
        pk.MetDate = DateOnly.FromDateTime(DateTime.Now);
        pk.CurrentLevel = byte.Parse(args[3]);
        var nature = Enum.Parse<Nature>(args[4], true);
        pk.Nature = nature;
        pk.StatAlignment = nature;
        pk.SetAbilityIndex(int.Parse(args[5]));
        pk.Gender = pk.GetSaneGender();
        pk.Nickname = args[6];
        pk.IsNicknamed = true;
        var moves = args[7].Split(',').Select(m => (ushort)Find(s.Move, m.Trim(), "move")).ToArray();
        pk.SetMoves(moves);
        pk.SetMaximumPPCurrent(moves);
        pk.SetIVs([31, 31, 31, 31, 31, 31]);
        var ev = args[8].Split('/').Select(int.Parse).ToArray();   // hp atk def spa spd spe
        pk.EV_HP = ev[0]; pk.EV_ATK = ev[1]; pk.EV_DEF = ev[2]; pk.EV_SPA = ev[3]; pk.EV_SPD = ev[4]; pk.EV_SPE = ev[5];
        pk.TeraTypeOriginal = (MoveType)pi.Type1;
        pk.TeraTypeOverride = (MoveType)TeraTypeUtil.OverrideNone;
        pk.HeightScalar = (byte)Util.Rand.Next(256);
        pk.WeightScalar = (byte)Util.Rand.Next(256);
        pk.Scale = (byte)Util.Rand.Next(256);
        pk.OriginalTrainerFriendship = pi.BaseFriendship;
        pk.HandlingTrainerName = "";
        pk.CurrentHandler = 0;
        pk.ResetPartyStats();
        pk.RefreshChecksum();
        if (sav.PartyCount < 6)
        {
            Console.WriteLine($"party slot {sav.PartyCount + 1}");
            sav.SetPartySlotAtIndex(pk, sav.PartyCount);
        }
        else
        {
            var slot = sav.NextOpenBoxSlot();
            if (slot < 0) throw new Exception("party and boxes full");
            sav.SetBoxSlotAtIndex(pk, slot);
            Console.WriteLine($"party full: box {slot / sav.BoxSlotCount + 1}, slot {slot % sav.BoxSlotCount + 1}");
        }
        Console.WriteLine($"{s.Species[species]} \"{pk.Nickname}\" Lv {pk.CurrentLevel} {pk.Nature} {s.Ability[pk.Ability]} " +
                          $"IVs {string.Join('/', IVs(pk))} EVs {pk.EV_HP}/{pk.EV_ATK}/{pk.EV_DEF}/{pk.EV_SPA}/{pk.EV_SPD}/{pk.EV_SPE} " +
                          $"moves {string.Join(", ", pk.Moves.Select(m => s.Move[m]))} stats {pk.Stat_HPMax}/{pk.Stat_ATK}/{pk.Stat_DEF}/{pk.Stat_SPA}/{pk.Stat_SPD}/{pk.Stat_SPE}");
        File.WriteAllBytes(path, sav.Write().ToArray());
        var check = (SAV9SV)SaveUtil.GetSaveFile(path)!;
        Console.WriteLine($"reloaded: party {check.PartyCount}: {string.Join(", ", check.PartyData.Select(p => p.Nickname))}");
        return 0;
    }
    case "probe":
    {
        // facts for the mod's own PK9 writer: box storage and slot layout, a mon's raw bytes both ways
        var box = sav.Accessor.GetBlock(0x0d66012c);
        Console.WriteLine($"box block {box.Data.Length:X} bytes, {box.Data.Length / 960.0} per slot of 960");
        var empty = box.Data.Slice(box.Data.Length - 0x158, 0x158).ToArray();
        Console.WriteLine($"last slot: {Convert.ToHexString(empty[..32])} nonzero {empty.Count(b => b != 0)}");
        var p = sav.PartyData[0];
        Console.WriteLine($"party0 {GameInfo.Strings.Species[p.Species]} raw species {BitConverter.ToUInt16(p.Data[8..])} " +
                          $"EC {p.EncryptionConstant:X8} ver {p.Version} {(int)p.Version} lang {p.Language} loc {p.MetLocation}");
        File.WriteAllBytes(args[2] + ".dec", p.Data.ToArray());
        File.WriteAllBytes(args[2] + ".enc", EncParty(p));
        return 0;
    }
    case "monsdata":
    {
        // monsdata <main> <dir>: the mon spawner's tables (<dir>\mons_*.txt, tab-separated), from PKHeX.Core's
        // personal / move / experience data (SV)
        var s = GameInfo.Strings;
        var dir = args[2];
        var sp = new List<string>();
        for (ushort n = 1; n <= 1025; n++)
        {
            var forms = FormConverter.GetFormList(n, s.Types, s.forms, GameInfo.GenderSymbolASCII, EntityContext.Gen9);
            var count = Math.Max(1, (int)PersonalTable.SV[n].FormCount);
            for (byte f = 0; f < count; f++)
            {
                var pi = PersonalTable.SV.GetFormEntry(n, f);
                if (!pi.IsPresentInGame) continue;
                var fname = f < forms.Length ? forms[f] : f.ToString();
                // national form internal name formname growth gender a1 a2 aH type1 type2 friendship hp atk def spa spd spe
                sp.Add(string.Join('\t', n, f, SpeciesConverter.GetInternal9(n), s.Species[n], fname, pi.EXPGrowth, pi.Gender,
                    pi.Ability1, pi.Ability2, pi.AbilityH, pi.Type1, pi.Type2, pi.BaseFriendship, pi.HP, pi.ATK, pi.DEF, pi.SPA, pi.SPD, pi.SPE));
            }
        }
        File.WriteAllLines(Path.Combine(dir, "mons_species.txt"), sp);
        // mons_eggs.txt, for the mod's Egg tab: each species / form that hatches from an egg in SV, with the facts the mod
        // copies. Each egg is built here with the rules PKHeX 26.10.18's legality check holds an unhatched SV egg to
        // (found 2026-11-11 with this save): the species' level-1 moves (the last four), relearn moves = those moves,
        // nickname flag set, the nickname buffer the species name with the egg name written over it (the rest is the
        // expected trash), egg date today and no met date, met location 0, egg location 30023 (picnic), level / met
        // level / obedience 1, Tera one of the species' types, hatch counter 1..its cycles (0 fails), never a Master or
        // Cherish Ball. Kept only if the check passes; each ball is tried too (the mask lists those it allows).
        //   national form cycles nickflag move1..4 relearn1..4 ballmask(hex, bit = ball id) nickname(26 bytes hex, the
        //   save's language)
        var eggs = new List<string>();
        int eggFail = 0;
        var today = DateOnly.FromDateTime(DateTime.Now);
        PK9 MakeEgg(ushort n, byte f)
        {
            var pi = PersonalTable.SV.GetFormEntry(n, f);
            var pk = new PK9 { Species = n, Form = f };
            pk.EncryptionConstant = Util.Rand32();
            pk.PID = Util.Rand32();
            pk.Version = sav.Version;
            pk.Language = sav.Language;
            pk.OriginalTrainerName = sav.OT;
            pk.OriginalTrainerGender = sav.Gender;
            pk.ID32 = sav.ID32;
            pk.CurrentLevel = 1;
            pk.Nature = Nature.Hardy;
            pk.StatAlignment = Nature.Hardy;
            pk.SetAbilityIndex(0);
            pk.Gender = pk.GetSaneGender();
            pk.SetIVs([31, 31, 31, 31, 31, 31]);
            var all = LearnSource9SV.Instance.GetLearnset(n, f).GetBaseEggMoves(1).ToArray();
            var moves = new ushort[4];
            all.AsSpan(0, Math.Min(4, all.Length)).CopyTo(moves);
            pk.SetMoves(moves);
            pk.HealPP();
            pk.RelearnMove1 = moves[0]; pk.RelearnMove2 = moves[1]; pk.RelearnMove3 = moves[2]; pk.RelearnMove4 = moves[3];
            pk.Nickname = SpeciesName.GetSpeciesNameGeneration(n, pk.Language, 9);     // under the egg name: the trash
            pk.IsEgg = true;
            pk.Nickname = SpeciesName.GetEggName(pk.Language, 9);
            pk.IsNicknamed = true;
            pk.OriginalTrainerFriendship = (byte)pi.HatchCycles;
            pk.MetLevel = 1;
            pk.MetLocation = 0;
            pk.EggLocation = 30023;                 // picnic
            pk.EggMetDate = today;
            pk.MetDate = null;
            pk.ObedienceLevel = 1;
            pk.Ball = (byte)Ball.Poke;
            pk.TeraTypeOriginal = (MoveType)pi.Type1;
            pk.TeraTypeOverride = (MoveType)TeraTypeUtil.OverrideNone;
            pk.HeightScalar = 128; pk.WeightScalar = 128; pk.Scale = 128;
            pk.HandlingTrainerName = "";
            pk.CurrentHandler = 0;
            pk.ResetPartyStats();
            pk.RefreshChecksum();
            return pk;
        }
        for (ushort n = 1; n <= 1025; n++)
        {
            var count = Math.Max(1, (int)PersonalTable.SV[n].FormCount);
            for (byte f = 0; f < count; f++)
            {
                var pi = PersonalTable.SV.GetFormEntry(n, f);
                if (!pi.IsPresentInGame || pi.HatchCycles == 0) continue;     // babies are Undiscovered yet hatch: no egg group test
                if (!Breeding.CanHatchAsEgg(n)) continue;
                var pk = MakeEgg(n, f);
                var la = new LegalityAnalysis(pk);
                if (!la.Valid)
                {
                    // most fail for a reason of their own (not a first stage, a form that only comes from evolving);
                    // these three surely hatch, so a failure there is the rules' fault: its report says which
                    eggFail++;
                    if (n is 1 or 133 or 906 && f == 0) Console.WriteLine($"egg {s.Species[n]}: NOT LEGAL as built (fix the rules):\n{la.Report()}");
                    continue;
                }
                ulong ballMask = 0;
                for (int b = 1; b < Math.Min(64, s.balllist.Length); b++)
                {
                    if (string.IsNullOrEmpty(s.balllist[b])) continue;
                    pk.Ball = (byte)b;
                    pk.RefreshChecksum();
                    if (new LegalityAnalysis(pk).Valid) ballMask |= 1ul << b;
                }
                var em = new[] { pk.Move1, pk.Move2, pk.Move3, pk.Move4 };
                var rl = new[] { pk.RelearnMove1, pk.RelearnMove2, pk.RelearnMove3, pk.RelearnMove4 };
                eggs.Add(string.Join('\t', new object[] { n, f, pi.HatchCycles, pk.IsNicknamed ? 1 : 0 }
                    .Concat(em.Select(m => (object)m)).Concat(rl.Select(m => (object)m))
                    .Append(ballMask.ToString("X")).Append(Convert.ToHexString(pk.Data.Slice(0x58, 26)))));
            }
        }
        File.WriteAllLines(Path.Combine(dir, "mons_eggs.txt"), eggs);
        Console.WriteLine($"{eggs.Count} egg species/forms ({eggFail} more not legal as eggs)");
        var mv = new List<string>();
        var dummied = MoveInfo.GetDummiedMovesHashSet(EntityContext.Gen9);
        for (ushort m = 1; m < s.Move.Count; m++)
            if (!MoveInfo.IsDummiedMove(dummied, m) && MoveInfo.IsMoveKnowable(m) && MoveInfo.GetPP(EntityContext.Gen9, m) > 0)
                mv.Add($"{m}\t{MoveInfo.GetPP(EntityContext.Gen9, m)}\t{s.Move[m]}");
        File.WriteAllLines(Path.Combine(dir, "mons_moves.txt"), mv);
        File.WriteAllLines(Path.Combine(dir, "mons_abilities.txt"), Enumerable.Range(0, s.Ability.Count).Select(i => $"{i}\t{s.Ability[i]}"));
        var exp = new List<string>();
        for (byte g = 0; g < 6; g++)
            exp.Add(string.Join('\t', Enumerable.Range(1, 100).Select(l => Experience.GetEXP((byte)l, g))));
        File.WriteAllLines(Path.Combine(dir, "mons_exp.txt"), exp);
        var balls = new List<string>();
        for (int b = 1; b < s.balllist.Length; b++) if (!string.IsNullOrEmpty(s.balllist[b])) balls.Add($"{b}\t{s.balllist[b]}");
        File.WriteAllLines(Path.Combine(dir, "mons_balls.txt"), balls);
        // the save's trainer: tid sid gender language version name
        File.WriteAllText(Path.Combine(dir, "mons_trainer.txt"),
            $"{sav.TID16}\t{sav.SID16}\t{sav.Gender}\t{sav.Language}\t{(int)sav.Version}\t{sav.OT}\n");
        Console.WriteLine($"{sp.Count} species/forms, {mv.Count} moves, {balls.Count} balls, trainer {sav.OT} -> {dir}");
        return 0;
    }
    case "layout":
    {
        // which decrypted bytes each property touches (to write PK9s without PKHeX)
        var sets = new (string, Action<PK9>)[] {
            ("EC", p => p.EncryptionConstant = 0xA1B2C3D4), ("PID", p => p.PID = 0xA1B2C3D4), ("Species", p => p.Species = 956),
            ("HeldItem", p => p.HeldItem = 0x123), ("TID16", p => p.TID16 = 0x1234), ("SID16", p => p.SID16 = 0x5678),
            ("EXP", p => p.EXP = 0x123456), ("Ability", p => p.Ability = 0x1FF), ("AbilityNumber", p => p.AbilityNumber = 4),
            ("Nature", p => p.Nature = Nature.Modest), ("StatNature", p => p.StatAlignment = Nature.Modest),
            ("Gender", p => p.Gender = 1), ("Form", p => p.Form = 3), ("FatefulEncounter", p => p.FatefulEncounter = true),
            ("EV_HP", p => p.EV_HP = 0xF1), ("EV_ATK", p => p.EV_ATK = 0xF2), ("EV_DEF", p => p.EV_DEF = 0xF3),
            ("EV_SPE", p => p.EV_SPE = 0xF4), ("EV_SPA", p => p.EV_SPA = 0xF5), ("EV_SPD", p => p.EV_SPD = 0xF6),
            ("Move1", p => p.Move1 = 0x101), ("Move2", p => p.Move2 = 0x102), ("Move3", p => p.Move3 = 0x103), ("Move4", p => p.Move4 = 0x104),
            ("Move1_PP", p => p.Move1_PP = 0x11), ("Move2_PP", p => p.Move2_PP = 0x12), ("Move3_PP", p => p.Move3_PP = 0x13), ("Move4_PP", p => p.Move4_PP = 0x14),
            ("Move1_PPUps", p => p.Move1_PPUps = 3), ("IV_HP", p => p.IV_HP = 31), ("IV_ATK", p => p.IV_ATK = 31), ("IV_DEF", p => p.IV_DEF = 31),
            ("IV_SPE", p => p.IV_SPE = 31), ("IV_SPA", p => p.IV_SPA = 31), ("IV_SPD", p => p.IV_SPD = 31),
            ("IsEgg", p => p.IsEgg = true), ("IsNicknamed", p => p.IsNicknamed = true), ("Nickname", p => p.Nickname = "ABCDEFGHIJKL"),
            ("OT", p => p.OriginalTrainerName = "ABCDEFGHIJKL"), ("OTGender", p => p.OriginalTrainerGender = 1),
            ("OTFriendship", p => p.OriginalTrainerFriendship = 0x77), ("HTName", p => p.HandlingTrainerName = "ABC"),
            ("CurrentHandler", p => p.CurrentHandler = 1), ("Version", p => p.Version = GameVersion.VL), ("Language", p => p.Language = 2),
            ("MetLocation", p => p.MetLocation = 0x1234), ("MetLevel", p => p.MetLevel = 0x55), ("MetDate", p => p.MetDate = new DateOnly(2026, 11, 6)),
            ("Ball", p => p.Ball = 0x0B), ("TeraTypeOriginal", p => p.TeraTypeOriginal = (MoveType)7), ("TeraTypeOverride", p => p.TeraTypeOverride = (MoveType)0x13),
            ("HeightScalar", p => p.HeightScalar = 0xAB), ("WeightScalar", p => p.WeightScalar = 0xAC), ("Scale", p => p.Scale = 0xAD),
            ("StatLevel", p => p.Stat_Level = 0x64), ("StatHPMax", p => p.Stat_HPMax = 0x1234), ("StatHPCur", p => p.Stat_HPCurrent = 0x1234),
            ("StatATK", p => p.Stat_ATK = 0x1234), ("StatDEF", p => p.Stat_DEF = 0x1234), ("StatSPE", p => p.Stat_SPE = 0x1234),
            ("StatSPA", p => p.Stat_SPA = 0x1234), ("StatSPD", p => p.Stat_SPD = 0x1234), ("Obedience", p => p.ObedienceLevel = 0x64),
            ("RelearnMove1", p => p.RelearnMove1 = 0x101),
        };
        foreach (var (name, set) in sets)
        {
            var a = new PK9(); var before = a.Data.ToArray(); set(a);
            var d = a.Data.ToArray();
            var ch = Enumerable.Range(0, d.Length).Where(i => d[i] != before[i]).Select(i => $"{i:X}={d[i]:X2}");
            Console.WriteLine($"{name}: {string.Join(' ', ch)}");
        }
        var b = new PK9(); b.Species = 956; b.RefreshChecksum();
        Console.WriteLine($"blank+species checksum {b.Checksum:X4} size {b.Data.Length:X} enc {Convert.ToHexString(EncParty(b)[..16])}");
        // crypto check vectors: decrypted with EC = 0x853A6302 and a marker per 0x50 block
        var c = new PK9(); c.EncryptionConstant = 0x853A6302; for (int k = 0; k < 4; k++) c.Data[8 + 0x50 * k] = (byte)(0xB0 + k);
        File.WriteAllBytes(args[2] + ".vdec", c.Data.ToArray()); File.WriteAllBytes(args[2] + ".venc", EncParty(c));
        return 0;
    }
    case "check":
    {
        // check <main> <pk9 file>: what PKHeX reads from a PK9 our writer made (encrypted, 0x158)
        var pk = new PK9(File.ReadAllBytes(args[2]));
        var s = GameInfo.Strings;
        Console.WriteLine($"checksum {(pk.ChecksumValid ? "ok" : "BAD")} species {s.Species[pk.Species]} form {pk.Form} lv {pk.CurrentLevel} " +
                          $"\"{pk.Nickname}\" nick {pk.IsNicknamed} {pk.Nature}/{pk.StatAlignment} ability {s.Ability[pk.Ability]} ({pk.AbilityNumber}) " +
                          $"gender {pk.Gender} shiny {pk.IsShiny} IVs {string.Join('/', IVs(pk))} EVs {pk.EV_HP}/{pk.EV_ATK}/{pk.EV_DEF}/{pk.EV_SPA}/{pk.EV_SPD}/{pk.EV_SPE} " +
                          $"moves {string.Join(", ", pk.Moves.Select(m => s.Move[m]))} PP {pk.Move1_PP}/{pk.Move2_PP}/{pk.Move3_PP}/{pk.Move4_PP} " +
                          $"OT {pk.OriginalTrainerName} {pk.TID16}/{pk.SID16} ball {pk.Ball} tera {pk.TeraTypeOriginal} lang {pk.Language} ver {pk.Version} " +
                          $"met {pk.MetLocation} lv{pk.MetLevel} {pk.MetDate} stats {pk.Stat_HPMax}/{pk.Stat_ATK}/{pk.Stat_SPA} party level {pk.Stat_Level}");
        if (pk.IsEgg)
            Console.WriteLine($"egg: cycles left {pk.OriginalTrainerFriendship} egg location {pk.EggLocation} met location {pk.MetLocation} " +
                              $"egg date {pk.EggMetDate} relearn {string.Join(", ", new[] { pk.RelearnMove1, pk.RelearnMove2, pk.RelearnMove3, pk.RelearnMove4 }.Select(m => s.Move[m]))}");
        Console.WriteLine($"OT ID No. {pk.ID32 % 1000000:D6} SID {pk.ID32 / 1000000:D4}, PSV {((pk.PID >> 16) ^ (pk.PID & 0xFFFF)) >> 4}");
        if (args.Length > 3)
        {
            // check <main> <pk9> <hatcher ID No.> <hatcher SID>: whether it hatches shiny for that trainer
            var id = uint.Parse(args[3]) + uint.Parse(args[4]) * 1000000u;
            var x = (pk.PID >> 16) ^ (pk.PID & 0xFFFF) ^ (id & 0xFFFF) ^ (id >> 16);
            Console.WriteLine($"for {args[3]}/{args[4]} (TSV {((id & 0xFFFF) ^ (id >> 16)) >> 4}): {(x < 16 ? $"shiny (xor {x})" : "not shiny")}");
        }
        var la = new LegalityAnalysis(pk);
        Console.WriteLine(la.Valid ? "legal" : la.Report());
        return 0;
    }
    case "dump":
    {
        // dump <main> <out dir> <key hex>...: each block's data to <out dir>\<key>.bin (type and size printed)
        Directory.CreateDirectory(args[2]);
        foreach (var k in args.Skip(3))
        {
            var key = Convert.ToUInt32(k, 16);
            var b = sav.Accessor.GetBlock(key);
            File.WriteAllBytes(Path.Combine(args[2], $"{key:X8}.bin"), b.Data.ToArray());
            Console.WriteLine($"{key:X8} {b.Type} {b.Data.Length} bytes, {b.Data.ToArray().Count(x => x != 0)} nonzero");
        }
        return 0;
    }
    case "copy":
    {
        // copy <main> <from main> <regex>: blocks whose PKHeX name matches, from another save into this one (data, or
        // the type for booleans). Event data: "^KBCAT|^KOutbreakBC" (delivered raids and outbreaks, active BCAT outbreaks)
        var src = (SAV9SV)SaveUtil.GetSaveFile(args[2])!;
        var re = new Regex(args[3], RegexOptions.IgnoreCase);
        int n = 0, same = 0;
        foreach (var f in typeof(SaveBlockAccessor9SV).GetFields(BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Static))
        {
            if (!f.IsLiteral || !re.IsMatch(f.Name)) continue;
            var key = Convert.ToUInt32(f.GetRawConstantValue());
            if (!src.Accessor.TryGetBlock(key, out var sb) || !sav.Accessor.TryGetBlock(key, out var db))
            { Console.WriteLine($"{f.Name} {key:X8}: missing in {(src.Accessor.TryGetBlock(key, out _) ? "this save" : "the source")}, skipped"); continue; }
            bool isBool(SCTypeCode t) => t is SCTypeCode.Bool1 or SCTypeCode.Bool2 or SCTypeCode.Bool3;
            if (isBool(sb!.Type) && isBool(db!.Type))
            {
                if (sb.Type == db.Type) { same++; continue; }
                Console.WriteLine($"{f.Name} {key:X8}: {db.Type} -> {sb.Type}");
                db.ChangeBooleanType(sb.Type); n++; continue;
            }
            if (sb.Type != db!.Type || sb.Data.Length != db.Data.Length)
            { Console.WriteLine($"{f.Name} {key:X8}: {sb.Type}/{sb.Data.Length} vs {db.Type}/{db.Data.Length}, skipped"); continue; }
            if (sb.Data.ToArray().SequenceEqual(db.Data.ToArray())) { same++; continue; }
            db.ChangeData(sb.Data.ToArray());
            Console.WriteLine($"{f.Name} {key:X8}: {db.Type} {db.Data.Length} bytes copied");
            n++;
        }
        File.WriteAllBytes(path, sav.Write().ToArray());
        SaveUtil.GetSaveFile(path);
        Console.WriteLine($"{n} blocks changed, {same} already the same; written and reloaded");
        return 0;
    }
    case "find":
    {
        var re = new Regex(args[2], RegexOptions.IgnoreCase);
        foreach (var f in typeof(SaveBlockAccessor9SV).GetFields(BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Static))
        {
            if (!f.IsLiteral || !re.IsMatch(f.Name)) continue;
            var key = Convert.ToUInt32(f.GetRawConstantValue());
            Console.WriteLine($"{f.Name} {key:X8} {(sav.Accessor.TryGetBlock(key, out var b) ? b!.Type.ToString() : "-")}");
        }
        return 0;
    }
}
Console.Error.WriteLine("unknown command " + args[0]);
return 1;

static byte[] EncParty(PKM p) { var b = new byte[p.SIZE_PARTY]; p.WriteEncryptedDataParty(b); return b; }
static int[] IVs(PKM p) { var a = new int[6]; p.GetIVs(a); return a; }
