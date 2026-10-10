# Builds dist\WattPulse.exe. Run it from anywhere, inside your virtualenv:
#   pip install -e ".[build]"
#   .\packaging\build.ps1
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Push-Location $root
try {
    python tools\make_icon.py
    if ($LASTEXITCODE -ne 0) { throw "tools\make_icon.py failed" }

    python tools\version_info.py packaging\version_info.txt
    if ($LASTEXITCODE -ne 0) { throw "tools\version_info.py failed" }

    pyinstaller --noconfirm --clean --onefile --noconsole `
        --name WattPulse `
        --icon assets\wattpulse.ico `
        --version-file packaging\version_info.txt `
        --manifest packaging\wattpulse.manifest `
        --add-data "assets\wattpulse.ico;." `
        --paths src `
        packaging\wattpulse_launcher.py
    if ($LASTEXITCODE -ne 0) { throw "pyinstaller failed" }

    $exe = Join-Path $root "dist\WattPulse.exe"
    if (-not (Test-Path $exe)) { throw "expected output $exe was not created" }
    Write-Host "Done: $exe  -> right-click it -> Pin to taskbar"
}
finally {
    Pop-Location
}
