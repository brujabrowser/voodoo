# WASMVoodooCompile

`voodoomc` reads a `.voodoom` or Chromium `.mojom` file and writes a C++ header. The header is the `Proxy_` / `Stub_` shape that [WASMCadidumBindings](WASMCadidumBindings) already speaks. The compiler runs on the host. The generated header is what a program includes.

```
HolePunch  →  Thunker  →  Cadidum kernel  →  Cadidum bindings  →  voodoomc
WASMHolePunch   WASMThunker   WASMCadidumKernel   WASMCadidumBindings   this directory
```

HolePunch opens the pipe. Thunker is the C system API. Cadidum kernel is the C++ system API. Cadidum bindings is the C++ message layer. `voodoomc` is the IDL compiler in front of that layer. [WASMSafeSpace](WASMSafeSpace) and [WASMv8bindings](WASMv8bindings) sit under the bindings for cage storage and heap tags.

## Build

MinGW g++ or MSVC. `voodoomc` itself needs only the C++ standard library. The golden tests also build the in-tree bindings.

```
cmake -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

`wvc_tests` covers the lexer, parser, and generator. `wvc_golden_tests` compiles generated headers and runs them through Cadidum. Both targets are produced by the commands above when `WASMCadidumBindings` is present, which it is in this tree.

## Compile one file

```
build\voodoomc.exe examples\echo\echo.voodoom -o echo_interface_gen.h
```

`examples/echo/echo.voodoom` is the small case: two interfaces, a string method, and an associated listener. The matching hand-written header lives at `WASMCadidumBindings/examples/echo/echo_interface.h`. `tests/golden/echo_roundtrip.cc` builds the generated header and runs the same roundtrip.

Other flags:

| Flag | What it does |
| --- | --- |
| `-o FILE` | Header for the file you named |
| `--namespace=NAME` | C++ namespace for that file. Default is its `module` line |
| `--guard=NAME` | Include guard. Default comes from the output filename |
| `--import-dir=DIR` | Extra directory for `import`. Repeatable. The importing file's own directory is searched first |
| `--out-dir=DIR` | Required once a file imports anything. Each imported file gets its own header here |
| `--import-out=PATH=NAME` | Override the generated filename for one resolved import |
| `--js-out=FILE` | Method names and their camelCase JS names, for the entry file only |
| `--gn-out=FILE` | A GN `source_set()` snippet wrapping `-o` |
| `--enable-if=FLAG` | Keep declarations whose `[EnableIf]` matches. Repeatable |
| `--typemap=Name=::CppType` | Replace a `[Native]` type with an existing C++ type |
| `--include=header` | Header included so a typemap's `mojo::NativeTraits` is visible |

Imports keep their own `module` namespace. `--namespace` and `--guard` apply to the entry file.

## What you can write

A file is a module, then imports, then declarations.

```
module echo;

interface EchoListener {
  OnEcho(string value);
};

