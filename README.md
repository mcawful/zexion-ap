# zexion-ap

[Archipelago](https://archipelago.gg) multiworld support for
[Zexion](https://store.steampowered.com/app/3392510/) (Gallant Leaf, 2025). Zexion's items are
shuffled with the items of other players' games: what you find may be theirs, and what they
find may be yours.

This is an unofficial fan project, not affiliated with Gallant Leaf. It contains no game files;
you need your own copy of Zexion. The code is under the [MIT license](LICENSE).

## Download

Get the latest release from the [Releases page](https://github.com/mcawful/zexion-ap/releases).
It has `zexion.apworld`, `zexion_ap.dll`, `version.dll` and the two player guides.

This is still in development: seeds have been played start to finish in testing. Each release
works with certain versions of Zexion; if Steam updates the game, the client tells you when
your version isn't supported.

## Features

- Start a seed from the game's own menu: pick an empty save slot and choose ARCHIPELAGO.
- Other players' items show the Archipelago logo, and picking one up says whose it is.
- Items sent to you pop up in the game with the sender's name.
- Two goals: grab Zexion in the core and reach the game's ending, or hunt for OS Install Disks.
- Intel Chips (on by default): each Area Map becomes a hint for one of your important items.
- DeathLink.
- The game's Easy, Standard and OHKO modes and its start rooms, and Custom mode: every
  setting of the game's randomizer menu in the YAML.
- Play your seed's save without the client; what you found is sent when you connect again.
- Archipelago saves earn no Feats, and the mod stops Steam achievements in them; normal saves
  are unaffected.

## Quickstart

1. Install [Archipelago](https://github.com/ArchipelagoMW/Archipelago/releases) 0.6.8 or newer,
   and download the latest release of this project (above).
2. In the Archipelago Launcher, choose Install APWorld and pick `zexion.apworld`.
3. Install the mod: with Zexion closed, copy `zexion_ap.dll` and `version.dll` into Zexion's
   folder (in Steam: right-click Zexion, Manage, Browse local files). The mod is required.
4. Make your YAML, then generate and host the game. To join someone else's game, send them
   your YAML: they need `zexion.apworld` from the same release to generate it.
5. Start Zexion. In the Launcher, open **Zexion Client**, connect to the server and enter your
   player name.
6. In Zexion, pick an empty save slot and choose **ARCHIPELAGO**.

Each release comes with the full player guides: `zexion-setup-guide.md` (setup, updating,
troubleshooting) and `zexion-game-page.md` (goals, options, what changes in the game).

## Help

Something wrong? Check the troubleshooting section of `zexion-setup-guide.md`, then
[open an issue](https://github.com/mcawful/zexion-ap/issues).

## AI disclosure

This project is developed with AI assistance. Claude (Anthropic), working through Claude Code
under the maintainer's direction, writes nearly all of the code, tests and documentation,
and almost every commit carries a `Co-Authored-By: Claude` trailer. The reverse engineering of
the game was done the same way, with the tools in `tools/`, a disassembler and a debugger.
Every fact in [`docs/re/`](docs/re/README.md) records how it was established and on which
build of the game, and nothing is called working in game until it has run in the real game,
played by the maintainer. The maintainer decides what goes in, and reviews and merges every change.

The Claude Code setup used here (review subagents, skills, and a hook that blocks writes to the
game and save folders) is committed in `.claude/`; [`CLAUDE.md`](CLAUDE.md) describes it.

## Contributing

How it works, how to build and test it, and what has been checked in the game:
[`docs/DEVELOPING.md`](docs/DEVELOPING.md).
