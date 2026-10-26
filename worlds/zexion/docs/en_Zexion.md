# Zexion

## What does randomization do to this game?

Zexion has its own randomizer. This world uses its map and its rules for what each item opens
up, but lets Archipelago decide what is at each of the game's 154 item locations. Items found
in Zexion can belong to other players, and their games can send items to you.

## What is the goal?

With the Goal option's default, core: reaching the ending, the first cutscene after you board
the ship. Getting there means grabbing
Zexion in the core, which takes the Alpha, Beta, Gamma and Omega Keys, then the escape and the
final battle. The seed's logic only makes sure you can reach the grab. While the client's
in-game helper runs, the game offers SKIP ESCAPE AND FINAL BATTLE? at the grab, which takes you
to just before the ship. Without it, a first playthrough gets no such offer, and the escape
needs one of several combinations of items, such as the Zenith Thrusters, which the logic does
not ask for.

With OS Install Disks: find enough OS Install Disks, shuffled through the multiworld (how many
are placed and how many win are options). The four keys are left out. This goal needs the
client's in-game helper: once you have enough disks, the game tells you to save at the
landing pod you started from, and saving there asks RETURN HOME WITH THE DISKS?. YES plays
the game's ending, which is your goal; NO lets you keep exploring, and the question comes
back the next time you save at the pod. The disks are not put in the game's
inventory: the client counts them, each one's popup gives your count, the equipment screen
shows them beside the Energy Tank, and the pause screen shows the count where the keys were,
as does the file select for your saves of the seed once you have one.
The room that showed you where the keys are tells you how many disks you need; until you have
been there the popups give the count alone and the pause screen shows "OF ?".

## Which modes are there?

The Mode option takes the game's own Easy, Standard or OHKO mode, with that mode's item pool,
starting items and settings. OHKO starts with no Energy Tanks and a second teleport target,
and a few of its locations cannot be reached at all; those are left out. The spoiler log names
the start room and, in OHKO, the second teleport target.

Extra starting items (`start_inventory`), counted together with the mode's own, must be ones a
new game can start with: one of each upgrade, at most two of each weapon (the second copy
gives its upgrade), Energy Tanks, Missile Boxes, up to 9 Missile Containers, keys from the
Alpha Key on (one of each), and OS Install Disks with their goal. Generation stops with an error for anything else, such as Scrap, Area Maps, Intel
Chips, Energy Shields, Mega Bombs or Bomb Boxes, or Assist Unlock (use the Allow Assists
option instead).

## Where does the game start?

In one of the game's own start rooms, chosen with the Start Room option. Easy and Standard
accept only the rooms from which their starting items reach a few locations; OHKO accepts all
34 and picks a second teleport target that makes up the rest. A room the mode does not accept
is replaced by one it does, at random. Each is named by its area and its
X and Y as the pause screen shows them, as in `kresezoa_16_13`; locations are named the same
way, as in "Guardia 22,7", so you can find them on the in-game map.

## What does another world's item look like in Zexion?

With the client's in-game helper running, a location holding another player's item shows
Archipelago's logo, with its flashing up arrow if the item is a progression item, and picking it
up pops up PROGRESSION ITEM or AP ITEM and whom it is for ("FOR ALICE"), or, for another Zexion
player's item, that item's own icon and name; the client then sends it to its owner. On the
map, progression items and your OS Install Disks show as important items, and the cursor
names every location holding another player's item or your disk (OS INSTALL DISK, or the item
and FOR ALICE). With the
OS Install Disks goal, your own disks in your world look and pop up as OS Install Disks (as a
Scrap Chunk without the helper) and are counted, not put in the game's inventory. Items for you arrive while you play, with the
game's own popup and the sender's name; an Energy Shield sent to you comes up around you.
Without the helper, another player's item looks like Scrap Energy (Scrap Chunk for a
progression item), and items for you arrive with no popup, except Energy Shields, which wait
until the helper runs.

## What do Area Maps and Intel Chips do?

With the Area Map Hints option (on by default), the Area Maps become Intel Chips, and each one
is a hint. Every Intel Chip you get, found in your world or sent by another player, reveals
where one of your upgrades, keys or OS Install Disks is that you do not have yet, in any world
(supplies such as Missile Containers and Energy Tanks are not hinted). The hint shows in your
hint list and in that of the player whose world holds the item, and with the client's in-game helper running, a popup
names it as the hint list does, such as "HINT: SLIDE MOD IS AT GUARDIA 22,7" for one in your
world or "HINT: SLIDE MOD IS AT <ITS LOCATION> IN ALICE'S WORLD" for one in another; one you
pick up in your world shows the game's own popup first, then that one. In the game an Intel
Chip looks like the Area Map, named INTEL CHIP, and does not reveal the map around it (with
the helper; without it the game shows it as AREA MAP and reveals the map as usual). Without
the helper the hint is still made, with no popup. Once every such item has been named by an
Intel Chip or is yours, an Intel Chip says HINT: NO HINTS LEFT (hints asked for with `!hint`
do not count). With the option off, Area Maps stay Area Maps:
filler that counts as Scrap Metal.

## Can I use the assist menu?

Not by default. The game's assist menu (pause, OPTIONS, ASSIST) has save states and settings
that make the game easier; with the client's in-game helper running, ASSIST is grayed out
with a padlock, the cursor skips it, and it does not open while your seed's save is loaded, so the assists stay at their
defaults. A seed
generated with the Allow Assists option leaves it alone. Without the helper, nothing stops it.

If a locked seed turns out to need the assists, the host can lift the lock for good by sending
you the Assist Unlock item from the server console (on archipelago.gg, the room page's command
box): `/send YOURNAME Assist Unlock`. It is never placed in the seed, so it only arrives that
way; placed in your own world by plando it does nothing, as the client acts only on items
from other worlds.

## Does Zexion support DeathLink?

Yes, with the DeathLink option. When you die, everyone else in the DeathLink dies too; when
another player dies, you die as soon as you are in play, even with an Energy Shield up. Deaths
received together queue: each one kills you in turn, after you respawn. A death
in Zexion takes you back to your last save, so it costs everything since you saved. Dying from
another player's death needs the client's in-game helper: without it, that death waits until
the helper runs again.
