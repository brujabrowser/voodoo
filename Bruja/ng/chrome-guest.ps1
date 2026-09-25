# Chrome is the harness. One DevTools session, one world per ChromeFrame.
$ErrorActionPreference = "Stop"
$addr = "127.0.0.1"
$port = 9362
$profile = "C:\Users\grego\Bruja\out\chrome-guest-profile"
$outTsv = Join-Path $PSScriptRoot "..\out\chrome-guest.tsv"
$probe = @'
(function () {
  var c = globalThis.chrome;
  var rows = [];
  rows.push("href\t" + location.href);
  rows.push("loadTimes\t" + (c && typeof c.loadTimes));
  rows.push("app\t" + (c && typeof c.app));
  rows.push("runtime.sendMessage\t" + (c && c.runtime && typeof c.runtime.sendMessage));
  rows.push("runtime.id\t" + (c && c.runtime && c.runtime.id ? c.runtime.id : "absent"));
  rows.push("Mojo\t" + (typeof Mojo));
  rows.push("Mojo.bindInterface\t" + (typeof Mojo !== "undefined" && typeof Mojo.bindInterface));
  rows.push("AddSink\t" + (typeof globalThis["access_code_cast.mojom.PageHandler"]));
  return rows.join("\n");
})()
'@
$chromeCandidates = @(
  "$env:ProgramFiles\Google\Chrome\Application\chrome.exe",
  "${env:ProgramFiles(x86)}\Google\Chrome\Application\chrome.exe",
  "$env:LOCALAPPDATA\Google\Chrome\Application\chrome.exe"
)
$chrome = $chromeCandidates | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
if (-not $chrome) { throw "chrome.exe not found" }
New-Item -ItemType Directory -Force -Path $profile | Out-Null
New-Item -ItemType Directory -Force -Path (Split-Path $outTsv) | Out-Null
$started = Start-Process -FilePath $chrome -PassThru -ArgumentList @(
  "--remote-debugging-port=$port", "--window-size=800,600", "--remote-allow-origins=*",
  "--user-data-dir=$profile", "--no-first-run", "--no-default-browser-check", "about:blank")
$ready = $false
for ($i = 0; $i -lt 50; $i++) {
  try { $c = New-Object System.Net.Sockets.TcpClient; $c.Connect($addr, $port); $c.Close(); $ready = $true; break }
  catch { Start-Sleep -Milliseconds 200 }
}
if (-not $ready) { throw "devtools port $port did not open" }
try {
  function Connect-Page([string]$match) {
    $list = Invoke-WebRequest -UseBasicParsing "http://${addr}:${port}/json/list"
    $ws = $null
    $hits = [regex]::Matches($list.Content, '"url"\s*:\s*"([^"]+)"[\s\S]{0,500}?"webSocketDebuggerUrl"\s*:\s*"(ws://[^"]+)"')
    foreach ($h in $hits) {
      if ($h.Groups[1].Value -match $match) { $ws = $h.Groups[2].Value -replace '\\/', '/'; break }
    }
    if (-not $ws) { throw "no target $match" }
    $sock = [System.Net.WebSockets.ClientWebSocket]::new()
    $sock.ConnectAsync([Uri]$ws, [System.Threading.CancellationToken]::None).GetAwaiter().GetResult() | Out-Null
    return $sock
  }
  $script:sock = Connect-Page "about:blank"
  $ct = [System.Threading.CancellationToken]::None
  $utf8 = [System.Text.Encoding]::UTF8
  $script:nextId = 1
  function Send-Cdp([string]$method, $params) {
    $id = $script:nextId; $script:nextId = $id + 1
    $msg = @{ id = $id; method = $method }
    if ($null -ne $params) { $msg.params = $params }
    $bytes = $utf8.GetBytes(($msg | ConvertTo-Json -Compress -Depth 6))
    $script:sock.SendAsync([ArraySegment[byte]]::new($bytes), [System.Net.WebSockets.WebSocketMessageType]::Text, $true, $ct).GetAwaiter().GetResult() | Out-Null
    return $id
  }
  function Read-Cdp([int]$id) {
    $buf = New-Object byte[] 1048576
    for ($n = 0; $n -lt 80; $n++) {
      $sb = New-Object System.Text.StringBuilder
      do {
        $got = $script:sock.ReceiveAsync([ArraySegment[byte]]::new($buf), $ct).GetAwaiter().GetResult()
        [void]$sb.Append($utf8.GetString($buf, 0, $got.Count))
      } while (-not $got.EndOfMessage)
      $t = $sb.ToString()
      if ($t -match ('"id"\s*:\s*' + $id + '\b')) { return $t }
    }
    throw "cdp id $id missing"
  }
  function Probe([string]$frame, [int]$contextId) {
    $params = @{ expression = $probe; returnByValue = $true }
    if ($contextId -gt 0) { $params.contextId = $contextId }
    $raw = Read-Cdp (Send-Cdp "Runtime.evaluate" $params)
    $m = [regex]::Match($raw, '"value"\s*:\s*"((?:\\.|[^"\\])*)"')
    if (-not $m.Success) { throw "$frame no value $raw" }
    $text = [regex]::Unescape($m.Groups[1].Value)
    $lines = New-Object System.Collections.Generic.List[string]
    foreach ($line in ($text -split "`n")) { $lines.Add($frame + "`t" + $line) }
    return ,$lines
  }
  $rows = New-Object System.Collections.Generic.List[string]
  $rows.Add("frame`tapi`tvalue")
  [void](Read-Cdp (Send-Cdp "Page.enable" $null))
  foreach ($line in (Probe "page" 0)) { $rows.Add($line); Write-Output $line }
  $tree = Read-Cdp (Send-Cdp "Page.getFrameTree" $null)
  $frameId = [regex]::Match($tree, '"id"\s*:\s*"([^"]+)"').Groups[1].Value
  $world = Read-Cdp (Send-Cdp "Page.createIsolatedWorld" @{ frameId = $frameId; worldName = "bruja"; grantUniveralAccess = $true })
  $ctx = [int]([regex]::Match($world, '"executionContextId"\s*:\s*(\d+)').Groups[1].Value)
  foreach ($line in (Probe "isolated" $ctx)) { $rows.Add($line); Write-Output $line }
  [void](Read-Cdp (Send-Cdp "Page.navigate" @{ url = "chrome-extension://nkeimhogjdpnpccoofpliimaahmaaome/background.html" }))
  Start-Sleep -Milliseconds 800
  $script:sock.Dispose()
  $script:sock = Connect-Page "nkeimhogjdpnpccoofpliimaahmaaome"
  $script:nextId = 1
  foreach ($line in (Probe "extension" 0)) { $rows.Add($line); Write-Output $line }
  [void](Read-Cdp (Send-Cdp "Page.enable" $null))
  [void](Read-Cdp (Send-Cdp "Page.navigate" @{ url = "chrome://policy" }))
  Start-Sleep -Milliseconds 800
  foreach ($line in (Probe "internals" 0)) { $rows.Add($line); Write-Output $line }
  [System.IO.File]::WriteAllLines($outTsv, $rows)
  Write-Output ("wrote " + ($rows.Count - 1) + " " + $outTsv)
  $script:sock.Dispose()
} finally {
  if ($started) { & taskkill.exe /PID $started.Id /T /F | Out-Null }
}
