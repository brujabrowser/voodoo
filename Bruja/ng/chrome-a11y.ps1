# Open each chrome:// URL from the mojom corpus and read the accessibility root.
# chrome://kill and chrome://gpucrash are skipped; they terminate the browser.
$ErrorActionPreference = "Stop"
$addr = "127.0.0.1"
$port = 9355
$profile = "C:\Users\grego\Bruja\out\chrome-a11y-profile"
$outTsv = Join-Path $PSScriptRoot "..\out\chrome-a11y.tsv"
$urls = @(
  "chrome://apps",
  "chrome://app-service-internals",
  "chrome://autofill-ml-internals",
  "chrome://batch-upload",
  "chrome://bluetooth-internals",
  "chrome://browser-actuator-internals",
  "chrome://cloud-upload",
  "chrome://contextual-cueing-internals",
  "chrome://discards",
  "chrome://discards/graph",
  "chrome://dlp-internals",
  "chrome://downloads",
  "chrome://drive-picker-host",
  "chrome://enterprise-reporting",
  "chrome://extensions",
  "chrome://favicon2",
  "chrome://flag",
  "chrome://flags#arc-extend-input-anr-timeout",
  "chrome://flags#arc-extend-intent-anr-timeout",
  "chrome://flags#arc-extend-service-anr-timeout",
  "chrome://flags#arc-friendlier-error-dialog",
  "chrome://flags#arc-ignore-hover-event-anr",
  "chrome://flags#arc-resize-compat",
  "chrome://flags#arc-rounded-window-compat",
  "chrome://flags#arc-touchscreen-emulation",
  "chrome://flags#arc-trackpad-scroll-touchscreen-emulation",
  "chrome://flags#arc-xdg-mode",
  "chrome://flags#enable-pip-double-tap-to-resize",
  "chrome://flags#jelly-colors",
  "chrome://flags#qs-revamp",
  "chrome://flags#render-arc-notifications-by-chrome",
  "chrome://flags#rounded-windows",
  "chrome://glic",
  "chrome://glic/internals",
  "chrome://gpu",
  "chrome://growth-internals/",
  "chrome://help-app",
  "chrome://help-app/help/sub/3399763/id/1282338#install-user",
  "chrome://histograms",
  "chrome://history",
  "chrome://history/journeys",
  "chrome://history-sync-optin",
  "chrome://indexeddb-internals",
  "chrome://indigo-internals",
  "chrome://launcher-internals",
  "chrome://location-internals",
  "chrome://mall",
  "chrome://management",
  "chrome://manage-mirrorsync",
  "chrome://media-app",
  "chrome://media-router-internals",
  "chrome://multidevice-setup",
  "chrome://nearby",
  "chrome://net-internals",
  "chrome://network",
  "chrome://newtab",
  "chrome://new-tab-page",
  "chrome://omnibox",
  "chrome://omnibox/ml",
  "chrome://on-device-internals",
  "chrome://oobe",
  "chrome://os-feedback",
  "chrome://os-settings",
  "chrome://os-settings/networks?type=WiFi",
  "chrome://page-action-internals",
  "chrome://password-manager",
  "chrome://personalization",
  "chrome://policy",
  "chrome://policy/logs",
  "chrome://print",
  "chrome://print-management",
  "chrome://process-internals",
  "chrome://proximity-auth",
  "chrome://quota-internals",
  "chrome://recorder-app",
  "chrome://sandbox",
  "chrome://scanning",
  "chrome://sensor-info",
  "chrome://settings",
  "chrome://settings/clearBrowsingData",
  "chrome://settings/security",
  "chrome://signout-confirmation",
  "chrome://skills",
  "chrome://skills/browse",
  "chrome://skills/browse-skills",
  "chrome://skills/dialog",
  "chrome://subresource-filter-internals",
  "chrome://test-removable-storage-writer",
  "chrome://theme/IDR_LOGIN_DEFAULT_X",
  "chrome://tracing",
  "chrome://unexportable-keys-internals",
  "chrome://updater",
  "chrome://user-education-internals",
  "chrome://web-app-internals",
  "chrome://webxr-internals"
)
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
  "--remote-debugging-port=$port",
  "--window-size=800,600",
  "--remote-allow-origins=*",
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
  $list = Invoke-WebRequest -UseBasicParsing "http://${addr}:${port}/json/list"
  $page = [regex]::Match($list.Content, '"webSocketDebuggerUrl"\s*:\s*"(ws://[^"]+/devtools/page/[^"]+)"')
  if (-not $page.Success) { throw "no page target" }
  $ws = $page.Groups[1].Value -replace '\\/', '/'
  $script:sock = [System.Net.WebSockets.ClientWebSocket]::new()
  $ct = [System.Threading.CancellationToken]::None
  $script:sock.ConnectAsync([Uri]$ws, $ct).GetAwaiter().GetResult() | Out-Null
  $utf8 = [System.Text.Encoding]::UTF8
  $script:nextId = 1
  function Send-Cdp([string]$method, $params) {
    $id = $script:nextId
    $script:nextId = $id + 1
    $msg = @{ id = $id; method = $method }
    if ($null -ne $params) { $msg.params = $params }
    $bytes = $utf8.GetBytes(($msg | ConvertTo-Json -Compress -Depth 6))
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
  [void](Read-Cdp (Send-Cdp "Accessibility.enable" $null))
  $rows = New-Object System.Collections.Generic.List[string]
  $rows.Add("url`taccess`trole`tname`tnodes")
  foreach ($url in $urls) {
    try {
    [void](Read-Cdp (Send-Cdp "Page.navigate" @{ url = $url }))
    Start-Sleep -Milliseconds 700
    $access = "ok"
    $role = ""
    $name = ""
    $nodes = 0
    try {
      $probe = Read-Cdp (Send-Cdp "Runtime.evaluate" @{ expression = "location.href + '\n' + document.title"; returnByValue = $true })
      $href = ""
      $pm = [regex]::Match($probe, '"value"\s*:\s*"((?:\\.|[^"\\])*)"')
      if ($pm.Success) {
        $got = [regex]::Unescape($pm.Groups[1].Value)
        $href = ($got -split "`n", 2)[0]
        if ($href -notlike "chrome://*") { $access = "landed " + $href }
      }
      $ax = Read-Cdp (Send-Cdp "Accessibility.getFullAXTree" @{ depth = 2 })
      $nodes = ([regex]::Matches($ax, '"nodeId"')).Count
      if ($ax -match '"value"\s*:\s*"RootWebArea"') { $role = "RootWebArea" }
      $nameM = [regex]::Match($ax, '"name"\s*:\s*\{\s*"type"\s*:\s*"[^"]*"\s*,\s*"value"\s*:\s*"((?:\\.|[^"\\])*)"')
      if ($nameM.Success) { $name = [regex]::Unescape($nameM.Groups[1].Value) -replace "`t", " " }
      if ($nodes -eq 0 -and $access -eq "ok") { $access = "no-tree" }
    } catch {
      if ($access -eq "ok") { $access = "ax-failed" }
    }
    $line = $url + "`t" + $access + "`t" + $role + "`t" + $name + "`t" + $nodes
    $rows.Add($line)
    Write-Output $line
    } catch {
      $line = $url + "`tcrashed`t`t`t0"
      $rows.Add($line)
      Write-Output $line
      try { if ($started) { & taskkill.exe /PID $started.Id /T /F | Out-Null } } catch {}
      Start-Sleep -Milliseconds 400
      $started = Start-Process -FilePath $chrome -PassThru -ArgumentList @(
        "--remote-debugging-port=$port", "--window-size=800,600", "--remote-allow-origins=*",
        "--user-data-dir=$profile", "--no-first-run", "--no-default-browser-check", "about:blank")
      $back = $false
      for ($i = 0; $i -lt 50; $i++) {
        try { $c = New-Object System.Net.Sockets.TcpClient; $c.Connect($addr, $port); $c.Close(); $back = $true; break } catch { Start-Sleep -Milliseconds 200 }
      }
      if ($back) {
        $list2 = Invoke-WebRequest -UseBasicParsing "http://${addr}:${port}/json/list"
        $page2 = [regex]::Match($list2.Content, '"webSocketDebuggerUrl"\s*:\s*"(ws://[^"]+/devtools/page/[^"]+)"')
        if ($page2.Success) {
          $script:sock = [System.Net.WebSockets.ClientWebSocket]::new()
          $script:sock.ConnectAsync([Uri]($page2.Groups[1].Value -replace '\\/','/'), $ct).GetAwaiter().GetResult() | Out-Null
          $script:nextId = 1
          [void](Read-Cdp (Send-Cdp "Page.enable" $null))
          [void](Read-Cdp (Send-Cdp "Accessibility.enable" $null))
        }
      }
    }
  }
  $rows.Add("chrome://kill`tskipped`t`t`t0")
  $rows.Add("chrome://gpucrash`tskipped`t`t`t0")
  [System.IO.File]::WriteAllLines($outTsv, $rows)
  Write-Output ("wrote " + ($rows.Count - 1) + " " + $outTsv)
  if ($script:sock) { $script:sock.Dispose() }
} finally {
  if ($started) { & taskkill.exe /PID $started.Id /T /F | Out-Null }
}
