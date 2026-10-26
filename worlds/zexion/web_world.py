from BaseClasses import Tutorial
from worlds.AutoWorld import WebWorld

from .items import GAME


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
