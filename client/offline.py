"""What the client leaves for the in-game helper, so AP saves keep their look without it.

The client sends the helper two kinds of file (``offline_put``), which it writes into
``%LOCALAPPDATA%\\ZexionAP`` and reads when no client runs: ``config-<build>.txt``, the
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
MAX_FILE = 48 * 1024  # the helper's kMaxFile: the pipe takes no message much over 64 KB
# Code the helper itself patches while it runs: its bytes are not the game's to check.
NOT_CHECKED = frozenset({"keys_branch", "popup_width_code", "ending_unlocks", "seen_marker_code"})


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
) -> str:
    """A seed file: ``table`` is the item table in the game's list order (SeedPlan.table);
    ``discs`` is (the count, the number needed, the scanner's line, the grid cells of this
    player's own disks not in the count), None without the disks; ``labels`` the helper's
    ``ap_labels`` request (aplabels.labels_request), or None."""
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
    if labels:
        lines.append(labels)
    return "\n".join(lines) + "\n"


def put_request(kind: str, text: str, name: str = "") -> str:
    """The helper's ``offline_put`` request: ``kind`` "config" or "seed" (with its file
    ``name``), then the file's text."""
    head = f"offline_put {kind}" + (f" {name}" if name else "")
    return f"{head}\n{text}"
