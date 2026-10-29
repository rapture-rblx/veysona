# Veysona v1.20 Release Quality Gate
# Run from PowerShell on Windows. Exits nonzero for any failed step.
# The portable C++ test suite runs by default; -BuildGui additionally compiles
# the actual JUCE application and bundled VST3 targets using Visual Studio.
param(
    [switch]$BuildGui,
    [switch]$SkipCore,
    [string]$ReportDirectory = ""
)
$ErrorActionPreference = "Stop"
$src = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if (-not $ReportDirectory) { $ReportDirectory = Join-Path $src "qa-results" }
New-Item -ItemType Directory -Force -Path $ReportDirectory | Out-Null
$report = Join-Path $ReportDirectory "release-gate-v1.20.txt"
$lines = [System.Collections.Generic.List[string]]::new()
$lines.Add("Veysona Studio 1.20 Release Quality Gate")
$lines.Add("Run: $(Get-Date -Format o)")
$lines.Add("Machine: $env:COMPUTERNAME")
$lines.Add("Note: A PASS means the specified automated checks passed, NOT 10/10 production readiness.")
function CheckStep([string]$label,[string]$exe,[string[]]$argv) {
    $lines.Add("CHECK: $label")
    Write-Host "Checking $label ..."
    $log = Join-Path $ReportDirectory (($label -replace '[^A-Za-z0-9-]','-') + ".log")
    $output = & $exe @argv 2>&1 | Out-String
    $result = $LASTEXITCODE
    $output | Out-File -FilePath $log -Encoding utf8
    if ($result -ne 0) {
        $lines.Add("FAIL: $label (exit $result; see $log)")
        $lines | Out-File -FilePath $report -Encoding utf8
        Write-Error "$label failed. See $log"
        exit 1
    }
    $lines.Add("PASS: $label")
}
try {
    Push-Location $src
    if (-not $SkipCore) {
        $core = Join-Path $src "build-qa-core"
        CheckStep "core-configure" "cmake" @("-S",$src,"-B",$core,"-DVEYSONA_BUILD_GUI=OFF","-DBUILD_TESTING=ON")
        CheckStep "core-build" "cmake" @("--build",$core,"--config","Release","--parallel","4")
        CheckStep "core-tests" "ctest" @("--test-dir",$core,"-C","Release","--output-on-failure")
    }
    if ($BuildGui) {
        # JUCE 8.0.10 is fetched by CMake; internet and a valid compatible
        # development toolchain are required. No installer is generated here.
        CheckStep "gui-configure" "cmake" @("--preset","windows-release")
        CheckStep "gui-build" "cmake" @("--build","--preset","windows-release")
        # A successful build does not prove audio-device or plugin-hosting operation.
        $lines.Add("MANUAL: Open Veysona.exe, record audio + MIDI, load VST3, reopen project and export WAV.")
        $lines.Add("MANUAL: Test ASIO/WASAPI latency, plugin crashes, high-DPI, offline export, >2h session.")
    } else {
        $lines.Add("NOT CHECKED: Windows JUCE executable/VST3 compilation (use -BuildGui).")
    }
    $lines.Add("NOT CHECKED: long-session stability, audio-interface matrix, perceptual quality blind tests.")
    $lines.Add("OVERALL: AUTOMATED CHECKS PASSED; production-ready rating not established.")
    $lines | Out-File -FilePath $report -Encoding utf8
    Write-Host "Automated gate passed. Read $report for unverified requirements."
} catch {
    $lines.Add("FAIL: $($_.Exception.Message)")
    $lines | Out-File -FilePath $report -Encoding utf8
    throw
} finally {
    Pop-Location
}
