# Zexion

## What does randomization do to this game?

Zexion has its own randomizer. This world uses its map and its rules for what each item opens
up, but lets Archipelago decide what is at each of the game's 154 item locations. In OHKO there
are 150, and in Custom the ones your settings and start room can reach, plus a 155th with
Secret Shuffle. A location the seed can't reach holds Scrap, as in the game's own randomizer.
Items found in Zexion can belong to other players, and their games can send items to
you. Excluded locations (`exclude_locations`) can hold only filler (Scrap, traps, Area Maps or
Intel Chips): exclude more locations than you have filler, and generation stops with an error that
says so, unless the other players' filler makes up the difference (not Area Maps kept in their
own world: "What do Area Maps and Intel Chips do?").

You need the Zexion Archipelago mod and the Zexion Client: see the setup guide
(`zexion-setup-guide.md` in the release).

## What is the goal?

Set with the Goal option.

- **Core** (default): find the Alpha, Beta, Gamma and Omega Keys, open the core and grab
  Zexion, then escape, win the final battle and board the ship: reaching the ending is your
  goal. The seed only makes sure you can reach the keys and Zexion, not that you have the
  items the escape needs (such as the Zenith Thrusters): the escape and final battle are yours
  to finish. With Abridged Finale (Custom), there is no escape: the ending begins when you
  grab Zexion.
- **OS Install Disks**: find enough OS Install Disks (the options set how many are placed and
  how many you need). The four keys are not in the seed.
  - So the hyperspace gates can't take keys: where Gates is Vanilla (OHKO's, also as Custom's
    `custom_base`, or `gates: vanilla` in Custom), whose gates need all four, the seed has
    Gates Unlocked instead, even if you start with keys (Start Keys, or keys in
    `start_inventory` or `start_inventory_from_pool`), and the generator's log says so. The gates
    then need no keys: once you have visited two of them, they work (checked in game). In the
    pause menu's RANDO SETTINGS, an OHKO seed then shows as Custom mode, with OHKO's items and
    amounts and Gates Unlocked (checked in game). The spoiler log's header names the change.
  - Your count shows after each disk's popup (a disk sent to you first pops up as any item
    does, with who sent it), on the pause screen and on the file select.
  - The scanner at Guardia 42,26, outside the core's door, which shows where the keys are in
    the normal game, tells you how many you need. Until you have been there, the pause screen
    shows "OF ?". With Hints Revealed (Custom), the game counts you as told from the start, so
    the number shows at once (seen in game: a number showed before the scanner, though what it
    read and where weren't checked).
  - Once you have enough, save at the Landing Pod's save point (Kresezoa 16,13, the
    `landing_pod` start room, whichever room you started in) and answer YES to RETURN HOME WITH
    THE DISKS?. The game's ending is your goal. NO lets you keep exploring; the question comes
    back the next time you save there. Once a save has reached the ending, saving there again
    offers it too, to watch the ending again; the goal has already been sent. The seed makes
    sure you can reach the Landing Pod. The
    pause menu's teleport goes to your start room (or second room, with Double Teleport), not
    the Landing Pod, unless you started there.
  - Each disk takes the place of another item: Scrap, then Area Maps or Intel Chips, then
    Energy Shields (how many of each a seed has is drawn per seed; in Custom, set by your pool).
    Excluded locations can hold only filler, so with many disks, excluding many locations can
    stop generation, as above (the OS Install Disks option gives the numbers). Where filler
    runs short, ten Missile Containers or Mega Bombs become a box and nine Scrap Metal, in any
    mode ("Which modes are there?").

## Which modes are there?

