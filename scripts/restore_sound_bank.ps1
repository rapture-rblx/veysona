# Restores the bundled sound bank bit-for-bit from FLAC into WAV before compilation.
# Normal source checkouts already have WAVs and need no download.
[CmdletBinding()]
param()
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$root=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$packed=Join-Path $root 'assets\packed_soundbank'
$manifest=Join-Path $packed 'sha256_manifest.tsv'
if(-not (Test-Path -LiteralPath $manifest)){ return }
$entries=@(Get-Content -LiteralPath $manifest -Encoding UTF8 | Where-Object { $_ -ne '' })
if($entries.Count -ne 2489){throw "Sound-bank manifest is incomplete: $($entries.Count)/2489"}
$missing=0
foreach($line in $entries){
 $pair=$line -split "`t",2
 if($pair.Count -ne 2){throw 'Malformed sound-bank manifest'}
 $target=Join-Path $root ($pair[1].Replace('/','\'))
 if(-not(Test-Path -LiteralPath $target -PathType Leaf)){$missing++;continue}
 if((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -ine $pair[0]){$missing++}
}
if($missing -eq 0){Write-Host 'Veysona sound bank already restored and verified (2,489 WAVs).';return}
Write-Host "Restoring $missing losslessly packed WAV files..."
$flacExe=Get-Command flac.exe -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty Source
if(-not $flacExe){
 $cache=Join-Path $env:LOCALAPPDATA 'Veysona Studio\build-tools\flac-1.5.0'
 $zip=Join-Path $cache 'flac-1.5.0-win.zip'
 New-Item -ItemType Directory -Path $cache -Force|Out-Null
 $expected='53f1500f0d6e7c61379d7fee50d4a9f7f504c650009506d9ba015530d76c0dde'
 if(-not (Test-Path -LiteralPath $zip) -or (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash -ine $expected){
  Write-Host 'Downloading the official 1.3 MB Xiph FLAC Windows tools archive (SHA-256 pinned)...'
  Invoke-WebRequest -UseBasicParsing -Uri 'https://github.com/xiph/flac/releases/download/1.5.0/flac-1.5.0-win.zip' -OutFile $zip
 }
 if((Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash -ine $expected){throw 'FLAC tool download has an unexpected SHA-256. Refusing to execute.'}
 $extracted=Join-Path $cache 'unpacked'
 if(-not (Test-Path -LiteralPath $extracted)){Expand-Archive -LiteralPath $zip -DestinationPath $extracted}
 $candidates=@(Get-ChildItem -LiteralPath $extracted -Recurse -File -Filter flac.exe)
 if($candidates.Count -eq 0){throw 'Official FLAC package contains no flac.exe decoder.'}
 $flacExe=($candidates | Where-Object { $_.FullName -match '(win64|x64|64-bit|64bit)' } | Select-Object -First 1 -ExpandProperty FullName)
 if(-not $flacExe){$flacExe=$candidates[0].FullName}
}
$restored=0
foreach($line in $entries){
 $pair=$line -split "`t",2
 $relative=$pair[1].Replace('/','\')
 $target=Join-Path $root $relative
 if(Test-Path -LiteralPath $target -PathType Leaf){
  if((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -ieq $pair[0]){continue}
 }
 $source=Join-Path $packed (($relative.Substring('assets\'.Length))+'.flac')
 if(-not(Test-Path -LiteralPath $source -PathType Leaf)){throw "Missing packed FLAC: $source"}
 $dir=Split-Path -Parent $target
 New-Item -ItemType Directory -Force -Path $dir|Out-Null
 & $flacExe --silent --decode --force --keep-foreign-metadata -o $target $source
 if($LASTEXITCODE -ne 0){throw "FLAC decode failed: $source"}
 if((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -ine $pair[0]){throw "Sound bank SHA-256 mismatch after restoring $target"}
 $restored++
 if($restored % 500 -eq 0){Write-Host "Restored $restored/$missing WAV files..."}
}
Write-Host "Veysona sound bank restored: $restored original WAV files recreated losslessly."
