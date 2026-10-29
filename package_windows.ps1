# Package a previously built Windows executable and five VST3 bundles.
# This is the same staging/validation logic used by the Inno Setup builder.
$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot 'scripts\build_installer.ps1') -NoBuild -SkipInstaller
if ($LASTEXITCODE -and $LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
