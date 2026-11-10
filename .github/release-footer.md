## Installing or updating

A quick reminder; the full steps are in the
[setup guide](https://github.com/mcawful/zexion-ap/blob/v{{VERSION}}/worlds/zexion/docs/setup_en.md),
also attached below as `zexion-setup-guide.md`. You need Zexion on Steam (Windows), Steam build
23943567, and Archipelago 0.6.8 or newer.

**New install**

1. In the Archipelago Launcher, choose **Install APWorld** and pick `zexion.apworld`.
2. Close Zexion and copy `zexion_ap.dll` and `version.dll` into its folder (in Steam:
   right-click Zexion, Manage, Browse local files).
3. Make your YAML from the sample for your mode (`zexion-standard.yaml` and the others), then
   generate and host as usual.
4. Start Zexion, open **Zexion Client** from the Launcher and connect.
5. In Zexion, pick an empty save slot and choose **ARCHIPELAGO**.

**Updating from {{PREVIOUS}}**

- Finish your seed first, or keep {{PREVIOUS}} until it ends. Everyone playing Zexion in a game should
  use the same release.
- Close Zexion and replace all three files together: install the new `zexion.apworld`, and copy
  the new `zexion_ap.dll` and `version.dll` into Zexion's folder.
- Then connect the client once with Zexion running.
{{UPDATE_STEPS}}

Something wrong? See Troubleshooting in the setup guide, or report it at
[github.com/mcawful/zexion-ap/issues](https://github.com/mcawful/zexion-ap/issues) with the
client's log.
