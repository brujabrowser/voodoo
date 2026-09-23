# SECRPC through chrome.runtime.sendMessage, once per mojo address.
# Writes Bruja/out/address-db.md: dagger, voodoo, mojo, and the reply.
$ErrorActionPreference = "Stop"
$addr = "127.0.0.1"
$port = 9341
$profile = "C:\Users\grego\Bruja\out\mojojs-chrome-db"
$outMd = "C:\Users\grego\Bruja\out\address-db.md"
$rowsPath = "C:\Users\grego\Bruja\out\address-rows.tsv"
$replyPath = "C:\Users\grego\Bruja\out\address-replies.tsv"
$chromeCandidates = @(
  "$env:ProgramFiles\Google\Chrome\Application\chrome.exe",
  "${env:ProgramFiles(x86)}\Google\Chrome\Application\chrome.exe",
  "$env:LOCALAPPDATA\Google\Chrome\Application\chrome.exe"
)
$chrome = $chromeCandidates | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
if (-not $chrome) { throw "chrome.exe not found" }
New-Item -ItemType Directory -Force -Path $profile | Out-Null
New-Item -ItemType Directory -Force -Path "C:\Users\grego\Bruja\out" | Out-Null

python "C:\Users\grego\Bruja\ng\address_rows.py" $rowsPath
$rows = Get-Content $rowsPath | Where-Object { $_ -and -not $_.StartsWith("#") }
if ($rows.Count -eq 0) { throw "no portfolio rows" }

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
  $text = ""
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
  if (-not $ws) { throw "no page target" }
  Write-Output "page $ws"

  $mojos = New-Object System.Collections.Generic.List[string]
  $seen = @{}
  foreach ($line in $rows) {
    $p = $line.Split("`t")
    if ($p.Length -lt 5) { continue }
    foreach ($col in 2, 3, 4) {
      $m = $p[$col]
      if (-not $seen.ContainsKey($m)) {
        $seen[$m] = $true
        $mojos.Add($m)
      }
    }
  }
  Write-Output ("mojo addresses " + $mojos.Count)

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
    $bytes = $utf8.GetBytes(($msg | ConvertTo-Json -Compress -Depth 6))
    $sock.SendAsync([ArraySegment[byte]]::new($bytes), [System.Net.WebSockets.WebSocketMessageType]::Text, $true, $ct).GetAwaiter().GetResult() | Out-Null
    return $id
  }
  function Read-Cdp([int]$id) {
    $buf = New-Object byte[] 1048576
    for ($n = 0; $n -lt 80; $n++) {
      $sb = New-Object System.Text.StringBuilder
      do {
        $got = $sock.ReceiveAsync([ArraySegment[byte]]::new($buf), $ct).GetAwaiter().GetResult()
        [void]$sb.Append($utf8.GetString($buf, 0, $got.Count))
      } while (-not $got.EndOfMessage)
      $text = $sb.ToString()
      if ($text -match ('"id"\s*:\s*' + $id + '\s*,\s*"result"')) { return $text }
    }
    throw "cdp id $id missing"
  }

  $enable = Send-Cdp "Runtime.enable" $null
  [void](Read-Cdp $enable)

  $listen = @'
(() => {
  if (globalThis.__secrpc) return "listener-already " + chrome.runtime.id;
  globalThis.__secrpc = true;
  chrome.runtime.onMessage.addListener((msg, sender, sendResponse) => {
    if (!msg || Number(msg.magic) !== 91556947316803) return false;
    Promise.resolve().then(() => {
      sendResponse({ result: "ok", method: msg.method, args: msg.args, state: "DeliverMessage" });
    });
    return true;
  });
  return "listener-on " + chrome.runtime.id;
})()
'@
  $lid = Send-Cdp "Runtime.evaluate" @{ expression = $listen; returnByValue = $true }
  $installed = Read-Cdp $lid
  Write-Output "listener $installed"

  $script:probe = @'
