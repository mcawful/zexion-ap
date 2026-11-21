# zexion-ap

[Archipelago](https://archipelago.gg) multiworld support for
[Zexion](https://store.steampowered.com/app/3392510/) (Gallant Leaf, 2025). Zexion's items are
shuffled with the items of other players' games: what you find may be theirs, and what they
find may be yours.

This is an unofficial fan project, not affiliated with Gallant Leaf. It contains no game files;
you need your own copy of Zexion. The code is under the [MIT license](LICENSE).

## Download

Get the latest release from the [Releases page](https://github.com/mcawful/zexion-ap/releases):
the archive for your system, `zexion-ap-<version>-windows.zip` for Windows or
`zexion-ap-<version>-linux.tar.gz` for Linux and the Steam Deck. Each holds `zexion.apworld`, the
mod for that system (`zexion_ap.dll` and `version.dll`, or `libzexion_ap.so`), the two player
guides, a sample YAML for each mode (Easy, Standard, OHKO and Custom) and a short `README.md`
with the install steps. `zexion.apworld` is also on the release on its own, for anyone who needs
only the world to generate a game.

Seeds have been played start to finish in testing.
It supports one version of Zexion, Steam build 23943567, on Windows, Linux and the Steam Deck; if
Steam updates the game, the client tells you when your version isn't supported.

## Features

- Start a seed from the game's own menu: pick an empty save slot and choose ARCHIPELAGO.
- Other players' items show the Archipelago logo, and picking one up says whose it is.
- Items sent to you pop up in the game with the sender's name.
- Two goals: grab Zexion in the core and reach the game's ending, or hunt for OS Install Disks.
- Intel Chips (on by default): each Area Map becomes a hint for one of your important items.
- Traps (on by default): some Scrap becomes a Disruption Trap or a Disorient Trap, which copy
  two enemies' touch for about 12 seconds, and that look like important items until you pick
  them up ([What do traps do?](worlds/zexion/docs/en_Zexion.md#what-do-traps-do)).
- DeathLink.
- The game's default English and English(HD), where the mod's text and icons use the HD font
  and art; in the other languages its text is in English (the setup guide's
  [Languages](worlds/zexion/docs/setup_en.md#languages)).
- The game's Easy, Standard and OHKO modes and its start rooms, and Custom mode: every
  setting of the game's randomizer menu in the YAML.
- Play your seed's save without the client; what you found is sent when you connect again.
- Steam Cloud is meant to carry your seed's saves to another computer or a Steam Deck as
  Archipelago saves, with no client run there (tried between two Windows PCs, and once Windows to
  Linux; the setup guide's
  [Playing on more than one machine](worlds/zexion/docs/setup_en.md#playing-on-more-than-one-machine)).
- The mod finds what it needs in the game by itself, so it is meant to work from Zexion's first
  start, before any client has run (the setup guide's
  [Install](worlds/zexion/docs/setup_en.md#install); not yet seen on a first start).
- Archipelago saves earn no Feats, and the mod stops Steam achievements in them; normal saves
  are unaffected.

## Quickstart

1. Install [Archipelago](https://github.com/ArchipelagoMW/Archipelago/releases) 0.6.8 or newer,
   then download the latest release's archive for your system (above) and unpack it.
2. In the Archipelago Launcher, choose Install APWorld and pick `zexion.apworld`.
3. Install the mod: with Zexion closed, copy `zexion_ap.dll` and `version.dll` into Zexion's
   folder (in Steam: right-click Zexion, Manage, Browse local files). The mod is required. On
   Linux or the Steam Deck, copy `libzexion_ap.so` there instead and set Zexion's launch option,
   as the setup guide's "Linux and Steam Deck" says.
4. Make your YAML (start from the release's sample for your mode), then generate and host the
   game. To join someone else's game, send them
   your YAML: they need `zexion.apworld` from the same release to generate it.
5. Start Zexion. In the Launcher, open **Zexion Client**, connect to the server and enter your
   player name.
6. In Zexion, pick an empty save slot and choose **ARCHIPELAGO**.

Each release's archives come with the full player guides: `zexion-setup-guide.md` (setup, updating,
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
game and save folders and the mod's offline files) is committed in `.claude/`;
[`CLAUDE.md`](CLAUDE.md) describes it.

## Contributing

How it works, how to build and test it, and what has been checked in the game:
[`docs/DEVELOPING.md`](docs/DEVELOPING.md).
