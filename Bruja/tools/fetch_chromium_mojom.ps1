# Pull every Chromium *.mojom into mojom/, paths matching chromium/src.
# Sparse clone (blobless) then copy. Re-run to refresh.
#   powershell -File tools/fetch_chromium_mojom.ps1
$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$Src = Join-Path $Root "third_party\chromium-src"
$Dst = Join-Path $Root "mojom"
$Remote = "https://chromium.googlesource.com/chromium/src.git"
if ($env:BRUJA_CHROMIUM_REMOTE) { $Remote = $env:BRUJA_CHROMIUM_REMOTE }

New-Item -ItemType Directory -Force -Path (Join-Path $Root "third_party") | Out-Null

if (-not (Test-Path (Join-Path $Src ".git"))) {
  Write-Host "sparse clone $Remote (blobless, depth 1)"
  git clone --depth 1 --filter=blob:none --sparse $Remote $Src
  if ($LASTEXITCODE -ne 0) { throw "git clone failed" }
}

Push-Location $Src
try {
  git sparse-checkout init --no-cone
  if ($LASTEXITCODE -ne 0) { throw "sparse-checkout init failed" }
  # Non-cone: every *.mojom in the tree, nothing else.
  @"
**/*.mojom
"@ | Set-Content -Encoding ascii .git\info\sparse-checkout
  git sparse-checkout reapply
  if ($LASTEXITCODE -ne 0) { throw "sparse-checkout reapply failed" }
  git checkout
  if ($LASTEXITCODE -ne 0) { throw "git checkout failed" }
} finally {
  Pop-Location
}

if (Test-Path $Dst) { Remove-Item -Recurse -Force $Dst }
New-Item -ItemType Directory -Force -Path $Dst | Out-Null

$files = Get-ChildItem -Path $Src -Recurse -Filter *.mojom -File |
  Where-Object { $_.FullName -notmatch '[\\/]\.git[\\/]' }
$count = 0
foreach ($f in $files) {
  $rel = $f.FullName.Substring($Src.Length).TrimStart('\', '/')
  $out = Join-Path $Dst $rel
  $dir = Split-Path $out -Parent
  if (-not (Test-Path $dir)) { New-Item -ItemType Directory -Force -Path $dir | Out-Null }
  Copy-Item -Force $f.FullName $out
  $count++
}

$rev = git -C $Src rev-parse HEAD
Set-Content -Encoding utf8 (Join-Path $Dst "REVISION.txt") @"
chromium/src $rev
remote $Remote
files $count
fetched $(Get-Date -Format o)
"@

Write-Host "vendored $count mojom files -> $Dst"
Write-Host "revision $rev"
