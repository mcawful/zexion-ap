# Zexion

## What does randomization do to this game?

Zexion has its own randomizer. This world uses its map and its rules for what each item opens
up, but lets Archipelago decide what is at each of the game's 154 item locations (150 in
OHKO). Items found in Zexion can belong to other players, and their games can send items to
you. Excluded locations (`exclude_locations`) can hold only filler (Scrap, Area Maps or Intel
Chips): exclude more locations than you have filler, and generation stops with an error that
says so, unless the other players' filler makes up the difference.

You need the Zexion Archipelago mod and the Zexion Client: see the setup guide
(`zexion-setup-guide.md` in the release).

## What is the goal?

Set with the Goal option.

- **Core** (default): find the Alpha, Beta, Gamma and Omega Keys, open the core and grab
  Zexion, then escape, win the final battle and board the ship: reaching the ending is your
  goal. The seed only makes sure you can reach the keys and Zexion, not that you have the
  items the escape needs (such as the Zenith Thrusters): the escape and final battle are yours
  to finish.
- **OS Install Disks**: find enough OS Install Disks (the options set how many are placed and
  how many you need). The four keys are not in the seed.
  - Your count shows on each disk's popup, on the pause screen and on the file select.
  - The scanner room at Vallahar 6,8, which shows where the keys are in the normal game, tells
    you how many you need. Until you have been there, the pause screen shows "OF ?".
  - Once you have enough, save at the start room (your landing pod) and answer YES to RETURN
    HOME WITH THE DISKS?. The game's ending is your goal. NO lets you keep exploring; the
    question comes back the next time you save there.
  - Each disk takes the place of another item: Scrap, then Area Maps or Intel Chips, then in
    Standard past 25 disks Energy Shields. Excluded locations can hold only filler, so with many
    disks, excluding many locations can stop generation, as above (the OS Install Disks option
    gives the numbers).

## Which modes are there?

The Mode option picks the game's own Easy, Standard or OHKO mode, with that mode's items,
starting items and settings, or Custom. OHKO has no Energy Tanks and gives you a second room to
teleport to; the 4 locations it can't reach are left out.

With `mode: custom`, every setting of the game's randomizer menu is yours, named as the menu
names it: the six amounts (Starting Energy Tanks to Item Pool Mega Bombs), each of the 27
items as `start`, `random`, `pool` or `excluded` (`custom_items`), and the settings of Edit
Options (Random Gun, Box Compression, Hints, Gates, Start Keys, X Crystals, X Blocks, Abridged
Finale and the rest). Each starts as `preset`: the value of `custom_base`, the mode Custom
starts from. The seed is built from them as the game builds its own: the coin flips, Random
Gun's draw, the boxes and the filler. Settings the game's menu cannot hold (more than 40
Energy Tanks, more missiles than the pool has room for, and so on) stop generation with an
error that names them. Not yet: Guns First off, Secret Shuffle on, and starting Mega Bombs.
Abridged Finale ends the game when you grab Zexion, so it can't go with the OS Install Disks
goal.

With Easy, Standard or OHKO, only Starting Location (`start_room`) and Safety can be changed,
as in the game (OHKO takes Safety `light` or `off`).

## Where does the game start?

In one of the game's 34 start rooms, chosen with the Starting Location option (`start_room`),
by its name in the game's menu, as in `landing_pod`: by default Landing Pod. Any room works
from which your starting items reach at least 3 locations (and, with the core goal, from
which you can reach Zexion's core); another stops generation with an error. With Easy's or
Standard's items that is 9 of the rooms; with a second room to teleport to, all 34. `random_start` (or
`random`) picks a room as the game's Random Start does. The spoiler log names the start room
(and a second room to teleport to, with Double Teleport, as in OHKO). The earlier names, such
as `kresezoa_16_13` (the area, then X and Y as the pause screen shows them), still work.

Locations are named by their area and their X and Y as the pause screen shows them, as in
"Guardia 22,7", so you can find them on the in-game map.

## Can I add starting items?

Yes, with `start_inventory`, as long as a new game can start with them (counted together with
the mode's own starting items):

- one of each upgrade, and at most two of each weapon (the second is its upgrade);
- Energy Tanks, Missile Boxes, and up to 9 Missile Containers (Custom's starting missiles
  come as Missile Boxes of ten and the rest as containers, and count toward the 9);
- keys, in order from the Alpha Key;
- OS Install Disks, with the OS Install Disks goal: they count toward the disks you need.

Anything else, such as Scrap, Area Maps, Intel Chips, Energy Shields, Mega Bombs, Bomb Boxes
or Assist Unlock, stops generation with an error that names it.

## What do other players' items look like?

- A location holding another player's item shows the Archipelago logo, with a flashing up arrow
  if the item is important. Picking it up says PROGRESSION ITEM or AP ITEM, and whom it is for
  (FOR ALICE).
- Another Zexion player's item looks like the item itself.
- On the map, other players' important items and your OS Install Disks are marked as important
  items (another Zexion player's supplies, the Energy Tanks, Missile Containers, Mega Bombs and
  Missile and Bomb Boxes, stay minor, as yours do). Once you have seen an item, the cursor says
  what it is (PROGRESSION ITEM, AP ITEM, or a Zexion item's name); before that, ADVANCED TECH
  or UNKNOWN ITEM, as for the game's own items. Whose it is shows only when you pick it up.
- Items sent to you pop up with the sender's name. An Energy Shield sent to you comes up
  around you.

## What do Area Maps and Intel Chips do?

With the Area Map Hints option (on by default), Area Maps become **Intel Chips**. Each Intel
Chip you get, found or sent to you, hints where one of your upgrades, keys or OS Install Disks
is that you don't have yet, in any world. A popup shows the hint, such as HINT: SLIDE MOD IS AT
GUARDIA 22,7, and it goes into your hint list. When nothing is left to hint, it says HINT: NO
HINTS LEFT. Intel Chips don't reveal the map.

With the option off, Area Maps work as in the normal game.

## Can I use the assist menu?

Not by default. ASSIST (pause, OPTIONS) is locked in your seed's saves. Turn on the Allow
Assists option to allow it.

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
and ARCHIPELAGO SAVE: NO FEATS EARNED. Your normal saves earn them as usual.

The mod also stops Steam achievements in Archipelago saves (for items, the map and the
ending). The ending's has been checked in game; the others so far only in the mod's own tests.
While `/reload_mod` reloads the mod, for a few seconds, they are not stopped.

This needs the mod in the game's folder. It is meant to hold with the client closed too, once
you have connected to that seed at least once, and to leave plain randomizer saves alone; both
are not yet checked in game. Lore entries you unlock in the database stay unlocked
for the whole game, as they would in any save.

## Does Zexion support DeathLink?

Yes, with the DeathLink option. When you die, everyone else in the DeathLink dies too. When
another player dies, you die as soon as you are in play, even with an Energy Shield up. Deaths
received together come one at a time, after you respawn; any still waiting when your ending
begins are dropped. A death takes you back to your last save.
