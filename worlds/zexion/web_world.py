from BaseClasses import Tutorial
from Options import OptionGroup
from worlds.AutoWorld import WebWorld

from . import options
from .items import GAME

# Custom mode's own options (options.py: "Custom mode only"), in the options' order: grouped
# after the options of every mode in the YAML templates and on the website.
CUSTOM_ONLY = {"custom_base", "custom_items", *options.AMOUNTS, *options.SETTINGS}


class ZexionWebWorld(WebWorld):
    game = GAME
    theme = "dirt"
    setup_en = Tutorial(
        "Multiworld Setup Guide",
        "A guide to setting up Zexion for Archipelago.",
        "English",
        "setup_en.md",
        "setup/en",
        ["mcawful"],
    )
    tutorials = [setup_en]
    option_groups = [
        OptionGroup(
            "Custom Mode Only",
            [t for n, t in options.ZexionOptions.type_hints.items() if n in CUSTOM_ONLY],
        ),
    ]
    # One per mode, each otherwise the defaults: the release's sample YAMLs
    # (tools/gen_sample_yamls.py), and the Launcher's Generate Template Options.
    options_presets = {
        "Easy": {"mode": "easy"},
        "Standard": {"mode": "standard"},
        "OHKO": {"mode": "ohko"},
        "Custom": {"mode": "custom"},
    }
