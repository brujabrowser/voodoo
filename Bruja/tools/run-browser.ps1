# Windowed occupancy: RendererImpl.CreateFrame → LocalFrameImpl → FrameWindow.
$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$LimeBuild = Join-Path (Split-Path $Root) "WASMLime\build"
$Exe = Join-Path $LimeBuild "bruja.exe"

cmake --build $LimeBuild --target bruja
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$Start = if ($args.Count -gt 0) { $args } else { @("about:bruja") }
& $Exe @Start
exit $LASTEXITCODE
