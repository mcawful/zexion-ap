"""Player options."""

from __future__ import annotations

from dataclasses import dataclass

from Options import Choice, DeathLink, DefaultOnToggle, PerGameCommonOptions, Range, Toggle

from .data import CELL_AREAS, MODES


class Mode(Choice):
    """The game's own randomizer mode, with its items, starting items and settings.
    Easy: start with 5 Energy Tanks and a few more items; more Energy Tanks and Missile Boxes
    to find, but fewer Missile Containers.
    Standard: the game's default.
    OHKO: no Energy Tanks at all, and a second room to teleport to; the 4 locations it can't
    reach are left out."""

    display_name = "Mode"
    option_easy = 0
    option_standard = 1
    option_ohko = 2
    default = 1


# Every start room valid in some mode, in a fixed order; which ones a mode accepts is in
# data.MODES[mode]["starts"].
START_CELLS = sorted({tuple(s["cell"]) for m in MODES.values() for s in m["starts"]})
AUTOMATIC = len(START_CELLS)
MODE_NAMES = {"easy": "Easy", "standard": "Standard", "ohko": "OHKO"}


def _room_option(a: int, b: int) -> str:
    """kresezoa_16_13: the area, then X and Y as the game displays them."""
    area = CELL_AREAS[(a, b)].lower().replace("'", "")
    return f"option_{area}_{b}_{a}"


def _rooms_by_mode() -> str:
    """Which rooms each mode can start in, by option name, for the description."""
    groups: dict[tuple, list[str]] = {}
    for mode, m in MODES.items():
        cells = tuple(sorted(tuple(s["cell"]) for s in m["starts"]))
        groups.setdefault(cells, []).append(MODE_NAMES[mode])
    parts = []
    for cells, modes in groups.items():
        if len(cells) == len(START_CELLS):
            rooms = "all of them"
        else:
            rooms = ", ".join(_room_option(a, b).removeprefix("option_") for a, b in cells)
        parts.append(f"{' and '.join(modes)}: {rooms}")
    return "; ".join(parts)


def _make_start_room() -> type[Choice]:
    attrs = {_room_option(a, b): i for i, (a, b) in enumerate(START_CELLS)}
    attrs["option_automatic"] = AUTOMATIC
    attrs["default"] = AUTOMATIC
    attrs["display_name"] = "Start Room"
    attrs["__module__"] = __name__  # so the class pickles: it is found as options.StartRoom
    attrs["__qualname__"] = "StartRoom"
    attrs["__doc__"] = (
        "Where you start: one of the game's start rooms, named by its area and its X and Y as "
        f"the pause screen shows them. The rooms each mode can use: {_rooms_by_mode()}. If your "
        "mode can't use the room you pick, a random one it can use is chosen. 'automatic' "
        "(the default): kresezoa_16_13. 'random': a random room your mode can use."
    )
    return type("StartRoom", (Choice,), attrs)


StartRoom = _make_start_room()


class AllowAssists(Toggle):
    """Allow the game's assist menu (pause, OPTIONS, ASSIST: save states, less damage and the
    like). Off: ASSIST is locked in this seed's saves. The host can unlock it during the game
    from the server console, with your player name: /send YOURNAME Assist Unlock (on
    archipelago.gg, the command box on the room page, which only whoever created the room
    sees)."""

    display_name = "Allow Assists"


class AreaMapHints(DefaultOnToggle):
    """Area Maps become Intel Chips: each one you get, found or sent to you, hints where one of
    your upgrades, keys or OS Install Disks is. Intel Chips don't reveal the map. Off: Area
    Maps work as in the normal game."""

    display_name = "Area Map Hints"


class Goal(Choice):
    """What wins the game.
    Core: find the four keys, grab Zexion in the core and reach the ending. When the game asks
    SKIP ESCAPE AND FINAL BATTLE?, answer YES unless you have what the escape needs (such as
    the Zenith Thrusters): the seed only makes sure you can reach Zexion.
    OS Install Disks: find enough OS Install Disks (the next two options), then save at the
    start room (your landing pod) and answer YES to RETURN HOME WITH THE DISKS?. The keys are
    not placed. The scanner room at Vallahar 6,8, which shows where the keys are in the normal
    game, tells you how many disks you need."""

    display_name = "Goal"
    option_core = 0
    option_os_install_disks = 1
    default = 0


class OSInstallDisks(Range):
    """OS Install Disks goal only: how many disks are placed. They replace filler items. Above
    16 in Standard (26 in Easy) they also replace Area Maps, so there are fewer Intel Chips
    (or Area Maps), and above 25 in Standard, Energy Shields. In OHKO they replace filler
    items only."""

    display_name = "OS Install Disks"
    range_start = 1
    range_end = 30
    default = 16


class OSInstallDisksRequired(Range):
    """OS Install Disks goal only: how many disks you need to win (no more than OS Install
    Disks)."""

    display_name = "OS Install Disks Required"
    range_start = 1
    range_end = 30
    default = 12


@dataclass
class ZexionOptions(PerGameCommonOptions):
    mode: Mode
    start_room: StartRoom
    death_link: DeathLink
    allow_assists: AllowAssists
    area_map_hints: AreaMapHints
    goal: Goal
    os_install_disks: OSInstallDisks
    os_install_disks_required: OSInstallDisksRequired
