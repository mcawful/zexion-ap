# Zexion Setup Guide

This world is in development. Seeds have been played through to the goal, but treat any seed
as a test.

## What you need

- Zexion on Steam (Windows). Each release works with certain versions of the game; the client
  tells you if yours isn't supported.
- Archipelago 0.6.8 or newer.
- A release of this world: `zexion.apworld`, `zexion_ap.dll` and `version.dll`, all three from
  the same release. There is no public release yet.

The two files `zexion_ap.dll` and `version.dll` are **the mod**. It is required: without it,
the game can't start a seed.

## Install

1. In the Archipelago Launcher, choose **Install APWorld** and pick `zexion.apworld`. The
   Launcher then lists **Zexion Client**.
2. Close Zexion. Copy `zexion_ap.dll` and `version.dll` into Zexion's folder (in Steam:
   right-click Zexion, Manage, Browse local files).

After you connect the client for the first time, the title screen shows ARCHIPELAGO MOD and
its version.

## Make your YAML and generate

1. In the Launcher, choose **Generate Template Options**. It writes `Zexion.yaml` into
   `Players/Templates` in your Archipelago folder.
2. Copy it into `Players`, then set your `name` and the options (see the game page).
3. Choose **Generate** in the Launcher. The game lands in `output`: host it with **Host** in
   the Launcher, or upload the `.zip` to archipelago.gg.

## Start playing

1. Start Zexion.
2. In the Launcher, open **Zexion Client**. Type the server address (for example
   `archipelago.gg:38281`), press **Connect**, then enter your slot name (the `name` from your
   YAML).
3. In Zexion, pick an empty save slot. Where the game offers NORMAL and RANDOMIZER, it now
   offers NORMAL and **ARCHIPELAGO**: choose ARCHIPELAGO. You start in the seed's start room
   (your landing pod) with its starting items, and the game saves at once.

To continue later, connect the client and load that save. You can start more saves of the
same seed the same way; they all count.

- Keep the client open while you play.
- Locations you check are sent at once.
- Items for you arrive while you play (not in menus).
- If you die or reload, items received since your last save are given again.
- The client says when your seed's save is loaded. In a save of another seed, nothing is sent,
  and items sent to you wait until you load your seed's save.

Goals, options and what's changed in the game: see the game page.

## Playing without the client

You can play your seed's save with the client closed, once you have connected to that seed at
least once. The game shows ARCHIPELAGO CLIENT NOT CONNECTED.

- Locations you check are sent the next time you connect with that save loaded, and so is
  your goal if you reached the core goal's ending meanwhile.
- Items from other players, hints and RETURN HOME WITH THE DISKS? wait for the client.

With the client closed, your other saves play as normal. While it is connected, every game
offers SKIP ESCAPE AND FINAL BATTLE? after you grab Zexion.

## Updating

Close Zexion, then replace all three files with the new release's: install the new
`zexion.apworld`, and copy the new `zexion_ap.dll` and `version.dll` into Zexion's folder.

## Turning the mod off or removing it

- **Turn it off for a while:** create an empty file named `zexion_ap.disabled` next to the two
  mod files. While it is there, neither the game nor the client loads the mod (the client says
  so), so you can't play a seed. Delete it to turn the mod back on.
- **Remove it:** close Zexion and delete `zexion_ap.dll` and `version.dll` from its folder.

## Troubleshooting

| Problem | What to do |
|---|---|
| The client says your version of Zexion isn't supported. | Steam probably updated the game. Wait for a new release of this world. |
| The client says the mod is the wrong version, or isn't loaded. | Close Zexion and copy this release's `zexion_ap.dll` and `version.dll` into its folder again. |
| There is no ARCHIPELAGO choice. | Connect the client first, and pick an empty slot. If the client says the mod isn't working, see the row above. |
| `version.dll` disappears, or your antivirus warns about it. | The mod isn't signed, so some antivirus tools flag it. Allow it, then copy it back. |
| Nothing is sent or received. | Check that the client is connected and the loaded save is from this seed. |

Found a bug? Report it at
[github.com/mcawful/zexion-ap/issues](https://github.com/mcawful/zexion-ap/issues).
