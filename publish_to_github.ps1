# Veysona Studio - create a PRIVATE GitHub repository for owner-only source backups.
# Run from the extracted project folder, with Git and GitHub CLI installed.
# This script never requests or stores a GitHub token.
$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath $PSScriptRoot

function Assert-Ok([string]$action) {
    if ($LASTEXITCODE -ne 0) { throw "$action failed (exit code $LASTEXITCODE)." }
}

Write-Host ''
Write-Host '======================================' -ForegroundColor Cyan
Write-Host ' Veysona - Publish and start Windows CI' -ForegroundColor Cyan
Write-Host '======================================' -ForegroundColor Cyan
Write-Host ''

if (-not (Test-Path -LiteralPath (Join-Path $PSScriptRoot 'CMakeLists.txt'))) {
    throw 'Run this script from the extracted Veysona source folder (CMakeLists.txt is missing).'
}
foreach ($cmd in @('git','gh')) {
    if (-not (Get-Command $cmd -ErrorAction SilentlyContinue)) {
        throw "$cmd is not installed or not on PATH. Install Git for Windows and GitHub CLI, then rerun this script."
    }
}

# Confirm authenticated GitHub identity. gh handles login in the user's browser.
& gh auth status --hostname github.com 2>$null | Out-Null
if ($LASTEXITCODE -ne 0) {
    Write-Host 'Sign into the GitHub account that should own Veysona.' -ForegroundColor Yellow
    & gh auth login --hostname github.com --git-protocol https --web
    Assert-Ok 'GitHub sign-in'
}

$owner = (& gh api user --jq '.login' 2>$null | Select-Object -First 1).Trim()
Assert-Ok 'Read GitHub account'
if (-not $owner) { throw 'Could not read the GitHub login from gh.' }
$repo = "$owner/Veysona"
Write-Host "Publishing to https://github.com/$repo" -ForegroundColor Green
Write-Host 'Visibility: PRIVATE (source will not be public)' -ForegroundColor Green
$answer = Read-Host 'Continue? Type YES to create/push the repository'
if ($answer -cne 'YES') {
    Write-Host 'No GitHub changes were made.'
    exit 0
}

if (-not (Test-Path -LiteralPath '.git')) {
    & git init -b main
    Assert-Ok 'Initialize Git'
}

# Configure identity for this repository only if the user has no Git identity.
$name = (& git config --get user.name 2>$null | Out-String).Trim()
if (-not $name) {
    & git config --local user.name $owner
    Assert-Ok 'Set Git username'
}
$email = (& git config --get user.email 2>$null | Out-String).Trim()
if (-not $email) {
    $uid = (& gh api user --jq '.id' 2>$null | Select-Object -First 1).Trim()
    Assert-Ok 'Read GitHub account ID'
    & git config --local user.email "$uid+$owner@users.noreply.github.com"
    Assert-Ok 'Set Git noreply email'
}

& git add --all
Assert-Ok 'Stage Veysona files'
& git diff --cached --quiet
if ($LASTEXITCODE -eq 1) {
    & git commit -m 'Backup Veysona Studio v1.7 private development source'
    Assert-Ok 'Commit source'
} elseif ($LASTEXITCODE -ne 0) {
    throw 'Unable to inspect staged changes.'
} else {
    Write-Host 'No changed files to commit.'
}
& git branch -M main
Assert-Ok 'Use main branch'

# Check if the destination exists, without overwriting other repositories.
& gh repo view $repo --json name --jq '.name' 2>$null | Out-Null
$exists = ($LASTEXITCODE -eq 0)
if (-not $exists) {
    Write-Host "Creating PRIVATE repository $repo ..." -ForegroundColor Cyan
    & gh repo create $repo --private --source . --remote origin --description 'Private development repository for Veysona Studio' --push
    Assert-Ok 'Create repository and push source'
} else {
    # A preexisting public repository would disclose the source. Never push to one.
    $visibility = (& gh repo view $repo --json visibility --jq '.visibility' 2>$null | Out-String).Trim()
    Assert-Ok 'Inspect repository visibility'
    if ($visibility -ne 'PRIVATE') { throw "Refusing to push to ${repo}: repository visibility is '$visibility', not PRIVATE. Make it private in GitHub settings first." }
    Write-Host "$repo already exists as a private repository. Checking its configured Git remote ..." -ForegroundColor Yellow
    $origin = (& git remote get-url origin 2>$null | Out-String).Trim()
    $expected1 = "https://github.com/$repo.git"
    $expected2 = "git@github.com:$repo.git"
    if (-not $origin) {
        & git remote add origin $expected1
        Assert-Ok 'Set origin remote'
    } elseif ($origin -ne $expected1 -and $origin -ne $expected2 -and $origin -ne "https://github.com/$repo") {
        throw "Current origin points to '$origin', not $repo. Refusing to push to a different repository."
    }
    & git push -u origin main
    Assert-Ok 'Push source'
}

Write-Host ''
Write-Host "Private repository created/updated: https://github.com/$repo" -ForegroundColor Green
Write-Host "Windows build status: https://github.com/$repo/actions" -ForegroundColor Cyan
Write-Host 'Open Actions, choose the Windows x64 workflow, and download the artifact after a successful build.'
Write-Host 'A successful build is not guaranteed; if it fails, open the failure log to see the compiler errors.'
Write-Host ''
