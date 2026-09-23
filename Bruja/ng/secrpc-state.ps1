# Open the extension channel for real: a service worker is listening, then
# SECRPC sendMessage runs for the three DispatchDisconnect addresses.
$ErrorActionPreference = "Stop"
$addr = "127.0.0.1"
$port = 9338
$profile = "C:\Users\grego\Bruja\out\mojojs-chrome-state3"
$ext = "C:\Users\grego\Bruja\ng\ext"
$chromeCandidates = @(
  "$env:ProgramFiles\Google\Chrome\Application\chrome.exe",
  "${env:ProgramFiles(x86)}\Google\Chrome\Application\chrome.exe",
  "$env:LOCALAPPDATA\Google\Chrome\Application\chrome.exe"
)
$chrome = $chromeCandidates | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
if (-not $chrome) { throw "chrome.exe not found" }
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
  for ($i = 0; $i -lt 25; $i++) {
    $list = Invoke-WebRequest -UseBasicParsing "http://${addr}:${port}/json/list"
    $m = [regex]::Match($list.Content, '"url"\s*:\s*"chrome-extension://nkeimhogjdpnpccoofpliimaahmaaome/[^"]*"[\s\S]{0,500}?"webSocketDebuggerUrl"\s*:\s*"(ws://[^"]+)"')
    if (-not $m.Success) {
      $m = [regex]::Match($list.Content, '"type"\s*:\s*"background_page"[\s\S]{0,500}?"webSocketDebuggerUrl"\s*:\s*"(ws://[^"]+)"')
    }
    if ($m.Success) { $ws = $m.Groups[1].Value -replace '\\/', '/'; break }
    Start-Sleep -Milliseconds 200
  }
  if (-not $ws) {
    Set-Content -Encoding utf8 "C:\Users\grego\Bruja\out\secrpc-targets.txt" $list.Content
    throw "extension background target missing"
  }
  Write-Output "worker $ws"

  $sock = [System.Net.WebSockets.ClientWebSocket]::new()
  $ct = [System.Threading.CancellationToken]::None
  $sock.ConnectAsync([Uri]$ws, $ct).GetAwaiter().GetResult() | Out-Null
  $utf8 = [System.Text.Encoding]::UTF8
  function Send-Cdp([int]$id, [string]$method, $params) {
    $msg = @{ id = $id; method = $method }
    if ($null -ne $params) { $msg.params = $params }
    $bytes = $utf8.GetBytes(($msg | ConvertTo-Json -Compress -Depth 6))
    $sock.SendAsync([ArraySegment[byte]]::new($bytes), [System.Net.WebSockets.WebSocketMessageType]::Text, $true, $ct).GetAwaiter().GetResult() | Out-Null
  }
  function Read-Cdp([int]$id) {
    $buf = New-Object byte[] 262144
    for ($n = 0; $n -lt 40; $n++) {
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
  Send-Cdp 1 "Runtime.enable" $null
  [void](Read-Cdp 1)
  $listen = @'
(() => {
  if (globalThis.__secrpc) return "listener-already";
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
  Send-Cdp 2 "Runtime.evaluate" @{ expression = $listen; returnByValue = $true }
  $installed = Read-Cdp 2
  Write-Output "listener $installed"
  $expr = @'
(async () => {
  const probes = [["dagger", 941764473], ["voodoo", 1481416133], ["mojo", 2592569]];
  const rows = ["extension=" + chrome.runtime.id];
  for (let i = 0; i < probes.length; i++) {
    const name = probes[i][0];
    const ord = probes[i][1];
    const env = {
      magic: 91556947316803,
      host: "*",
      origin_host: "",
      method: "ordinal",
      args: [ord],
      extension_id: chrome.runtime.id,
      channel: "runtime"
    };
  const reply = await new Promise((resolve) => {
    let done = false;
    const finish = (s) => { if (!done) { done = true; resolve(s); } };
    const frame = document.createElement("iframe");
    frame.src = location.href;
    frame.onload = () => {
      try {
        const rt = frame.contentWindow.chrome.runtime;
        rt.sendMessage(env, (h) => {
          let err = "";
          try { if (rt.lastError) err = String(rt.lastError.message || rt.lastError); } catch (e) {}
          let body = "";
          try { body = h === undefined ? "undefined" : JSON.stringify(h); } catch (e) { body = "unstringifiable"; }
          finish("lastError=" + (err || "none") + " reply=" + body);
        });
      } catch (e) { finish("throw " + (e && e.message ? e.message : "error")); }
    };
    setTimeout(() => finish("timeout"), 2500);
    document.documentElement.appendChild(frame);
  });
    rows.push(name + " " + ord + " " + reply);
  }
  return rows.join("\n");
})()
'@
  Send-Cdp 3 "Runtime.evaluate" @{ expression = $expr; awaitPromise = $true; returnByValue = $true; timeout = 20000 }
  $raw = Read-Cdp 3
  $sock.Dispose()
  Write-Output "capture"
  Write-Output $raw
} finally {
  if ($started) { & taskkill.exe /PID $started.Id /T /F | Out-Null }
}