interface Echo {
  EchoString(string in) => (string out);
  SetListener(pending_associated_remote<EchoListener> listener);
};
```

The examples under `examples/` are the language, one feature each. The golden test next to each name is the proof it roundtrips.

| Example | What it covers |
| --- | --- |
| `examples/echo` | Interface, response, associated remote |
| `examples/registry` | Enum, struct, array, `pending_remote` |
| `examples/settings` | Union and map |
| `examples/logging` | Nested types |
| `examples/profile` | Imports across files |
| `examples/telemetry` | Nullable handles |
| `examples/calculator` | `result<T, E>` |
| `examples/versioning` | Struct `[MinVersion]` |
| `examples/extensibility` | Interface `[MinVersion]` and `[Extensible]` |
| `examples/union_versioning` | Union version and `[Default]` |
| `examples/enum_versioning` | Enum `[Extensible]` |
| `examples/handle_types` | `handle` and `pending_*` |
| `examples/fixed_array` | `array<T, N>` |
| `examples/optional_handles` | Trailing `?` |
| `examples/feature_decl` | `feature` and `[EnableIf]` |
| `examples/const_parity` | Const values |
| `examples/cadmium_voodoo` | A real control channel: associated listener plus a struct argument |

Attributes the compiler acts on: `[Sync]`, `[MinVersion=N]`, `[Extensible]`, `[Native]`, `[EnableIf]`, `[EnableIfNot]`, `[Default]`. Any other attribute name is accepted and ignored. A malformed value is a parse error.

A struct or union field may not form a by-value cycle (`struct A { B b; }; struct B { A a; };`). `array<T>` and `map<K, T>` may, because those fields are stored in a cage vector or map. There is no `StructPtr<T>`.

## Calling a published `.mojom`

[Bruja](Bruja) is the Chromium `.mojom` side of this tree. The blobs themselves are fetched, not committed:

```
Bruja\tools\fetch-chromium-mojom.ps1
```

That checks the published `.mojom` files into `Bruja/third_party/chromium-src`. Point the tools at that directory with `--root` or `MOJOVM_MOJOM`.

`mojovm` loads one method on the call. `api` is `module.Interface.Method`. `arg` is fields separated by `0x1F`. The reply starts with `ok`, then the same separator, then the ordinal.

```
build\mojovm.exe --root Bruja\third_party\chromium-src call extensions.mojom.MessagePort.DispatchDisconnect
build\mojovm.exe --root Bruja\third_party\chromium-src portfolio --out Bruja\out\mojovm-portfolio.txt
```

The portfolio is every method the compiler accepted: 9062 methods, 1860 files, 0 failures. `Bruja/out/mojovm-portfolio.txt` is that list.

`ng` is the same lookup plus a fire. The key may be any width, including SECRPC `91556947316803`.

```
build\ng.exe --root Bruja\third_party\chromium-src call extensions.mojom.MessagePort.DispatchDisconnect
build\ng.exe --root Bruja\third_party\chromium-src fire-name extensions.mojom.MessagePort.DispatchDisconnect expected
build\ng.exe fire 91556947316803
```

`fire-name` with `expected` uses the Chrome ordinal. `voodoo` uses the FNV ordinal below. `ng call` prints both.

## Ordinals

One method has three numbers. They are not interchangeable.

| Name | How it is computed |
| --- | --- |
| voodoo | FNV-1a of the full ABI name: `func module.Interface.Method(params)->(response)`. Offset `2166136261`, prime `16777619`, XOR then multiply, then `& 0x7fffffff`. Same constants as go++ `hash/fnv` `New32a` |
| dagger | FNV-1 of the short method name only. Multiply, then XOR, same offset and prime |
| mojo | Chrome's scrambled ordinal: SHA-256 of the `chrome/VERSION` salt, the short interface name, and the method's index. The declaration index in the generated header is not this number |

`MessagePort.DispatchDisconnect` is the worked example: dagger `941764473`, voodoo `1481416133`, mojo `2592569`.

That number is the message name. It does not run a function by itself. The stub for the interface is the proto: it knows the method signature, reads the payload, and calls the object. The object is named on CHPT. The callee pointer is named on EPT. The type record is named on TPT. `heap_exec` loads all three and calls. `Bruja/out/address-db.md` has that return value in the `heap` column.

The process is the invitation. `OutgoingInvitation` attaches a named pipe and `Send`s it. The other process `Accept`s the same channel and `ExtractMessagePipe`s that name. `Receiver::Bind` on the extracted pipe builds the stub, names the object on CHPT, and starts reading. The caller's `Remote` writes a message whose `name` is the method ordinal. The stub matches that name, reads the payload, and calls the object. The object loads itself from CHPT, the callee from EPT, and the type record from TPT, then the callee writes the reply. `golden_invitation_runs_the_function` is that path for `echo.Echo.EchoString`.

Changing the ABI spelling or those two FNV constants renumbers the voodoo column. `Bruja/out/address-db.md` is the three columns for the whole portfolio. The sqlite file next to it is `Bruja/out/address-db.db`. How those rows were collected is written up in `Bruja/docs/mojo-address-research.md`.

## Layout

| Path | What it is |
| --- | --- |
| `src/` | Lexer, parser, module loader, C++ generator, `mojovm` |
| `examples/` | `.voodoom` files, one language feature each |
| `tests/` | Frontend tests and golden roundtrips |
| `WASMHolePunch/` | Pipes |
| `WASMThunker/` | Mojo C system |
| `WASMCadidumKernel/` | Mojo C++ system |
| `WASMCadidumBindings/` | C++ bindings and the generated-code contract |
| `WASMSafeSpace/` | Cage the bindings store interned types in |
| `WASMv8bindings/` | Heap tags for those interned types |
| `Bruja/` | Published `.mojom` checkout, `ng`, and the address catalog |

`build/`, `Bruja/third_party/chromium-src/`, and generated `*_gen.h` headers are local. They are not in the git tree.

## License

BSD-3-Clause. See [LICENSE](LICENSE).
