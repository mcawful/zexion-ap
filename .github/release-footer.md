## Installing or updating

A quick reminder; the full steps are in the
[setup guide](https://github.com/mcawful/zexion-ap/blob/v{{VERSION}}/worlds/zexion/docs/setup_en.md),
also in each archive below as `zexion-setup-guide.md`. You need Zexion on Steam (Windows, Linux or
the Steam Deck), Steam build 23943567, and Archipelago 0.6.8 or newer.

**Download** the archive for your system, below, and unpack it:

- Windows: `zexion-ap-{{VERSION}}-windows.zip`
- Linux and the Steam Deck: `zexion-ap-{{VERSION}}-linux.tar.gz`

Its folder holds `zexion.apworld`, the mod for your system, the player guides, a sample YAML for
each mode and a short `README.md`. `zexion.apworld` is also attached on its own, for anyone who
needs only that (to generate a game with Zexion in it).

**New install**

1. In the Archipelago Launcher, choose **Install APWorld** and pick `zexion.apworld`.
2. Close Zexion and copy `zexion_ap.dll` and `version.dll` into its folder (in Steam:
   right-click Zexion, Manage, Browse local files). On Linux or the Steam Deck, copy
   `libzexion_ap.so` there instead, and set Zexion's launch options to
   `LD_PRELOAD="./libzexion_ap.so:$LD_PRELOAD" %command%` (if the mod doesn't load, with the
   folder's full path in front of `libzexion_ap.so`: the setup guide, "Linux and Steam Deck").
   Leave Steam's compatibility tool off for Zexion, so it runs the Linux version, not Proton.
3. Make your YAML from the sample for your mode (`zexion-standard.yaml` and the others), then
   generate and host as usual.
4. Start Zexion, open **Zexion Client** from the Launcher and connect.
5. In Zexion, pick an empty save slot and choose **ARCHIPELAGO**.

**Updating from {{PREVIOUS}}**

- Finish your seed first, or keep {{PREVIOUS}} until it ends. Everyone playing Zexion in a game should
  use the same release.
- Close Zexion and replace the mod files and `zexion.apworld` together, from the new archive for
  your system: install the new `zexion.apworld`, and copy the new `zexion_ap.dll` and
  `version.dll` into Zexion's folder (on Linux, the new `libzexion_ap.so`). Releases before 2.0.0
  came as loose files, not an archive; the files in the archive have the same names, so copy them
  over the old ones.
- Then connect the client once with Zexion running.
{{UPDATE_STEPS}}

Something wrong? See Troubleshooting in the setup guide, or report it at
[github.com/mcawful/zexion-ap/issues](https://github.com/mcawful/zexion-ap/issues) with the
client's log.