The Mode option picks the game's own Easy, Standard or OHKO mode, with that mode's items, starting
items and settings (with the OS Install Disks goal, OHKO's Gates Vanilla becomes Unlocked: "What
is the goal?"), or Custom. Each seed draws its items as the game does for a new game: Standard
starts with a random weapon (the Machine Gun, Force Blaster, Snake or Lance, with the other three
in the item pool), and every mode draws its filler (Scrap, Area Maps, Energy Shields), and traps
take some of the Scrap ("What do traps do?"). OHKO has no Energy Tanks and gives you a second room
to teleport to; the 4 locations it can't reach are left out.

With `mode: custom`, every setting of the game's randomizer menu is yours, named as the menu
names it: the six amounts (Starting Energy Tanks to Item Pool Mega Bombs), each of the 27 items
as `start`, `random`, `pool` or `excluded` (`custom_items`: start with it, a coin flip
between starting with it and finding it, find it, or neither), and the settings of Edit Options
(Random Gun, Box Compression, Hints, Gates, Start Keys, X Crystals, X Blocks, Abridged Finale
and the rest). Each starts as `preset`: the value of `custom_base`, the mode Custom starts
from. The seed is built from them as the game builds its own: the coin flips, Random Gun's
draw, the boxes and the filler. Settings the game's menu cannot hold (more than 40 Energy
Tanks, more missiles than the pool has room for, and so on) stop generation with an error that
names them, and so do a few the menu allows: missiles and Mega Bombs (with Bulk Missiles, Bulk
Bombs or Box Compression) that would make more items than the pool has places, which the game
would cut at random, keys included. Where the filler runs short, in any mode, for the locations
your start leaves out and the disks, ten Missile Containers or Mega Bombs become a box and nine
Scrap Metal, as the game makes filler; with none to spare, generation stops. Not yet: starting
Mega Bombs. Secret Shuffle adds a 155th location, the Bomb Box at X48 Y20, named ??? from the
game's area table ("??? 48,20").
With Guns First on (every preset), each gun and its upgrade are one item, Progressive
Machine Gun and the like: the first is the gun, the second its upgrade. With it off,
the five upgrades (Pulse Wave, Pentashot, Tunneler, Focal Crystal and Plasma Burst) are
items of their own, and the seed's logic counts each only once you have its gun, as the
game's own logic does. With it off, the guns keep their Progressive names (Progressive
Machine Gun and the like), but there is one of each and it is the gun alone.
With Gates Open the seed's logic travels by gate, as the game's logic does, but not from
R'Talva's gate, which the game keeps hidden until a boss is beaten. Gates Off works with the
core goal: the core's door needs only the four keys. With the OS Install Disks goal, Gates
Vanilla becomes Unlocked ("What is the goal?").
Abridged Finale ends the game when you grab Zexion, so it can't go with the OS Install Disks
goal.

With Easy, Standard or OHKO, only Starting Location (`start_room`) and Safety can be changed,
as in the game (OHKO takes Safety `light` or `off`).

## Where does the game start?

