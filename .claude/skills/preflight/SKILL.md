---
name: preflight
description: Run the checks CI runs (ruff format, ruff check, pytest) and, once worlds/zexion exists, Archipelago's own world tests against the pinned checkout in .ap/. Reports the first failure with enough context to act on it. Use before handing work back, before committing, or whenever asked whether a change will pass CI.
---

# Pre-flight

Run these in order and stop at the first failure. A format failure makes the lint output noise,
and a lint failure often means the tests are importing the same broken code.

```sh
.venv/Scripts/ruff format --check .
.venv/Scripts/ruff check .
.venv/Scripts/python -m pytest
```

`.github/workflows/ci.yml` runs exactly these three, on Windows and Ubuntu.

## Before you start

Check the toolchain: `.venv/Scripts/python --version` must say 3.13 (Archipelago 0.6.8 refuses
3.14). If `.venv` is missing or on the wrong version, **say the change is unverified** and give
the setup commands from `CLAUDE.md`. An unverified change is a normal outcome to report, not
something to paper over.

## Archipelago's world tests

Only once `worlds/zexion/` exists. Until then, say they were not run because there is no world.

1. `.ap/Archipelago` must exist at the tag in `AP_VERSION`. If not, run
   `.venv/Scripts/python tools/ap_checkout.py`, which also links `worlds/zexion/` into it.
   Check the link: `ls .ap/Archipelago/worlds/zexion` lists this repo's world.
2. From `.ap/Archipelago`:

   ```sh
   ../../.venv/Scripts/python -m pytest test/general worlds/zexion/test
   ```

   `test/general` generates seeds for every world, ours included: fill, accessibility,
   item and location name groups, and that the world loads at all. AP prints a `_speedups not
   available` warning on this machine; it is harmless.

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
`tools/`, `client/`, or any fact in `docs/re/`.
