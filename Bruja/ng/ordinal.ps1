# Send portfolio ordinals on the extension channel and record each reply.
# No arguments: every mojo ordinal in the portfolio (one per method).
# Arguments: those addresses only.
$ErrorActionPreference = "Stop"
$addr = "127.0.0.1"
$port = 9351
$profile = "C:\Users\grego\Bruja\out\mojojs-chrome-ordinal"
$hostJs = Join-Path $PSScriptRoot "ext\host.js"
$portfolioCandidates = @(
  (Join-Path $PSScriptRoot "..\out\mojovm-portfolio.txt"),
  "C:\Users\grego\Bruja\out\mojovm-portfolio.txt"
)
$outTsv = Join-Path $PSScriptRoot "..\out\ordinal-states.tsv"
$deviceTsv = Join-Path $PSScriptRoot "..\out\device-log.tsv"
$chunk = 250

$addresses = @()
foreach ($a in $args) { $addresses += $a }
if ($addresses.Count -eq 0) {
  $portfolio = $portfolioCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
  if (-not $portfolio) { throw "portfolio not found" }
  $seen = @{}
  foreach ($line in Get-Content $portfolio) {
    if (-not $line -or $line.StartsWith("#")) { continue }
    $p = $line.Split("`t")
    if ($p.Length -lt 3) { continue }
    $m = $p[2]
    if (-not $seen.ContainsKey($m)) {
      $seen[$m] = $true
      $addresses += $m
    }
  }
}
if ($addresses.Count -eq 0) { throw "no ordinals" }
Write-Output ("ordinals " + $addresses.Count)

$chromeCandidates = @(
  "$env:ProgramFiles\Google\Chrome\Application\chrome.exe",
  "${env:ProgramFiles(x86)}\Google\Chrome\Application\chrome.exe",
  "$env:LOCALAPPDATA\Google\Chrome\Application\chrome.exe"
)
$chrome = $chromeCandidates | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
if (-not $chrome) { throw "chrome.exe not found" }
if (-not (Test-Path $hostJs)) { throw "missing $hostJs" }
New-Item -ItemType Directory -Force -Path (Split-Path $outTsv) | Out-Null
New-Item -ItemType Directory -Force -Path $profile | Out-Null

$started = Start-Process -FilePath $chrome -PassThru -ArgumentList @(
  "--headless=new",
  "--remote-debugging-port=$port",
  "--remote-allow-origins=*",
  "--enable-blink-features=MojoJS,MojoJSTest",
  "--user-data-dir=$profile",
  "--no-first-run",
  "--no-default-browser-check",
  "about:blank"
)
$ready = $false
for ($i = 0; $i -lt 50; $i++) {
  try {
    $client = New-Object System.Net.Sockets.TcpClient
    $client.Connect($addr, $port)
    $client.Close()
    $ready = $true
    break
  } catch { Start-Sleep -Milliseconds 200 }
}
if (-not $ready) { throw "devtools port $port did not open" }

