# See whether V8 has installed Mojo on a page, an extension frame, and a WebUI page.
$ErrorActionPreference = "Stop"
$addr = "127.0.0.1"
$port = 9361
$profile = "C:\Users\grego\Bruja\out\mojo-global-profile"
$expr = "(function(){return location.href+' Mojo='+(typeof Mojo)+' mojo='+(typeof mojo)+' bind='+(typeof Mojo!=='undefined'&&Mojo.bindInterface?'yes':'no');})()"
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
  "--user-data-dir=$profile", "--no-first-run", "--no-default-browser-check", "about:blank")
$ready = $false
for ($i = 0; $i -lt 50; $i++) {
  try { $c = New-Object System.Net.Sockets.TcpClient; $c.Connect($addr, $port); $c.Close(); $ready = $true; break }
  catch { Start-Sleep -Milliseconds 200 }
}
if (-not $ready) { throw "devtools port $port did not open" }
try {
  $list = Invoke-WebRequest -UseBasicParsing "http://${addr}:${port}/json/list"
  $page = [regex]::Match($list.Content, '"webSocketDebuggerUrl"\s*:\s*"(ws://[^"]+/devtools/page/[^"]+)"')
  if (-not $page.Success) { throw "no page target" }
  $script:sock = [System.Net.WebSockets.ClientWebSocket]::new()
  $ct = [System.Threading.CancellationToken]::None
  $script:sock.ConnectAsync([Uri]($page.Groups[1].Value -replace '\\/','/'), $ct).GetAwaiter().GetResult() | Out-Null
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
    $buf = New-Object byte[] 262144
    for ($n = 0; $n -lt 40; $n++) {
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
  foreach ($url in @("about:blank", "chrome-extension://nkeimhogjdpnpccoofpliimaahmaaome/background.html", "chrome://gpu")) {
    [void](Read-Cdp (Send-Cdp "Page.navigate" @{ url = $url }))
    Start-Sleep -Milliseconds 700
    $raw = Read-Cdp (Send-Cdp "Runtime.evaluate" @{ expression = $expr; returnByValue = $true })
    $m = [regex]::Match($raw, '"value"\s*:\s*"((?:\\.|[^"\\])*)"')
    if ($m.Success) { Write-Output ([regex]::Unescape($m.Groups[1].Value)) } else { Write-Output $raw }
  }
  $script:sock.Dispose()
} finally {
  if ($started) { & taskkill.exe /PID $started.Id /T /F | Out-Null }
}
