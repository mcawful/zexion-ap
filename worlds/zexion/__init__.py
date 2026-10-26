from worlds.LauncherComponents import Component, Type, components
from worlds.LauncherComponents import launch as launch_component

from .world import ZexionWorld

__all__ = ["ZexionWorld"]


def run_client(*args: str) -> None:
    """The Zexion client (client/zexion_client.py), in the launcher's own process for it: the
    subpackage ``client`` that tools/build_apworld.py bundles into the apworld beside this file
    (zipimport reads it from there), or, with the world run from source (this folder linked
    into an Archipelago checkout), this repo's ``client`` package, its root added to the end of
    the path. The client's modules import one another relatively, so none of their names lands
    on the path."""
    try:
        from .client.zexion_client import launch
    except ModuleNotFoundError as error:
        if error.name != f"{__name__}.client":
            raise
        import os
        import sys

        here = os.path.dirname(os.path.realpath(__file__))  # through .ap's link to this repo
        root = os.path.normpath(os.path.join(here, os.pardir, os.pardir))
        if not os.path.isfile(os.path.join(root, "client", "zexion_client.py")):
            raise
        if root not in sys.path:
            sys.path.append(root)
        from client.zexion_client import launch

    launch(*args)


def launch_client(*args: str) -> None:
    launch_component(run_client, name="ZexionClient", args=args)


components.append(
    Component(
        "Zexion Client",
        func=launch_client,
        component_type=Type.CLIENT,
        game_name="Zexion",
        supports_uri=True,
        description="Connect Zexion to an Archipelago server.",
    )
)
