from worlds.LauncherComponents import Component, Type, components
from worlds.LauncherComponents import launch as launch_component

from .world import ZexionWorld

__all__ = ["ZexionWorld"]


def run_client(*args: str) -> None:
    """The Zexion client (client/zexion_client.py): from this repo's client/ folder when the
    world is run from source, else from the copy tools/build_apworld.py bundles into the
    apworld beside this file (zipimport reads it from there). Its modules import one another by
    plain name, so their folder goes on the path; this runs in the launcher's own process for
    the client."""
    import os
    import sys

    here = os.path.dirname(os.path.realpath(__file__))  # through .ap's link to this repo
    source = os.path.normpath(os.path.join(here, os.pardir, os.pardir, "client"))
    if os.path.isfile(os.path.join(source, "zexion_client.py")):
        folder = source
    else:
        folder = os.path.join(here, "client")
    if folder not in sys.path:
        sys.path.insert(0, folder)
    from zexion_client import launch

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
