import sqlite3

import pytest

from wattpulse.storage import Store

NOW = 1_000_000.0


def store(tmp_path, now=NOW, **kw):
    return Store(tmp_path / "t.db", wall=lambda: now, **kw)


def test_write_accumulates_per_minute(tmp_path):
    s = store(tmp_path)
    s.write({NOW - 120: 5.0})
    s.write({NOW - 120: 2.5})
    assert s.total(3600) == 7.5


def test_windows_include_only_recent_rows(tmp_path):
    s = store(tmp_path)
    s.write({NOW - 60: 1.0, NOW - 7200: 10.0, NOW - 3 * 86400: 100.0})
    assert s.total(3600) == 1.0
    assert s.total(86400) == 11.0
    assert s.total(7 * 86400) == 111.0


def test_unsaved_extra_is_included(tmp_path):
    s = store(tmp_path)
    s.write({NOW - 60: 1.0})
    assert s.total(3600, {NOW - 30: 4.0, NOW - 99999: 50.0}) == 5.0


def test_old_rows_are_pruned_on_open(tmp_path):
    path = tmp_path / "t.db"
    s = Store(path, wall=lambda: NOW)
    s.write({NOW - 40 * 86400: 9.0, NOW - 86400: 1.0})
    s.close()
    s2 = Store(path, keep_days=35, wall=lambda: NOW)
    assert s2.total(60 * 86400) == 1.0


def test_persists_across_restart(tmp_path):
    path = tmp_path / "t.db"
    s = Store(path, wall=lambda: NOW)
    s.write({NOW - 60: 3.0})
    s.close()
    assert Store(path, wall=lambda: NOW).total(3600) == 3.0


def test_write_empty_is_noop(tmp_path):
    store(tmp_path).write({})


def test_write_error_surfaces(tmp_path):
    s = store(tmp_path)
    s.close()
    with pytest.raises(sqlite3.Error):
        s.write({NOW: 1.0})
