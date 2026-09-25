# Run blink.mojom.LocalFrame.JavaScriptExecuteRequest through DevTools,
# then load local_frame_exec.wasm in that page.
$ErrorActionPreference = "Stop"
$addr = "127.0.0.1"
$port = 9358
$profile = "C:\Users\grego\Bruja\out\local-frame-exec-profile"
$pageUrl = "about:blank"
$chromeCandidates = @(
  "$env:ProgramFiles\Google\Chrome\Application\chrome.exe",
  "${env:ProgramFiles(x86)}\Google\Chrome\Application\chrome.exe",
  "$env:LOCALAPPDATA\Google\Chrome\Application\chrome.exe"
)
$chrome = $chromeCandidates | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
if (-not $chrome) { throw "chrome.exe not found" }
New-Item -ItemType Directory -Force -Path $profile | Out-Null
$started = Start-Process -FilePath $chrome -PassThru -ArgumentList @(
  "--remote-debugging-port=$port", "--window-size=800,600", "--remote-allow-origins=*",
  "--user-data-dir=$profile", "--no-first-run", "--no-default-browser-check", $pageUrl)
$ready = $false
for ($i = 0; $i -lt 50; $i++) {
  try { $c = New-Object System.Net.Sockets.TcpClient; $c.Connect($addr, $port); $c.Close(); $ready = $true; break }
  catch { Start-Sleep -Milliseconds 200 }
}
if (-not $ready) { throw "devtools port $port did not open" }
try {
  $list = Invoke-WebRequest -UseBasicParsing "http://${addr}:${port}/json/list"
  $targets = [regex]::Matches($list.Content, '\{[^{}]*"type"\s*:\s*"page"[^{}]*\}')
  $ws = $null
  foreach ($t in $targets) {
    if ($t.Value -match 'about:blank' -and $t.Value -match '"webSocketDebuggerUrl"\s*:\s*"(ws://[^"]+)"') {
      $ws = $Matches[1] -replace '\\/', '/'
      break
    }
  }
  if (-not $ws) {
    $page = [regex]::Match($list.Content, '"webSocketDebuggerUrl"\s*:\s*"(ws://[^"]+/devtools/page/[^"]+)"')
    if (-not $page.Success) { throw "no page target" }
    $ws = $page.Groups[1].Value -replace '\\/', '/'
  }
  $script:sock = [System.Net.WebSockets.ClientWebSocket]::new()
  $ct = [System.Threading.CancellationToken]::None
  $script:sock.ConnectAsync([Uri]$ws, $ct).GetAwaiter().GetResult() | Out-Null
  $utf8 = [System.Text.Encoding]::UTF8
  $script:nextId = 1
  function Send-Cdp([string]$method, $params) {
    $id = $script:nextId; $script:nextId = $id + 1
    $msg = @{ id = $id; method = $method }
    if ($null -ne $params) { $msg.params = $params }
    $bytes = $utf8.GetBytes(($msg | ConvertTo-Json -Compress -Depth 8))
    $script:sock.SendAsync([ArraySegment[byte]]::new($bytes), [System.Net.WebSockets.WebSocketMessageType]::Text, $true, $ct).GetAwaiter().GetResult() | Out-Null
    return $id
  }
  function Read-Cdp([int]$id) {
    $buf = New-Object byte[] 1048576
    for ($n = 0; $n -lt 200; $n++) {
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
  [void](Read-Cdp (Send-Cdp "Page.enable" $null))
  [void](Read-Cdp (Send-Cdp "Page.navigate" @{ url = $pageUrl }))
  Start-Sleep -Milliseconds 800
  $main = Read-Cdp (Send-Cdp "Runtime.evaluate" @{ expression = "1+1"; returnByValue = $true })
  Write-Output ("JavaScriptExecuteRequest " + $main)
  $tree = Read-Cdp (Send-Cdp "Page.getFrameTree" $null)
  $frame = [regex]::Match($tree, '"id"\s*:\s*"([^"]+)"')
  if (-not $frame.Success) { throw "no frame id" }
  $world = Read-Cdp (Send-Cdp "Page.createIsolatedWorld" @{ frameId = $frame.Groups[1].Value; worldName = "bruja"; grantUniveralAccess = $false })
  Write-Output ("isolatedWorld " + $world)
  $ctx = [regex]::Match($world, '"executionContextId"\s*:\s*(\d+)')
  if ($ctx.Success) {
    $iso = Read-Cdp (Send-Cdp "Runtime.evaluate" @{ expression = "1+1"; contextId = [int]$ctx.Groups[1].Value; returnByValue = $true })
    Write-Output ("JavaScriptExecuteRequestInIsolatedWorld " + $iso)
  }
  $made = Read-Cdp (Send-Cdp "Runtime.evaluate" @{ expression = "globalThis.bruja = { add: function(a, b) { return a + b; } }; bruja"; returnByValue = $false })
  $objectId = [regex]::Match($made, '"objectId"\s*:\s*"([^"]+)"').Groups[1].Value
  $called = Read-Cdp (Send-Cdp "Runtime.callFunctionOn" @{
    objectId = $objectId
    functionDeclaration = "function(a, b) { return this.add(a, b); }"
    arguments = @(@{ value = 1 }, @{ value = 1 })
    returnByValue = $true
  })
  Write-Output ("JavaScriptMethodExecuteRequest " + $called)
  $tests = Read-Cdp (Send-Cdp "Runtime.evaluate" @{
    expression = "Promise.resolve(1+1)"
    userGesture = $true
    awaitPromise = $true
    returnByValue = $true
  })
  Write-Output ("JavaScriptExecuteRequestForTests " + $tests)
  if ($script:sock) { $script:sock.Dispose() }
} finally {
  if ($started) { & taskkill.exe /PID $started.Id /T /F | Out-Null }
}
