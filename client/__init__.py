"""The Zexion Archipelago client, a package: its modules import one another relatively.

From this repo it is the top-level ``client`` package (``python -m client.zexion_client``, and
the tests and tools, with the repo root on the path); in the apworld, the subpackage
``worlds.zexion.client`` (tools/build_apworld.py), which the world's launcher entry starts
(``worlds/zexion/__init__.py``, ``run_client``). Either way its module names (``game``,
``session``, ...) never land on the path as top-level names.
"""
