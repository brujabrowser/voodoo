# Pull every Chromium .mojom from published source
# (chromium.googlesource.com/chromium/src), not from a local leftover.
#
# --no-checkout is required: `git clone --sparse` otherwise smudges the
# Chromium *root* first (promisor blob-per-file) and looks dead after
# "Receiving objects: 100%". Sparse **/*.mojom first, then checkout.
param(
  [string]$Dest = (Join-Path $PSScriptRoot "..\third_party\chromium-src")
)

$ErrorActionPreference = "Stop"
$Dest = [IO.Path]::GetFullPath($Dest)
$googlesource = "https://chromium.googlesource.com/chromium/src.git"
$github = "https://github.com/chromium/chromium.git"

New-Item -ItemType Directory -Force -Path (Split-Path $Dest) | Out-Null

function Init-SparseMojom {
  git sparse-checkout init --no-cone
  git sparse-checkout set "**/*.mojom"
}

if (-not (Test-Path (Join-Path $Dest ".git"))) {
  Write-Host "clone $googlesource (blobless, no checkout)"
  git clone --depth 1 --filter=blob:none --no-checkout $googlesource $Dest
  if ($LASTEXITCODE -ne 0) {
    Write-Host "googlesource clone failed; $github"
    git clone --depth 1 --filter=blob:none --no-checkout $github $Dest
  }
}

Set-Location $Dest
Init-SparseMojom

Write-Host "fetch published main"
git fetch --depth 1 --filter=blob:none --progress origin main
if ($LASTEXITCODE -ne 0) {
  git remote set-url origin $github
  git fetch --depth 1 --filter=blob:none --progress origin main
}

git checkout -B main origin/main --progress
$count = @(Get-ChildItem -Recurse -Filter *.mojom -File).Count
Write-Host "mojom files: $count"
if ($count -lt 1) { throw "no .mojom files checked out" }
