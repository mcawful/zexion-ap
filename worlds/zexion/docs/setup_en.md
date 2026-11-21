# Zexion Setup Guide

## What you need

- Zexion on Steam, Steam build 23943567: the only version of the game this release supports,
  on Windows or Linux (and the Steam Deck). The client tells you if yours isn't supported.
- Archipelago 0.6.8 or newer, for your system.
- The latest release of this world, from the
  [Releases page](https://github.com/mcawful/zexion-ap/releases): `zexion.apworld` and the mod
  for your system, from the same release. Each release also has this guide
  (`zexion-setup-guide.md`), the game page (`zexion-game-page.md`) and a sample YAML for each
  mode.

**The mod** is `zexion_ap.dll` and `version.dll` on Windows, and `libzexion_ap.so` on Linux and
the Steam Deck. It is required: without it, the game can't start a seed.

## Install

1. In the Archipelago Launcher, choose **Install APWorld** and pick `zexion.apworld`. The
   Launcher then lists **Zexion Client**.
2. Close Zexion. Copy `zexion_ap.dll` and `version.dll` into Zexion's folder (in Steam:
   right-click Zexion, Manage, Browse local files). On Linux or the Steam Deck, copy
   `libzexion_ap.so` there instead and set the launch option: "Linux and Steam Deck", below.

Once the mod is in, the title screen shows ARCHIPELAGO MOD and its version. It also says START
THE AP CLIENT TO PLAY A SEED until the client connects, and again once you close the client (in
English(HD), in its font: Archipelago Mod and Start the AP client to play a seed). The mod is meant
to find what it needs in the game by itself, so this should show the first time Zexion starts. If
it doesn't, connect the client once (Start playing, below) and start Zexion again; if the title
screen still looks as it always did, the mod isn't loaded: check that both files are in Zexion's
folder (Troubleshooting, below).

## Make your YAML

1. Take the release's sample YAML for your mode: `zexion-easy.yaml`, `zexion-standard.yaml`,
   `zexion-ohko.yaml` or `zexion-custom.yaml`. Each lists the options its mode can change with
   what they do, set to their defaults, so it works as it is. Easy, Standard and OHKO leave out
   Custom mode's settings, which they can't change; Custom's start as Standard's. The
   Launcher's **Generate Template Options** writes similar files, with Custom mode's settings
   in each, into `Players/Templates/Presets` in your Archipelago folder (`Zexion - Easy.yaml`
   and so on), and a plain `Zexion.yaml` into `Players/Templates`.
2. Copy it into `Players`, then set your `name` (your player name) and any options you want to
   change (see the game page, `zexion-game-page.md`). On Linux, with Archipelago's AppImage,
   your Archipelago folder (`Players`, `output` and `logs`) is `~/.local/share/Archipelago`
   (or `Archipelago` in `$XDG_DATA_HOME`, if you set that); with the `.tar.gz`, it is the
   `Archipelago` folder it unpacks to (or the AppImage's, if you can't write there).

## Generate and host

- **Your own game:** choose **Generate** in the Launcher. The game lands in `output`: host it
  with **Host** in the Launcher, or upload the `.zip` to archipelago.gg.
- **Someone else's game:** send them your YAML. To generate the game, they need
  `zexion.apworld` from the same release as yours installed in their Archipelago.
  archipelago.gg can host the generated game, but can't generate it: Zexion isn't one of its
  games.
- **Item plando:** a YAML with `plando_items` needs item plando turned on where the game is
  generated (`plando_options` in `host.yaml` including `items`), or the plando is skipped with
  a warning. What works and what generation refuses: the game page, `zexion-game-page.md`,
  "Can I use plando?".

## Start playing

1. Start Zexion.
2. In the Launcher, open **Zexion Client**. Type the server address (for example
   `archipelago.gg:38281`), press **Connect**, then enter your player name (the `name` from
   your YAML).
3. In Zexion, pick an empty save slot. Where the game offers NORMAL and RANDOMIZER, it now
   offers NORMAL and **ARCHIPELAGO**: choose ARCHIPELAGO. You start at the save point
   of the seed's start room with its starting items, and the game saves at once. If it still
   offers RANDOMIZER, start the seed from RANDOMIZER as the client says (Troubleshooting).

To continue later, connect the client and load that save. You can start more saves of the
same seed the same way; they all count. On the file select, Archipelago saves show the
Archipelago logo beside the file number.

- Keep the client open while you play.
- Locations you check are sent at once.
- Items for you arrive while you play (not in menus).
- If you die or reload, you don't lose the items sent to you: they're given back. Other
  players' items you had found stay found too (see the game page).
- The client says when your seed's save is loaded. When you leave it, or play a save of
  another seed, nothing is sent, and items sent to you wait until you load your seed's save
  again. Plain (not Archipelago) normal or randomizer saves are left alone.
