from worlds.LauncherComponents import Component, Type, components
from worlds.LauncherComponents import launch as launch_component

from .world import ZexionWorld

__all__ = ["ZexionWorld"]


def run_client(*args: str) -> None:
    """The Zexion client, run in the Launcher's process: the ``client`` subpackage bundled into
    the apworld (tools/build_apworld.py), or, run from source, this repo's ``client`` package
    (its root appended to the path)."""
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
