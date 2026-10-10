import sys

from .base import Reading, Sensor, State
from .null import NullSensor


def create_sensor(demo: bool = False) -> Sensor:
    if demo:
        from .demo import DemoSensor
        return DemoSensor()
    if sys.platform == "win32":
        from .windows import BatterySensor
        return BatterySensor()
    return NullSensor()


__all__ = ["Reading", "Sensor", "State", "NullSensor", "create_sensor"]
