# Zexion Setup Guide

This world is in development. Its client has run against a local Archipelago server but not
yet in a full game, so treat any seed as a test.

## Requirements

- Zexion on Steam (Windows), the build the client knows (it refuses others).
- Archipelago 0.6.8 or newer, with this world installed.
- The client's in-game helper, `zexion_ap.dll`. The client loads it into Zexion when it
  attaches; without it, use `/prepare` (below). Or let the game load it as it starts: with
  Zexion closed, copy `zexion_ap.dll` and `version.dll` from the same release into Zexion's
  folder (Steam: Zexion, Manage, Browse local files). Without the client running, a save that
  is not from a seed you have played with the client plays as it always does. To update, replace both files with the game closed; to remove them,
  delete both (or, to turn them off for a while, create an empty file named
  `zexion_ap.disabled` beside them). With the files there, a save of a seed you have played
  with the client keeps its look when you play it without the client: Archipelago items,
  Intel Chips, the ASSIST lock and your OS Install Disk count (counting disks you find in
  your own world), with a notice that the client is not connected. Items from other players,
  hints and finishing the goal wait until you run the client again; what you pick up is sent
  then, after a notice that the client is connected, and the popups it would have shown (a
  disk's count, an Intel Chip's hint) come then too. For this, the client has the helper keep a
  few small files in `%LOCALAPPDATA%\ZexionAP`; deleting that folder is safe (the client writes
  them again), and with `zexion_ap.disabled` the helper does not load at all. Once the client
  has connected with the helper running, the title screen shows ARCHIPELAGO MOD and the
  helper's version under the copyright line, from then on also when the game starts without
  the client, so you can see which helper is in; while no client is connected, it also
  says to start the client.

## Starting a game

The client starts the seed as a new game in an empty slot; it never changes a game you are
playing.

1. Start Zexion and stay at the title screen.
2. Start the client and connect to the server. Once it attaches to Zexion it says "Ready for a
   new game".
3. In Zexion, pick an empty slot. Where the game offers NORMAL and RANDOMIZER, it now offers
   NORMAL and **ARCHIPELAGO**: choose ARCHIPELAGO. You start in the seed's start room with its
   starting items, and the game saves the new game at once.

Once that game is in play, ARCHIPELAGO is offered again (the client says so), so another empty
slot can start a second save of the same seed; every save of the seed counts, whatever its slot.

This works for every mode. If you would rather start from the game's own menu, start the client
with `--keep-randomizer`: RANDOMIZER then stays, and choosing it, the seed's mode with no
options changed, and START GAME starts the seed the same way.

When the client disconnects, the choice reads RANDOMIZER again.

## Preparing a save instead

Without the helper, or for a seed whose starting items a new game cannot hold (the client
says so), the client writes the seed into a saved new game:

1. Pause Steam Cloud for Zexion (Steam, Library, Zexion, Properties, General), so the cloud
   keeps a copy of your saves as they were.
2. In Zexion, start a new randomizer game in an empty slot, in the mode your seed uses (Easy,
   Standard or OHKO, with no options changed), and save at once at the first save point.
3. Close Zexion.
4. Start the client, connect to the server, and type `/prepare` and the slot number, for
   example `/prepare 7`. The client backs the slot up first (into `zexion_backups` in your
   Archipelago folder) and refuses a slot that is not a new game of the seed's mode.
5. Start Zexion and load that slot.

## Playing

Keep the client running while you play. It sends each location as you collect it, gives you
the items other players find for you (only while you are in play, not in menus), and reports
your goal when the ending begins, as you leave on the ship after grabbing Zexion in the core
(with the OS Install Disks goal, when you answer YES to RETURN HOME WITH THE DISKS?, which
the helper offers when you save at the landing pod you started from with enough disks:
this goal needs the helper). If you die or reload, items received since your
last save are given again. Items that arrive once the ending has begun (as a release or
collect on your goal) are given the next time you load that save. With the DeathLink option on, the client also sends your deaths to
the other players and ends your life when one of them dies; the second part needs the helper.
With Area Map hints on (the default), each Intel Chip (the Area Map, renamed) you get makes an
Archipelago hint for one of your upgrades, keys or OS Install Disks, and the helper shows it in
a popup.

Play the seed's slot while the client is connected. If you load a save of a different
seed, the game says the client is for another seed, and nothing is sent or received
for that save. Without it (with the helper loaded with
the game) you can keep playing: what you pick up is sent when the client next connects with
that save loaded, but items from other players, hints and finishing the goal wait for it.
