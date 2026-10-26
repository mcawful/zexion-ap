"""Stand-ins the tests share: a fake process and game.Game on it, and a seed's plan with a fresh
randomizer save to write it into (no game, no exe, no real save)."""

from __future__ import annotations

import struct

from client import game, ids
from client import save_patch as sp

# --- a fake process and the client's Game on it ---------------------------------------------


class FakeProcess:
    """Memory to read and write as the client reads and writes the game's."""

    def __init__(self, size: int = 0x1000) -> None:
        self.memory = bytearray(size)
        self.writes = 0
        self.fail_write = False
        self.overwrite_with: int | None = None  # the game changes the byte straight after

    def read_bytes(self, address: int, size: int) -> bytes:
        return bytes(self.memory[address : address + size])

    def write_bytes(self, address: int, data: bytes) -> None:
        if self.fail_write:
            raise OSError("access denied")
        self.writes += 1
        self.memory[address : address + len(data)] = data
        if self.overwrite_with is not None:
            self.memory[address] = self.overwrite_with


class FakeGame(game.Game):
    """game.Game on a FakeProcess, alive until the test says otherwise."""

    alive = True

    def _alive(self) -> bool:
        return self.alive


# --- a seed's plan, and a fresh save of its mode ---------------------------------------------

AMOUNTS = (3, 17, 3, 122, 0, 12)  # Standard's, in save order
CELLS = [(i % 54, i // 54) for i in range(sp.LOCATIONS)]  # distinct, sorted by y then x


def fresh(start_cell: tuple[int, int] | None = (13, 16)) -> bytearray:
    """A fresh randomizer save as far as the patcher looks: the mode's amounts, a few
    settings, one revealed start cell, a nonzero item table and inventory."""
    save = bytearray(sp.SAVE_SIZE)
    save[sp.RANDO_MARKER] = 1
    struct.pack_into("<6h", save, sp.AMOUNTS, *AMOUNTS)
    save[sp.OPTIONS + 42] = 2  # Safety Normal
    save[sp.OPTIONS + 72] = 1  # Gates Vanilla
    save[sp.ITEM_TABLE : sp.ITEM_TABLE + sp.LOCATIONS] = bytes([7] * sp.LOCATIONS)
    save[sp.INVENTORY + 0] = 3
    save[sp.INVENTORY + 50] = 9  # item 50 is left alone
    if start_cell is not None:
        x, y = start_cell
        save[sp.MAP_GRID + y * ids.MAP_SIZE + x] = sp.SAVE_ROOM
    return save


def menu_setup(**changes) -> sp.MenuSetup:
    """A menu setup of plan()'s mode: a mode with one start."""
    settings = bytearray(64)
    settings[42 - 10], settings[72 - 10] = 2, 1  # as plan()'s options
    fields = dict(
        seed_text="0123456789ABCDEF",
        start=bytes([0] * 4 + [1] + [0] * 22),
        pool=bytes([1] * 4 + [0] + [1] * 22),
        settings=bytes(settings),
        second_target=(0, 20, 509),
    )
    fields.update(changes)
    return sp.MenuSetup(**fields)


def plan(**changes) -> sp.SeedPlan:
    fields = dict(
        location_items=tuple(i % 46 for i in range(sp.LOCATIONS)),
        start_items={0: 2, 1: 3, 36: 1, 3: 1},
        start=(5, 23 * 14 + 4, 22 * 20 + 9),
        second_start=None,
        amounts=AMOUNTS,
        options={42: 2, 72: 1},
        menu=menu_setup(),
    )
    fields.update(changes)
    return sp.SeedPlan(**fields)


# --- a scripted time.sleep, for the tools' polling loops ---------------------------------------


def scripted_sleep(*steps):
    """A ``time.sleep`` that runs the next step at each call, then stops the loop as Ctrl+C
    does (KeyboardInterrupt) once the steps run out."""
    remaining = iter(steps)

    def sleep(_seconds: float) -> None:
        step = next(remaining, None)
        if step is None:
            raise KeyboardInterrupt
        step()

    return sleep