In one of the game's 34 start rooms, chosen with the Starting Location option (`start_room`), by
its name in the game's menu, as in `landing_pod`. The default, `automatic`, is Landing Pod, or
where your settings rule Landing Pod out, a random start (as `random_start` below; the
generator's log warns that it did so). Any room works from which your starting items reach at
least one location (and from which you can reach Zexion's core with the core goal, or the
Landing Pod with the OS Install Disks goal); another stops generation with an error (where the
starting weapon is drawn, as in Standard, only if no weapon suits it). With
either goal, Easy's items take 17 of the rooms, and OHKO's, with a second room to teleport to,
all 34. Standard's take 17 to 20, by the weapon the seed draws. These 17 suit all four
weapons, and Easy's are the same 17: `exodia`, `upper_tower`, `below_arena`, `tower_gauntlet`,
`landing_pod`, `guardia_lab`, `ocean_cliff`, `vallahar`, `jungle_entry`, `guardia_tower`,
`lower_tower`, `ktarel_sub`, `open_cave`, `lower_machinus`, `below_machinus`, `fijedar_entry`
and `vulknor_entry`. The Force Blaster adds `below_junkyard` and `phobyss_entry`, and the Lance
`crysmoon_entry`, `white_crysmoon` and `phobyss_entry`; a seed started in one of those draws
one of the weapons that suit it. With Double
Teleport the second room is any other, but the eight rooms the game flags (Exodia, Below
Junkyard, Below Machinus, Fijedar Entry, R'talva, Phobyss Entry, Vulknor Entry and Waste Zone)
only when your starting items score 27 or more, as the game's logic counts them: Energy Tanks
(doubled with the Magnus Armor), Missile Containers and Mega Bombs one each, 10 per box, and one
per item of the menu's 27 but the Navcore and Magnus Armor. `random_start` (or `random`) picks a
room as the game's Random Start does. The spoiler log names the start room (and a second room to
teleport to, with Double Teleport, as in OHKO) and the starting items, Standard's weapon among
them.

In any game, alone or in a multiworld, the few items that open your start are placed in your own
world, in the area around your start: until your starting items reach 20 of your locations that
are not excluded, the generator places one of your items that opens more (or two that open more
together) in a location you can already reach, then fills the rest as usual. From a room where
your starting items reach only one or two locations, that may be several items in a row. An item
shared by an item link you are in counts as yours. An item in your `non_local_items` is not
placed this way, since it must go to another world. If every item that opens your start (alone,
or with another) is in it, or if you exclude (or fill by plando) every location your starting
items reach, or so many of them that two items that open it only together no longer fit, the
generator logs a warning naming which, and generation may fail.

Locations are named by their area and their X and Y as the pause screen shows them, as in
"Guardia 22,7", so you can find them on the in-game map. Each area's name (Guardia,
Machinus, and so on) is also a group of its locations, for options such as
`exclude_locations`; the item groups are Keys, Weapons, Weapon Upgrades, Supplies, Scrap and
Traps.

## Can I add starting items?

Yes, as long as a new game can start with them (counted together with the mode's own starting
items). As in the game's own randomizer, where a starting item is no longer in the item pool,
take an item the pool holds with `start_inventory_from_pool`: you start with it, and a Scrap
Metal takes its place in the pool. `start_inventory` adds items on top of the pool, so use it
only for the items the game holds a number of (Energy Tanks, Missile Containers, Missile Boxes)
and for those your seed's pool doesn't hold. A `start_inventory` copy of any other item the
pool holds stops generation with an error: the game holds such an item once, so the pool's copy
would be a location that gives nothing. Where the seed draws its own starting items (Standard's
weapon, Custom's coin flips), it draws again until they fit with yours (and stops with an error
if none does): with two Progressive Snakes from the pool, a Standard seed never starts with the
Snake, which would make three.

What a new game can start with:

- one of each item such as the Slide Mod or the Power Core;
- with Guns First on (every preset), at most two of each Progressive weapon (the second is its
  upgrade), and none of the five upgrades as items of their own; with it off, one of each
  gun and one of each of the five upgrades;
- up to 40 Energy Tanks, and missiles up to 999 in all, ten per Missile Box, with up to 9
  Missile Containers (Custom's starting missiles come as Missile Boxes of ten and the rest as
  containers, and count toward these): the most the game's randomizer menu starts a game
  with;
- keys, in order from the Alpha Key;
- OS Install Disks, with the OS Install Disks goal: they count toward the disks you need.

Anything else, such as Scrap, Area Maps, Intel Chips, Energy Shields, Mega Bombs, Bomb Boxes,
Assist Unlock or a trap, stops generation with an error that names it.

## Can I use plando?

Item plando (`plando_items`) works as in other Archipelago games: it puts the items you name in
the locations you name, in your world or another player's, and other players can put theirs in
yours. Generation reads it only with item plando turned on: `plando_options` in your Archipelago
folder's `host.yaml` must include `items` (it doesn't by default, and generation then skips the
plando with a warning). For example:

```yaml
Zexion:
  plando_items:
    - items:
        Gravity Harness: 1
      locations:
        - Kresezoa 25,25
    - items:
        OS Install Disk: 3
      world: Bob
      locations:
        - Machinus 24,29
        - Machinus 27,24
        - Vallahar 6,14
```

