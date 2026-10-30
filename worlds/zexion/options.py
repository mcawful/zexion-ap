"""Player options.

The randomizer's own settings are named as the game's menu labels them, described with the
blurb the game shows for each, and their values named by the game's labels for them
(docs/re/randocells.md, "The randomizer menu's settings"). Every one of them but Starting
Location (start_room) and Safety is Custom mode's: with a preset mode it must stay 'preset', as
in the game, where changing one makes the mode Custom. Colorizer has no option: the game treats
it as the player's own (the mode check and the mode buttons leave it), so it stays as the menu
has it.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any

from Options import (
    Choice,
    DeathLink,
    DefaultOnToggle,
    NamedRange,
    OptionDict,
    PerGameCommonOptions,
    Range,
    StartInventoryPool,
    Toggle,
)

from . import custom


class Mode(Choice):
    """The game's own randomizer mode, with its items, starting items and settings.
    Easy: "An easier mode. Start with Navcore and Power Cartridge. More energy tanks and
    missiles to be found."
    Standard: "Standard difficulty. Start with a random weapon." Here it always starts with the
    Force Blaster, as the game's fresh Standard save did; Custom, whose Random Gun is on with
    Standard's base, draws one.
    OHKO: "No energy tanks at all. Can teleport to two locations."
    Custom: "Use custom rules.": every setting below, from custom_base's.
    Easy, Standard and OHKO are each the game's preset exactly, with one fresh save's items:
    only Starting Location (start_room) and Safety can be changed with them, as in the game."""

    display_name = "Mode"
    option_easy = 0
    option_standard = 1
    option_ohko = 2
    option_custom = 3
    default = 1


class CustomBase(Choice):
    """Custom mode only: the preset whose items, amounts and settings Custom starts from, the
    values every 'preset' below stands for."""

    display_name = "Custom Base"
    option_easy = 0
    option_standard = 1
    option_ohko = 2
    default = 1


class Amount(NamedRange):
    """An amount of the menu's first screen, or Start Keys; 'preset': the mode's (Custom:
    custom_base's)."""

    range_start = 0
    special_range_names = {"preset": -1}
    default = -1

    @classmethod
    def get_option_name(cls, value: int) -> str:
        return "Preset" if value == cls.default else str(value)


class StartingEnergyTanks(Amount):
    """Choose the items you start the game with.
    Custom mode only. 0 to 40, with Item Pool Energy Tanks 40 at most together."""

    display_name = "Starting Energy Tanks"
    range_end = custom.MAX_ENERGY


class PoolEnergyTanks(Amount):
    """Choose the items to find throughout the world.
    Custom mode only. 0 to 40, with Starting Energy Tanks 40 at most together."""

    display_name = "Item Pool Energy Tanks"
    range_end = custom.MAX_ENERGY


class StartingMissiles(Amount):
    """Choose the items you start the game with.
    Custom mode only. 0 to 999, with Item Pool Missiles 999 at most together. You start with
    each ten as a Missile Box and the rest as Missile Containers."""

    display_name = "Starting Missiles"
    range_end = custom.MAX_AMMO


class PoolMissiles(Amount):
    """Choose the items to find throughout the world.
    Custom mode only. 0 to 999, with Starting Missiles 999 at most together, and no more than
    the menu allows: ten for each place the pool's keys, items, Energy Tanks and Mega Bombs
    leave free. Above what fits, some come in Missile Boxes of ten. Refused, though the menu
    allows it, where the boxes and the rest would be more items than the pool's places (see
    Bulk Missiles and Box Compression)."""

    display_name = "Item Pool Missiles"
    range_end = custom.MAX_AMMO


class StartingMegaBombs(Amount):
    """Choose the items you start the game with.
    Custom mode only. 0 to 999, with Item Pool Mega Bombs 999 at most together. Not supported
    yet: anything above 0 is refused, as the client cannot start a game with Mega Bombs."""

    display_name = "Starting Mega Bombs"
    range_end = custom.MAX_AMMO


class PoolMegaBombs(Amount):
    """Choose the items to find throughout the world.
    Custom mode only. 0 to 999, with Starting Mega Bombs 999 at most together, and no more
    than the menu allows: ten for each place the pool's keys, items, Energy Tanks and missiles
    leave free. Above what fits, some come in Bomb Boxes of ten. Refused, though the menu
    allows it, where the boxes and the rest would be more items than the pool's places (see
    Bulk Bombs and Box Compression)."""

    display_name = "Item Pool Mega Bombs"
    range_end = custom.MAX_AMMO


class CustomItems(OptionDict):
    """Custom mode only: each of the menu's 27 items, by the game's name, and its state:
    start (":Starting Item": "Choose the items you start the game with."), pool (found
    throughout the world: "Choose the items to find throughout the world."), random
    (":Random": a coin flip between the two) or excluded (":Excluded": neither). Items not
    listed keep custom_base's state. With Guns First on, an upgrade started without its gun
    starts as the gun: the world holds each gun and its upgrade as one progressive item. With
    it off, each upgrade starts as itself."""

    display_name = "Starting Items and Item Pool"
    valid_keys = [name for name, _ in custom.MENU_ITEMS]
    default: dict[str, Any] = {}


class Setting(Choice):
    """A setting of the menu's Edit Options screen (``bits``: its byte t)."""

    option_off = 0
    option_on = 1
    option_preset = 2
    default = 2
    bits: tuple[int]


class RandomGun(Setting):
    """Start the game with a random gun.
    Custom mode only; 'preset': the mode's."""

    display_name = "Random Gun"
    bits = (custom.RANDOM_GUN,)


class GunsFirst(Setting):
    """Guns will be found before their upgrade that specifically requires them.
    Custom mode only; 'preset': the mode's. On, each gun and its upgrade are one progressive
    item ("Progressive Machine Gun": the gun, then its upgrade); off, each upgrade (Pulse Wave,
    Pentashot, Tunneler, Focal Crystal, Plasma Burst) is an item of its own, of no use without
    its gun."""

    display_name = "Guns First"
    bits = (custom.GUNS_FIRST,)


class BulkMissiles(Setting):
    """Missile containers will all be in boxes of ten.
    Custom mode only; 'preset': the mode's."""

    display_name = "Bulk Missiles"
    bits = (custom.BULK_MISSILES,)


class BulkBombs(Setting):
    """All mega bombs will be in boxes of ten.
    Custom mode only; 'preset': the mode's."""

    display_name = "Bulk Bombs"
    bits = (custom.BULK_BOMBS,)


class BoxCompression(Setting):
    """Missiles and bombs will be put into boxes more. Other items will also appear.
    Custom mode only; 'preset': the mode's."""

    display_name = "Box Compression"
    bits = (custom.BOX_COMPRESSION,)


class DoubleTeleport(Setting):
    """Be able to teleport to a random second location.
    Custom mode only; 'preset': the mode's. The second location is any room but the start; the
    game's logic counts one of the eight flagged rooms only once your Energy (doubled with the
    Magnus Armor), missiles, Mega Bombs, 10 per box and the menu's items owned (but the Navcore
    and the Magnus Armor) add up to 27, so the world draws one only when your starting items do.
    It draws among the rooms with which the starting items collect a location."""

    display_name = "Double Teleport"
    bits = (custom.DOUBLE_TELEPORT,)


class XCrystals(Setting):
    """Removes crystal blocks that are destroyed by plasma.
    Custom mode only; 'preset': the mode's."""

    display_name = "X Crystals"
    bits = (custom.X_CRYSTALS,)


class XBlocks(Setting):
    """Removes cracked blocks that are destroyed by missiles.
    Custom mode only; 'preset': the mode's."""

    display_name = "X Blocks"
    bits = (custom.X_BLOCKS,)


class AbridgedFinale(Setting):
    """Victory is achieved when you first grab Zexion in the core.
    Custom mode only; 'preset': the mode's. Not with the OS Install Disks goal."""

    display_name = "Abridged Finale"
    bits = (custom.ABRIDGED_FINALE,)


class SecretShuffle(Setting):
    """Allows an item to be placed in the 7th layer.
    Custom mode only; 'preset': the mode's. On adds a 155th location, the Bomb Box at X48 Y20
    ("??? 48,20")."""

    display_name = "Secret Shuffle"
    bits = (custom.SECRET_SHUFFLE,)


class ObscureMajors(Setting):
    """Major items will not be identified when revealed on map.
    Custom mode only; 'preset': the mode's."""

    display_name = "Obscure Majors"
    bits = (29,)


class ObscureMinors(Setting):
    """Minor items will not be identified when revealed on map.
    Custom mode only; 'preset': the mode's."""

    display_name = "Obscure Minors"
    bits = (38,)


class ObscureClass(Setting):
    """Obscures whether or not items are major or minor.
    Custom mode only; 'preset': the mode's."""

    display_name = "Obscure Class"
    bits = (41,)


class ShowObtainable(Setting):
    """The map screen will display the items currently obtainable to you according to logic.
    Custom mode only; 'preset': the mode's."""

    display_name = "Show Obtainable"
    bits = (11,)


class Hints(Choice):
    """Custom mode only; 'preset': the mode's.
    None: Navigation rooms will give no hints.
    Vanilla: Navigation rooms will reveal the same sections of the map that they do in the base
    game.
    Vanilla+: Navigation rooms reveal the sections of the map that they do in the base game
    plus hidden items.
    Small: Navigation rooms will give small sized hints.
    Normal: Navigation rooms will give normal sized hints.
    Large: Navigation rooms will give large sized hints.
    Full Map: Navigation rooms will reveal the full map.
    Revealed: Map will be fully revealed from the start."""

    display_name = "Hints"
    option_none = 0
    option_vanilla = 1
    option_vanilla_plus = 2
    option_small = 3
    option_normal = 4
    option_large = 5
    option_full_map = 6
    option_revealed = 7
    option_preset = 8
    default = 8
    bits = custom.HINTS


class Gates(Choice):
    """Custom mode only; 'preset': the mode's.
    Off: Hyperspace gates are disabled.
    Vanilla: Gates require the four keys to access all like in the base game.
    Unlocked: Gates can be opened one by one by visiting their locations.
    Open: Gates can be moved through freely. (The R'talva gate is locked before defeating
    Aesos) The seed's logic then travels by gate, as the game's logic does, but not from the
    R'talva gate.
    The core's own door takes the four keys whatever this is."""

    display_name = "Gates"
    option_off = 0
    option_vanilla = 1
    option_unlocked = 2
    option_open = 3
    option_preset = 4
    default = 4
    bits = custom.GATES


class Safety(Choice):
    """Any mode; 'preset': the mode's. OHKO takes Light or Off only.
    Light: Energy Tanks and items are taken into account for some combat encounters.
    Early: More Energy Tanks and Equipment are required for certain tough early game
    situations.
    Normal: More Energy Tanks and Equipment are required for certain tough situations.
    Off: Energy Tanks and Equipment are not taken into account for tough combat encounters."""

    display_name = "Safety"
    option_light = 0
    option_early = 1
    option_normal = 2
    option_off = 3
    option_preset = 4
    default = 4
    bits = custom.SAFETY


class StartKeys(Amount):
    """Custom mode only; 'preset': the mode's. Keys in your start_inventory_from_pool add to these,
    which are the first ones from the Alpha Key (a start_inventory key the item pool holds is
    refused: take it from the pool).
    0: Start the game with no keys.
    1: Start the game with 1 key.
    2: Start the game with 2 keys.
    3: Start the game with 3 keys.
    4: Start the game with all 4 keys."""

    display_name = "Start Keys"
    range_end = 4
    bits = custom.START_KEYS


# Starting Location's rooms, in the menu's order: value n is start-table room n - 1
# (docs/re/randocells.md, "Starting Location and the second teleport target").
ROOM_NAMES = (
    "exodia", "upper_tower", "below_junkyard", "below_arena", "tower_gauntlet",
    "top_of_pulsava", "landing_pod", "guardia_lab", "ocean_cliff", "vallahar", "jungle_entry",
    "guardia_tower", "kresezoa_cave", "tarazors_nest", "lower_tower", "ktarel_sub",
    "spider_caves", "open_cave", "machinus_entry", "upper_machinus", "near_colossus",
    "near_orballus", "crysmoon_entry", "lower_machinus", "white_crysmoon", "below_machinus",
    "fijedar_lab", "fijedar_entry", "deep_crysmoon", "rtalva", "phobyss_entry",
    "magma_chamber", "vulknor_entry", "waste_zone",
)  # fmt: skip
RANDOM_START = 0
AUTOMATIC = len(ROOM_NAMES) + 1
AUTOMATIC_ROOM = ROOM_NAMES.index("landing_pod")  # where one of the owner's seeds started


def _make_start_room() -> type[Choice]:
    attrs: dict[str, Any] = {"option_random_start": RANDOM_START}
    attrs |= {f"option_{name}": i + 1 for i, name in enumerate(ROOM_NAMES)}
    attrs["option_automatic"] = AUTOMATIC
    attrs["default"] = AUTOMATIC
    attrs["display_name"] = "Starting Location"
    attrs["__module__"] = __name__  # so the class pickles: it is found as options.StartRoom
    attrs["__qualname__"] = "StartRoom"
    attrs["__doc__"] = (  # one line each, as a docstring is (the YAML templates' comments)
        "Any mode. random_start: Start in a random location: the game's own rule, a room from\n"
        "which your starting items reach locations, and no room the game flags unless you\n"
        "start with 27 or more of Energy Tanks, missiles, Mega Bombs and items together\n"
        "('random' is random_start too). Or one of the game's 34 rooms, by its name in the\n"
        "menu: a room from which your starting items reach no location, or every item does\n"
        "not reach Zexion's core with the core goal, is refused. From a room where they reach\n"
        "only one, generation can fail now and then: try again or pick another. automatic\n"
        "(the default): landing_pod, or a random start where that room is refused."
    )

    @classmethod
    def from_text(cls: type[Choice], text: str) -> Choice:
        if text.lower() == "random":
            return cls(RANDOM_START)
        return Choice.from_text.__func__(cls, text)

    attrs["from_text"] = from_text
    return type("StartRoom", (Choice,), attrs)


StartRoom = _make_start_room()


class AllowAssists(Toggle):
    """Allow the game's assist menu (pause, OPTIONS, ASSIST: save states, less damage and the
    like). Off: ASSIST is locked in this seed's saves. The host can unlock it during the game
    from the server console, with your player name: /send YOURNAME Assist Unlock (on
    archipelago.gg, the command box on the room page, which only whoever created the room
    sees)."""

    display_name = "Enable Assist Options"


class AreaMapHints(DefaultOnToggle):
    """Area Maps become Intel Chips: each one you get, found or sent to you, hints where one of
    your upgrades, keys or OS Install Disks is. Intel Chips don't reveal the map. Off: Area
    Maps work as in the normal game."""

    display_name = "Area Map Hints"


class Goal(Choice):
    """What wins the game.
    Core: find the four keys, grab Zexion in the core and reach the ending. The seed only makes
    sure you can reach the four keys and Zexion: the escape and final battle after the grab are
    yours to finish, and may need items it doesn't make sure of (such as the Zenith Thrusters).
    The game's Abridged Finale, a Custom-mode setting, ends the game at the grab instead.
    OS Install Disks: find enough OS Install Disks (the next two options), then save at the
    save point in your start room (the reminder that pops up once you have enough calls it the
    landing pod) and answer YES to RETURN HOME WITH THE DISKS?. The keys are not placed. The
    scanner room at Vallahar 6,8, which shows where the keys are in the normal game, tells you
    how many disks you need."""

    display_name = "Goal"
    option_core = 0
    option_os_install_disks = 1
    default = 0


class OSInstallDisks(Range):
    """OS Install Disks goal only: how many disks are placed. They replace filler items. Above
    16 in Standard (26 in Easy) they also replace Area Maps, so there are fewer Intel Chips
    (or Area Maps), and above 25 in Standard, Energy Shields. In OHKO they replace filler
    items only. In Custom they replace them in the same order, the numbers set by your pool.
    Excluded locations can hold only filler items, of which Standard keeps 25
    minus the disks (Easy 30, OHKO 36): exclude more locations than that, and generation stops
    with an error saying so, unless the other players' filler makes up the difference. Where
    there is not enough filler for the disks, ten Missile Containers or Mega Bombs become a box
    and nine Scrap Metal, as the game makes filler when it runs short. The disks placed and any
    in start_inventory can be 99 at most, the most the pause screen and the file select show."""

    display_name = "OS Install Disks"
    range_start = 1
    range_end = 30
    default = 16


class OSInstallDisksRequired(Range):
    """OS Install Disks goal only: how many disks you need to win (no more than OS Install
    Disks plus any disks in start_inventory)."""

    display_name = "OS Install Disks Required"
    range_start = 1
    range_end = 30
    default = 12


@dataclass
class ZexionOptions(PerGameCommonOptions):
    mode: Mode
    custom_base: CustomBase
    starting_energy_tanks: StartingEnergyTanks
    pool_energy_tanks: PoolEnergyTanks
    starting_missiles: StartingMissiles
    pool_missiles: PoolMissiles
    starting_mega_bombs: StartingMegaBombs
    pool_mega_bombs: PoolMegaBombs
    custom_items: CustomItems
    random_gun: RandomGun
    guns_first: GunsFirst
    bulk_missiles: BulkMissiles
    bulk_bombs: BulkBombs
    box_compression: BoxCompression
    hints: Hints
    gates: Gates
    start_room: StartRoom
    double_teleport: DoubleTeleport
    start_keys: StartKeys
    x_crystals: XCrystals
    x_blocks: XBlocks
    abridged_finale: AbridgedFinale
    secret_shuffle: SecretShuffle
    safety: Safety
    obscure_majors: ObscureMajors
    obscure_minors: ObscureMinors
    obscure_class: ObscureClass
    show_obtainable: ShowObtainable
    death_link: DeathLink
    allow_assists: AllowAssists
    area_map_hints: AreaMapHints
    goal: Goal
    os_install_disks: OSInstallDisks
    os_install_disks_required: OSInstallDisksRequired
    # Archipelago takes these out of the item pool after create_items, as the game's randomizer
    # takes a starting item out of its pool (worlds/zexion/starting.py).
    start_inventory_from_pool: StartInventoryPool


_OPTIONS = ZexionOptions.type_hints.items()
# The amounts by option name, in the menu's and the save's order (custom.START_ENERGY on).
AMOUNTS = tuple(n for n, t in _OPTIONS if issubclass(t, Amount) and t is not StartKeys)
# The settings of Edit Options, by option name: Custom only, unlike start_room and safety.
SETTINGS = tuple(n for n, t in _OPTIONS if t is not Safety and hasattr(t, "bits"))
