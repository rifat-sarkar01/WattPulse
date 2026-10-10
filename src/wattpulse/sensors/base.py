from dataclasses import dataclass
from enum import Enum
from typing import Optional, Protocol


class State(str, Enum):
    DISCHARGING = "discharging"
    CHARGING = "charging"
    AC = "ac"
    NONE = "none"


@dataclass(frozen=True)
class Reading:
    state: State
    milliwatts: Optional[int] = None
    percent: Optional[int] = None


class Sensor(Protocol):
    def read(self) -> Reading: ...
    def close(self) -> None: ...
