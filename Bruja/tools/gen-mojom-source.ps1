# voodoomc every published chromium/src .mojom into Bruja occupancy source.
# This is the corpus -> our headers hop. Engines are not rewritten here.
param(
  [string]$Root = (Join-Path $PSScriptRoot "..")
)

$ErrorActionPreference = "Stop"
$Root = [IO.Path]::GetFullPath($Root)
$Voodoo = Join-Path $Root "..\WASMVoodooCompile"
$Chromium = Join-Path $Root "third_party\chromium-src"
$Blinker = Join-Path $Root "..\WASMBlinker"
$Renderer = Join-Path $Root "..\WASMRenderer"
$Lime = Join-Path $Root "..\WASMLime"
$Out = Join-Path $Root "build\generated\mojom"
$Gen = Join-Path $Root "build\generated"

function Find-Exe([string[]]$Candidates) {
  foreach ($c in $Candidates) {
    if (Test-Path $c) { return [IO.Path]::GetFullPath($c) }
  }
  return $null
}

$voodoomc = Find-Exe @(
  (Join-Path $Voodoo "build\voodoomc.exe"),
  (Join-Path $Voodoo "build\Release\voodoomc.exe"),
  (Join-Path $Voodoo "build\Debug\voodoomc.exe")
)
if (-not $voodoomc) { throw "voodoomc.exe not found" }
if (-not (Test-Path $Chromium)) { throw "chromium-src missing; run tools/fetch-chromium-mojom.ps1" }

New-Item -ItemType Directory -Force -Path $Out | Out-Null
New-Item -ItemType Directory -Force -Path $Gen | Out-Null

Write-Host "[voodoomc] sibling occupancy IDL -> $Gen"
& $voodoomc (Join-Path $Root "idl\browser.voodoom") -o (Join-Path $Gen "browser_mojo.h") "--out-dir=$Gen"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $voodoomc (Join-Path $Blinker "interfaces\local_frame.voodoom") -o (Join-Path $Gen "local_frame_mojo.h") "--out-dir=$Gen"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $voodoomc (Join-Path $Renderer "interfaces\renderer.voodoom") `
  "--import-dir=$(Join-Path $Blinker 'interfaces')" -o (Join-Path $Gen "renderer_mojo.h") "--out-dir=$Gen"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
$limeIf = Join-Path $Lime "interfaces"
& $voodoomc (Join-Path $limeIf "navigation.voodoom") "--import-dir=$limeIf" -o (Join-Path $Gen "navigation_mojo.h") "--out-dir=$Gen"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $voodoomc (Join-Path $limeIf "frame.voodoom") "--import-dir=$limeIf" -o (Join-Path $Gen "frame_mojo.h") "--out-dir=$Gen"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "[voodoomc] published chromium/src .mojom corpus -> $Out"
cmake -DVOODOOMC="$voodoomc" -DCORPUS="$Chromium" -DOUT="$Out" `
  -P (Join-Path $Root "tools\parse_mojom_corpus.cmake")
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$n = (Get-ChildItem $Out -Filter *.h -Recurse -File -ErrorAction SilentlyContinue | Measure-Object).Count
Write-Host "mojom occupancy headers: $n"
if ($n -eq 0) { throw "voodoomc wrote no occupancy headers under $Out" }