Locations are named by area and X,Y ("Where does the game start?"). In `locations`, an area's
name, such as Machinus, stands for its locations; in `items`, an item group (Keys, Weapons,
Weapon Upgrades, Supplies, Scrap, Traps) for one of its items. Keep `from_pool: true`, the
default: it moves an item from your pool rather than making a new one.

Played in game: another game's item and another Zexion player's in your locations, your own OS
Install Disk and Intel Chip in your own, a Progressive Snake at Kresezoa 25,25, and your disks
and Intel Chips in another Zexion player's world. What the generator refuses or warns about,
below, is checked in generation only.

Generation stops with an error naming the item if plando gives you items the game doesn't hold
(`from_pool: false` makes new ones, and so does `from_pool: true` for an item your pool has
none left of):

- a second copy of an item the game holds once; with Guns First on (every preset), a third of
  a Progressive weapon, or one of the five upgrades as an item of its own;
- more than 40 Energy Tanks, or more than 999 missiles or Mega Bombs in all (ten per box), your
  starting ones counted: the most the game's randomizer menu allows;
- an OS Install Disk without the OS Install Disks goal, or more disks than `os_install_disks`
  (with your starting disks) makes: set the number there;
- an Area Map with Area Map Hints on (they are Intel Chips), or an Intel Chip with it off.

With Area Map Hints off, your Area Maps stay in your own world, plando too ("What do Area Maps
and Intel Chips do?").

The copies an item link's `replacement_item` adds aren't plando and are never refused, though a
second copy of an item the game holds once gives nothing.

If a plando names a location your seed doesn't have (and, in a plando into several worlds, no
other of them has), the generator logs a warning saying why:
Secret Shuffle's location with Secret Shuffle off, or a location your settings don't reach from
your start (OHKO leaves some out). Archipelago then doesn't place it there (with `force: true`,
generation stops). Plando into the locations around your start counts when the generator opens
your start ("Where does the game start?"). Your own Assist Unlock can't go in your own
locations.

Plando connections, bosses and texts don't apply: Zexion has no entrances to shuffle (choose the
start with `start_room`), its randomizer doesn't move bosses, and it has no hint texts.

## What do other players' items look like?

- A location holding another player's item shows the Archipelago logo, with a flashing up arrow
  if the item is important. Picking it up says PROGRESSION ITEM or AP ITEM, and whom it is for
  (FOR ALICE; in English(HD), Progression Item or AP Item, for Alice).
- Another Zexion player's item looks like the item itself.
- A trap looks important: another game's like its progression items, and a Zexion trap like one
  of its owner's upgrades, keys or OS Install Disks that's in the seed. That's on purpose: you
  can't tell it from a real one until you pick it up ("What do traps do?").
- On the map, other players' important items and your OS Install Disks are marked as important
  items (another Zexion player's supplies, the Energy Tanks, Missile Containers, Mega Bombs and
  Missile and Bomb Boxes, stay minor, as yours do). Once you have seen an item, the cursor says
  what it is (PROGRESSION ITEM, AP ITEM, or a Zexion item's name; in English(HD), Progression
  Item, AP Item). Before that, an important
  one reads as the game's own important items do under your seed's Obscure settings: UNKNOWN
  ITEM, as every item does, with Obscure Class on; otherwise ADVANCED TECH with Obscure Majors
  on (every preset mode has it), and its name with Obscure Majors off. Whose it is shows only
  when you pick it up.
- Items sent to you pop up with the sender's name (a trap too: "What do traps do?"). An Energy
  Shield sent to you comes up around you. Scrap sent to you drops what the game's own Scrap
  does, after its popup, once you have stayed on one map square for a second. Both, and the
  popups of items sent to you, wait out a teleport from the pause menu and come once you
  arrive (checked in the game for an Energy Shield and TELEPORT TO START; not yet for Scrap or
  popups). Scrap whose popup
  you saw before version 1.1.0, or that reached you before your first connection to the seed (it
  gets no popup), drops nothing, and nothing drops during the game's ending: what arrives then
  drops the next time you load the save.