- Archipelago saves earn no Feats, and the mod stops Steam achievements in them too (see the
  game page); your normal saves earn them as usual.

Goals, options and what's changed in the game: see the game page (`zexion-game-page.md`).

## Linux and Steam Deck

Zexion's Linux version, which Steam runs on Linux and the Steam Deck, takes the mod as
`libzexion_ap.so`, loaded by a launch option.

1. Install Archipelago for Linux, 0.6.8 or newer: from Archipelago's
   [releases page](https://github.com/ArchipelagoMW/Archipelago/releases), download the Linux
   build, either the `.AppImage` (then make it executable: in its Properties, or `chmod +x`) or
   the `.tar.gz` (then unpack it). Start the Launcher from it: the AppImage itself, or
   `ArchipelagoLauncher` in the unpacked folder. Then install `zexion.apworld` (Install, above).
2. Close Zexion. Copy `libzexion_ap.so` into Zexion's folder: in Steam, right-click Zexion,
   Manage, Browse local files. Note the folder's full path: copy it from the file manager's
   address bar. It is usually `/home/YOU/.local/share/Steam/steamapps/common/Zexion` (on the
   Steam Deck, `/home/deck/.local/share/Steam/steamapps/common/Zexion`), and with Steam installed
   from Flathub
   `/home/YOU/.var/app/com.valvesoftware.Steam/.local/share/Steam/steamapps/common/Zexion`. If
   Zexion is on an SD card or another drive, as is common on the Steam Deck, the path is
   different (an SD card's starts with `/run/media/`).
3. In Steam, right-click Zexion, Properties, General, and set its **Launch Options** to this
   line, with your folder's full path in it:

   ```
   LD_PRELOAD="/home/YOU/.local/share/Steam/steamapps/common/Zexion/libzexion_ap.so:$LD_PRELOAD" %command%
   ```

   Keep `:$LD_PRELOAD` and `%command%` as they are: `%command%` is where Steam puts the game,
   and `:$LD_PRELOAD` keeps anything else that is preloaded, such as Steam's overlay, loading
   too. If you add other launch options, keep this one first.
4. Start Zexion, then open **Zexion Client** from the Launcher, as in "Start playing" above. Run
   the client as your own user, not inside Steam (not as a non-Steam game).

On the Steam Deck, play in **Desktop Mode**, where the Launcher and the client run next to the
game. When the mod is in, Zexion's native Linux version's title screen is meant to show
ARCHIPELAGO MOD and START THE AP CLIENT TO PLAY A SEED the first time it starts, as on Windows,
with no client having connected (not yet seen on Linux from a first start: if it doesn't, it
shows once the client has connected; if it still doesn't, the mod isn't loaded: check the launch
option and that `libzexion_ap.so` is in the folder it names (Troubleshooting, below)). The
client's "Can't reach the running game" has the same causes.

The mod's small files for playing without the client are in `~/.local/share/ZexionAP` (with
Flathub's Steam, usually `~/.var/app/com.valvesoftware.Steam/.local/share/ZexionAP`). Playing
the Windows version under Proton instead is not tried and not supported: use the Linux version.

## Languages

The mod supports the game's default English and **English(HD)** (OPTION, Video, Language). In
English(HD) the mod's text (its popups, notices and menu text, the names of other players'
items) shows in the game's HD font, in mixed case, and its disk, padlock and Archipelago icons
in HD art. In the other languages the mod's text shows in that language's font, but in English
and in capitals (another Zexion player's items by that language's own names), and some of it
may not show (a font without Latin letters, say); the client
says so when it sees one. On Windows, received items were seen working in French and Japanese,
and the mod's text in French; on Linux, received items' popups in English(HD) and Japanese. If
something
shows wrong or not at all, switch to the default English and please report it (below).

## Playing without the client

You can keep playing your seed's save with the client closed: what you find is sent the next
time you connect. This works once you have connected to that seed at least once; the game then
shows ARCHIPELAGO CLIENT NOT CONNECTED (in English(HD), Archipelago client not connected.). The
same goes for a save you load while the client is
open but not connected (the server down, say), and for the one you are in when you disconnect.

- Other players' items are kept found only while the client is connected: with it closed,
  one you had found before a death or a reload is back in its room.
- Locations you check are sent the next time you connect with that save loaded, and so is
  your goal if you reached the core goal's ending meanwhile.
- Items from other players, hints and RETURN HOME WITH THE DISKS? wait for the client.

With the client closed, your other saves play as normal.

## Playing on more than one machine

With Steam Cloud on, your saves follow you to another computer, a Steam Deck or the other
operating system. The mod is meant to know your seed's saves there too, with no client run on
that machine: it keeps a small file for each seed a save of yours uses, named `ZexionAP-seed-`
and the seed's name, ending in `.sav`, in Zexion's save folder beside the saves, and Steam Cloud
syncs it with them. This is expected to work but has not been tried on two machines yet.

- Both machines need the mod, from a release with this feature.
- The file comes from the client, so it exists only once the client has connected for that seed.
  The first machine is expected to write it while Zexion runs with the mod, within a moment of the
  game holding a save of the seed (a new game of it, or a loaded save), with the client connected
  or not: closing the game after playing writes nothing more, so the mod must have been running
  while the game held that save. The file goes up when Steam syncs, which it is expected to do
  when you close Zexion: wait for the cloud to say it is up to date before you start it on the
  second machine.
- The save folder is `%APPDATA%\Gallant Leaf\Zexion` on Windows; on Linux
  `~/.local/share/Gallant Leaf/Zexion`, or Flatpak Steam's `~/.var/app/com.valvesoftware.Steam/.local/share/Gallant Leaf/Zexion`
  ("Linux and Steam Deck", above). Steam Cloud syncs that Linux folder only if it is where the
  game keeps its saves, which it is unless `XDG_DATA_HOME` points somewhere else.
- Leave these files alone. The mod writes only a copy for a seed one of your 30 saves uses,
  deletes nothing, and no longer updates a copy once no save uses its seed (so a few old ones
  may stay; delete them if you like).
- A copy keeps the free ASSIST menu and the larger OS Install Disks count it has seen: a room
  you host again, or a server restored from an older save, does not take them back.

If a save on the other machine still shows as a plain randomizer game (no Archipelago logo on the
file select, other players' items look like Scrap), the first machine did not write the file yet
or Steam did not sync it. Connect the client once with Zexion running and that seed's room up:
it gives the mod the seed's file again. Saves from before this feature need that once, as before.

## Updating

- Finish your seed first, or keep the old release until it ends: a seed made with an older
  release may not play with a newer one (the client says so). Every Zexion player in a game
  uses the same release.
- To update: download the new release from the Releases page, close Zexion, then replace all
  the files with the new release's. Install the new `zexion.apworld`, and copy the new
  `zexion_ap.dll` and `version.dll` into Zexion's folder (on Linux, the new `libzexion_ap.so`:
  the launch option stays as it is).
- Then connect the client once with Zexion running. It gives the mod what it needs to play your
  seed's saves with the client closed, such as where your own traps are, which go off at their
  pickup only once it has. (If you update from 1.0.0, the mod's taller messages may flicker as
  they open and close until you have.)

## When Steam updates Zexion

The client may then say your version of Zexion isn't supported: wait for a new release of this
world. Meanwhile Zexion plays as normal, and your saves of a seed are safe: the client and the
mod leave a version of the game they don't know alone.

## Turning the mod off or removing it

You don't need to turn the mod off to play normally: with the client closed, your other saves
play as normal.

- **Turn it off for a while:** close Zexion, then create an empty file named `zexion_ap.disabled`
  next to the mod's files (on Linux, next to `libzexion_ap.so`). Windows may hide the end of file
  names, so check it isn't called `zexion_ap.disabled.txt` (in File Explorer: View, then turn on
  File name extensions). On Windows, while it is there, neither Zexion nor the client loads the
  mod (the client says so). Your saves of a seed still play with the client, but items arrive
  without popups, you can't start a new game of a seed, and they can earn Feats and Steam
  achievements.
  On Linux it is different: the client can't reach the game at all while the mod is off. It says
  only "Can't reach the running game; trying again (details in the log).", nothing is sent or
  received, and your saves of a seed play as if the mod were removed (next item). Delete the
  file to turn the mod back on.
