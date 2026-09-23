# WASMThunker

A real implementation of the public
[Mojo C System API](https://source.chromium.org/chromium/chromium/src/+/main:mojo/public/c/system/)
(`Mojo*` — `MojoCreateMessagePipe`, `MojoWriteMessage`, `MojoCreateTrap`, …),
backed by [WASMHolePunch](../WASMHolePunch)'s `whp::c` layer instead of real
Mojo Core. This is the `mojo/public/c/system` rung of the stack:

```
sockets  →  HolePunch  →  Mojo C System (this repo)  →  … →  Bindings
whp::net    whp::punch     WASMThunker (wst)
```

Any code written against the real Mojo C ABI links against `wst` and talks to
a WASMHolePunch pipe instead of the Chromium tree. There is no
`MojoSystemThunks2` vtable here — Chromium's own `thunks.cc` dispatches
through an embedder-supplied function table so Mojo Core implementations are
swappable at runtime; WASMThunker *is* the one implementation, so every
`Mojo*` entry point calls straight into `whp::c`.

## What's wired vs. stubbed

| Area | Status |
|---|---|
| Message pipes, messages, traps, data pipes, shared buffers | Wired 1:1 to `whp::c` |
| `MojoGetTimeTicksNow` | Wired to `whp::TimeTicks::Now()` |
| `MojoNotifyBadMessage` | Logs to stderr, returns `MOJO_RESULT_OK` |
| `MojoCreateInvitation` / `Attach` / `Extract` / `Send` / `Accept` | Loopback when the transport handle is `TYPE_INVALID` (channel id). A live fd/SOCKET is adopted as a wasigocvm UDP socket and attached to `whp::platform::Invitation`. |
| Platform-handle wrap/unwrap | Wired: OS HANDLE (Windows), fd (elsewhere), Mach send-right token (`WASMYetiKernel` `MachSendRight` analog). Wrapped handles transit message pipes. |
| Shared-memory-region wrap/unwrap | Wired to `WhpWrapPlatformSharedMemoryRegion` / `Unwrap`. Windows/POSIX map the OS region; Mach tokens use in-process backing like Yeti `IPC::SharedMemory`. |
| Quotas (`SetQuota`/`QueryQuota`) | Wired: unread message count and size on a pipe end |
| Pipe fusion (`FuseMessagePipes`) | Wired: splices the peers of two endpoints together |
| Message serialize / reserve / context | Wired (`WhpSerializeMessage`, lazy context serializer) |
| `GetBufferInfo` | Wired |
| `SetDefaultProcessErrorHandler` | Wired (also invoked from `NotifyBadMessage`) |

### Invitations

`TYPE_INVALID` platform handles are the in-process channel-id convention
(unit tests, fuzzers). A `FILE_DESCRIPTOR` / `WINDOWS_HANDLE` is adopted
as a UDP socket and attached to HolePunch `whp::platform::Invitation`
(punched UDP + Mojo V3 framing). Same-process loopback still works; cross
process uses the HolePunch path, not a second Thunker transport.

## Public headers

`include/mojo/public/c/system/*.h` — our own authored reimplementation of
the well-known, stable public Mojo C ABI shape (not vendored Chromium
source), so unmodified upstream `mojo/public/cpp` code can `#include` and
link against them unchanged. Numeric result codes match `whp/c/types.h`
(and therefore real Mojo). `third_party/chromium/thunks.cc` is Chromium's
own reference file, kept **spec only** — not compiled.

## JS bindings

The Mojo C System API is also reachable from JS on the
[WASMv8bindings](../WASMv8bindings) V8 embedder-API facade (`Isolate`/
`Context`/`FunctionTemplate`, quickjs-ng execution through
[WASMSafeSpace](../WASMSafeSpace)). Handles are plain JS numbers (no
cppgc). Namespace is `wst` to distinguish from CadidumKernel's `mojo`
C++ bindings. A sibling `WASMv8bindings` checkout is required — building
`wst_bindings` pulls it in via a guarded `add_subdirectory`.

```
cmake -B build
cmake --build build --target wst_bindings_everything
ctest --test-dir build -R wst_bindings --output-on-failure
```

| JS namespace | Source | Tier |
|---|---|---|
| `wst.createMessagePipe` / `writeMessage` / `readMessage` / `close` | `src/bindings/message_pipe_bindings.cc` | B1 |

## Build (native, Windows)

MinGW g++ or MSVC. Requires the sibling `../WASMHolePunch` checkout.
JS bindings additionally need `../WASMv8bindings` (and its
`../WASMSafeSpace`).

```
cmake -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## WASI

When wasi-sdk is installed:

```
cmake -B build-wasi -DCMAKE_TOOLCHAIN_FILE=cmake/wasi-sdk.cmake
cmake --build build-wasi
wasmtime --wasi sockets build-wasi/wst_tests.wasm
```

## License

New code is BSD-3-Clause. Chromium reference files remain Copyright The
Chromium Authors.
