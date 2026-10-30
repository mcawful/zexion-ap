"""The release's sample YAMLs (samples/, tools/gen_sample_yamls.py): one per mode, each the
world's preset as Archipelago's template writes it, and each the defaults but its mode."""

from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

import Options
from Generate import roll_settings
from Utils import parse_yamls

from ..options import ZexionOptions
from ..web_world import ZexionWebWorld

# The repo's samples/, found from this file through the link .ap's checkout has to the world
# (tools/ap_checkout.py); none where the world is installed on its own.
SAMPLES = Path(__file__).resolve().parents[3] / "samples"
PRESETS = ZexionWebWorld.options_presets
MODES = {"Easy": "easy", "Standard": "standard", "OHKO": "ohko", "Custom": "custom"}


def sample(preset: str) -> Path:
    return SAMPLES / f"zexion-{preset.lower()}.yaml"  # as tools/gen_sample_yamls.py names it


class TestSamples(unittest.TestCase):
    def test_one_preset_per_mode_each_only_its_mode(self) -> None:
        self.assertEqual({name: {"mode": mode} for name, mode in MODES.items()}, PRESETS)
        for preset in PRESETS.values():
            for key, value in preset.items():
                ZexionOptions.type_hints[key].from_any(value)  # a value the option takes

    def test_the_samples_are_archipelagos_templates_of_the_presets(self) -> None:
        if not SAMPLES.is_dir():
            self.skipTest(f"no samples folder at {SAMPLES}")
        self.assertEqual({sample(n).name for n in PRESETS}, {p.name for p in SAMPLES.glob("*")})
        with tempfile.TemporaryDirectory() as folder:
            Options.generate_yaml_templates(folder)  # every world's: as the Launcher makes them
            for name in PRESETS:
                made = Path(folder) / "Presets" / f"Zexion - {name}.yaml"
                with self.subTest(name):
                    self.assertEqual(
                        made.read_text(encoding="utf-8-sig").replace("\r\n", "\n"),
                        sample(name).read_bytes().decode("utf-8"),
                        "out of date: run tools/gen_sample_yamls.py",
                    )

    def test_each_sample_rolls_to_the_defaults_with_its_mode(self) -> None:
        if not SAMPLES.is_dir():
            self.skipTest(f"no samples folder at {SAMPLES}")
        for name, mode in MODES.items():
            with self.subTest(name):
                (weights,) = parse_yamls(sample(name).read_text(encoding="utf-8"))
                rolled = roll_settings(weights)
                for key, option in ZexionOptions.type_hints.items():
                    want = option.from_any(mode if key == "mode" else option.default)
                    self.assertEqual(getattr(rolled, key).value, want.value, key)
