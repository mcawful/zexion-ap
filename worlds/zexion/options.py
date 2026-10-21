"""Player options."""

from __future__ import annotations

from dataclasses import dataclass

from Options import Choice, PerGameCommonOptions

from .data import STARTS


def _start_option_name(start: dict) -> str:
    x, y = start["cell"]
    return f"option_map_{x}_{y}"  # the map cell, as the game displays it


def _make_start_room() -> type[Choice]:
    attrs = {_start_option_name(s): i for i, s in enumerate(STARTS)}
    default_cell = (13, 16)  # where the default preset's seeds have started
    default = next((i for i, s in enumerate(STARTS) if tuple(s["cell"]) == default_cell), 0)
    attrs["default"] = default
    attrs["display_name"] = "Start Room"
    attrs["__module__"] = __name__  # so the class pickles: it is found as options.StartRoom
    attrs["__qualname__"] = "StartRoom"
    attrs["__doc__"] = (
        "The save room the game starts in, by its map cell as the game displays it. Only "
        "rooms from which the starting items reach a few locations are offered; 'random' "
        "picks one of them."
    )
    return type("StartRoom", (Choice,), attrs)


StartRoom = _make_start_room()


@dataclass
class ZexionOptions(PerGameCommonOptions):
    start_room: StartRoom
