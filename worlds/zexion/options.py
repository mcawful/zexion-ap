"""Player options."""

from __future__ import annotations

from dataclasses import dataclass

from Options import Choice, DeathLink, DefaultOnToggle, PerGameCommonOptions, Range, Toggle

from .data import CELL_AREAS, MODES


class Mode(Choice):
    """The randomizer's mode, as in the game's menu: its item pool, starting items and
    settings. Easy starts with 5 Energy Tanks and more of everything; Standard is the game's
    default; OHKO has no Energy Tanks at all and a second teleport target, and some of its
    locations cannot be reached, so they are left out."""

    display_name = "Mode"
    option_easy = 0
    option_standard = 1
    option_ohko = 2
    default = 1


MODE_NAMES = {Mode.option_easy: "easy", Mode.option_standard: "standard", Mode.option_ohko: "ohko"}

# Every start room valid in some mode, in a fixed order; which ones a mode accepts is in
# data.MODES[mode]["starts"].
START_CELLS = sorted({tuple(s["cell"]) for m in MODES.values() for s in m["starts"]})
AUTOMATIC = len(START_CELLS)


def _make_start_room() -> type[Choice]:
    attrs = {
        f"option_{CELL_AREAS[(a, b)].lower().replace(chr(39), '')}_{b}_{a}": i
        for i, (a, b) in enumerate(START_CELLS)
    }
    attrs["option_automatic"] = AUTOMATIC
    attrs["default"] = AUTOMATIC
    attrs["display_name"] = "Start Room"
    attrs["__module__"] = __name__  # so the class pickles: it is found as options.StartRoom
    attrs["__qualname__"] = "StartRoom"
    attrs["__doc__"] = (
        "The save room the game starts in, by its area and its map cell as the game displays "
        "them (X then Y). Easy and "
        "Standard accept only rooms from which their starting items reach a few locations; "
        "OHKO accepts any, and picks a second teleport target that makes up the rest. For a "
        "room the chosen mode does not accept, one it accepts is picked at random instead. "
        "'automatic' takes kresezoa_16_13 when the mode accepts it, otherwise the mode's first "
        "room; 'random' picks any of the listed rooms."
    )
    return type("StartRoom", (Choice,), attrs)


StartRoom = _make_start_room()


class AllowAssists(Toggle):
    """Lets the game's assist menu (pause, OPTIONS, ASSIST: save states, less damage, refills,
    longer invincibility and the like) be used. Off by default: the client's in-game helper
    greys out ASSIST, puts a padlock after it, has the cursor skip it and keeps it from opening
    while this seed's save is loaded, so the assists stay at their defaults. To lift the lock
    later in a seed, send that player the Assist Unlock item from the server console
    (/send PLAYER Assist Unlock)."""

    display_name = "Allow Assists"


class AreaMapHints(DefaultOnToggle):
    """Makes each Area Map a hint. Every Area Map this player gets, found in this world or
    sent by another player, reveals where one of this player's upgrades, keys or OS Install
    Disks not received yet is, in any world (supplies such as Missile Containers are not
    hinted): the client
    creates an Archipelago hint for it, shown in every player's hint list, and with the
    client's in-game helper a popup names it ("HINT: SLIDE MOD IS AT GUARDIA 22,7", with "IN
    ALICE'S WORLD" after another world's location). Off, an Area Map is filler that counts as
    Scrap Metal."""

    display_name = "Area Map Hints"


class Goal(Choice):
    """What wins the game. Core: grab Zexion in the core, behind the four keys, as in the
    game's own randomizer. OS Install Disks: find enough OS Install Disks, shuffled into the
    multiworld (OS Install Disks and OS Install Disks Required); the four keys are left out of
    the item pool, and the core is not needed. The disks are counted by the client, not put in
    the game's inventory (they are the game's Data Disc, which changes its endings)."""

    display_name = "Goal"
    option_core = 0
    option_os_install_disks = 1
    default = 0


class OSInstallDisks(Range):
    """With the OS Install Disks goal, how many disks are shuffled into the multiworld. Each
    takes the place of an item in this world's pool: Scrap (the keys' places among it), then
    Area Maps, then Energy Shields. Without taking an Area Map or an Energy Shield, 26 fit in
    Easy, 16 in Standard and 30 in OHKO; up to 30 fit in every mode."""

    display_name = "OS Install Disks"
    range_start = 1
    range_end = 30
    default = 16


class OSInstallDisksRequired(Range):
    """With the OS Install Disks goal, how many disks win the game. At most OS Install
    Disks."""

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
