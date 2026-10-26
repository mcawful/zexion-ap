"""What the client leaves for the in-game helper, so AP saves keep their look without it.

The helper may be loaded with the game (the proxy in its folder, native/README.md) and running
when the client is not. For that, the client sends it two kinds of file over its pipe
(``offline_put``), and the helper writes them, from inside the game, into a per-user folder,
``%LOCALAPPDATA%\\ZexionAP``, never the game's folder. The helper writes them rather than the
client because, run from a packaged Windows app (MSIX, the Claude desktop app in this repo's
tests), the client's own writes there were not found by the game (2026-10-25: error 3, path
not found), inferred to be the package's AppData redirect.

- ``config-<build>.txt``: the ``config`` request with the addresses it resolved by pattern
  for that build of the game (client/patterns.py), then the first bytes of each game function
  among them (``check KEY HEX``). The helper uses it only for its own build, and only if the
  game in memory holds those same bytes at those addresses.
- ``seeds/<seed>-<team>-<slot>.txt``: one per seed it connects to, in the format
  native/src/offline.h describes: the seed's item table and start (to know its saves), the
  Intel Chip and ASSIST settings, the OS Install Disks (the count when the client last saw it,
  the number needed, the scanner's line, and the cells of this player's own disks not counted
  yet), and the item labels.

The helper reads them when it starts and each time a client goes away, and gives a save of
one of those seeds the same look the client would (docs/re/memory.md, "AP saves without the
client"). Pure functions here; the client sends them (zexion_client.py, offline_tick).
"""

from __future__ import annotations

import re
import struct
from collections.abc import Iterable, Sequence

from .ids import MAP_SIZE

CHECK_BYTES = 16  # of each function, for the helper's check (native/src/offline.cpp)
MAX_FILE = 48 * 1024  # the helper's kMaxFile: the pipe takes no message much over 64 KB
# Code the helper itself patches while it runs: its bytes are not the game's to check.
NOT_CHECKED = frozenset({"keys_branch", "popup_width_code"})


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
    """A seed file: ``discs`` is (the count, the number needed, the scanner's line, the grid
    cells of this player's own disks not in the count), None without the disks; ``labels`` the
    helper's ``ap_labels`` request (aplabels.labels_request), or None."""
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
