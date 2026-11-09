# pkmn-violet

Pokémon Violet 4.0.0 + DLC, statically recompiled to a native Windows x64 program. The source is in `violetrecomp/` (`game.json`,
`game.cmake`, scripts, mods). The shared runtime and tools (`nxrecomp/`), the build entry and `start.ps1` are in
the workspace, [HelixNX/ns-port](https://github.com/HelixNX/ns-port).

This repo goes inside the workspace, as `ns-port/pkmn-violet/`:

```powershell
git clone https://github.com/HelixNX/ns-port.git
cd ns-port
.\start.ps1 clone violet            # or: git clone https://github.com/HelixNX/pkmn-violet.git pkmn-violet
```

Then follow the workspace README (keys, firmware, setup) and play with `.\start.ps1 violet`. `CLAUDE.md` is where to
start (state, what is open).

No game data is included, and none may ever be committed: your ROMs go in `rom/`, and the dumps and the build
are made next to it by setup. The `.gitignore` is a whitelist, so they stay local.
