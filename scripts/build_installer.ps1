[CmdletBinding()]
param(
    [switch]$NoBuild,
    [switch]$SkipInstaller
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$build = Join-Path $root 'build'
$release = Join-Path $build 'Veysona_artefacts\Release'
$stage = Join-Path $root 'stage\Veysona'
$dist = Join-Path $root 'dist'
$pluginTargets = @('VeysonaPrism', 'VeysonaEcho', 'VeysonaEQ', 'VeysonaCompressor', 'VeysonaReverb')

function Copy-Directory([string]$source, [string]$destination) {
    if (-not (Test-Path -LiteralPath $source -PathType Container)) {
        throw "Missing required directory: $source"
    }
    New-Item -ItemType Directory -Force -Path $destination | Out-Null
    Copy-Item -Path (Join-Path $source '*') -Destination $destination -Recurse -Force
}
if (-not $NoBuild) {
    & cmake -S $root -B $build -G 'Visual Studio 17 2022' -A x64 -DVEYSONA_BUILD_GUI=ON -DVEYSONA_BUILD_BUNDLED_VST3=ON
    if ($LASTEXITCODE -ne 0) { throw 'Veysona CMake configure failed.' }
    & cmake --build $build --config Release --parallel 4
    if ($LASTEXITCODE -ne 0) { throw 'Veysona native build failed.' }
    & ctest --test-dir $build -C Release --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw 'Veysona tests failed; installer creation aborted.' }
}
$exe = Join-Path $release 'Veysona.exe'
if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) { throw "Missing desktop application: $exe" }
if (Test-Path -LiteralPath $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory -Force -Path $stage,$dist | Out-Null
Copy-Item -LiteralPath $exe -Destination $stage -Force
$cliCandidates = @(
    (Join-Path $build 'Release\veysona_audio_tool.exe'),
    (Join-Path $build 'veysona_audio_tool.exe')
)
$cli = $cliCandidates | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
if (-not $cli) { throw 'Missing compiled offline audio tool veysona_audio_tool.exe' }
Copy-Item -LiteralPath $cli -Destination $stage -Force
Copy-Directory (Join-Path $root 'assets') (Join-Path $stage 'assets')
# The packed FLAC bank is source-only; the installed application uses restored WAV assets.
$packedStage = Join-Path $stage 'assets\packed_soundbank'
if (Test-Path -LiteralPath $packedStage) { Remove-Item -LiteralPath $packedStage -Recurse -Force }
# Bundled recorded-sample installer installs CC0 recordings into per-user AppData.
$recordedScript = Join-Path $root 'tools\install_recorded_instruments.py'
$recordedCmd = Join-Path $root 'Install_Recorded_Instruments.cmd'
$recordedDocs = Join-Path $root 'RECORDED_INSTRUMENTS.md'
foreach ($required in @($recordedScript,$recordedCmd,$recordedDocs)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) { throw "Missing recorded-instrument installer file: $required" }
}
New-Item -ItemType Directory -Force -Path (Join-Path $stage 'tools') | Out-Null
Copy-Item -LiteralPath $recordedScript -Destination (Join-Path $stage 'tools') -Force
Copy-Item -LiteralPath $recordedCmd -Destination $stage -Force
Copy-Item -LiteralPath $recordedDocs -Destination $stage -Force
$updaterHelper = Join-Path $root 'tools\veysona_update_client.ps1'
if (-not (Test-Path -LiteralPath $updaterHelper -PathType Leaf)) { throw 'Windows updater helper is missing.' }
Copy-Item -LiteralPath $updaterHelper -Destination (Join-Path $stage 'tools') -Force
$docs = Join-Path $stage 'docs'
New-Item -ItemType Directory -Force -Path $docs | Out-Null
foreach ($filename in @('README.md','LICENSE','DISTRIBUTION_NOTICE.md','INSTALLER_GUIDE.md','OFFLINE_TOOLS.md','RELEASE_NOTES_v1.31.md','PUBLISH_UPDATES_v1.31.md','SOURCE_AND_DISTRIBUTION_POLICY.md','WINDOWS_QA_v1.31.md','REBASE_NOTES_v1.28.md')) {
    $source = Join-Path $root $filename
    if (-not (Test-Path $source)) { throw "Required documentation is missing: $filename" }
    Copy-Item -LiteralPath $source -Destination $docs -Force
}
$pluginDest = Join-Path $stage 'VST3'
New-Item -ItemType Directory -Force -Path $pluginDest | Out-Null
foreach ($target in $pluginTargets) {
    $pluginRoot = Join-Path $build ("{0}_artefacts\Release\VST3" -f $target)
    if (-not (Test-Path -LiteralPath $pluginRoot -PathType Container)) {
        throw "VST3 build missing: $pluginRoot"
    }
    $bundles = @(Get-ChildItem -LiteralPath $pluginRoot | Where-Object { $_.Name -like '*.vst3' })
    if ($bundles.Count -ne 1) { throw "Expected exactly one VST3 bundle for $target in $pluginRoot, found $($bundles.Count)" }
    Copy-Item -LiteralPath $bundles[0].FullName -Destination $pluginDest -Recurse -Force
}
$actualPlugins = @(Get-ChildItem -LiteralPath $pluginDest | Where-Object { $_.Name -like '*.vst3' })
if ($actualPlugins.Count -ne $pluginTargets.Count) { throw 'The staged VST3 bundle count is incorrect.' }
$sampleCount = @(Get-ChildItem -LiteralPath (Join-Path $stage 'assets\samples') -Filter '*.wav').Count
$instrumentCount = @(Get-ChildItem -LiteralPath (Join-Path $stage 'assets\instruments') -Filter '*.wav').Count
$midiCount = @(Get-ChildItem -LiteralPath (Join-Path $stage 'assets\midi') -Filter '*.mid').Count
if ($sampleCount -lt 753 -or $instrumentCount -lt 360 -or $midiCount -lt 126) {
    throw "Sample pack is incomplete: $sampleCount samples, $instrumentCount instruments, $midiCount MIDI patterns."
}
$effectPresetCount = @(Get-ChildItem -LiteralPath (Join-Path $stage 'assets\effect_presets') -Filter '*.vfx.json').Count
if ($effectPresetCount -lt 132) { throw "Effects-chain presets are incomplete: $effectPresetCount" }
if (-not (Test-Path (Join-Path $stage 'assets\branding\Veysona-Logo.png'))) { throw 'Branded application logo is missing.' }
$portable = Join-Path $dist 'Veysona-Studio-1.31.0-Portable-Windows-x64.zip'
if (Test-Path $portable) { Remove-Item $portable -Force }
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $portable -CompressionLevel Optimal
Write-Host "Staged validated Veysona application with $sampleCount samples, $instrumentCount instruments, $midiCount MIDI patterns, $($actualPlugins.Count) VST3 bundles."
Write-Host "Portable ZIP: $portable"
if ($SkipInstaller) { return }
$compilerCandidates = @(
    "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
    "${env:ProgramFiles}\Inno Setup 6\ISCC.exe"
)
$compiler = $compilerCandidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (-not $compiler) { throw 'Inno Setup 6 (ISCC.exe) is required. Install it with: choco install innosetup -y' }
& $compiler (Join-Path $root 'installer\Veysona.iss')
if ($LASTEXITCODE -ne 0) { throw 'Inno Setup compilation failed.' }
$installer = Join-Path $dist 'Veysona-Studio-Setup-1.31.0.exe'
if (-not (Test-Path -LiteralPath $installer -PathType Leaf)) { throw 'Installer output not found.' }
Write-Host "INSTALLER SUCCESS: $installer"
Write-Host "SHA256: $((Get-FileHash -LiteralPath $installer -Algorithm SHA256).Hash)"
