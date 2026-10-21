"""Player options."""

from __future__ import annotations

from dataclasses import dataclass

from Options import Choice, PerGameCommonOptions

from .data import MODES


class Mode(Choice):
    """The randomizer's mode, as in the game's menu: its item pool, starting items and
    settings. Easy starts with 5 Energy Tanks and more of everything; Standard is the game's
    default. OHKO is not offered yet."""

    display_name = "Mode"
    option_easy = 0
    option_standard = 1
    default = 1


MODE_NAMES = {Mode.option_easy: "easy", Mode.option_standard: "standard"}

# Every start room valid in some mode, in a fixed order; which ones a mode accepts is in
# data.MODES[mode]["starts"].
START_CELLS = sorted({tuple(s["cell"]) for m in MODES.values() for s in m["starts"]})
AUTOMATIC = len(START_CELLS)


def _make_start_room() -> type[Choice]:
    attrs = {f"option_map_{x}_{y}": i for i, (x, y) in enumerate(START_CELLS)}
    attrs["option_automatic"] = AUTOMATIC
    attrs["default"] = AUTOMATIC
    attrs["display_name"] = "Start Room"
    attrs["__module__"] = __name__  # so the class pickles: it is found as options.StartRoom
    attrs["__qualname__"] = "StartRoom"
    attrs["__doc__"] = (
        "The save room the game starts in, by its map cell as the game displays it. Only "
        "rooms from which the mode's starting items reach a few locations are offered; a room "
        "the chosen mode does not accept is an error. 'automatic' takes map 13,16 when the "
        "mode accepts it, otherwise the mode's first room; 'random' picks any of the listed "
        "rooms."
    )
    return type("StartRoom", (Choice,), attrs)


StartRoom = _make_start_room()


@dataclass
class ZexionOptions(PerGameCommonOptions):
    mode: Mode
    start_room: StartRoom