- **Remove it:** close Zexion and delete `zexion_ap.dll` and `version.dll` from its folder (on
  Linux, delete `libzexion_ap.so` and clear the launch option). Your
  saves of a seed still load, as plain randomizer games (other players' items, OS Install Disks
  and traps look like Scrap, and a trap does nothing: not one sent to you, and not your own
  when you pick it up), and they can earn Feats and
  Steam achievements again. To play a seed again, install the mod again. The mod's small files for
  playing without the client stay in `%LOCALAPPDATA%\ZexionAP` (on Linux, the folder named in
  "Linux and Steam Deck"); you can delete that folder. So do the copies named `ZexionAP-seed-`
  and ending in `.sav` in Zexion's save folder, which Steam Cloud syncs ("Playing on more than
  one machine"); delete those too if you don't want them, and none of your saves.

## Troubleshooting

| Problem | What to do |
|---|---|
| The client says your version of Zexion isn't supported. | Steam probably updated the game. Wait for a new release of this world. |
| The client says Zexion is running as administrator. | Start Zexion normally, or run the Archipelago Launcher as administrator too. |
| The client says the Zexion mod and the client don't match. | The mod's two files and `zexion.apworld` are from different releases; the client's message names both versions. Close Zexion and copy `zexion_ap.dll` and `version.dll` from the release of your `zexion.apworld` into its folder. If the message says the mod is the newer one, you can instead install that release's `zexion.apworld`. |
| The client says the mod isn't installed. | Close Zexion and copy this release's `zexion_ap.dll` and `version.dll` into its folder again. |
| On Linux, the client says "Can't reach the running game". | The mod isn't loaded, or is from another release. Check that `libzexion_ap.so` is in the folder the launch option names; that the launch option is as in "Linux and Steam Deck" (that folder's full path, with `:$LD_PRELOAD" %command%` after it); that no `zexion_ap.disabled` is beside it; and that it is from the same release as your `zexion.apworld` (when they differ, the client's log names both versions: copy that release's `libzexion_ap.so` into the folder). Close Zexion and start it again after a change. |
| On Windows, the title screen doesn't say ARCHIPELAGO MOD after Zexion starts. | Connect the client once while Zexion runs, then start Zexion again. If it still doesn't show, the mod isn't loaded: close Zexion, check that `zexion_ap.dll` and `version.dll` are both in its folder and that no `zexion_ap.disabled` is beside them, and start it again. If they are there and it still doesn't show, this release doesn't know your version of Zexion, or the mod couldn't find what it needs in it: read what the client says about the mod, and report it (below) if it names the game's addresses. |
| The client says the mod didn't start. | Restart Zexion. If it keeps happening, copy the two files again as in the "mod isn't installed" row above. |
| The mod misbehaves: popups, labels or menus look wrong while the client is connected. | On Linux, restart Zexion (the mod can't be reloaded there). On Windows, type `/reload_mod` in the client. It reloads the mod in a few seconds; popups still waiting are lost, and so are traps waiting for a save that isn't loaded; items are not. For those seconds Steam achievements are not stopped in an Archipelago save, so stand still and pick nothing up. If it says it can't now, close any menu or the map, or let a trap end, and try again. |
| On Windows, the client says the mod is turned off. | Delete `zexion_ap.disabled` from Zexion's folder. |
| There is no ARCHIPELAGO choice. | Connect the client first, and pick an empty save slot. If the client says something about the mod, see the rows above. If the client is connected and the game still offers RANDOMIZER, start the seed from RANDOMIZER (next row): the client's line on how to play says so too. |
| The client says to start the seed from RANDOMIZER. | In an empty save slot, choose RANDOMIZER, enter the 16-character seed the client's message gives, and choose START GAME. The menu's other settings don't matter: the game gets your seed's own. If the game says it couldn't generate a winnable seed and asks whether to play anyway, choose YES. When the client also offers ARCHIPELAGO, it shows the seed on a line of its own: you can try clicking that line in the client to copy it, then the menu's Paste; if that doesn't work, type the 16 characters. |
| The client says that new game isn't this seed. | The game was started from RANDOMIZER with another seed in the menu, so it is an ordinary randomizer game, not your Archipelago one. Start again in an empty save slot as the client's message says, with your seed's 16 characters. |
| The client says this seed's second teleport target isn't a room this game can offer. | The seed can't be started as a new game. Report it (below), with the client's log. |
| The client says that new game couldn't be made this seed. | Start another new game in an empty save slot. |
| The client says the mod couldn't fully turn off Feats and Steam achievements, or doesn't recognize this save as an Archipelago save. | Feats and Steam achievements may be earned in that save. Report it (below), with the client's log. |
| The client says it can't find what it needs in this version of Zexion. | Report it (below), with the client's log. |
| The client says it can't mark other players' items collected right now. | Items and checks still work, but other players' items you found, or that their owners collected, may show again in their rooms. Reconnect the client; if the message comes back, report it (below), with the client's log. |
| The client says it can't mark other players' items collected in this build of the game. | The same, for this version of Zexion: items and checks still work, and the items stay unmarked. Report it (below), with the client's log. |
| The client says that without the mod, traps sent to you or your own traps do nothing. | Traps go off only with the mod loaded. Install it or turn it back on (the rows above); a trap that came while it was missing does not go off later. |
| `version.dll` disappears, or your antivirus warns about it. | The mod isn't signed, so an antivirus may flag it. Windows Security may name it, for example, `Program:Win32/Contebrew.A!ml`: a false alarm. If Windows Security removed it, restore it: Windows Security, Virus & threat protection, Protection history, then open the entry and choose Restore. Check that the file is back in Zexion's folder, or copy it there again. |
| Nothing is sent or received. | Check that the client is connected and the loaded save is from this seed. |

## Reporting a bug

Report it at [github.com/mcawful/zexion-ap/issues](https://github.com/mcawful/zexion-ap/issues),
and attach the client's log: in your Archipelago folder (on Linux, see "Make your YAML"), the
`logs` folder has one file per client run, named `ZexionClient_` and the date and time it
started.