## What do Area Maps and Intel Chips do?

With the Area Map Hints option (on by default), Area Maps become **Intel Chips**. Each Intel
Chip you get, found or sent to you, hints where one of your upgrades, keys or OS Install Disks
is that you don't have yet, in any world. After its popup (with who sent it, if it was sent),
a second shows the hint, such as HINT: SLIDE MOD IS AT GUARDIA 22,7 (in English(HD), Hint:
Slide Mod is at Guardia 22,7), and it goes into your hint list. When nothing is left to hint,
it says HINT: NO HINTS LEFT. Intel Chips don't reveal the map.

With the option off, Area Maps work as in the normal game: picking one up reveals the map around
the square it lay in. So they stay in your own world: the generator never places your Area Maps
in another player's world (as if they were in your `local_items`, which wins over your
`non_local_items`), and they don't count toward filling other players' excluded locations. An
item plando that puts one there isn't placed: the generator logs a warning saying why (not with
`force: silent`), and with `force: true` generation stops. An Area Map that still reaches you
from elsewhere, such as one sent from the server's console or through an item link (Area Maps in
an item link aren't kept this way), has no square in your world and reveals nothing.

## What do traps do?

Traps take the place of some of your Scrap: 15% of it by default, so usually 2 or 3 (1 to 4) in
an Easy, Standard or OHKO seed (Trap Percentage, `trap_percentage`; 0 for none). Each is one of
two, picked by their weights (`disruption_trap_weight`, `disorient_trap_weight`). With the OS
Install Disks goal the disks take the Scrap first, so there may be few traps or none. Traps are
filler, so excluded locations can hold them.

- **Disruption Trap:** what a Phazo's touch does, for about 12 seconds (a Phazo's lasts about 4):
  your energy weapons don't fire and your charge stays empty; missiles and Mega Bombs still
  work, and your HUD flashes. An Energy Shield doesn't stop it.
- **Disorient Trap:** what a Balorb's wave does, for about 12 seconds: its sound, then your
  controls are reversed, with swirls around your head; a flash and a sound come just before it
  wears off.

A trap goes off as its popup shows (DISRUPTION TRAP, FROM ALICE), once you are in play. Its popup
takes its turn after the popups that came before it, such as an OS Install Disk's and its count, so
the trap comes after them (a second of the same kind shows its popup as it starts, and the popups
after it don't wait for it meanwhile). It waits through the pause menu, other menus, a death or a
load, and through RETURN HOME WITH THE DISKS? and the game's ending (it comes at your next load
then). It also waits through a teleport from the pause menu, such as TELEPORT TO START (the other
TELEPORT TO entries work the same way but were not tried), and goes off once you arrive. Each trap
happens at most once, including one sent while your client was closed, or before you first connected
(it comes when you connect); if the client closes just as a trap goes off, it may come again; a trap
still waiting for its popup is lost if the game closes, or if the mod is reloaded while it waits for
a save that isn't loaded; and with no mod running a trap sent to you does nothing. A second trap of
the same kind waits for the first to end and then goes off, so each lasts its full 12 seconds. The
game's AILMENTS assist setting (pause, OPTIONS, ASSIST, where assists are allowed) doesn't change
traps: while one runs, it is left out for that effect, so an enemy's touch of the same kind during a
trap isn't shortened by it either.

