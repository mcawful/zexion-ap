# Zexion Setup Guide

This world is in development. Its client has run against a local Archipelago server but not
yet in a full game, so treat any seed as a test.

## Requirements

- Zexion on Steam (Windows), the build the client knows (it refuses others).
- Archipelago 0.6.8 or newer, with this world installed.

## Preparing a save

The client writes the seed into a new randomizer game of yours; it never changes a game you
are playing.

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
your goal when you grab Zexion in the core. If you die or reload, items received since your
last save are given again.

Only play the prepared slot while the client is connected.
