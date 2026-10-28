from test.bases import WorldTestBase

from ..world import ZexionWorld


class ZexionTestBase(WorldTestBase):
    game = "Zexion"
    world: ZexionWorld


class SeededZexionTestBase(ZexionTestBase):
    """A Double Teleport seed (OHKO) whose default tests fill it: Archipelago's fill fails about
    one time in a hundred there, when a flagged second target is drawn from a start whose
    starting items reach few locations, as accepted (docs/re/locations.md, "OHKO: two starts,
    and only the locations they reach"), so these use one seed rather than a random one."""

    fixed_seed = 1

    def world_setup(self, seed: int | None = None) -> None:
        super().world_setup(self.fixed_seed if seed is None else seed)