In a Zexion world, a trap looks like an important item: one of yours, or another Zexion player's,
looks like one of that player's upgrades, keys or OS Install Disks that's in the seed (not one they
start with), on the map too, and another game's trap like that game's progression items. That's on
purpose: you can't tell a trap from a real item until you pick it up. Picking up a Zexion trap says
what it is (DISORIENT TRAP, and FOR BOB if it's Bob's), and one that looks like an OS Install Disk
never counts as a disk; another game's trap still says PROGRESSION ITEM. If none of those is in the
seed (its owner starts with, or leaves out, every upgrade, and has no key or disk in the seed), a
Zexion trap looks like any upgrade: maybe one they already have, or one not in the seed. Once the
scanner, or a map revealed in full (Hints Revealed, in Custom mode), has shown your keys on the map,
a trap that looks like one of your keys has no key marker there: just an important item's (checked
in the game with Hints Revealed; after the scanner, not yet checked). Picking up your own sets it
off at once; if you picked it up with the client closed, it goes off when you next connect with that
save loaded. Other games' traps sent to you do nothing in Zexion.

Checked in the game: both traps sent to you, at once, for about 12 seconds; two of one kind in a
row; one past a shield; AILMENTS at both settings not changing them; one sent while paused, while
dead or while the client was disconnected; one sent before you first connected, with the client
restarted before you played; your own trap and other players' in your world, their look (an upgrade,
a key or an OS Install Disk) and their pickup popups, and a disk-look trap not counting; with Hints
Revealed, a key-look trap having no key marker; a second trap's popup waiting for it; the client
closed while a trap waits or runs; a trap sent just after an OS Install Disk, coming after the
disk's popup and count; and one sent while paused, then TELEPORT TO START, RELOAD FROM CHECKPOINT or
RELOAD FROM SAVE, going off once you are back in play. Not checked in the game: a trap through
RETURN HOME or the ending, a key-look trap after the scanner has shown your keys, other popups
showing first while a trap waits for one of its kind to end, and the mod refusing to reload while a
trap waits to go off.

## Can I use the assist menu?

Not by default. ASSIST (pause, OPTIONS) is locked in your seed's saves. Turn on the Enable Assist
Options option (`allow_assists: true`) to allow it.

If a seed turns out to need the assists, the host can unlock them for good by sending you the
Assist Unlock item from the server console, with your player name:
`/send YOURNAME Assist Unlock`. On archipelago.gg, the console is the command box on the room
page, which only whoever created the room sees.

## How do I tell Archipelago saves apart?

On the file select, COPY FILE and DELETE FILE, each Archipelago save shows the Archipelago
logo to the right of its file number. Normal saves and plain randomizer saves don't. The mod
knows a save once you have connected to its seed at least once, and the logo shows with the
client closed too.

## Do Archipelago saves earn Feats and Steam achievements?

No Feats: the mod keeps Zexion's Feats for the game as it ships, so an Archipelago save's
ending earns none. The Feats of Natu screen may still come after it, showing your Feats so far
and ARCHIPELAGO SAVE: NO FEATS EARNED. If you have never seen the game's expanded ending, the
game puts a hint where the screen's title goes; the mod shows FEATS OF NATU! and that line there
instead. Your normal saves earn them as usual.

The mod also stops Steam achievements in Archipelago saves (for items, the map and the
ending). The ending's has been checked in game; the others so far only in the mod's own tests.
While `/reload_mod` reloads the mod, for a few seconds, they are not stopped.

This needs the mod in the game's folder. It holds with the client closed too, once you have
connected to that seed at least once: endings of Archipelago saves played with no client have
been checked in game. Plain randomizer saves are left alone: checked in game with the client
connected, not yet with it closed. Lore entries you unlock in the database stay unlocked
for the whole game, as they would in any save.

## Does Zexion support DeathLink?

Yes, with the DeathLink option. When you die, everyone else in the DeathLink dies too. When another
player dies, you die as soon as you are in play (after a teleport from the pause menu, once you
arrive: not yet tried in the game), even with an Energy Shield up. Deaths received together come one
at a time, after you respawn. Any still waiting when your ending begins, or sent during it, are
dropped, unless the client first saw your save after the ending began (opened or connected during
it): then they can still reach you during the ending. A death takes you back to your last save.
