# zexion-ap

[Archipelago](https://archipelago.gg) multiworld support for
[Zexion](https://store.steampowered.com/app/3392510/) (Gallant Leaf, 2025). Zexion's items are
shuffled with the items of other players' games: what you find may be theirs, and what they
find may be yours.

This is an unofficial fan project, not affiliated with Gallant Leaf. It contains no game files;
you need your own copy of Zexion.

## Status

**Not released yet.** Seeds have been played start to finish in testing, but there is no
download yet. Each release will work with certain versions of Zexion; if Steam updates the
game, the client tells you when your version isn't supported.

## Features

- Start a seed from the game's own menu: pick an empty save slot and choose ARCHIPELAGO.
- Other players' items show the Archipelago logo, and picking one up says whose it is.
- Items sent to you pop up in the game with the sender's name.
- Two goals: reach the core and grab Zexion, or hunt for OS Install Disks.
- Intel Chips (on by default): each Area Map becomes a hint for one of your important items.
- DeathLink.
- The game's Easy, Standard and OHKO modes and its start rooms.
- Play your seed's save without the client; what you found is sent when you connect again.

## Quickstart

1. Install [Archipelago](https://github.com/ArchipelagoMW/Archipelago/releases) 0.6.8 or newer.
2. In the Archipelago Launcher, choose Install APWorld and pick `zexion.apworld`.
3. Install the mod: with Zexion closed, copy `zexion_ap.dll` and `version.dll` into Zexion's
   folder (in Steam: right-click Zexion, Manage, Browse local files). The mod is required.
4. Make your YAML, then generate and host the game, or join one someone else hosts.
5. Start Zexion. In the Launcher, open **Zexion Client** and connect to the server.
6. In Zexion, pick an empty save slot and choose **ARCHIPELAGO**.

The full player guide (setup, options, goals and troubleshooting) comes with each release,
inside the APWorld. It is not online yet.

## Help

Something wrong? Check the troubleshooting section of the player guide, then
[open an issue](https://github.com/mcawful/zexion-ap/issues).

## Contributing

How it works, how to build and test it, and what has been checked in the game:
[`docs/DEVELOPING.md`](docs/DEVELOPING.md).