(async () => {
  const ordinals = [ORDINAL_LIST];
  if (!globalThis.__frame || !globalThis.__frame.contentWindow) {
    globalThis.__frame = await new Promise((resolve, reject) => {
      const frame = document.createElement("iframe");
      frame.src = location.href;
      frame.onload = () => resolve(frame);
      setTimeout(() => reject(new Error("frame timeout")), 3000);
      document.documentElement.appendChild(frame);
    });
  }
  const rt = globalThis.__frame.contentWindow.chrome.runtime;
  const lines = ["EXT\t" + chrome.runtime.id];
  for (let k = 0; k < ordinals.length; k++) {
    const ord = ordinals[k];
    const env = {
      magic: 91556947316803,
      host: "*",
      origin_host: "",
      method: "ordinal",
      args: [ord],
      extension_id: chrome.runtime.id,
      channel: "offscreen"
    };
    const reply = await new Promise((resolve) => {
      let done = false;
      const finish = (s) => { if (!done) { done = true; resolve(s); } };
      setTimeout(() => finish("timeout"), 500);
      try {
        rt.sendMessage(env, (h) => {
          let err = "";
          try { if (rt.lastError) err = String(rt.lastError.message || rt.lastError); } catch (e) {}
          let body = "";
          try { body = h === undefined ? "undefined" : JSON.stringify(h); } catch (e) { body = "unstringifiable"; }
          finish((err || "none") + " reply=" + (body || "undefined"));
        });
      } catch (e) {
        finish("throw " + (e && e.message ? e.message : "error"));
      }
    });
    lines.push(String(ord) + "\t" + reply.replace(/\t/g, " ").replace(/\n/g, " "));
  }
  return lines.join("\n");
})()
'@

  $replies = @{}
  $chunk = 250
  for ($i = 0; $i -lt $mojos.Count; $i += $chunk) {
    $end = [Math]::Min($i + $chunk, $mojos.Count)
    $slice = $mojos.GetRange($i, $end - $i)
    $listJson = ($slice | ForEach-Object { $_ }) -join ","
    $expr = $script:probe.Replace("ORDINAL_LIST", $listJson)
    $cid = Send-Cdp "Runtime.evaluate" @{ expression = $expr; awaitPromise = $true; returnByValue = $true; timeout = 60000 }
    $raw = Read-Cdp $cid
    $m = [regex]::Match($raw, '"value"\s*:\s*"((?:\\.|[^"\\])*)"')
    if (-not $m.Success) { throw ("no value at " + $i + " " + $raw.Substring(0, [Math]::Min(400, $raw.Length))) }
    $value = [regex]::Unescape($m.Groups[1].Value)
    foreach ($line in ($value -split "`n")) {
      if ($line.StartsWith("EXT`t")) { $script:extId = $line.Substring(4); continue }
      if ($line.StartsWith("MISSING")) { throw "sendMessage missing" }
      $tab = $line.IndexOf("`t")
      if ($tab -lt 1) { continue }
      $replies[$line.Substring(0, $tab)] = $line.Substring($tab + 1)
    }
    Write-Output ("chunk " + $end + "/" + $mojos.Count)
  }
  $sock.Dispose()

  $counts = @{}
  $sb = New-Object System.Text.StringBuilder
  [void]$sb.AppendLine("# address db")
  [void]$sb.AppendLine("")
  [void]$sb.AppendLine("SECRPC ``91556947316803`` via ``chrome.runtime.sendMessage``. Each row is one portfolio method. dagger is FNV-1 of the method name. voodoo is FNV-1a of the fully qualified ABI name. mojo is the Chrome scramble ordinal, and that is the address the send used.")
  [void]$sb.AppendLine("")
  [void]$sb.AppendLine("extension ``$script:extId``")
  [void]$sb.AppendLine("")
  [void]$sb.AppendLine("| name | dagger | voodoo | mojo | dagger reply | voodoo reply | mojo reply |")
  [void]$sb.AppendLine("| --- | --- | --- | --- | --- | --- | --- |")
  foreach ($line in $rows) {
    $p = $line.Split("`t")
    if ($p.Length -lt 5) { continue }
    $d = $replies[$p[2]]
    $v = $replies[$p[3]]
    $j = $replies[$p[4]]
    if (-not $d) { $d = "no reply" }
    if (-not $v) { $v = "no reply" }
    if (-not $j) { $j = "no reply" }
    if (-not $counts.ContainsKey($j)) { $counts[$j] = 0 }
    $counts[$j] = $counts[$j] + 1
    [void]$sb.AppendLine("| $($p[0]) | $($p[2]) | $($p[3]) | $($p[4]) | $($d.Replace('|','/')) | $($v.Replace('|','/')) | $($j.Replace('|','/')) |")
  }
  $summary = New-Object System.Text.StringBuilder
  [void]$summary.AppendLine("# address db")
  [void]$summary.AppendLine("")
  [void]$summary.AppendLine("SECRPC ``91556947316803`` via ``chrome.runtime.sendMessage``. The listener is ``onMessage`` on the extension background. The send comes from the other frame. Returning true is ResponsePending. The callback is DeliverMessage. $($rows.Count) methods. dagger is FNV-1 of the method name. voodoo is FNV-1a of the fully qualified ABI name. mojo is the Chrome scramble ordinal. Each address is sent as the envelope args.")
  [void]$summary.AppendLine("")
  [void]$summary.AppendLine("extension ``$script:extId``")
  [void]$summary.AppendLine("")
  [void]$summary.AppendLine("| response | count |")
  [void]$summary.AppendLine("| --- | --- |")
  foreach ($k in ($counts.Keys | Sort-Object)) {
    [void]$summary.AppendLine("| $($k.Replace('|','/')) | $($counts[$k]) |")
  }
  [void]$summary.AppendLine("")
  $body = $sb.ToString()
  $body = $body.Substring($body.IndexOf("| name |"))
  [System.IO.File]::WriteAllText($outMd, $summary.ToString() + $body)
  Write-Output ("wrote " + $outMd)
  foreach ($k in ($counts.Keys | Sort-Object)) {
    Write-Output ("count " + $counts[$k] + " " + $k)
  }
} finally {
  if ($started) { & taskkill.exe /PID $started.Id /T /F | Out-Null }
}
