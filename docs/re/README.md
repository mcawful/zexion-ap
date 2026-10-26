# Reverse-engineering record

Everything this project knows about Zexion's internals, and how it knows it. Zexion has no
modding API, so the APWorld, the client and the helper rest entirely on what is written here.
Code that relies on a fact cites the page and heading it comes from.

## Pages

- [`builds.md`](builds.md): the game builds facts were measured on.
- [`new-build.md`](new-build.md): what to do when the game is patched.
- [`save-format.md`](save-format.md): the `.sav` files.
- [`memory.md`](memory.md): where the running game keeps its state.
- [`randocells.md`](randocells.md): the randomizer's logic file.
- [`locations.md`](locations.md) (on `apworld`): how the APWorld's locations and rules are
  derived from it, and what backs them; the `logic-reviewer` subagent checks the APWorld
  against it.

## How a fact is recorded

Each fact is a heading followed by this table:

| Field | What goes in it |
|---|---|
| Value | The fact itself: an offset, a pattern, a format, an ID. |
| Status | **assumed**, **measured**, or **working in game** (see below). |
| Method | How it was established: save diff, memory scan, disassembly, file inspection, the developer, or "assumption" with the reason. |
| Build | A build from [`builds.md`](builds.md), or "older build" with what is known about it. |
| Evidence | The command and what it showed, the date, and the `lab/` file it used (local only, never committed). |

A long Value runs over several rows, one paragraph each:

| Field | |
|---|---|
| Value | **The first part.** ... |
| | **The second part.** ... |
| Status | ... |

Statuses:

- **assumed**: believed for a stated reason, not yet measured. Fine to build on, but anything that
  depends on it says so.
- **measured**: observed directly, by the stated method, on the stated build.
- **superseded**: replaced by a later finding, kept for its evidence; says what replaced it.
- **open**: not yet tried or answered.
- **working in game**: something built on the fact has run in the real game and done the right
  thing. Give the date, the build, and what was seen. A test on synthetic data does not count.
  Elsewhere (`native/README.md`, `docs/DEVELOPING.md`) "working in game (date, B1)" means this,
  with the `docs/re/` heading that records the run.

When a new game build changes a fact, record the new value under the same heading with the new
build, and keep the old value with its build rather than deleting it.

Never paste copyrighted game content beyond what a fact needs (an item name, a short string the
loader searches for).
