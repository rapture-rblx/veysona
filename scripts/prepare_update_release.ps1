# Produces the next public HTTPS update manifest after the Windows Setup EXE is signed.
[CmdletBinding()]
param(
 [Parameter(Mandatory=$true)][string]$Installer,
 [Parameter(Mandatory=$true)][ValidatePattern('^\d+\.\d+\.\d+$')][string]$Version,
 [Parameter(Mandatory=$true)][string]$DownloadUrl,
 [Parameter(Mandatory=$true)][string]$PublisherThumbprint,
 [string]$ReleasePage='',
 [string]$ReleaseNotes='Veysona Studio update',
 [string]$Output='updates/stable.json'
)
$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
if (-not (Test-Path -LiteralPath $Installer -PathType Leaf)) { throw 'Setup EXE does not exist' }
if ($DownloadUrl -notmatch '^https://[^/@ ]+/.+\.exe(?:\?.*)?$') { throw 'Download URL must be HTTPS and point to the signed Setup EXE' }
if ($ReleasePage -ne '' -and $ReleasePage -notmatch '^https://[^/@ ]+/') { throw 'Release page must be HTTPS' }
if ($PublisherThumbprint -notmatch '^[A-Fa-f0-9]{40}$') { throw 'Expected a 40-character Windows certificate thumbprint' }
$signature=Get-AuthenticodeSignature -LiteralPath $Installer
if ($signature.Status -ne 'Valid' -or $null -eq $signature.SignerCertificate -or
    $signature.SignerCertificate.Thumbprint -ine $PublisherThumbprint) {
 throw 'Setup EXE must be Authenticode-signed by the pinned publisher before publishing it'
}
$setup=Get-Item -LiteralPath $Installer
$manifest=[ordered]@{
 version=$Version
 installer_url=$DownloadUrl
 sha256=(Get-FileHash -LiteralPath $Installer -Algorithm SHA256).Hash.ToLowerInvariant()
 size_bytes=[long]$setup.Length
 release_page=$ReleasePage
 release_notes=$ReleaseNotes
}
$parent=Split-Path -Parent $Output
if($parent){New-Item -ItemType Directory -Path $parent -Force|Out-Null}
$manifest | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath $Output -Encoding UTF8
Write-Host "Created $Output for Veysona $Version. SHA256: $($manifest.sha256)"
Write-Host "Upload $($setup.Name) to a public GitHub Release, then publish this JSON at your stable HTTPS URL."
