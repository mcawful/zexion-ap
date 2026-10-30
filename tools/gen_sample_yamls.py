"""Write the release's sample YAMLs: one per mode, each the world's preset for that mode
(worlds/zexion/web_world.py, ``options_presets``) as Archipelago's own template writes it, with
every option, its description and its default, so a player can use it as it is.

    .venv/Scripts/python tools/gen_sample_yamls.py

They go into samples/ as zexion-<preset>.yaml (zexion-easy.yaml, zexion-standard.yaml,
zexion-ohko.yaml, zexion-custom.yaml), which tools/build_apworld.py copies beside the apworld.
It runs Archipelago's Options.generate_yaml_templates (what the Launcher's Generate Template
Options runs) in .ap/Archipelago (tools/ap_checkout.py, with the world linked in), and keeps
Zexion's preset files with LF line ends and no byte-order mark. Run it again after a change to
the options, their descriptions, the presets or the version: worlds/zexion/test/test_samples.py
fails while a sample differs from what this writes.
"""

from __future__ import annotations

import argparse
import os
import sys
import tempfile
from pathlib import Path

from protected import ROOT

AP = ROOT / ".ap" / "Archipelago"
SAMPLES = ROOT / "samples"


def sample_name(preset: str) -> str:
    """The release's name for ``preset``'s sample: zexion-easy.yaml for Easy, and so on."""
    return f"zexion-{preset.lower()}.yaml"


def templates() -> dict[str, str]:
    """Each Zexion preset's template as Archipelago writes it, by the sample's name."""
    if not (AP / "worlds" / "zexion").exists():
        raise SystemExit(f"no Archipelago with the world linked in at {AP}: run ap_checkout.py")
    sys.path.insert(0, str(AP))
    os.chdir(AP)  # as Archipelago runs: its data and worlds are found from here
    import Options  # noqa: PLC0415 (Archipelago's, on the path only now)

    from worlds import AutoWorldRegister  # noqa: PLC0415

    presets = AutoWorldRegister.world_types["Zexion"].web.options_presets
    with tempfile.TemporaryDirectory() as folder:
        Options.generate_yaml_templates(folder)
        made = Path(folder) / "Presets"
        return {
            sample_name(name): (made / f"Zexion - {name}.yaml")
            .read_text(encoding="utf-8-sig")
            .replace("\r\n", "\n")
            for name in presets
        }


def main(argv: list[str] | None = None) -> int:
    argparse.ArgumentParser(description=__doc__.splitlines()[0]).parse_args(argv)
    written = templates()
    SAMPLES.mkdir(exist_ok=True)
    for old in SAMPLES.glob("*.yaml"):
        if old.name not in written:
            old.unlink()  # a preset no longer there
    for name, text in written.items():
        (SAMPLES / name).write_bytes(text.encode("utf-8"))
    print(f"Wrote {', '.join(written)} into {SAMPLES}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
