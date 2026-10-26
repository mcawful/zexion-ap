# Zexion Setup Guide

This world is in development. Its client has played seeds through to the goal, but treat any
seed as a test.

## Requirements

- Zexion on Steam (Windows), at a build this release knows: so far only one, Steam build
  23943567, the one current on 2026-10-20. When Steam updates the game, the client and its
  helper refuse the new build until a release of this world supports it, so wait for that
  release. (Steam's per-game Updates setting can put an update off until you start the game;
  it cannot keep the old build.)
- Archipelago 0.6.8 (the only version tested; newer ones may work).
- A release of this world: `zexion.apworld`, `zexion_ap.dll` and `version.dll`, all three
  from the same release.

## Installing

1. Install the world: in the Archipelago Launcher, choose Install APWorld and pick
   `zexion.apworld` (or open the file). The Launcher then lists **Zexion Client**.
2. Install the client's in-game helper: with Zexion closed, copy `zexion_ap.dll` and
   `version.dll` into Zexion's folder (Steam: Zexion, Manage, Browse local files). The game
   then loads the helper as it starts. The title screen shows ARCHIPELAGO MOD and the
   helper's version under the copyright line once the client has connected with it once, and
   from then on also without the client, so you can see which helper is in; while no client
   is connected, it also says to start the client.

To update, replace all three with the game closed. If the helper in the game's folder is not
the version the client needs, the client says so and cannot use it: items still arrive, with
no popups, the game acts as if no client were connected, and no new game can start the seed
(a save that already holds it still plays).
Copying this release's two DLLs there, with the game closed, fixes it. To remove the helper,
delete both DLLs (or, to turn it off for a while, create an empty file named
`zexion_ap.disabled` beside them). A save that is not from a seed you have played with the
client plays as it always does.

## Generating a seed

1. In the Archipelago Launcher, choose Generate Template Options. It writes a template for
   each installed game, `Zexion.yaml` among them, into `Players/Templates` in your Archipelago
   folder (Options Creator in the Launcher makes one too).
2. Copy it into `Players` with the other players' YAMLs, and set your `name` and the options.
3. Choose Generate in the Launcher. The game lands in `output`; host it with Host in the
   Launcher, or upload it to archipelago.gg.

## Starting a game

The client starts the seed as a new game in an empty slot; it never changes a game you are
playing.

1. Start Zexion and stay at the title screen.
2. In the Archipelago Launcher, open Zexion Client and connect to the server. Once it attaches
   to Zexion it says how to play: load this seed's save, or start a new game.
3. In Zexion, pick an empty slot. Where the game offers NORMAL and RANDOMIZER, it now offers
   NORMAL and **ARCHIPELAGO**: choose ARCHIPELAGO. You start in the seed's start room with its
   starting items, and the game saves the new game at once.

Once that game is in play, ARCHIPELAGO is offered again, so another empty slot can start a
second save of the same seed; every save of the seed counts, whatever its slot.

This works for every mode. If you would rather start from the game's own menu, start the client
with `--keep-randomizer`, from a command line in your Archipelago folder:
`ArchipelagoLauncher.exe "Zexion Client" -- --keep-randomizer`. RANDOMIZER then stays, and
choosing it, the seed's mode with no options changed, and START GAME starts the seed the same
way.

When the client disconnects, the choice reads RANDOMIZER again.

## Playing

Keep the client running while you play. It sends each location as you collect it, gives you
the items other players find for you (only while you are in play, not in menus), and reports
your goal when the ending begins, as you leave on the ship after grabbing Zexion in the core
(with the OS Install Disks goal, when you answer YES to RETURN HOME WITH THE DISKS?, which the
helper offers when you save at the landing pod you started from with enough disks: this goal
needs the helper). If you die or reload, items received since your last save are given again.
Items that arrive once the ending has begun (as a release or collect on your goal) are given
the next time you load that save. With the DeathLink option on, the client also sends your
deaths to the other players and ends your life when one of them dies; the second part needs
the helper. With Area Map hints on (the default), each Intel Chip (the Area Map, renamed) you
get makes an Archipelago hint for one of your upgrades, keys or OS Install Disks, and the
helper shows it in a popup.

The client's window says when a save of your seed is loaded (and in which slot) and when it is
closed. If you load a save of a different seed while the client is connected, the game says
the client is for another seed, and nothing is sent or received for that save.

## Playing without the client

With the helper in the game's folder, a save of a seed you have played with the client keeps
its look without the client: Archipelago items, Intel Chips, the ASSIST lock, the escape skip
and your OS Install Disk count (counting disks you find in your own world), with a notice that
the client is not connected. Items from other players, hints and RETURN HOME WITH THE DISKS?
wait for the client. Once it connects with that save loaded, what you picked up is sent, after
a notice that the client is connected, with the popups it would have shown (a disk's count, an
Intel Chip's hint), and a core ending reached meanwhile sends your goal. For this, the client
has the helper keep a few small files in `%LOCALAPPDATA%\ZexionAP`; deleting that folder is
safe (the client writes them again).
