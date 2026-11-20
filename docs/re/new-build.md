# When the game is patched

The client and the helper know game builds by name (B1 so far). A build they do not know is
refused: the client will not attach (`client/game.py` checks `KNOWN_BUILDS`), `helper.py load`
refuses it, and the helper hooks nothing (`native/src/build.cpp`). The Linux build has a registry
of its own, by GNU build id: the client will not attach to a Linux binary missing from
`KNOWN_LINUX_BUILDS` (`client/patterns.py`), and the Linux helper serves nothing in one missing
from `kKnownBuilds` (`native/linux/src/core.cpp`), saying `zexion-ap <version>: not a known build
of Zexion (build id <hex>)` on the game's stderr. So after a patch nothing
works in the game until the steps below are done. Every fact recorded along the way follows
[`README.md`](README.md): its method, the new build, and its evidence. A new value goes under
the same heading with the new build, beside the old one, never in its place.

## Checklist

1. **Identify the build.** `.venv/Scripts/python tools/build_id.py` prints a block for
   [`builds.md`](builds.md). If the exe's SHA-256 differs from every build there, add a new
   section (B2, and so on) with it, and note which data files changed. The Linux build has a
   hash of its own by design: fingerprint it too, with `build_id.py` on a Linux install, and
   record it beside the new build ("B1 on Linux" is the example). `linux.md` is pinned to B1's
   Linux binary: re-check its Lookup, and `gGameState`'s shift, against the new binary's
   symbols and `LoadGame`. The steps below take the Windows exe.
2. **Copy the new build's files** into `lab/`: `.venv/Scripts/python tools/game_data.py
   RandoCells MapData Text Objects AreaEvents Enemies Bosses Missiles Sprites Sprites.png Palettes --exe`
   (into `lab/data/<exe-hash-12>/`, the first 12 hex digits of the exe's SHA-256, so builds
   never mix): every data file the layouts in `memory.md` and `randocells.md` depend on.
   Everything below reads these copies, never the install. `build_id.py` fingerprints all but
   `Sprites`, `Sprites.png` and `Palettes`: compare those with the old build's copies by hand.
3. **Check every pattern** against the copy: `.venv/Scripts/python tools/check_patterns.py
   lab/data/<exe-hash-12>/Zexion.exe`. Each target must resolve (every pattern once, all agreeing);
   for a build not yet known the tool reports the addresses found, for `docs/re/memory.md`.
   Two targets may fail without stopping the client: `cell_state` and `pickup_counter`
   (`patterns.CLIENT_ONLY_TARGETS`), without either of which it still attaches and only stops
   marking other players' items collected, saying so once. Replace their patterns all the
   same.
4. **Replace the patterns that fail** with `.venv/Scripts/python tools/make_pattern.py EXE RVA`
   (the `/find-address` skill, step 4): find the target's new RVA from the old code (its
   references, its function), take candidates, and keep two or more from different code. A
   change to `client/patterns.py` updates the digest in `tests/test_patterns.py` in the same
   change (`.venv/Scripts/python tests/test_patterns.py` prints it). A target added here also
   needs its row in `linux.md`'s Lookup (its Linux counterpart, or that it is not found yet);
   `tests/test_patterns.py` fails without it.
   **Regenerate the helper's copy of the patterns** in the same change:
   `.venv/Scripts/python tools/gen_native_patterns.py` writes `native/src/findaddr_patterns.h`,
   which the helper scans the game with to find its own addresses (`native/README.md`, "Finding
   its own addresses"); `tests/test_gen_native_patterns.py` fails while it is out of date. A
   target that is new or renamed also needs its key in `kConfigKeys`
   (`native/src/game_calls.cpp`) and `CONFIG_KEYS` follows the patterns, as
   `tests/test_cross_checks.py` checks. Then build `native/` and check the helper's own scan
   finds what the client's does on the new exe: `ZEXION_EXE=lab/data/<exe-hash-12>/Zexion.exe
   .venv/Scripts/python -m pytest tests/test_native_scan.py` (the native tests built first;
   it skips without the variable), which runs `findaddr`'s real-exe check too.
5. **Record the new build:**
   - each target's RVA on it in `Target.rvas` (`client/patterns.py`), which
     `check_patterns.py` then compares;
   - the build in `KNOWN_BUILDS` (`client/patterns.py`: name, exe SHA-256, PE timestamp and
     SizeOfImage) and in `kKnownBuilds` (`native/src/build.cpp`: name, timestamp and
     SizeOfImage); `tests/test_cross_checks.py` checks the two agree;
   - the Linux binary in `KNOWN_LINUX_BUILDS` (`client/patterns.py`: name, SHA-256 and GNU
     build id) and in `kKnownBuilds` (`native/linux/src/core.cpp`: name and GNU build id), kept
     in step by `tests/test_cross_checks.py` too;
   - the patterns' state on the new build in `docs/re/memory.md` ("Byte patterns"): which still
     match once, which moved, and which broke and were replaced, with each replacement's source
     instruction in "Pattern sources".
6. **Re-check the struct layouts.** Patterns find addresses, not what is inside. Every struct
   offset the helper uses (`native/src/game_layout.h` and each part file, citing `memory.md`)
   is labeled B1 in `docs/re/memory.md`: confirm each on the new build and record it. The
   helper's own checks before writing (handlers, sizes, expected values) are not
   measurements.
7. **Re-check the save format** ([`save-format.md`](save-format.md)) if saves of the new build
   differ: `client/save_patch.py` and `client/ids.py` take their offsets from it (`/save-lab`,
   diffing a fresh save of each mode).
8. **Regenerate the world's data.** `tools/gen_world_data.py` pins what it
   reads by build:
   - the hashes of `MapData` and `Text` (`MAP_DATA_SHA256`, `TEXT_SHA256`), whose cell and area
     offsets it uses;
   - each mode's menu preset in the exe (`PRESETS`, by `<exe-hash-12>`).

   Re-measure what changed, then run it to regenerate `worlds/zexion/data.py`. `DATA_BUILD` and
   the start table follow the exe it read; `client/newgame.py` uses that table only on the
   build it was measured on. If `RandoCells` changed, compare the logic (`randocells.md`,
   `locations.md`) and have the `logic-reviewer` subagent check the world.
9. **The proxy.** If `SDL2.dll` changed, check that it still imports from `VERSION.dll` by
   name with no delay-load (`memory.md`, "Loading with the game").
10. **Run every check:** ruff, pytest and AP's world tests (`/preflight`), and the native
    build and its tests (`CLAUDE.md`, "Checks").
11. **Live-test in the game** on the new build before calling anything working: the client
    attaching, `check_patterns.py --live`, the helper loading and hooking (`helper.py status`:
    with no client, `scan=ok addresses=scan` and the title screen's lines), a grant with its popup, a new game from ARCHIPELAGO, and each feature in
    [`docs/DEVELOPING.md`](../DEVELOPING.md) as it is touched. Record each run with its date and
    the build.
12. **Release it:** raise the version and release
    ([`docs/DEVELOPING.md`](../DEVELOPING.md), "Branches and releases"), so players get a build
    that supports the new game version.
