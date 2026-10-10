from wattpulse.sensors import NullSensor, create_sensor
from wattpulse.sensors.base import State
from wattpulse.sensors.demo import DemoSensor


def test_null_sensor():
    assert NullSensor().read().state is State.NONE


def test_demo_sensor_in_range():
    r = DemoSensor().read()
    assert r.state is State.DISCHARGING and 2_500 <= r.milliwatts <= 16_000


def test_factory_demo():
    assert isinstance(create_sensor(demo=True), DemoSensor)
