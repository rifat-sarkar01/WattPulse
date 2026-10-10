from wattpulse.format import DASH, fmt_energy, fmt_watts, nice_ceiling


def test_fmt_watts():
    assert fmt_watts(None) == DASH
    assert fmt_watts(9.46) == "9.5"
    assert fmt_watts(123.4) == "123"


def test_fmt_energy_units():
    assert fmt_energy(1234) == "1.2 Wh"
    assert fmt_energy(500_000) == "500 Wh"
    assert fmt_energy(2_500_000) == "2.50 kWh"


def test_nice_ceiling():
    assert nice_ceiling(0) == 5
    assert nice_ceiling(5.1) == 10
    assert nice_ceiling(300) == 300
    assert nice_ceiling(301) == 400
