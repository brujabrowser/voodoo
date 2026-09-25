# Call the chrome.* bindings a LocalFrame script can see.
# Page frame: loadTimes, csi, app. Extension frame: the component background.
$ErrorActionPreference = "Stop"
$addr = "127.0.0.1"
$port = 9360
$profile = "C:\Users\grego\Bruja\out\chrome-bind-profile"
$outTsv = Join-Path $PSScriptRoot "..\out\chrome-bind.tsv"
$expr = @'
(async function () {
  function dump(v) {
    try { return JSON.stringify(v); } catch (e) { return String(v); }
  }
  function keys(obj) {
    if (obj == null) return "absent";
    return Object.getOwnPropertyNames(obj).join(",");
  }
  function once(api, fn) {
    return new Promise(function (resolve) {
      var done = false;
      function finish(v) { if (!done) { done = true; resolve(api + "\t" + v); } }
      try {
        var ret = fn(function (v) { finish(dump(v)); });
        if (ret !== undefined) finish(dump(ret));
        setTimeout(function () { finish("timeout"); }, 1200);
      } catch (e) { finish("ERR " + (e && e.message ? e.message : e)); }
    });
  }
  var rows = [];
  var c = globalThis.chrome;
  rows.push("chrome\t" + keys(c));
  rows.push("href\t" + location.href);
  if (!c || !c.runtime || !c.runtime.id) {
    if (c && typeof c.loadTimes === "function") rows.push("chrome.loadTimes\t" + dump(c.loadTimes()));
    if (c && typeof c.csi === "function") rows.push("chrome.csi\t" + dump(c.csi()));
    if (c && c.app) rows.push("chrome.app\t" + keys(c.app));
    return rows.join("\n");
  }
  rows.push("chrome.runtime.id\t" + c.runtime.id);
  rows.push("chrome.runtime.getManifest\t" + dump(c.runtime.getManifest()));
  rows.push("chrome.runtime.getURL\t" + c.runtime.getURL(""));
  rows.push("chrome.extension.inIncognitoContext\t" + String(c.extension.inIncognitoContext));
  rows.push("chrome.extension.getViews\t" + String(c.extension.getViews().length));
  if (c.i18n && c.i18n.getUILanguage) rows.push("chrome.i18n.getUILanguage\t" + c.i18n.getUILanguage());
  if (c.app && c.app.getDetails) rows.push("chrome.app.getDetails\t" + dump(c.app.getDetails()));
  rows.push("chrome.dom\t" + keys(c.dom));
  rows.push("chrome.enterprise\t" + keys(c.enterprise));
  rows.push("chrome.processes\t" + keys(c.processes));
  rows.push("chrome.system\t" + keys(c.system));
  rows.push("chrome.feedbackPrivate\t" + keys(c.feedbackPrivate));
  rows.push("chrome.webrtcLoggingPrivate\t" + keys(c.webrtcLoggingPrivate));
  rows.push(await once("chrome.runtime.getPlatformInfo", function (cb) { c.runtime.getPlatformInfo(cb); }));
  if (c.management && c.management.getSelf) rows.push(await once("chrome.management.getSelf", function (cb) { c.management.getSelf(cb); }));
  if (c.permissions && c.permissions.getAll) rows.push(await once("chrome.permissions.getAll", function (cb) { c.permissions.getAll(cb); }));
  if (c.tabs && c.tabs.query) rows.push(await once("chrome.tabs.query", function (cb) { c.tabs.query({}, cb); }));
  if (c.windows && c.windows.getAll) rows.push(await once("chrome.windows.getAll", function (cb) { c.windows.getAll(cb); }));
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
  function Invoke-Bind([string]$label) {
    $raw = Read-Cdp (Send-Cdp "Runtime.evaluate" @{ expression = $expr; awaitPromise = $true; returnByValue = $true })
    $m = [regex]::Match($raw, '"value"\s*:\s*"((?:\\.|[^"\\])*)"')
    if (-not $m.Success) { throw "$label no value $raw" }
    $text = [regex]::Unescape($m.Groups[1].Value)
    $lines = New-Object System.Collections.Generic.List[string]
    foreach ($line in ($text -split "`n")) {
      $row = $label + "`t" + $line
      $lines.Add($row)
    }
    return ,$lines
  }
  $rows = New-Object System.Collections.Generic.List[string]
  $rows.Add("frame`tapi`tvalue")
  foreach ($line in (Invoke-Bind "page")) { $rows.Add($line) }
  [void](Read-Cdp (Send-Cdp "Page.enable" $null))
  [void](Read-Cdp (Send-Cdp "Page.navigate" @{ url = "chrome-extension://nkeimhogjdpnpccoofpliimaahmaaome/background.html" }))
  Start-Sleep -Milliseconds 800
  $script:sock.Dispose()
  $script:sock = Connect-Page "nkeimhogjdpnpccoofpliimaahmaaome"
  $script:nextId = 1
  foreach ($line in (Invoke-Bind "extension")) { $rows.Add($line) }
  [System.IO.File]::WriteAllLines($outTsv, $rows)
  Write-Output ("wrote " + ($rows.Count - 1) + " " + $outTsv)
  $script:sock.Dispose()
} finally {
  if ($started) { & taskkill.exe /PID $started.Id /T /F | Out-Null }
}
