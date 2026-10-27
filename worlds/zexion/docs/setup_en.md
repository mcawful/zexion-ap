# Zexion Setup Guide

This world is in development. Seeds have been played through to the goal, but treat any seed
as a test.

## What you need

- Zexion on Steam (Windows). Each release works with certain versions of the game; the client
  tells you if yours isn't supported.
- Archipelago 0.6.8 or newer.
- The latest release of this world, from the
  [Releases page](https://github.com/mcawful/zexion-ap/releases): `zexion.apworld`,
  `zexion_ap.dll` and `version.dll`, all three from the same release. Each release also has
  this guide (`zexion-setup-guide.md`) and the game page (`zexion-game-page.md`).

The two files `zexion_ap.dll` and `version.dll` are **the mod**. It is required: without it,
the game can't start a seed.

## Install

1. In the Archipelago Launcher, choose **Install APWorld** and pick `zexion.apworld`. The
   Launcher then lists **Zexion Client**.
2. Close Zexion. Copy `zexion_ap.dll` and `version.dll` into Zexion's folder (in Steam:
   right-click Zexion, Manage, Browse local files).

Right after installing, the title screen looks as it always did. Once the client has connected
while Zexion runs (Start playing, below), the title screen shows ARCHIPELAGO MOD and its
version: the mod is in.

## Make your YAML

1. In the Launcher, choose **Generate Template Options**. It writes `Zexion.yaml` into
   `Players/Templates` in your Archipelago folder.
2. Copy it into `Players`, then set your `name` (your player name) and the options (see the
   game page, `zexion-game-page.md`).

## Generate and host

- **Your own game:** choose **Generate** in the Launcher. The game lands in `output`: host it
  with **Host** in the Launcher, or upload the `.zip` to archipelago.gg.
- **Someone else's game:** send them your YAML. To generate the game, they need
  `zexion.apworld` from the same release as yours installed in their Archipelago.
  archipelago.gg can host the generated game, but can't generate it: Zexion isn't one of its
  games.

## Start playing

1. Start Zexion.
2. In the Launcher, open **Zexion Client**. Type the server address (for example
   `archipelago.gg:38281`), press **Connect**, then enter your player name (the `name` from
   your YAML).
3. In Zexion, pick an empty save slot. Where the game offers NORMAL and RANDOMIZER, it now
   offers NORMAL and **ARCHIPELAGO**: choose ARCHIPELAGO. You start in the seed's start room
   (your landing pod) with its starting items, and the game saves at once.

To continue later, connect the client and load that save. You can start more saves of the
same seed the same way; they all count. On the file select, Archipelago saves show the
Archipelago logo beside the file number.

- Keep the client open while you play.
- Locations you check are sent at once.
- Items for you arrive while you play (not in menus).
- If you die or reload, you don't lose the items sent to you: they're given back.
- The client says when your seed's save is loaded. When you leave it, or play a save of
  another seed, nothing is sent, and items sent to you wait until you load your seed's save
  again. Plain (not Archipelago) normal or randomizer saves are left alone.
- Archipelago saves earn no Feats, and the mod stops Steam achievements in them too (see the
  game page); your normal saves earn them as usual.

Goals, options and what's changed in the game: see the game page (`zexion-game-page.md`).

## Playing without the client

You can keep playing your seed's save with the client closed: what you find is sent the next
time you connect. This works once you have connected to that seed at least once; the game then
shows ARCHIPELAGO CLIENT NOT CONNECTED.

- Locations you check are sent the next time you connect with that save loaded, and so is
  your goal if you reached the core goal's ending meanwhile.
- Items from other players, hints and RETURN HOME WITH THE DISKS? wait for the client.

With the client closed, your other saves play as normal. While it is connected, every game
offers SKIP ESCAPE AND FINAL BATTLE? after you grab Zexion.

## Updating

- Finish your seed first, or keep the old release until it ends: a seed made with an older
  release may not play with a newer one (the client says so). Every Zexion player in a game
  uses the same release.
- To update: download the new release from the Releases page, close Zexion, then replace all
  three files with the new release's. Install the new `zexion.apworld`, and copy the new
  `zexion_ap.dll` and `version.dll` into Zexion's folder.

## When Steam updates Zexion

The client may then say your version of Zexion isn't supported: wait for a new release of this
world. Meanwhile Zexion plays as normal, and your saves of a seed are safe: the client and the
mod leave a version of the game they don't know alone.

## Turning the mod off or removing it

You don't need to turn the mod off to play normally: with the client closed, your other saves
play as normal.

- **Turn it off for a while:** close Zexion, then create an empty file named
  `zexion_ap.disabled` next to the two mod files. Windows may hide the end of file names, so check it isn't called
  `zexion_ap.disabled.txt` (in File Explorer: View, then turn on File name extensions). While
  it is there, neither Zexion nor the client loads the mod (the client says so). Your saves of
  a seed still play with the client, but items arrive without popups, you can't start a
  new game of a seed, and they can earn Feats and Steam achievements. Delete the file to turn
  the mod back on.
- **Remove it:** close Zexion and delete `zexion_ap.dll` and `version.dll` from its folder.
  Your saves of a seed still load, as plain randomizer games (other players' items and OS
  Install Disks look like Scrap), and they can earn Feats and Steam achievements again. To
  play a seed again, install the mod again.

## Troubleshooting

| Problem | What to do |
|---|---|
| The client says your version of Zexion isn't supported. | Steam probably updated the game. Wait for a new release of this world. |
| The client says Zexion is running as administrator. | Start Zexion normally, or run the Archipelago Launcher as administrator too. |
| The client says the mod is the wrong version, or isn't installed. | Close Zexion and copy this release's `zexion_ap.dll` and `version.dll` into its folder again. |
| The client says the mod didn't start. | Restart Zexion. If it keeps happening, copy the two files again as in the row above. |
| The mod misbehaves: popups, labels or menus look wrong while the client is connected. | Type `/reload_mod` in the client. It reloads the mod in a few seconds; popups still waiting are lost, items are not. If it says it can't now, close any menu or the map and try again. |
| The client says the mod is turned off. | Delete `zexion_ap.disabled` from Zexion's folder. |
| There is no ARCHIPELAGO choice. | Connect the client first, and pick an empty save slot. If the client says something about the mod, see the rows above. |
| The client says to start the seed from RANDOMIZER. | In an empty save slot, choose RANDOMIZER, pick your seed's mode (Easy, Standard or OHKO, as in your YAML), change no other options, and choose START GAME. |
| `version.dll` disappears, or your antivirus warns about it. | The mod isn't signed, so an antivirus may flag it. If Windows Security removed it, restore it: Windows Security, Virus & threat protection, Protection history, then open the entry and choose Restore. Check that the file is back in Zexion's folder, or copy it there again. |
| Nothing is sent or received. | Check that the client is connected and the loaded save is from this seed. |

## Reporting a bug

Report it at [github.com/mcawful/zexion-ap/issues](https://github.com/mcawful/zexion-ap/issues),
and attach the client's log: in your Archipelago folder, the `logs` folder has one file per
client run, named `ZexionClient_` and the date and time it started.
