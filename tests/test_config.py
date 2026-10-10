from pathlib import Path

from wattpulse import config


def test_data_dir_uses_wattpulse_name(tmp_path, monkeypatch):
    monkeypatch.setenv("APPDATA", str(tmp_path))
    assert config.data_dir() == tmp_path / "WattPulse"
    assert (tmp_path / "WattPulse").is_dir()


def test_legacy_wattly_folder_is_migrated(tmp_path, monkeypatch):
    monkeypatch.setenv("APPDATA", str(tmp_path))
    legacy = tmp_path / "Wattly"
    legacy.mkdir()
    (legacy / "power.db").write_text("old", encoding="utf-8")

    path = config.data_dir()

    assert path == tmp_path / "WattPulse"
    assert (path / "power.db").read_text(encoding="utf-8") == "old"
    assert not legacy.exists()


def test_existing_wattpulse_folder_is_never_overwritten(tmp_path, monkeypatch):
    monkeypatch.setenv("APPDATA", str(tmp_path))
    (tmp_path / "WattPulse").mkdir()
    (tmp_path / "Wattly").mkdir()
    (tmp_path / "Wattly" / "power.db").write_text("old", encoding="utf-8")

    config.data_dir()

    assert not (tmp_path / "WattPulse" / "power.db").exists()
    assert (tmp_path / "Wattly").exists()


def test_bundled_icon_points_at_the_repo_asset():
    ico = config.bundled_icon()
    assert isinstance(ico, Path)
    assert ico.name == "wattpulse.ico"
