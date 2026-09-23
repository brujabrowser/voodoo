# Bruja

Chromium-free Chromium. Occupancy on **go++ / wasigocvm**. Host IDL:
sibling **WASMVoodooCompile** `voodoomc` and **WASMBruja** `brujac`.

Frame engine is **WASMBlinker** `LocalFrameImpl` (parse / script / layout /
paint via **WASMSkia** + **WASMRasta**). **WASMRenderer** is the CreateFrame
include hop (`pending_remote<blink.LocalFrame>`). **WASMLime** is Context /
Frame / NavigationController. Not Loki. Not a leftover host Chrome.

```
published chromium/src .mojom     →  voodoomc intern
WASMBlinker local_frame.voodoom   →  interface:blink.LocalFrame
WASMRenderer renderer.voodoom     →  interface:content.Renderer  (CreateFrame)
WASMLime frame/navigation         →  interface:lime.Frame
Web IDL .bruja                    →  brujac --backend=goxx
Go++ occupancy                    →  compile.bat → wasitime
```

## Parity

End to end in this directory. Published `.mojom` in, a call out. `mojovm.Call(api, arg)` is the same two-string shape as `gocvm.Call(api, arg)`.

1. `tools/fetch-chromium-mojom.ps1` checks out **1860** `.mojom` files into `third_party/chromium-src`.
2. Sibling `voodoomc` compiles each file, import closure included, into `out/voodoo`. That run wrote **1860** entry headers and **1131** import `*_gen.h` headers (**2991**), **0** failures.
3. Sibling `mojovm` loads the `.mojom` for the call. The corpus stays in this tree. It is not linked into gocvm.

```
..\WASMVoodooCompile\build\voodoomc.exe third_party\chromium-src\extensions\common\mojom\message_port.mojom -o out\voodoo\message_port.h --import-dir=third_party\chromium-src --out-dir=out\voodoo
..\WASMVoodooCompile\build\mojovm.exe call extensions.mojom.MessagePort.DispatchDisconnect
..\WASMVoodooCompile\build\mojovm.exe portfolio --out out\mojovm-portfolio.txt
```

`api` is `module.Interface.Method`. `arg` is `0x1F`-separated fields. The reply is `ok`, then `0x1F`, then the ordinal.

The ordinal is the FNV-1a of the fully qualified name, the ABI key `voodoomc` already interns: `func module.Interface.Method(params)` and, when the method has a response, `->(response)`. Offset `2166136261`, prime `16777619` (Go++ `hash/fnv` `New32a`). The 32-bit result is masked with `0x7fffffff`, the `uint32` width `project_lovelace` `ExtractMojo` stores (`1` .. `kMaxMojoOrdinal`). The hashed string stays the full name.

`extensions/common/mojom/message_port.mojom` is both interfaces:

| name | voodoo | mojo |
| --- | --- | --- |
| `extensions.mojom.MessagePort.DispatchDisconnect` | 1481416133 | 2592569 |
| `extensions.mojom.MessagePort.DeliverMessage` | 703090134 | 1122766509 |
| `extensions.mojom.MessagePortHost.ClosePort` | 2146739443 | 568318427 |
| `extensions.mojom.MessagePortHost.PostMessage` | 1165969513 | 1529189607 |
| `extensions.mojom.MessagePortHost.ResponsePending` | 1981901972 | 1848577363 |

None of those pairs are equal. Chrome does not hash the fully qualified name. An official desktop build runs `ScrambleMethodOrdinals`: `sha256(//chrome/VERSION ‖ interface name ‖ 1-based index)`, first 4 bytes little-endian, `& 0x7fffffff`. The salt here is `chrome/VERSION` on main, `MAJOR=156 MINOR=0 BUILD=8070 PATCH=0`. The interface name is the short one (`MessagePort`), and the index counts methods that have no explicit `@N`. A build with scrambling off uses the declaration index instead (`0`, `1`, `2`).

`Call` returns `ok`, then the voodoo ordinal, then the Chrome ordinal. The voodoo number is unexpected routing: Chrome has no method at that id. The Chrome number is expected routing: it is the ordinal an official build puts on the wire. The fully qualified name stays on the portfolio row either way.

