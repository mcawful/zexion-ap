from test.bases import WorldTestBase

from ..world import ZexionWorld


class ZexionTestBase(WorldTestBase):
    game = "Zexion"
    world: ZexionWorld
