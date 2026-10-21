from BaseClasses import Tutorial
from worlds.AutoWorld import WebWorld


class ZexionWebWorld(WebWorld):
    game = "Zexion"
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
