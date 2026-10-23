# Zexion Setup Guide

This world is in development. Its client has run against a local Archipelago server but not
yet in a full game, so treat any seed as a test.

## Requirements

- Zexion on Steam (Windows), the build the client knows (it refuses others).
- Archipelago 0.6.8 or newer, with this world installed.
- The client's in-game helper, `zexion_ap.dll`. The client loads it into Zexion when it
  attaches; without it, use `/prepare` (below).

## Starting a game

The client starts the seed as a new game in an empty slot; it never changes a game you are
playing.

1. Start Zexion and stay at the title screen.
2. Start the client and connect to the server. Once it attaches to Zexion it says "Ready for a
   new game".
3. In Zexion, pick an empty slot. Where the game offers NORMAL and RANDOMIZER, it now offers
   NORMAL and **ARCHIPELAGO**: choose ARCHIPELAGO. You start in the seed's start room with its
   starting items, and the game saves the new game at once.

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
your goal when the ending begins, as you leave on the ship after grabbing Zexion in the core. If you die or reload, items received since your
last save are given again.

Only play the seed's slot while the client is connected.
