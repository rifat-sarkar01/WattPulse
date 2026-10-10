from .base import Reading, State


class NullSensor:
    """Used on platforms or machines without a battery sensor."""

    def read(self) -> Reading:
        return Reading(State.NONE)

    def close(self) -> None:
        pass
