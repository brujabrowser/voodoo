# WASMHolePunch

A library that speaks [Chromium Mojo](https://github.com/chromium/chromium/blob/main/mojo/public/cpp/bindings)
**without the Chromium tree**. Every layer is a public API: sockets, HolePunch,
the Mojo C System API, and C++ Bindings (`Remote` / `Receiver`). WASM
sockets are go++ **wasigocvm** sysroot POSIX sockets (`tcp_socket.cc` /
`udp_socket.cc`), never Bytecode Alliance `wasi:sockets` / wasmtime host
sockets.

```
sockets  →  HolePunch  →  Mojo C System + invitations  →  C++ System  →  Bindings
whp::net    whp::punch     whp::c / whp::platform          whp::system    whp::
```

You can stop at any layer. Bindings never hide the pipes or the sockets.

The five Chromium sources under `third_party/chromium/` are **spec only** — they
are not compiled.

## JS bindings

The Whp C System API is also reachable from JS on the
[WASMv8bindings](../WASMv8bindings) V8 embedder-API facade (`Isolate`/
`Context`/`FunctionTemplate`, quickjs-ng execution through
[WASMSafeSpace](../WASMSafeSpace)). Handles are plain JS numbers (no
cppgc). Namespace is `whp`. A sibling `WASMv8bindings` checkout is required —
building `whp_bindings` pulls it in via a guarded `add_subdirectory`.

```
cmake -B build
cmake --build build --target whp_bindings_everything
ctest --test-dir build -R whp_bindings --output-on-failure
```

| JS namespace | Source | Tier |
|---|---|---|
| `whp.init` / `shutdown` / `createMessagePipe` / `writeMessage` / `readMessage` / `createDataPipe` / `writeData` / `readData` / `close` | `src/js_bindings/system_bindings.cc` | B1 |

## Build (native, Windows)

MinGW g++ or MSVC. Inner-loop tests do not need wasmtime.

```
cmake -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## WASM (wasigocvm)

WASM is go++ wasigocvm (`~/go++`, `WASIGO_GOCVM`). Tcp/UdpSocket compile
the same POSIX sources as native against the wasigocvm sysroot
(`wasigocvm_net.hpp`). Do not use Bytecode Alliance wasi-sdk socket stubs
or `wasmtime --wasi sockets`.

## Public headers

| Layer | Headers |
|---|---|
| Sockets | `include/whp/net/` |
| HolePunch | `include/whp/punch/` |
| Base | `include/whp/base/` (`OnceCallback`, `expected`, `TimeTicks`, `Executor`) |
| C System API | `include/whp/c/` |
| Invitations | `include/whp/platform/` |
| C++ System | `include/whp/system/` |
| Bindings | `include/whp/message.h`, `message_header_validator.h`, … |

`WhpCreateDataPipe` / `WhpCreateSharedBuffer` work in-process today.
`WhpWrapPlatformHandle` / `WhpUnwrapPlatformHandle` wrap an OS HANDLE (Windows),
fd (elsewhere), or Mach send-right token (`WASMYetiKernel` `MachSendRight`
analog). `WhpWrapPlatformSharedMemoryRegion` wraps those primitives as a
shared-buffer handle (OS mapping, or in-process backing for Mach tokens).

`whp::platform::Invitation` is the broker: punch a UDP path, `InviteOver`, then
`Fire(ordinal)` writes a Mojo V3 frame (`header.name` = Cadmium magic-key
ordinal) on the primordial pipe. Control `Run` (QueryVersion) auto-replies.

`FireToken` fires Mojo ordinals and SRPC magics (`SECRPC` / `VOODOO`). It
does not reject SRPC.

HolePunch is its own class. Cadmium stays unchanged. The bridge is
`whp_cadmium`:

```
# offerer
whp_cadmium --bind 127.0.0.1:0 --offerer --cadmium http://127.0.0.1:8879 --sid <sid>
# answerer (use the printed candidate as --peer)
whp_cadmium --bind 127.0.0.1:0 --peer 127.0.0.1:<port> --cadmium http://127.0.0.1:8879 --sid <sid>
```

It polls Cadmium `GET /api/rbi/ipc?hook=1` and maps `{ordinal,args}` onto
`Invitation::Fire`. Cadmium still queues through the same IPC envelope.

## License

New code is BSD-3-Clause. Chromium reference files remain Copyright The
Chromium Authors.
