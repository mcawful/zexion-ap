# Zexion

## What does randomization do to this game?

Zexion has its own randomizer. This world uses its map and its rules for what each item opens
up, but lets Archipelago decide what is at each of the game's 154 item locations (150 in
OHKO). Items found in Zexion can belong to other players, and their games can send items to
you. Excluded locations (`exclude_locations`) can hold only filler (Scrap, Area Maps or Intel
Chips): excluding more locations than there is filler stops generation with an error that says
so.

You need the Zexion Archipelago mod and the Zexion Client: see the setup guide
(`zexion-setup-guide.md` in the release).

## What is the goal?

Set with the Goal option.

- **Core** (default): find the Alpha, Beta, Gamma and Omega Keys, open the core and grab
  Zexion. The game then asks SKIP ESCAPE AND FINAL BATTLE?: YES takes you to just before the
  ship. Board the ship: reaching the ending is your goal. The seed only makes sure you can
  reach Zexion, not that you have the items the escape needs (such as the Zenith Thrusters),
  so answer YES unless you have them.
- **OS Install Disks**: find enough OS Install Disks (the options set how many are placed and
  how many you need). The four keys are not in the seed.
  - Your count shows on each disk's popup, on the pause screen and on the file select.
  - The scanner room at Vallahar 6,8, which shows where the keys are in the normal game, tells
    you how many you need. Until you have been there, the pause screen shows "OF ?".
  - Once you have enough, save at the start room (your landing pod) and answer YES to RETURN
    HOME WITH THE DISKS?. The game's ending is your goal. NO lets you keep exploring; the
    question comes back the next time you save there.
  - Each disk takes the place of a filler item (Scrap, then Area Maps or Intel Chips), and
    excluded locations can hold only filler: with many disks, excluding many locations stops
    generation with an error that says so (the OS Install Disks option gives the numbers).

## Which modes are there?

The Mode option picks the game's own Easy, Standard or OHKO mode, with that mode's items,
starting items and settings. OHKO has no Energy Tanks and gives you a second room to teleport
to; the 4 locations it can't reach are left out.

## Where does the game start?

In one of the game's own start rooms, chosen with the Start Room option: by default
`kresezoa_16_13`. Easy and Standard can start in 9 of them, OHKO in all 34. If your mode can't
use the room you pick, a random one it can use is chosen. The spoiler log names the start room
(and OHKO's second room).

Rooms are named by their area and their X and Y as the pause screen shows them, as in
`kresezoa_16_13`. Locations are named the same way, as in "Guardia 22,7", so you can find them
on the in-game map.

## Can I add starting items?

Yes, with `start_inventory`, as long as a new game can start with them (counted together with
the mode's own starting items):

- one of each upgrade, and at most two of each weapon (the second is its upgrade);
- Energy Tanks, Missile Boxes, and up to 9 Missile Containers;
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
  Missile and Bomb Boxes, stay minor, as yours do), and the cursor says what each one is
  (PROGRESSION ITEM, AP ITEM, or a Zexion item's name). Whose it is shows only when you pick it
  up.
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

This needs the mod in the game's folder. It is meant to hold with the client closed too, once
you have connected to that seed at least once, and to leave plain randomizer saves alone; both
are not yet checked in game. Lore entries you unlock in the database stay unlocked
for the whole game, as they would in any save.

## Does Zexion support DeathLink?

Yes, with the DeathLink option. When you die, everyone else in the DeathLink dies too. When
another player dies, you die as soon as you are in play, even with an Energy Shield up. Deaths
received together come one at a time, after you respawn; any still waiting when your ending
begins are dropped. A death takes you back to your last save.
