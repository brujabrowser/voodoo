# WASMCadidumBindings

A real implementation of the public
[Mojo C++ Bindings API](https://source.chromium.org/chromium/chromium/src/+/main:mojo/public/cpp/bindings/)
(`mojo::Message`, `mojo::Connector`, `mojo::Remote<T>`/`mojo::Receiver<T>`,
`mojo::AssociatedRemote<T>`/`mojo::AssociatedReceiver<T>`, …), built on top
of [WASMCadidumKernel](../WASMCadidumKernel)'s `wck` (the Mojo C++ System
API). This is the `mojo/public/cpp/bindings` rung of the stack:

```
sockets  →  HolePunch  →  Mojo C System  →  Mojo C++ System  →  Bindings (this repo)
whp::net    whp::punch     WASMThunker        WASMCadidumKernel   WASMCadidumBindings
            (wst)          (wck)              (wcb)
```

Everything here is built on `wck`'s `mojo::CreateMessagePipe` /
`mojo::WriteMessageRaw` / `mojo::ReadMessageRaw` / `mojo::SimpleWatcher` --
it never touches `wst`'s C ABI directly. A future `.voodoom` IDL compiler
(**WASMVoodooCompile**) targets this runtime the way `mojom_bindgen`
targets real Mojo's bindings: it only needs to emit the `Proxy_`/`Stub_`
pair described below, not a new runtime.

## What's here

| Header | Provides |
|---|---|
| `lib/message_internal.h` | `StructHeader`, `ArrayHeader`, `Pointer<T>`, `MessageHeader`/`V1`/`V2` -- byte-for-byte the real Mojo wire structs |
| `message.h` | `mojo::Message`, `mojo::MessageReceiver` |
| `interface_id.h` | `InterfaceId`, `kPrimaryInterfaceId`, `kInterfaceIdNamespaceMask` |
| `connector.h` | `mojo::Connector` -- one physical pipe, in and out, plus `SyncWaitFor(RepeatingCallback<bool()>)` |
| `report_bad_message.h` | `ReportBadMessage` / `GetBadMessageCallback` -- Stub_ rejects a malformed call (NotifyBadMessage + RaiseError) |
| `self_owned_receiver.h` | `SelfOwnedReceiver<T>` -- impl lifetime tied to the pipe |
| `unique_receiver_set.h` | `UniqueReceiverSet<T>` -- owns N impl+Receiver pairs; peer disconnect drops the entry |
| `receiver_set.h` / `remote_set.h` | Many receivers on one impl / many remotes; disconnect drops the entry |
| `unique_associated_receiver_set.h` | Same as UniqueReceiverSet for associated endpoints |
| `shared_remote.h` | Copyable `SharedRemote<T>` sharing one bound Remote |
| `generic_pending_receiver.h` | Type-erased pending receiver; `As<T>()` matches generated `intern_key()` |
| `associated_group.h` | `mojo::ScopedInterfaceEndpointHandle`, `mojo::AssociatedGroup` |
| `lib/multiplex_router.h` | `mojo::MultiplexRouter` -- fans one pipe out to a primary + N associated interfaces; also owns the router-level pipe-control-message protocol (`SetPeerClosedHandler`/`NotifyPeerEndpointClosed`) that tells a peer when an associated endpoint closed |
| `lib/response_dispatcher.h` | `mojo::internal::ResponseDispatcher` -- matches responses to pending calls by request id; handlers are `base::OnceCallback` (HolePunch `whp::OnceCallback` re-exported from `base/callback.h`) |
| `base/callback.h` | `base::OnceCallback` / `RepeatingCallback` / `BindOnce` -- Bindings is the `base::` rung; source of truth is HolePunch |
| `base/expected.h` | `base::expected` / `unexpected` -- same; what generated `result<T, E>` methods take |
| `base/time.h` | `base::TimeTicks` / `TimeDelta` -- same |
| `pending_remote.h` / `pending_receiver.h` | `mojo::PendingRemote<T>`, `mojo::PendingReceiver<T>` |
| `remote.h` / `receiver.h` | `mojo::Remote<T>`, `mojo::Receiver<T>`. Bound remotes and receivers are named on CHPT; disconnect handlers are `base::OnceClosure` (HolePunch `OnceCallback`). |
| `pending_associated_remote.h` / `pending_associated_receiver.h` | `mojo::PendingAssociatedRemote<T>`, `mojo::PendingAssociatedReceiver<T>` |
| `associated_remote.h` / `associated_receiver.h` | `mojo::AssociatedRemote<T>`, `mojo::AssociatedReceiver<T>` |
| `lib/interface_control_messages.h` | `mojo::internal::kRunMessageId`/`kRunOrClosePipeMessageId` (reserved ordinals) and `HandleQueryVersionMessage`/`HandleRequireVersionMessage` -- per-interface version control, matching real Mojo's `interface_control_messages.mojom` (`Run_`/`RunOrClosePipe_`) |
| `lib/type_intern.h` | Object Type Identifier intern: `type_key` → cage `InternedTypeRec` named on WASMSafeSpace TPT; CHPT (WASMv8Bindings) names C++ objects with the interned tag; `Get` fails on a tag mismatch |

