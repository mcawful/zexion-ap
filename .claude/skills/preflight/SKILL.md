---
name: preflight
description: Run the checks CI runs (ruff format, ruff check, pytest, and Archipelago's own world tests against the pinned checkout in .ap/). Reports the first failure with enough context to act on it. Use before handing work back, before committing, or whenever asked whether a change will pass CI.
---

# Pre-flight

Run these in order and stop at the first failure. On Linux and macOS the venv's programs are
in `.venv/bin/`, not `.venv/Scripts/`, here and below.

```sh
.venv/Scripts/ruff format --check .
.venv/Scripts/ruff check .
.venv/Scripts/python -m pytest
```

`.github/workflows/ci.yml` runs these three on Windows, Ubuntu and macOS, a `world` job with
Archipelago's world tests (below, on Ubuntu), and a Windows `native` job (the CMake build of `native/`, its ctest tests, and
`tests/test_native.py` against the built DLL) that preflight does not run. Locally,
`tests/test_native.py` skips its load test unless `native/build/Release/zexion_ap.dll` exists (or
`ZEXION_DLL` names a build); build it as `CLAUDE.md` ("Checks") shows when `native/` or
`client/native.py` changed. Without `sh` the hook runner's tests skip, and without the world
linked into `.ap/` so do `test_client_agrees.py`'s; CI fails on either (`ZEXION_REQUIRE`,
`docs/DEVELOPING.md`, "Checks"), so name any such skip in the report.

## Before you start

Check the toolchain: `.venv/Scripts/python --version` must say 3.13 (Archipelago 0.6.8 refuses
3.14). If `.venv` is missing or on the wrong version, **say the change is unverified** and give
the setup commands from `CLAUDE.md`. An unverified change is a normal outcome to report, not
something to paper over.

## Archipelago's world tests

1. `.ap/Archipelago` must exist at the tag in `AP_VERSION`. If not, run
   `.venv/Scripts/python tools/ap_checkout.py`, which also links `worlds/zexion/` into it.
   Check the link: `ls .ap/Archipelago/worlds/zexion` lists this repo's world.
2. From `.ap/Archipelago`:

   ```sh
   AP_TEST_WORLDS=zexion ../../.venv/Scripts/python -m pytest test/general worlds/zexion/test
   ```

   `AP_TEST_WORLDS=zexion` limits `test/general` to this world; it generates seeds: fill,
   accessibility, item and location name groups, and that the world loads at all. AP may print
   a `_speedups not available` warning; it is harmless.

If the AP checkout can't be created (no network, say), report the world as untested against AP.

## Reading the failures

- `ruff format`: run `.venv/Scripts/ruff format .` and re-check. Never hand-format to satisfy it.
- `ruff check`: fix the cause. A `# noqa` needs a reason next to it.
- `pytest`: `tests/test_guard_hook.py` failing means the write guard lets something through or
  blocks a read. Treat a newly allowed write as a safety bug, not a test to update.
- AP's tests: a fill or accessibility failure usually means a rule in `worlds/zexion/` is wrong
  or a progression item is missing from the pool. Check against `docs/re/locations.md`.

## Not covered here

Anything in the running game. Passing pre-flight says nothing about whether a save patch, a
memory pattern, or item delivery works in Zexion. Say so explicitly when a change touches
`tools/`, `client/`, `native/`, or any fact in `docs/re/`.
