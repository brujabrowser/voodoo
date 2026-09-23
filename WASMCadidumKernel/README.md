# WASMCadidumKernel

A real implementation of the public
[Mojo C++ System API](https://source.chromium.org/chromium/chromium/src/+/main:mojo/public/cpp/system/)
(`mojo::Handle`, `mojo::ScopedMessagePipeHandle`, `mojo::CreateMessagePipe`,
`mojo::SimpleWatcher`, …), built on top of [WASMThunker](../WASMThunker)'s
`wst` (the Mojo C ABI). This is the `mojo/public/cpp/system` rung of the
stack:

```
sockets  →  HolePunch  →  Mojo C System  →  Mojo C++ System (this repo)  →  Bindings
whp::net    whp::punch     WASMThunker (wst)   WASMCadidumKernel (wck)
```

Everything here calls straight into `wst`'s `Mojo*` functions -- it never
touches `whp::c` directly. Any unmodified upstream `mojo/public/cpp` bindings
code that expects the real `mojo::` namespace (`Remote<T>`/`Receiver<T>`
generated interfaces, message serialization, …) can link against `wck` and
run over a WASMHolePunch pipe.

## What's here

| Header | Provides |
|---|---|
| `handle.h` | `mojo::Handle`, `mojo::ScopedHandleBase<T>` |
| `message_pipe.h` | `mojo::MessagePipeHandle`, `mojo::CreateMessagePipe`, `mojo::WriteMessageRaw`/`ReadMessageRaw` |
| `data_pipe.h` | `mojo::DataPipeProducerHandle`/`ConsumerHandle`, `mojo::CreateDataPipe`, raw write/read (including two-phase begin/end) |
| `buffer.h` | `mojo::SharedBufferHandle`, `mojo::CreateSharedBuffer`, `mojo::ScopedSharedBufferMapping` |
| `invitation.h` | `mojo::OutgoingInvitation`/`IncomingInvitation` over `wst`'s in-process loopback registry (see caveat below) |
| `simple_watcher.h` | `mojo::SimpleWatcher` -- a trap-based, level-triggered watcher |

### `SimpleWatcher` dispatch

`whp`'s trap handler fires synchronously, possibly nested inside whatever
`Mojo*` call changed the watched handle's signals (flagged
`MOJO_TRAP_EVENT_FLAG_WITHIN_API_CALL`, exactly like real Mojo Core). So the
handler here never calls your `ReadyCallback` inline -- it posts through
`whp::Executor`, and your callback only runs once that queue is pumped
(`whp::Executor::Current().RunUntilIdle()` or `.Run()`). With
`ArmingPolicy::kAutomatic` the watcher re-arms itself right after each
callback returns; if you haven't drained the signal (e.g. read the message)
by then, it will refire on the next pump -- that's real level-triggered
behavior, not a bug, but it will spin a `RunUntilIdle()` loop forever if
nothing ever drains the handle.

### Invitations are still loopback-only

Same caveat as `wst`: `OutgoingInvitation::Send` / `IncomingInvitation::Accept`
go through an in-process registry keyed by a `LoopbackChannel` id, not a real
transport. Use `whp::platform::Invitation` directly for cross-process
delivery over punched UDP.

## JS bindings

Every layer above is also reachable from JS, running on the
[WASMv8bindings](../WASMv8bindings) V8 embedder-API facade (`Isolate`/
`Context`/`FunctionTemplate`, cppgc-backed, quickjs-ng execution through
[WASMSafeSpace](../WASMSafeSpace)). A sibling `WASMv8bindings` checkout is
required — building `wck_bindings` pulls it in via a guarded
`add_subdirectory`, which in turn `FetchContent`s quickjs-ng on first
configure. Same idiom as [WASMExtWrench](../WASMExtWrench)'s `wew_bindings`.

```
cmake -B build
cmake --build build --target wck_bindings_everything
ctest --test-dir build -R wck_bindings --output-on-failure
```

| JS namespace | Source | Tier |
|---|---|---|
| `mojo.createMessagePipe` / `writeMessageRaw` / `readMessageRaw` / `close` | `src/bindings/message_pipe_bindings.cc` | B1 |
| `mojo.createDataPipe` / `writeData` / `readData` / `close` | `src/bindings/data_pipe_bindings.cc` | B2 |

## Build (native, Windows)

MinGW g++ or MSVC. Requires the sibling `../WASMThunker` (which in turn
requires `../WASMHolePunch`) checkouts. JS bindings additionally need
`../WASMv8bindings` (and its `../WASMSafeSpace`).

```
cmake -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## WASI

```
cmake -B build-wasi -DCMAKE_TOOLCHAIN_FILE=cmake/wasi-sdk.cmake
cmake --build build-wasi
wasmtime --wasi sockets build-wasi/wck_tests.wasm
```

## License

New code is BSD-3-Clause. Chromium reference files remain Copyright The
Chromium Authors.
