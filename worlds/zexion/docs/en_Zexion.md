# Zexion

## What does randomization do to this game?

Zexion has its own randomizer. This world uses its map and its rules for what each item opens
up, but lets Archipelago decide what is at each of the game's 154 item locations. Items found
in Zexion can belong to other players, and their games can send items to you.

## What is the goal?

Reaching the ending: the first cutscene after you board the ship. Getting there means grabbing
Zexion in the core, which takes the Alpha, Beta, Gamma and Omega Keys, then the escape and the
final battle. The seed's logic only makes sure you can reach the grab. While the client's
in-game helper runs, the game offers SKIP ESCAPE AND FINAL BATTLE? at the grab, which takes you
to just before the ship. Without it, a first playthrough gets no such offer, and the escape
needs one of several combinations of items, such as the Zenith Thrusters, which the logic does
not ask for.

## Which modes are there?

The Mode option takes the game's own Easy, Standard or OHKO mode, with that mode's item pool,
starting items and settings. OHKO starts with no Energy Tanks and a second teleport target,
and a few of its locations cannot be reached at all; those are left out. The spoiler log names
the start room and, in OHKO, the second teleport target.

## Where does the game start?

In one of the game's own start rooms, chosen with the Start Room option. Only rooms from which
the mode's starting items reach a few locations are offered. Each is named by its area and its
X and Y as the pause screen shows them, as in `kresezoa_16_13`; locations are named the same
way, as in "Guardia 22,7", so you can find them on the in-game map.

## What does another world's item look like in Zexion?

With the client's in-game helper running, a location holding another player's item shows
Archipelago's logo, with its flashing up arrow if the item is a progression item, and picking it
up pops up PROGRESSION ITEM or AP ITEM and whom it is for ("FOR ALICE"), or, for another Zexion
player's item, that item's own icon and name; the client then sends it to its owner. Items for you arrive while you play, with the
game's own popup and the sender's name. Without the helper, another player's item looks like
Scrap Energy (Scrap Chunk for a progression item), and items for you arrive with no popup.

## What do Area Maps do?

With the Area Map Hints option (on by default), each Area Map is a hint. Every Area Map you
get, found in your world or sent by another player, reveals where one of your upgrades or keys
is that you do not have yet, in any world (supplies such as Missile Containers and Energy Tanks
are not hinted). The hint shows in your client's and every player's hint list, and with the
client's in-game helper running, a popup names it as the hint list does, such as "HINT:
SLIDE MOD IS AT GUARDIA 22,7" for one in your world or "HINT: SLIDE MOD IS AT <ITS LOCATION>
IN ALICE'S WORLD" for one in another; an Area Map you pick up in your world should show the
game's own popup first, then that one (not tried in the game yet). Without the helper the hint
is still made, with no popup. Once every such item has been hinted or found, an Area Map says
HINT: NO HINTS LEFT. With the option off,
an Area Map is filler that counts as Scrap Metal.

## Can I use the assist menu?

Not by default. The game's assist menu (pause, OPTIONS, ASSIST) has save states and settings
that make the game easier; with the client's in-game helper running, ASSIST is greyed out
with a padlock, the cursor skips it, and it does not open while your seed's save is loaded, so the assists stay at their
defaults. A seed
generated with the Allow Assists option leaves it alone. Without the helper, nothing stops it.

If a locked seed turns out to need the assists, the host can lift the lock for good by sending
you the Assist Unlock item from the server console: `/send YOURNAME Assist Unlock`. It is never
placed in the seed, so it only arrives that way.

## Does Zexion support DeathLink?

Yes, with the DeathLink option. When you die, everyone else in the DeathLink dies too; when
another player dies, you die as soon as you are in play, even with an Energy Shield up. Deaths
received together queue: each one kills you in turn, after you respawn. A death
in Zexion takes you back to your last save, so it costs everything since you saved. Dying from
another player's death needs the client's in-game helper: without it, that death waits until
the helper runs again.