try {
  $ws = ""
  for ($try = 0; $try -lt 25; $try++) {
    $list = Invoke-WebRequest -UseBasicParsing "http://${addr}:${port}/json/list"
    $text = $list.Content
    $ext = [regex]::Match($text, '"url"\s*:\s*"chrome-extension://nkeimhogjdpnpccoofpliimaahmaaome/[^"]*"[\s\S]{0,500}?"webSocketDebuggerUrl"\s*:\s*"(ws://[^"]+)"')
    if (-not $ext.Success) {
      $ext = [regex]::Match($text, '"type"\s*:\s*"background_page"[\s\S]{0,500}?"webSocketDebuggerUrl"\s*:\s*"(ws://[^"]+)"')
    }
    if ($ext.Success) { $ws = $ext.Groups[1].Value; break }
    Start-Sleep -Milliseconds 200
  }
  $ws = $ws -replace '\\/', '/'
  if (-not $ws) { throw "no extension background" }
  Write-Output "page $ws"

  $sock = [System.Net.WebSockets.ClientWebSocket]::new()
  $ct = [System.Threading.CancellationToken]::None
  $sock.ConnectAsync([Uri]$ws, $ct).GetAwaiter().GetResult() | Out-Null
  $utf8 = [System.Text.Encoding]::UTF8
  $script:nextId = 1
  function Send-Cdp([string]$method, $params) {
    $id = $script:nextId
    $script:nextId = $id + 1
    $msg = @{ id = $id; method = $method }
    if ($null -ne $params) { $msg.params = $params }
    $bytes = $utf8.GetBytes(($msg | ConvertTo-Json -Compress -Depth 8))
    $sock.SendAsync([ArraySegment[byte]]::new($bytes), [System.Net.WebSockets.WebSocketMessageType]::Text, $true, $ct).GetAwaiter().GetResult() | Out-Null
    return $id
  }
  function Read-Cdp([int]$id) {
    $buf = New-Object byte[] 1048576
    for ($n = 0; $n -lt 400; $n++) {
      $sb = New-Object System.Text.StringBuilder
      do {
        $got = $sock.ReceiveAsync([ArraySegment[byte]]::new($buf), $ct).GetAwaiter().GetResult()
        [void]$sb.Append($utf8.GetString($buf, 0, $got.Count))
      } while (-not $got.EndOfMessage)
      $t = $sb.ToString()
      if ($t -match ('"id"\s*:\s*' + $id + '\b')) { return $t }
    }
    throw "cdp id $id missing"
  }
  function Eval([string]$expression, [bool]$await) {
    $params = @{ expression = $expression; returnByValue = $true }
    if ($await) { $params.awaitPromise = $true; $params.timeout = 120000 }
    $raw = Read-Cdp (Send-Cdp "Runtime.evaluate" $params)
    $m = [regex]::Match($raw, '"value"\s*:\s*"((?:\\.|[^"\\])*)"')
    if (-not $m.Success) { throw $raw.Substring(0, [Math]::Min(500, $raw.Length)) }
    return [regex]::Unescape($m.Groups[1].Value)
  }

  [void](Read-Cdp (Send-Cdp "Runtime.enable" $null))
  Write-Output (Eval (Get-Content -Raw $hostJs) $false)
  $opened = Eval @'
(async () => {
  if (globalThis.__frame && globalThis.__frame.contentWindow) return "frame-already";
  globalThis.__frame = await new Promise((resolve, reject) => {
    const node = document.createElement("iframe");
    node.src = location.href;
    node.onload = () => resolve(node);
    setTimeout(() => reject(new Error("frame timeout")), 3000);
    document.documentElement.appendChild(node);
  });
  return "frame-open " + chrome.runtime.id;
})()
'@ $true
  Write-Output $opened

  $lines = New-Object System.Collections.Generic.List[string]
  $ok = 0
  for ($i = 0; $i -lt $addresses.Count; $i += $chunk) {
    $end = [Math]::Min($i + $chunk, $addresses.Count)
    $slice = $addresses[$i..($end - 1)]
    $listJson = ($slice | ForEach-Object { $_ }) -join ","
    $expr = @"
(async () => {
  const rt = globalThis.__frame.contentWindow.chrome.runtime;
  const ordinals = [$listJson];
  const rows = [];
  for (let k = 0; k < ordinals.length; k++) {
    const ord = ordinals[k];
    const env = {
      magic: 91556947316803,
      host: "offscreen",
      method: "cycle",
      args: [ord],
      extension_id: chrome.runtime.id,
      channel: "offscreen"
    };
    const reply = await new Promise((resolve) => {
      let done = false;
      const finish = (s) => { if (!done) { done = true; resolve(s); } };
      setTimeout(() => finish("timeout"), 1500);
      try {
        rt.sendMessage(env, (h) => {
          let err = "";
          try { if (rt.lastError) err = String(rt.lastError.message || rt.lastError); } catch (e) {}
          let body = "";
          try { body = h === undefined ? "undefined" : JSON.stringify(h); } catch (e) { body = "unstringifiable"; }
          finish((err || "none") + "\t" + body);
        });
      } catch (e) { finish("throw\t" + (e && e.message ? e.message : "error")); }
    });
    rows.push(String(ord) + "\t" + reply);
  }
  return rows.join("\n");
})()
"@
    $value = Eval $expr $true
    foreach ($line in ($value -split "`n")) {
      if (-not $line) { continue }
      $lines.Add($line)
      if ($line -match "`tnone`t") { $ok++ }
    }
    Write-Output ("sent " + $end + " ok " + $ok)
  }
  $stat = Eval @'
(async () => {
  const rt = globalThis.__frame.contentWindow.chrome.runtime;
  const env = {
    magic: 91556947316803,
    host: "offscreen",
    method: "stat",
    args: [],
    extension_id: chrome.runtime.id,
    channel: "offscreen"
  };
  return await new Promise((resolve) => {
    let done = false;
    const finish = (s) => { if (!done) { done = true; resolve(s); } };
    setTimeout(() => finish("timeout"), 2000);
    rt.sendMessage(env, (h) => {
      let body = "";
      try { body = JSON.stringify(h); } catch (e) { body = "unstringifiable"; }
      finish(body);
    });
  });
})()
'@ $true
  $device = Eval @'
(async () => {
  const rt = globalThis.__frame.contentWindow.chrome.runtime;
  const env = {
    magic: 91556947316803,
    host: "offscreen",
    method: "deviceLog",
    args: [],
    extension_id: chrome.runtime.id,
    channel: "offscreen"
  };
  return await new Promise((resolve) => {
    let done = false;
    const finish = (s) => { if (!done) { done = true; resolve(s); } };
    setTimeout(() => finish("timeout"), 2000);
    rt.sendMessage(env, (h) => {
      let body = "";
      try { body = JSON.stringify(h); } catch (e) { body = "unstringifiable"; }
      finish(body);
    });
  });
})()
'@ $true
  Write-Output ("stat " + $stat)
  Write-Output ("device " + $device)
  $deviceLines = New-Object System.Collections.Generic.List[string]
  $deviceLines.Add("address`tstate`texecuting")
  foreach ($line in $lines) {
    $parts = $line.Split("`t", 3)
    if ($parts.Length -lt 3) { continue }
    $executing = "false"
    if ($parts[2] -match '"executing":true') { $executing = "true" }
    $deviceLines.Add($parts[0] + "`trun`t" + $executing)
  }
  [System.IO.File]::WriteAllLines($outTsv, $lines)
  [System.IO.File]::WriteAllLines($deviceTsv, $deviceLines)
  Write-Output ("wrote " + $lines.Count + " " + $outTsv)
  Write-Output ("wrote " + ($deviceLines.Count - 1) + " " + $deviceTsv)
  $sock.Dispose()
} finally {
  if ($started) { & taskkill.exe /PID $started.Id /T /F | Out-Null }
}
