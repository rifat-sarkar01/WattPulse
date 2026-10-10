from wattpulse.sampler import Sampler
from wattpulse.sensors.base import Reading, State
from wattpulse.sensors.null import NullSensor


def make(now=6000.0):
    return Sampler(NullSensor(), wall=lambda: now)


def test_discharging_integrates_energy():
    s = make()
    for _ in range(3600):
        s.ingest(Reading(State.DISCHARGING, 10_000), 1.0)      # 10 W for one hour
    assert abs(sum(s.pending().values()) - 10_000) < 1e-6      # 10 Wh = 10 000 mWh


def test_charging_and_ac_are_not_counted():
    s = make()
    s.ingest(Reading(State.CHARGING, 20_000), 1.0)
    s.ingest(Reading(State.AC), 1.0)
    s.ingest(Reading(State.DISCHARGING, None), 1.0)            # unknown rate
    assert s.pending() == {}


def test_history_keeps_gaps_as_none():
    s = make()
    s.ingest(Reading(State.DISCHARGING, 4_000), 1.0)
    s.ingest(Reading(State.AC), 1.0)
    assert s.snapshot().history == [4.0, None]
    assert s.snapshot().state is State.AC


def test_drain_and_restore_roundtrip():
    s = make()
    s.ingest(Reading(State.DISCHARGING, 3_600), 1.0)           # 1 mWh
    rows = s.drain()
    assert s.pending() == {}
    s.restore(rows)
    s.restore(rows)
    assert abs(sum(s.pending().values()) - 2.0) < 1e-9


def test_minute_bucketing():
    s = Sampler(NullSensor(), wall=lambda: 6061.7)
    s.ingest(Reading(State.DISCHARGING, 1_000), 1.0)
    assert list(s.pending()) == [6060]


def test_thread_runs_and_stops():
    class Fixed:
        def read(self):
            return Reading(State.DISCHARGING, 5_000, 80)

        def close(self):
            pass

    s = Sampler(Fixed(), interval=0.01)
    s.start()
    import time
    time.sleep(0.15)
    s.stop()
    snap = s.snapshot()
    assert len(snap.history) >= 3 and snap.percent == 80


def test_sensor_exception_is_survivable():
    class Broken:
        def read(self):
            raise OSError("boom")

        def close(self):
            pass

    s = Sampler(Broken(), interval=0.01)
    s.start()
    import time
    time.sleep(0.05)
    s.stop()
    assert s.snapshot().state is State.NONE
