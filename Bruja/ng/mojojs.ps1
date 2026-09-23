# Start Chrome with MojoJS and the DevTools port. SECRPC is the magic
# chrome.runtime.sendMessage accepts. The callback is the reply.
$ErrorActionPreference = "Stop"
$addr = "127.0.0.1"
$port = 9333
$wasm = "C:\Users\grego\WASMVoodooCompile\build\mojojs.wasm"
$profile = "C:\Users\grego\Bruja\out\mojojs-chrome"
$chromeCandidates = @(
  "$env:ProgramFiles\Google\Chrome\Application\chrome.exe",
  "${env:ProgramFiles(x86)}\Google\Chrome\Application\chrome.exe",
  "$env:LOCALAPPDATA\Google\Chrome\Application\chrome.exe"
)
$chrome = $chromeCandidates | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
if (-not $chrome) { throw "chrome.exe not found" }
if (-not (Test-Path $wasm)) { throw "missing $wasm" }
New-Item -ItemType Directory -Force -Path $profile | Out-Null

$listening = $false
try {
  $client = New-Object System.Net.Sockets.TcpClient
  $client.Connect($addr, $port)
  $listening = $true
  $client.Close()
} catch {
  $listening = $false
}

$started = $null
if (-not $listening) {
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
    } catch {
      Start-Sleep -Milliseconds 200
    }
  }
  if (-not $ready) { throw "devtools port $port did not open" }
}

try {
  $list = Invoke-WebRequest -UseBasicParsing "http://127.0.0.1:${port}/json/list"
  $text = $list.Content
  $ws = ""
  $ext = [regex]::Match($text, '"url"\s*:\s*"chrome-extension:[^"]*"[\s\S]{0,800}?"webSocketDebuggerUrl"\s*:\s*"(ws://[^"]+)"')
  if ($ext.Success) { $ws = $ext.Groups[1].Value }
  if (-not $ws) {
    $page = [regex]::Match($text, '"webSocketDebuggerUrl"\s*:\s*"(ws://[^"]+/devtools/page/[^"]+)"')
    if ($page.Success) { $ws = $page.Groups[1].Value }
  }
  $ws = $ws -replace '\\/', '/'
  if (-not $ws) { throw "no page target" }

  $expr = @'
(async () => {
  const SECRPC = 91556947316803;
  const rows = ["secrpc=" + SECRPC, "api=chrome.runtime.sendMessage"];
  let id = "callobklhcbilhphinckomhgkigmfocg";
  let send = null;
  try {
    if (typeof chrome !== "undefined" && chrome.runtime && chrome.runtime.sendMessage) {
      send = chrome.runtime.sendMessage.bind(chrome.runtime);
      if (chrome.runtime.id) id = String(chrome.runtime.id);
    }
  } catch (e) {}
  rows.push("extension=" + id);
  if (!send) {
    rows.push("sendMessage-missing");
    return rows.join("\n");
  }
  const env = {
    magic: SECRPC,
    host: "*",
    origin_host: (location && location.host) ? location.host : "",
    method: "SECRPC",
    args: [],
    extension_id: id,
    channel: "runtime"
  };
  const reply = await new Promise((resolve) => {
    let done = false;
    const finish = (s) => { if (!done) { done = true; resolve(s); } };
    setTimeout(() => finish("timeout"), 1500);
    try {
      send(id, env, (h) => {
        let err = "";
        try {
          if (chrome.runtime.lastError) err = String(chrome.runtime.lastError.message || chrome.runtime.lastError);
        } catch (e) {}
        let body = "";
        try { body = h === undefined ? "undefined" : JSON.stringify(h); }
        catch (e) { body = "unstringifiable"; }
        finish("callback lastError=" + (err || "none") + " reply=" + body);
      });
    } catch (e) {
      finish("throw " + (e && e.message ? e.message : "error"));
    }
  });
  rows.push(reply);
  return rows.join("\n");
})()
'@
  $sock = [System.Net.WebSockets.ClientWebSocket]::new()
  $ct = [System.Threading.CancellationToken]::None
  $sock.ConnectAsync([Uri]$ws, $ct).GetAwaiter().GetResult()
  $utf8 = [System.Text.Encoding]::UTF8
  function Send-Cdp([int]$id, [string]$method, $params) {
    $msg = @{ id = $id; method = $method }
    if ($null -ne $params) { $msg.params = $params }
    $bytes = $utf8.GetBytes(($msg | ConvertTo-Json -Compress -Depth 8))
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
      if ($text -match ('"id"\s*:\s*' + $id + '\b')) { return $text }
      Write-Output ("event " + $text.Substring(0, [Math]::Min(500, $text.Length)))
    }
    throw "cdp id $id missing"
  }
  Send-Cdp 1 "Runtime.enable" $null
  [void](Read-Cdp 1)
  Send-Cdp 2 "Runtime.evaluate" @{ expression = $expr; awaitPromise = $true; returnByValue = $true; timeout = 8000 }
  $reply = Read-Cdp 2
  $sock.Dispose()
  Write-Output "capture"
  Write-Output $reply

  & "C:\Users\grego\go++\wasitime.bat" $wasm $ws
  exit $LASTEXITCODE
} finally {
  if ($started) {
    & taskkill.exe /PID $started.Id /T /F | Out-Null
  }
}
