import math
import time

from .base import Reading, State


class DemoSensor:
    """Synthetic 3-15 W load for previewing the UI."""

    def read(self) -> Reading:
        t = time.time()
        mw = 9000 + 4500 * math.sin(t / 11) + 1800 * math.sin(t / 2.3) + 400 * math.sin(t * 7)
        return Reading(State.DISCHARGING, int(max(2500, mw)), 72)

    def close(self) -> None:
        pass