## Generated-code contract

[WASMVoodooCompile](../WASMVoodooCompile) emits `Proxy_`/`Stub_` for this
runtime. `examples/echo/echo_interface.h` is the hand-written contract for:

```
interface EchoListener {
  OnEcho(string value);
};
interface Echo {
  EchoString(string in) => (string out);
  SetListener(pending_associated_remote<EchoListener> listener);
};
```

Any interface usable with `Remote<T>`/`Receiver<T>`/`AssociatedRemote<T>`/
`AssociatedReceiver<T>` needs exactly two nested types:

```cpp
class Interface {
  // ...pure virtual methods...

  class Proxy_ : public Interface, public mojo::MessageReceiver {
   public:
    explicit Proxy_(mojo::MultiplexRouter* router,
                     mojo::InterfaceId id = mojo::kPrimaryInterfaceId);
    // Implements each method by building a mojo::Message and calling
    // router->SendMessage(); Accept() dispatches responses via a
    // mojo::internal::ResponseDispatcher.
  };

  class Stub_ : public mojo::MessageReceiver {
   public:
    Stub_(Interface* impl, mojo::MultiplexRouter* router,
          mojo::InterfaceId id = mojo::kPrimaryInterfaceId);
    // Accept() decodes an incoming message by ordinal (Interface::kFooName)
    // and calls into impl_, sending a response via router->SendMessage()
    // if the method expects one.
  };
};
```

The same `Proxy_`/`Stub_` pair backs both the primary interface
(`Remote`/`Receiver`, `id` defaults to `kPrimaryInterfaceId`) and any
associated interface minted from it (`AssociatedRemote`/`AssociatedReceiver`,
`id` from `PendingAssociatedRemote<T>::interface_id()`). Wire payloads are
hand-rolled (length-prefixed strings, a bare `{interface_id, version}` pair
for an associated-interface parameter) -- see `message.h`'s header comment
for why this repo doesn't ship a generic struct (de)serializer.

### Sync methods