`out/mojovm-portfolio.txt` is one row per method: call name, voodoo ordinal, Chrome ordinal, fully qualified name, path under `third_party/chromium-src`. The portfolio run is **9062** methods, **1860** files, **0** failures.

That loop is parity. Every method in this checkout is a call.

### Why this locks voodoo to go++

`voodoomc` already names a method by its fully qualified name. That string is the ABI. `MethodKey` in WASMVoodooCompile `src/type_intern.cc` is:

```
func module.Interface.Method(param types)
func module.Interface.Method(param types)->(response types)
```

`DispatchDisconnect(string)` and `DispatchDisconnect(string?)` are different strings. A signature change changes the name. The name is what a Go occupancy already prints (`func ns.Name.M(...)->(...)` in the voodoo README).

go++ already owns the hash. `stdlib/hash/fnv` `New32a` is FNV-1a: offset `2166136261`, prime `16777619`, XOR the byte, then multiply. `mojovm` uses that pair on the fully qualified name and masks with `0x7fffffff`, the same `uint32` width `ExtractMojo` pulls out of `sendMessage(ordinal, ...)`. voodoo does not pick a private hash. go++ does not invent the method string.

The ordinal is that fold. It exists only while both sides stay put. An edit to `MethodKey` or to `offset32` / `prime32` renumbers every row in `out/mojovm-portfolio.txt`. That is the connection. The two trees now share one ABI number, computed, not stored in a table either side can drift.

`mojovm.Call(api, arg)` matches `gocvm.Call(api, arg)`: two strings, fields separated by `0x1F`, reply `ok` then the ordinal. A Go program names `extensions.mojom.MessagePort.DeliverMessage` the way it names any other API. The 9062 methods stay in this tree and are parsed on the call. They are not compiled into the gocvm link.

The guest compile is go++ `compile.bat`, which calls `wasigocvm.bat`. That driver routed its own clang++:

```
C:\Users\grego\go++\toolchain\bin\wasm32-wasip2-clang++.exe
```

That binary is the go++ toolchain. `~/wasi-sdk` is not the compiler on this route. `examples/occupancy/ping/ping.go` linked to `WASMVoodooCompile\build\ping.wasm` and `wasitime` printed the fully qualified name and `1`:

```
interface:mojotool.PingTool
func mojotool.PingTool.Ping(int32)->(int32)
1
```

A row carries both numbers. `out/voodoo` headers still emit `kMethodName` as the mojom declaration ordinal (`@N`, otherwise the declaration index). The voodoo column is the FNV of the fully qualified name. The Chrome column is `ScrambleMethodOrdinals` for `chrome/VERSION` `156.0.8070.0`.

## Chromium mojom corpus

Every `.mojom` comes from published source:

```
powershell -File tools/fetch-chromium-mojom.ps1
```

Blobless sparse-checkout of `**/*.mojom` into `third_party/chromium-src/`.
Fallback remote: `https://github.com/chromium/chromium.git`.

## Occupancy (go++ wasigocvm)

```
powershell -File tools/occupy.ps1
```

Host-compiles sibling Blinker / Renderer / Lime IDL plus published
`time.mojom` / `battery_monitor.mojom`, `brujac` on console/navigator,
then:

```
..\go++\compile.bat occupancy\bruja.go -o build\bruja.wasm
..\go++\wasitime.bat build\bruja.wasm
```

`about:bruja` occupies UserAgent / title intern. Real HTML Navigate /
LoadHTML / Eval / paint stay on `LocalFrameImpl` (C ABI
`BlinkerLoadHTML` / Lime `LoadUrl`). Occupancy does not rewrite that
engine.

## Occupancy source (published mojom)

voodoomc writes occupancy C++ from every `third_party/chromium-src/**/*.mojom`:

```
powershell -File tools/gen-mojom-source.ps1
```

## Windowed occupancy

`src/browser.cc` binds sibling **WASMRenderer** `RendererImpl.CreateFrame`
to **WASMBlinker** `LocalFrameImpl` (generated `blink.LocalFrame` remote)
and paints with **WASMLime** `FrameWindow`. Not WASMv16 `bruja_browser`.

```
powershell -File tools/run-browser.ps1
powershell -File tools/run-browser.ps1 http://example.com/
```

## License

Bruja code: BSD-3-Clause. Chromium `.mojom` files keep the Chromium
Authors license in-tree.
