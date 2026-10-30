"""The release's sample YAMLs (samples/, tools/gen_sample_yamls.py): one per mode, each the
world's preset as Archipelago's template writes it (a preset mode's without Custom mode's own
options), and each the defaults but its mode."""

from __future__ import annotations

import importlib.util
import tempfile
import unittest
from pathlib import Path

import Options
from Generate import roll_settings
from Utils import parse_yamls

from ..options import ZexionOptions
from ..web_world import CUSTOM_ONLY, ZexionWebWorld

# The repo's samples/ and tools/, found from this file through the link .ap's checkout has to
# the world (tools/ap_checkout.py); none where the world is installed on its own.
ROOT = Path(__file__).resolve().parents[3]
SAMPLES = ROOT / "samples"
TOOL = ROOT / "tools" / "gen_sample_yamls.py"
PRESETS = ZexionWebWorld.options_presets
MODES = {"Easy": "easy", "Standard": "standard", "OHKO": "ohko", "Custom": "custom"}


def sample(preset: str) -> Path:
    return SAMPLES / f"zexion-{preset.lower()}.yaml"  # as tools/gen_sample_yamls.py names it


def sample_text(preset: str, template: str) -> str:
    """The tool's sample from Archipelago's template (it imports tools/protected.py)."""
    import sys  # noqa: PLC0415

    sys.path.insert(0, str(TOOL.parent))
    try:
        spec = importlib.util.spec_from_file_location("gen_sample_yamls", TOOL)
        tool = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(tool)
        return tool.sample_text(preset, template)
    finally:
        sys.path.remove(str(TOOL.parent))


class TestSamples(unittest.TestCase):
    def setUp(self) -> None:
        if not (SAMPLES.is_dir() and TOOL.is_file()):
            self.skipTest(f"no repo with samples/ and tools/ at {ROOT}")

    def test_one_preset_per_mode_each_only_its_mode(self) -> None:
        self.assertEqual({name: {"mode": mode} for name, mode in MODES.items()}, PRESETS)
        for preset in PRESETS.values():
            for key, value in preset.items():
                ZexionOptions.type_hints[key].from_any(value)  # a value the option takes

    def test_the_samples_are_archipelagos_templates_of_the_presets(self) -> None:
        self.assertEqual({sample(n).name for n in PRESETS}, {p.name for p in SAMPLES.glob("*")})
        with tempfile.TemporaryDirectory() as folder:
            Options.generate_yaml_templates(folder)  # every world's: as the Launcher makes them
            for name in PRESETS:
                made = Path(folder) / "Presets" / f"Zexion - {name}.yaml"
                with self.subTest(name):
                    self.assertEqual(
                        sample_text(name, made.read_text(encoding="utf-8-sig")),
                        sample(name).read_bytes().decode("utf-8"),
                        "out of date: run tools/gen_sample_yamls.py",
                    )

    def test_each_sample_shows_its_modes_options_and_rolls_to_the_defaults(self) -> None:
        every_mode = set(ZexionOptions.type_hints) - CUSTOM_ONLY
        for name, mode in MODES.items():
            with self.subTest(name):
                (weights,) = parse_yamls(sample(name).read_text(encoding="utf-8"))
                shown = set(weights["Zexion"])
                # Custom mode's own options in Custom's alone; every other option in each.
                self.assertEqual(shown & CUSTOM_ONLY, CUSTOM_ONLY if mode == "custom" else set())
                self.assertLessEqual(every_mode, shown)
                rolled = roll_settings(weights)
                for key, option in ZexionOptions.type_hints.items():
                    want = option.from_any(mode if key == "mode" else option.default)
                    self.assertEqual(getattr(rolled, key).value, want.value, key)
