# Veysona Studio Windows updater. Runs outside the audio process. PowerShell 5.1+.
# Only the developer's HTTPS release channel and Authenticode-pinned installers may self-install.
[CmdletBinding()]
param(
  [Parameter(Mandatory=$true)][ValidateSet('Check','Download','Install')][string]$Mode,
  [string]$ManifestUrl = '',
  [string]$InstalledVersion = '',
  [string]$InstallerUrl = '',
  [string]$ReleaseVersion = '',
  [string]$ExpectedSha256 = '',
  [string]$ExpectedThumbprint = '',
  [string]$ExpectedSize = '',
  [string]$StatusPath = '',
  [string]$RequestId = '',
  [string]$InstallerPath = '',
  [int]$ParentProcessId = 0,
  [string]$AppExe = ''
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
Add-Type -AssemblyName System.Net.Http

function Assert-Https([string]$url) {
  $uri = $null
  if (-not [Uri]::TryCreate($url,[UriKind]::Absolute,[ref]$uri) -or
      $uri.Scheme -cne 'https' -or $uri.Host -notmatch '\.' -or
      $uri.HostNameType -ne [UriHostNameType]::Dns -or
      $uri.UserInfo -ne '' -or $uri.Port -ne 443) {
    throw 'The release endpoint must be a normal HTTPS URL on port 443.'
  }
  return $uri
}
function Assert-Hash([string]$hash) {
  if ($hash -cnotmatch '^[a-fA-F0-9]{64}$') { throw 'Missing or invalid SHA-256 release digest.' }
}
function Assert-Publisher([string]$thumb) {
  if ($thumb -cnotmatch '^[a-fA-F0-9]{40}$') { throw 'Update installation disabled: no pinned publisher certificate configured.' }
}
function Check-SignedInstaller([string]$file, [string]$sha, [string]$thumb) {
  Assert-Hash $sha
  Assert-Publisher $thumb
  if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw 'Downloaded installer is missing.' }
  $hash = (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash
  if ($hash -ine $sha) { throw 'Installer SHA-256 does not match the published manifest.' }
  $signature = Get-AuthenticodeSignature -LiteralPath $file
  if ($signature.Status -ne [System.Management.Automation.SignatureStatus]::Valid -or
      $null -eq $signature.SignerCertificate -or
      $signature.SignerCertificate.Thumbprint -ine $thumb) {
    throw 'The installer is not signed by the configured, trusted Veysona publisher.'
  }
}
function Report([string]$state, [string]$message, [hashtable]$details=@{}) {
  if ([string]::IsNullOrWhiteSpace($StatusPath)) { return }
  $data = @{ request_id=$RequestId; state=$state; message=$message }
  foreach ($key in $details.Keys) { $data[$key]=$details[$key] }
  $parent = Split-Path -Parent $StatusPath
  New-Item -ItemType Directory -Force -Path $parent | Out-Null
  $tmp = "$StatusPath.$PID.tmp"
  $json=$data | ConvertTo-Json -Depth 4 -Compress
  [IO.File]::WriteAllText($tmp,$json,[Text.UTF8Encoding]::new($false))
  Move-Item -LiteralPath $tmp -Destination $StatusPath -Force
}
function Read-HttpsResponse([string]$url, [System.Net.Http.HttpClient]$client) {
  $uri = Assert-Https $url
  for ($i=0;$i -lt 6;$i++) {
    $response = $client.GetAsync($uri,[System.Net.Http.HttpCompletionOption]::ResponseHeadersRead).GetAwaiter().GetResult()
    $status=[int]$response.StatusCode
    if ($status -in @(301,302,303,307,308)) {
      $location=$response.Headers.Location
      $response.Dispose()
      if ($null -eq $location) { throw 'Release server redirected without a destination.' }
      $uri = if($location.IsAbsoluteUri){Assert-Https $location.AbsoluteUri}else{Assert-Https ([Uri]::new($uri,$location).AbsoluteUri)}
      continue
    }
    if (-not $response.IsSuccessStatusCode) {
      $response.Dispose()
      throw "Release server returned HTTP $status."
    }
    return $response
  }
  throw 'Too many release redirects.'
}

try {
  if ($Mode -eq 'Install') {
    Check-SignedInstaller $InstallerPath $ExpectedSha256 $ExpectedThumbprint
    if ($ParentProcessId -lt 1) { throw 'Missing the Veysona process ID.' }
    for ($i=0;$i -lt 240;$i++) {
      if (-not (Get-Process -Id $ParentProcessId -ErrorAction SilentlyContinue)) { break }
      Start-Sleep -Milliseconds 250
    }
    if (Get-Process -Id $ParentProcessId -ErrorAction SilentlyContinue) { throw 'Veysona is still running; update cancelled.' }
    Check-SignedInstaller $InstallerPath $ExpectedSha256 $ExpectedThumbprint
    $proc=Start-Process -FilePath $InstallerPath -ArgumentList @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART','/CLOSEAPPLICATIONS') -PassThru -Wait
    if ($proc.ExitCode -notin @(0,3010)) { throw "Setup failed with exit code $($proc.ExitCode)." }
    if ($proc.ExitCode -eq 0 -and $AppExe -ne '' -and (Test-Path -LiteralPath $AppExe)) {
      Start-Process -FilePath $AppExe | Out-Null
    }
    exit 0
  }

  $handler=[System.Net.Http.HttpClientHandler]::new()
  $handler.AllowAutoRedirect=$false
  $handler.UseCookies=$false
  $client=[System.Net.Http.HttpClient]::new($handler)
  $client.Timeout=[TimeSpan]::FromSeconds(65)
  $client.DefaultRequestHeaders.UserAgent.ParseAdd('VeysonaStudio-Updater/1.31')
  try {
    if ($Mode -eq 'Check') {
      $response=Read-HttpsResponse $ManifestUrl $client
      try {
        if ($null -ne $response.Content.Headers.ContentLength -and
            $response.Content.Headers.ContentLength -gt 65536) { throw 'Release manifest is too large.' }
        $stream=$response.Content.ReadAsStreamAsync().GetAwaiter().GetResult()
        $memory=[IO.MemoryStream]::new()
        try {
          $chunk=New-Object byte[] 8192
          [int]$count=0
          while(($n=$stream.Read($chunk,0,$chunk.Length)) -gt 0){
            $count += $n
            if($count -gt 65536){throw 'Release manifest is too large.'}
            $memory.Write($chunk,0,$n)
          }
          $text=[Text.Encoding]::UTF8.GetString($memory.ToArray())
        } finally { $memory.Dispose();$stream.Dispose() }
      } finally { $response.Dispose() }
      $manifest=$text | ConvertFrom-Json
      if ($null -eq $manifest -or $null -eq $manifest.version -or $null -eq $manifest.installer_url -or
          $null -eq $manifest.sha256 -or $null -eq $manifest.size_bytes) { throw 'Release manifest is incomplete.' }
      $versionText=[string]$manifest.version
      $current=$null; $latest=$null
      if (-not [version]::TryParse($InstalledVersion,[ref]$current) -or
          -not [version]::TryParse($versionText,[ref]$latest) -or
          $versionText -cnotmatch '^\d+\.\d+\.\d+$') { throw 'Invalid release version.' }
      Assert-Https ([string]$manifest.installer_url) | Out-Null
      Assert-Hash ([string]$manifest.sha256)
      $length=[long]$manifest.size_bytes
      if ($length -lt 1024 -or $length -gt 2000000000) { throw 'Invalid installer size.' }
      if ($latest -le $current) { Report 'up_to_date' 'Veysona Studio is up to date.'; exit 0 }
      $notes=if ($manifest.PSObject.Properties.Name -contains 'release_notes') {[string]$manifest.release_notes} else {''}
      $page=if ($manifest.PSObject.Properties.Name -contains 'release_page') {[string]$manifest.release_page}else{''}
      if ($page -ne '') { Assert-Https $page | Out-Null }
      Report 'available' "Update $versionText is available." @{
        version=$versionText; installer_url=[string]$manifest.installer_url;
        sha256=[string]$manifest.sha256; size_bytes=$length;
        release_notes=$notes.Substring(0,[Math]::Min(1200,$notes.Length)); release_page=$page
      }
      exit 0
    }
    if ($Mode -eq 'Download') {
      Assert-Publisher $ExpectedThumbprint
      Assert-Https $InstallerUrl | Out-Null
      Assert-Hash $ExpectedSha256
      if ($ReleaseVersion -cnotmatch '^\d+\.\d+\.\d+$') { throw 'Invalid requested update version.' }
      $length=[long]$ExpectedSize
      if ($length -lt 1024 -or $length -gt 2000000000) { throw 'Invalid requested update size.' }
      $updates=Join-Path $env:LOCALAPPDATA 'Veysona Studio\updates'
      New-Item -ItemType Directory -Force -Path $updates | Out-Null
      $target=Join-Path $updates ("Veysona-Setup-$ReleaseVersion.exe")
      $part="$target.part"
      Remove-Item -LiteralPath $part -Force -ErrorAction SilentlyContinue
      Report 'downloading' "Downloading Veysona $ReleaseVersion..."
      $response=Read-HttpsResponse $InstallerUrl $client
      try {
        $sourceStream=$response.Content.ReadAsStreamAsync().GetAwaiter().GetResult()
        $output=[IO.FileStream]::new($part,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
        try {
          $buffer=New-Object byte[] 65536
          [long]$received=0
          while (($read=$sourceStream.Read($buffer,0,$buffer.Length)) -gt 0) {
            $received += $read
            if ($received -gt $length) { throw 'Downloaded file exceeds its declared size.' }
            $output.Write($buffer,0,$read)
          }
        } finally { $output.Dispose(); $sourceStream.Dispose() }
      } finally { $response.Dispose() }
      if ($received -ne $length) { throw 'Downloaded installer size mismatch.' }
      $partHash=(Get-FileHash -LiteralPath $part -Algorithm SHA256).Hash
      if ($partHash -ine $ExpectedSha256) { throw 'Downloaded file SHA-256 mismatch.' }
      Move-Item -LiteralPath $part -Destination $target -Force
      try { Check-SignedInstaller $target $ExpectedSha256 $ExpectedThumbprint }
      catch { Remove-Item -LiteralPath $target -Force -ErrorAction SilentlyContinue; throw }
      Report 'ready' "Veysona $ReleaseVersion is verified and ready to install." @{installer_path=$target}
      exit 0
    }
  } finally { $client.Dispose();$handler.Dispose() }
} catch {
  # Never report ready or execute downloaded binaries after any network, hash, or signer failure.
  $errorText=$_.Exception.Message
  if ($Mode -eq 'Install') {
    $directory=Join-Path $env:LOCALAPPDATA 'Veysona Studio\updates'
    New-Item -ItemType Directory -Force -Path $directory | Out-Null
    Add-Content -LiteralPath (Join-Path $directory 'last-update-error.log') -Value ((Get-Date).ToUniversalTime().ToString('o')+' '+$errorText)
  } else { Report 'error' $errorText }
  exit 1
}