A generated-code producer (like `.voodoom`'s `[Sync]`) can give a method an
*additional* blocking overload on `Proxy_` only -- `Stub_`/the impl side
never changes, since blocking is purely a caller-side concern:

```cpp
class Interface {
  // ...the ordinary pure virtual methods, unchanged...

  class Proxy_ : public Interface, public mojo::MessageReceiver {
   public:
    // ...the ordinary callback-taking Method(...) override, unchanged...

    // The extra overload: same name, response values come back through
    // trailing out-pointers instead of a callback, bool return means "got
    // a response" (false = send failure or a SyncWaitFor error). Built on
    // Connector::SyncWaitFor (connector.h) -- register the pending
    // response exactly like the async path does, send the request, then
    // block until the handler fires.
    [[nodiscard]] bool Method(Args..., ResponseArgs*... out);
  };
};
```

Because this overload only exists on `Proxy_`, not on `Interface` itself,
a caller can't reach it through the ordinary `remote->Method(...)`
(`Remote<T>::operator->()` only ever returns `Interface*`, by design --
`Stub_` implementations are only ever called async, so forcing every impl
to also implement a synchronous-call-to-itself would be a needless
constraint just to make one accessor's type simpler). Instead, both
`Remote<T>` and `AssociatedRemote<T>` expose `proxy()`, returning the
concrete `Interface::Proxy_*`: `remote.proxy()->Method(...)` reaches the
blocking overload; `remote->Method(...)` keeps reaching the ordinary async
one through `Interface*`.

### Interface version control

`lib/interface_control_messages.h` provides the wire-level half of
per-interface version negotiation -- `QueryVersion`/`RequireVersion`,
matching real Mojo's own `Run_`/`RunOrClosePipe_` (see that header's
comment for the full reasoning and the reserved ordinals
`kRunMessageId`/`kRunOrClosePipeMessageId`). A generated-code producer
wires these in unconditionally, on every interface, not behind any opt-in
attribute:

```cpp
class Interface {
  // ...the ordinary pure virtual methods, unchanged...
  static constexpr uint32_t kVersion = /* max method MinVersion, 0 if none */;

  class Proxy_ : public Interface, public mojo::MessageReceiver {
   public:
    // Sends a kRunMessageId request; the callback receives the
    // responder's own kVersion once the response arrives.
    void QueryVersion(base::OnceCallback<void(uint32_t)> callback);

    // Sends a kRunOrClosePipeMessageId request carrying `version`, no
    // response. If the responder's own version is lower, its
    // Stub_::Accept() (see below) returns false for that message --
    // Connector::RaiseError() then stops that side from reading
    // anything further off the pipe (see connector.cc), the "close the
    // pipe if unsupported" real Mojo's own RequireVersion has, without
    // an explicit disconnect signal reaching the caller directly (see
    // that mechanism's own caveat, above under "Interface versioning" --
    // this repo doesn't propagate errors across the pipe without an
    // explicit close).
    void RequireVersion(uint32_t version);
  };

  class Stub_ : public mojo::MessageReceiver {
   public:
    [[nodiscard]] bool Accept(mojo::Message* message) override {
      if (message->name() == mojo::internal::kRunMessageId) {
        return mojo::internal::HandleQueryVersionMessage(
            message, router_, id_, kVersion);
      }
      if (message->name() == mojo::internal::kRunOrClosePipeMessageId) {
        return mojo::internal::HandleRequireVersionMessage(*message, kVersion);
      }
      // ...the ordinary per-method ordinal dispatch, unchanged...
    }
  };
};
```

Because `QueryVersion` always expects a response, a generated `Proxy_`
needs its `mojo::internal::ResponseDispatcher responses_` member and
response-dispatching `Accept()` override unconditionally now too -- not
gated on whether the interface happens to have any response-bearing
*application* method, the way it would be without this feature.

### Associated-endpoint peer-closed notification

Unlike `QueryVersion`/`RequireVersion` above (per-*interface*, dispatched
by `Message::name()`), `MultiplexRouter` also implements a per-*pipe*
control message, matching the role of real Mojo's
`PipeControlMessageHandler`: closing a `ScopedInterfaceEndpointHandle`
(via `AssociatedReceiver<T>::reset()`/`AssociatedRemote<T>::reset()`, or
either object's destructor) sends the peer's router a best-effort
notification carrying the closed id. It's distinguished from ordinary
interface traffic by targeting the reserved `kInvalidInterfaceId` as the
message's `interface_id()` -- never a value any real primary or associated
endpoint could have -- rather than by a reserved `Message::name()` ordinal,
since this operates one layer below any single interface's Stub_/Proxy_:

```cpp
mojo::AssociatedReceiver<Listener> listener_receiver(&impl);
listener_receiver.set_disconnect_handler([] {
  // Fires once the peer's AssociatedRemote<Listener> for this same
  // endpoint resets/destructs -- the underlying physical pipe can still
  // be perfectly healthy; only this one associated endpoint went away.
});
```

`AssociatedRemote<T>` gets the same `set_disconnect_handler`, symmetric to
`Remote<T>`/`Receiver<T>`'s existing pipe-wide one
(`set_connection_error_handler`, driven by `Connector::RaiseError`) --
except this one is per-endpoint rather than per-pipe, and it does cross the
wire, unlike `RaiseError()` (see "Interface version control" above and
`connector.cc`). A handler registered *after* the notification has already
arrived does not fire retroactively (matching
`set_connection_error_handler`'s own no-catch-up behavior).

## Known simplifications (documented, not hidden)

- **Message header**: produced messages are v2 (payload behind a relative
  pointer immediately after the 48-byte header). WrapWireBytes accepts v0
  and v1 and normalizes them to v2. Versions newer than 2 are rejected.
- **Associated-interface id allocation**: the top-bit namespace split
  (`kInterfaceIdNamespaceMask`) is this library's own collision-avoidance
  policy, not a wire-format requirement -- ids are opaque to whichever peer
  didn't mint them.
- **Pipe-control-message protocol is a single-purpose stand-in for real
  Mojo's `PipeControlMessageHandler`**: it implements peer-closed
  propagation (see "Associated-endpoint peer-closed notification" above)
  but not the rest of Chromium's `pipe_control_message.mojom` surface
  (e.g. `RunOrClosePipe`-style pipe-wide capability negotiation, idle
  tracking) -- there was only ever the one gap worth closing here.
- **No generic struct serializer**: every interface hand-rolls its own
  payload encoding (see `examples/echo`), same as any real mojom-generated
  interface would, just without the code generator.
- **No isolated/unassociated endpoints**: every `AssociatedRemote`/
  `AssociatedReceiver` pair genuinely travels over a live `MultiplexRouter`'s
  physical pipe; there's no same-process-only shortcut.
- **JS `Remote<T>` lives in [WASMExtWrench](../WASMExtWrench)** as
  `wew.mojo.Remote` (cppgc-wrapped `mojo::Remote`), not a second facade
  inside this repo.
- **`Connector::SyncWaitFor` joins a WASMv8Bindings `Platform::PostJob`
  (`JobHandle::Join`) and waits on HolePunch `Executor::WaitForWork`
  (condition_variable, optional `TimeDelta` timeout) instead of spinning.

## JS bindings

Every Mojo C++ Bindings surface is also reachable from JS on the
[WASMv8bindings](../WASMv8bindings) V8 embedder-API facade (quickjs-ng
execution through [WASMSafeSpace](../WASMSafeSpace)). Same idiom as
[WASMExtWrench](../WASMExtWrench) / [WASMCadidumKernel](../WASMCadidumKernel).

```
cmake -B build
cmake --build build --target wcb_bindings_everything
ctest --test-dir build -R wcb_bindings --output-on-failure
```

| JS API | Source | Tier |
|---|---|---|
| `wcb.echo(string) → string` | `src/bindings/echo_bindings.cc` | B1 (Echo Remote/Receiver) |
| `wcb.echoWithListener(string) → {echo, notification, notificationCount}` | same | B2 (associated EchoListener) |

## Build (native, Windows)

MinGW g++ or MSVC. Requires the sibling `../WASMCadidumKernel` checkout
(which in turn requires `../WASMThunker` and `../WASMHolePunch`).

```
cmake -B build
cmake --build build
ctest --test-dir build --output-on-failure
build/wcb_example
```

## WASI

```
cmake -B build-wasi -DCMAKE_TOOLCHAIN_FILE=cmake/wasi-sdk.cmake
cmake --build build-wasi
wasmtime --wasi sockets build-wasi/wcb_tests.wasm
```

## License

New code is BSD-3-Clause. Chromium reference files remain Copyright The
Chromium Authors.
