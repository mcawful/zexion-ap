"""What the client leaves for the in-game helper, so AP saves keep their look without it.

The client sends the helper two kinds of file (``offline_put``), which it writes into
``%LOCALAPPDATA%\\ZexionAP`` (the Linux helper the seeds alone, under ``$XDG_DATA_HOME``) and
reads when no client runs: ``config-<build>.txt``, the
``config`` addresses plus the first bytes of each function among them (``check KEY HEX``, used
only if the game still holds them), and ``seeds/<seed>-<team>-<slot>.txt`` in the format
native/src/offline.h describes. The helper writes them, not the client: a client run from a
packaged (MSIX) app has its AppData writes redirected where the game does not see them
(docs/re/memory.md, "AP saves without the client"). Pure functions; client/ticks.py
(offline_tick) sends them.
"""

from __future__ import annotations

import re
import struct
from collections.abc import Iterable, Sequence

from .ids import MAP_SIZE

CHECK_BYTES = 16  # of each function, for the helper's check (native/src/offline.cpp)
MAX_FILE = 48 * 1024  # the helper's kMaxFile: it keeps no file larger
MAX_REQUEST = 64 * 1024  # the pipe's kBufferSize: a longer request ends the connection
# Code the helper itself patches while it runs: its bytes are not the game's to check.
NOT_CHECKED = frozenset(
    {
        "keys_branch",
        "popup_width_code",
        "ending_unlocks",
        "seen_marker_code",
        "text_cut_code",
        "ailments_hit_code",
        "ailments_wave_code",
    }
)


def seed_file_name(seed_name: str, team: int, slot: int) -> str:
    """The seed's file name, without ``.txt``: letters, digits, dashes and underscores."""
    safe = re.sub(r"[^A-Za-z0-9-]", "_", seed_name)[:64] or "seed"
    return f"{safe}-{team}-{slot}"


def config_text(config: str, checks: dict[str, bytes]) -> str:
    """The cached config: the ``config`` request, then a check line per function."""
    lines = [config]
    lines += [f"check {key} {data.hex()}" for key, data in sorted(checks.items())]
    return "\n".join(lines) + "\n"


def grid_cells(cells: Sequence[tuple[int, int]], indices: Iterable[int]) -> list[int]:
    """The map grid's index (y * 54 + x) of each location index, in order."""
    return sorted(cells[i][1] * MAP_SIZE + cells[i][0] for i in indices)


def seed_text(
    table: Sequence[int],
    start: tuple[int, int, int],
    intel: bool,
    lock_assists: bool,
    discs: tuple[int, int, str, Sequence[int]] | None,
    labels: str | None,
    traps: Sequence[tuple[int, int]] = (),
) -> str:
    """A seed file: ``table`` is the item table in the game's list order (SeedPlan.table);
    ``discs`` is (the count every save of the seed shares: the disks the slot starts with and
    those received; the number needed; the scanner's line; the grid cells of this player's own
    disks, each of which a save counts once its map shows it collected), None without the
    disks; ``labels`` the helper's ``ap_labels`` request (aplabels.labels_request), or None;
    ``traps`` this player's own traps, each one's grid cell and item code, which the helper
    sets off at their pickup (native/README.md, "Traps")."""
    lines = [
        "zexion-ap offline 1",
        "table " + bytes(table).hex(),
        "start " + struct.pack("<3H", *start).hex(),
        f"intel {int(intel)}",
        "assists " + ("lock" if lock_assists else "free"),
    ]
    if discs is None:
        lines += ["discs -", "unfound -"]
    else:
        count, required, line, unfound = discs
        lines.append(f"discs {count} {required} {line.encode('ascii').hex()}")
        lines.append("unfound " + (",".join(str(c) for c in unfound) if unfound else "-"))
    listed = ",".join(f"{cell}:{item}" for cell, item in sorted(traps))
    lines.append("traps " + (listed or "-"))
    if labels:
        lines.append(labels)
    return "\n".join(lines) + "\n"


def keep_richer(text: str, stored: str | None) -> str:
    """``text``, a seed file with less than the whole (the slot data's alone: no labels, and no
    disk counts until the items are listed), with what ``stored``, the file the helper already
    keeps under the seed's name, has that ``text`` lacks: its ``ap_labels`` block, its
    ``discs`` and ``unfound`` lines, and ``assists free`` (Assist Unlock is never taken back, and
    the slot data's lock stands before this connection's items are listed). So putting ``text``
    never replaces a richer file with a poorer one (the helper keeps what it is put as it is),
    while the helper still takes the seed's own traps from it. ``text`` as it is when ``stored``
    is no seed file, or one of another seed (its table differs)."""
    if not stored:
        return text
    old, new = stored.splitlines(), text.splitlines()
    if not old or old[0] != "zexion-ap offline 1" or not new or new[0] != old[0]:
        return text
    table = [line for line in old if line.startswith("table ")]
    if not table or table[:1] != [line for line in new if line.startswith("table ")][:1]:
        return text
    kept = {key: line for key in ("discs", "unfound") for line in old if line.startswith(key + " ")}
    merged = [
        kept.get(line.split(" ", 1)[0], line) if line.endswith(" -") else line for line in new
    ]
    # Assist Unlock is never taken back: the slot data's lock, before this connection's items are
    # listed, does not undo the free menu the file already holds.
    if "assists free" in old:
        merged = ["assists free" if line == "assists lock" else line for line in merged]
    if "ap_labels" not in new and "ap_labels" in old:
        merged += old[old.index("ap_labels") :]
    return "\n".join(merged) + "\n"


def put_request(kind: str, text: str, name: str = "") -> str:
    """The helper's ``offline_put`` request: ``kind`` "config" or "seed" (with its file
    ``name``), then the file's text."""
    head = f"offline_put {kind}" + (f" {name}" if name else "")
    return f"{head}\n{text}"
