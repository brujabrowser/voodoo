# WASMVoodooCompile

A `.voodoom` IDL compiler (`voodoomc`) that emits the exact `Proxy_`/`Stub_`
C++ shape [WASMCadidumBindings](WASMCadidumBindings)'s generated-code
contract documents, the way Chromium's real mojom compiler --
[`mojom_bindings_generator.py`](https://chromium.googlesource.com/chromium/src/+/main/mojo/public/tools/bindings/mojom_bindings_generator.py),
its [lexer](https://chromium.googlesource.com/chromium/src/+/main/mojo/public/tools/mojom/mojom/parse/lexer.py)/[parser](https://chromium.googlesource.com/chromium/src/+/main/mojo/public/tools/mojom/mojom/parse/parser.py)/[ast](https://chromium.googlesource.com/chromium/src/+/main/mojo/public/tools/mojom/mojom/parse/ast.py)
frontend, and its
[C++ generator](https://chromium.googlesource.com/chromium/src/+/main/mojo/public/tools/bindings/generators/mojom_cpp_generator.py) +
[`interface_definition.tmpl`](https://chromium.googlesource.com/chromium/src/+/main/mojo/public/tools/bindings/generators/cpp_templates/interface_definition.tmpl)
backend -- targets real Mojo's `mojo/public/cpp/bindings`. This is the last
rung of the stack:

```
sockets  →  HolePunch  →  Mojo C System  →  Mojo C++ System  →  Bindings  →  IDL compiler (this repo)
whp::net    whp::punch     WASMThunker        WASMCadidumKernel   WASMCadidumBindings   WASMVoodooCompile
            (wst)          (wck)              (wcb)               (voodoomc)
```

like `protoc` or `mojom_bindings_generator.py` itself, it never
runs inside the sandboxed WASI viewer. It reads a `.voodoom` file and writes
a C++ header; nothing here links against `wst`/`wck`/`wcb` at runtime (only
this repo's *tests* do, to prove the generated code actually works).

## Why "real implementation," not a toy grammar

`examples/echo/echo_interface.h` in WASMCadidumBindings was hand-written
*as* what a future `.voodoom` compiler would emit -- its header comment says
so directly, and `WASMCadidumBindings/README.md`'s "Generated-code contract"
section is the spec `voodoomc` targets. Feeding
[`examples/echo/echo.voodoom`](examples/echo/echo.voodoom) (the same
`EchoListener`/`Echo` interfaces that file documents in its own header
comment) through `voodoomc` produces a header structurally identical to
that hand-written one -- and `tests/golden` compiles and runs the generated
version through the same request/response and associated-interface
roundtrip WASMCadidumBindings' own tests use, against real
`wcb`/`wck`/`wst` machinery. That's the parity bar, not "looks plausible."

## Type intern (Object Type Identifier)

Same fashion as Go++ `type_key_of<T>()` / `go/types` intern / CHPT tags:

- The compiler interns every type shape (`int32`, `array<int32,4>`,
  `pending_remote<echo.Echo>`, `interface:echo.Echo`, method signatures)
  to one id. `Identical` is id equality.
- Generated interfaces/structs/unions emit `intern_key()`, `type_key()`
  (static-char address, no RTTI), `chpt_tag()`, `NameOnHeap` / `FromHandle`.
- Bindings `mojo::internal::TypeIntern` stores intern records in the
  [WASMSafeSpace](WASMSafeSpace) cage, names them on TPT, and names
  C++ objects on [WASMv8bindings](WASMv8bindings) CHPT. `FromHandle`
  returns null unless the interned tag matches.
  [WASMLime](../WASMLime) generated Frame/Context types use this
  automatically.

## go++ occupancy

Chromium `.mojom` / `.voodoom` is compiled on the **host** by `voodoomc`
(like `protoc`). The implementation occupies **wasigocvm** as current
Go++ — `wasigoc` → `compile.bat` (`-DWASIGO_GOCVM=1`) → `.wasigocvm.wasm`
→ `wasitime`. That is the machine, not a host leftover Mojo applet and
not Lovelace's old gc-Go glue generator / shim_sandbox `HostBridge`.

Two files per occupancy example under `examples/occupancy/<name>/`:

| File | Role |
| --- | --- |
| `<name>.voodoom` | proto; `voodoomc` emits cage intern (`interface:ns.Name`, `func ns.Name.M(...)->(...)`) |
| `<name>.go` | current Go++ occupancy (`package main`); prints the same intern keys and runs the method |

```
# host proto
voodoomc examples/occupancy/ping/ping.voodoom -o ping_mojo.h

# occupancy (from the go++ checkout)
compile.bat ..\WASMVoodooCompile\examples\occupancy\ping\ping.go -o ping.wasm
wasitime ping.wasm
```

`ping` / `greet` prove scalar and string occupancy. CMake runs the intern
contract in `wvc_tests` always, and (when sibling `../go++` `wasigoc` is
present) a native leftover of the generated C++ the same way go++'s own
`*_native` goldens do.

### The go++ hash is the method ordinal

`MethodKey` (`src/type_intern.cc`) is the fully qualified name:

```
func module.Interface.Method(param types)->(response types)
```

[Bruja](Bruja) holds the published Chromium `.mojom` tree. `mojovm`
(`src/mojovm.cc`) loads a file on `mojovm.Call(api, arg)` — the same two
strings as `gocvm.Call` — and the ordinal it returns is the `uint32` FNV of that
name. The hash is go++ `hash/fnv` `New32a`: offset `2166136261`,
prime `16777619`, XOR then multiply, then `& 0x7fffffff`. That is the
`uint32` width `project_lovelace` `ExtractMojo` stores. The input stays
the full FQN.

voodoo owns the string. go++ owns the constants. The ordinal is the two
of them. Changing `MethodKey` or `offset32` / `prime32` renumbers the
catalog. Bruja's `out/mojovm-portfolio.txt` is that catalog: **9062**
methods, **1860** files, **0** failures. `kMethodName` in a generated
header is still the declaration ordinal. The call ordinal is the hash.

The guest link is the same go++ `wasigocvm.bat`. It routed

`C:\Users\grego\go++\toolchain\bin\wasm32-wasip2-clang++.exe`

from the go++ toolchain. `~/wasi-sdk` is not that compiler. `ping.go` linked to `build/ping.wasm`. `wasitime` printed `func mojotool.PingTool.Ping(int32)->(int32)` and `1`.

## `.voodoom` grammar (v23 subset of mojom)

```
voodoom_file    := module_stmt? import_stmt* top_level_decl*
module_stmt     := 'module' NAME ('.' NAME)* ';'
import_stmt     := 'import' STRING ';'
top_level_decl  := interface_decl | struct_decl | union_decl | enum_decl
                  | const_decl | feature_decl

feature_decl    := attribute_list? 'feature' NAME '{' const_decl* '}' ';'
                  -- (v21) real mojom's own FeatureBody grammar allows
                     nothing but `const` inside one -- see "`feature`
                     declarations" below

interface_decl  := attribute_list? 'interface' NAME '{' (enum_decl | const_decl | struct_decl | union_decl | method_decl)* '}' ';'
                  -- (v16) nested enum, (v17) nested const, (v24) nested
                     struct/union -- mangled `Foo_Params` plus `using`
                     inside class Foo
method_decl     := attribute_list? NAME ('@' ORDINAL)? '(' param_list? ')' ('@' ORDINAL)? ('=>' response)? ';'
                  -- Chromium puts `@N` between the name and `(`; this
                     compiler also still accepts the historical
                     `Name(...)@N` form. Not both at once.
response        := '(' param_list? ')'
                  | 'result' '<' type_spec ',' type_spec '>'
                  -- (v20) real mojom's alternative response shape -- see
                     "`result<T, E>` responses" below
attribute_list  := '[' attribute (',' attribute)* ']'
attribute       := NAME ('=' attribute_value)?
attribute_value := '-'? NUMBER | '-'? FLOAT | 'true' | 'false' | STRING
                  | NAME | NAME ('|' NAME)+ | NAME ('&' NAME)+
                  -- (v17) widened from integer-only to match real mojom's
                     own attribute-value grammar (an identifier, a
                     literal, or a pipe-/ampersand-delimited name list --
                     e.g. `[EnableIf=some_flag]`, `[EnableIf=a|b]`). Acted-on
                     attributes, each on a specific site:
                     'Sync' (no value) only on a method_decl; 'MinVersion=N'
                     (integer value required, N >= 0) on a method_decl, a
                     union_field, a struct_field, or an enum_value (wire
                     behavior for struct fields and enum-as-struct-field
                     reads via IsKnownXAsOf; metadata for methods/union
                     fields); 'Extensible' (no value) only on
                     an interface_decl, a union_decl, or an enum_decl;
                     'Native' (no value) on an empty struct/enum;
                     'EnableIf'/'EnableIfNot' (flag / a|b / a&b) on any
                     decl -- voodoomc `--enable-if=FLAG` keeps matching
                     decls and drops the rest (file_path.mojom's two
                     `path` fields). Any of these used with the wrong
                     value shape is still a parse error --
                     but (v17) *any other attribute name* is now accepted
                     and silently ignored, not rejected, at every legal
                     attribute_list position (interface/struct/union/enum/
                     const decls, and method/struct-field/union-field
                     items) -- required for real .mojom files, which use
                     dozens of Chromium/build-specific attributes
                     (`[ServiceSandbox]`, `[Uuid]`, `[Stable]`, ...)
                     this compiler has no meaning for outside a real
                     Chromium build. See "Known simplifications"
param_list      := param (',' param)*
param           := attribute_list? type_spec NAME ('@' ORDINAL)?
                  -- (v23) `[MinVersion=N]` and `@N` on a parameter are
                     accepted (metadata / ignored for layout)

struct_decl     := attribute_list? 'struct' NAME '{' (enum_decl | const_decl | struct_field)* '}' ';'
                  | attribute_list? 'struct' NAME ';'
                  -- (v23) empty/native `struct Foo;`
                  -- (v16) nested enum, (v17) nested const, and (v17) a
                     struct-level attribute_list (accepted, ignored --
                     nothing acts on one) -- same as interface_decl above
struct_field    := attribute_list? type_spec NAME ('@' ORDINAL)?
                   ('=' field_default)? ';'
field_default   := 'true' | 'false' | '-'? NUMBER | FLOAT | STRING | NAME
                  | float_const
                  -- only legal when type_spec is a non-nullable
                     bool/integer/float/double/string/enum (v17 adds
                     float/double -- see "Nested enum and const" below);
                     a bare NAME (or, v17, a qualified `Container.NAME`) is
                     either a previously-declared const of the field's
                     exact type (bool/integer/float/double/string fields)
                     or one of the field's own enum's declared value names
                     (enum fields only)
float_const     := ('float' | 'double') '.'
                    ('INFINITY' | 'NEGATIVE_INFINITY' | 'NAN')
                  -- (v22) real mojom's special float/double constant
                     literals, only legal when type_spec is float/double
                     -- found via a real vendored .mojom file, see "Known
                     simplifications"

union_decl      := attribute_list? 'union' NAME '{' union_field* '}' ';'
union_field     := attribute_list? type_spec NAME ('@' ORDINAL)? ';'

enum_decl       := attribute_list? 'enum' NAME '{' enum_value_list? '}' ';'
                  | attribute_list? 'enum' NAME ';'
                  -- (v23) empty/native `enum Foo;`
enum_value_list := enum_value (',' enum_value)* ','?
enum_value      := attribute_list? NAME ('=' '-'? NUMBER | '=' NAME)?
                  -- (v23) `[MinVersion=N]` / `[Default]`; `kFoo = kBar`
                     aliases an earlier value in the same enum

const_decl      := attribute_list? 'const' const_type NAME '=' field_default ';'
                  -- (v17) const_type widened from scalar-only to match
                     field_default's own legal type set (bool/integer/
                     float/double/string/enum), and a top-level attribute_list
                     is now legal too (accepted, ignored) -- a const's value
                     can itself be a NAME/qualified-NAME reference to an
                     earlier-declared const, same rule field defaults have
const_type      := scalar | 'string' | NAME | NAME '.' NAME

type_spec       := nullable_type '?'?
nullable_type   := 'string' | scalar
                  | 'array' '<' type_spec '>'
                  | 'array' '<' type_spec ',' INT_CONST_DEC '>'
                  -- (v19) a fixed-size array -- see "Fixed-size arrays"
                     below
                  | 'map' '<' map_key_type ',' type_spec '>'
                  | 'hash_map' '<' map_key_type ',' type_spec '>'
                  -- (v23) real mojom's other associative-array spelling;
                     generates the same `std::unordered_map` as `map`
                  | pending_kind '<' NAME '>'
                  | 'associated' NAME '&'?
                  -- (v24) Chromium shorthand: `associated Foo` is
                     pending_associated_remote<Foo>; `associated Foo&` is
                     pending_associated_receiver<Foo>
                  | handle_kind
                  | NAME   -- a previously-declared struct, union, or enum
                  | NAME ('.' NAME)+  -- (v16) originally just a qualified
                     reference to a nested enum (exactly `Container.
                     EnumName`); (v22) generalized to any number of
                     dotted segments, the last being the actual type and
                     everything before it either a nested-enum container
                     (still only for exactly one dot) or a cross-file
                     `module` statement (e.g. `mojo_base.mojom.Time`) --
                     see "Nested enum and const" below and "Known
                     simplifications"
map_key_type    := 'string' | scalar | NAME  -- NAME must name an enum
scalar          := 'bool' | 'int8' | 'uint8' | 'int16' | 'uint16'
                  | 'int32' | 'uint32' | 'int64' | 'uint64' | 'float' | 'double'
pending_kind    := 'pending_remote' | 'pending_receiver'
                  | 'pending_associated_remote' | 'pending_associated_receiver'
handle_kind     := 'handle' | 'handle' '<' handle_subtype '>'
                  -- (v18) a raw handle, distinct from pending_kind (which
                     is always a message pipe bound to a specific
                     *interface*) -- see "Handle types" below
handle_subtype  := 'message_pipe' | 'data_pipe_consumer'
                  | 'data_pipe_producer' | 'shared_buffer'
                  -- never 'platform' -- real mojom's raw-OS-handle/fd
                     subtype has no C++ type in this WASM-hosted stack at
                     all, so it's a parse error naming exactly that, not a
                     silent guess (see "Known simplifications")
```

A dotted `module a.b.c;` produces nested C++ namespaces
(`namespace a { namespace b { namespace c { ... } } }`), same as real
mojom -- a single-segment name (`module echo;`, still the common case)
generates exactly what it always did. `--namespace` accepts either
spelling (`a.b.c` or C++'s own `a::b::c`).

A `field_default` doesn't touch the wire format at all -- every field is
still always written and read unconditionally, same as before. It only
changes what a freshly-constructed struct's member starts out as
(`int32_t x{42};` instead of `int32_t x{};`), the same thing a default
does in real mojom. bool/integer/float/double/`string`/enum fields can
have one; struct/union, `array`/`map`, and nullable fields can't (see
"Known simplifications" for why). A NAME (or, v17, qualified
`Container.NAME`) default resolves differently depending on the field's
type: for an enum field it's always looked up among *that* enum's own
declared values (`Status s = OK;`) and is order-independent -- the enum
can be declared anywhere in the file, exactly like an ordinary
(non-defaulted) field of that type already could; for a bool/integer/
float/double/string field it's a reference to a previously-declared
`const` of the exact same type (`int32 x = kMax;`, never a widening/
narrowing match), and -- unlike the enum case -- that const **must be
declared earlier in the file** (or in one of this file's imports). The
generated C++ reflects which one it was: `Status s{Status::OK};` /
`int32_t x{kMax};`, not the resolved literal inlined in place -- more
readable, and it's what real mojom's own generator does too.

A `struct_field`'s `[MinVersion=N]` is a completely different thing from a
`field_default` -- see "Struct versioning" below for what it actually
changes (the wire format, not just a C++ member-initializer). C++ members
stay in declaration order; Write/Read emit fields in non-decreasing
`MinVersion` order so an old reader still sees version-0 fields first.

The trailing `?` is legal on `string`, `array<...>`, `map<...>`, a
struct/union `NAME`, (v14) a `pending_kind`, and (v18) a `handle_kind` --
mojom's "reference" types, the ones with a meaningful absent state. It's a
parse error on a scalar or an enum `NAME` (neither has a null
representation in mojom either). A nullable `pending_kind`/`handle_kind`
behaves differently from the others at codegen time -- see "Nullable
types" below -- but is grammatically no different: same trailing `?`,
same "only where a `type_spec` is legal" rule as everything else, which
for `pending_kind`/`handle_kind` means a method parameter, a struct/union
field, an array element, or a map value (v23), never a map key. `?` applies to whatever `type_spec` it
immediately follows, so it nests: `array<string?>` is a non-nullable
array of nullable strings, `array<string>?` is a nullable array of
non-nullable strings. A `map<K, V>`'s key `K` can never be nullable (a
key can't be absent).

`import_stmt` must come right after `module_stmt` (if any) and before every
other top-level decl -- a real parse-time restriction, so "what's already
visible when this file's own decls start" is unambiguous, not textual-order
dependent. `STRING`'s contents are a path resolved relative to the
importing file's own directory first, then against each `--import-dir` in
order -- see "Imports" below for the full story: each file in the graph now
gets its own generated header, in its own namespace (real mojom parity),
wired together by `#include`s.

Ordinals: within one interface, either every method has an explicit `@N` or
none do (matching mojom) -- unordinaled methods number sequentially from 0
in declaration order. Union field tags follow the same all-explicit-or-all-
implicit rule. Enum values follow the same idea: an omitted value continues
from the previous one + 1 (0 if first).

`[Sync]` requires a response (`=> (...)`) -- a parse error otherwise, since
a blocking call needs something to block *for*. See "Sync methods" below
for what it generates and how a caller reaches it.

A top-level `[` is ambiguous on its own -- `[Extensible]` could be about
to precede `interface`, `union`, or `enum`. The parser resolves this by
parsing the attribute list first, then dispatching on whichever keyword
actually follows it (see `ParseBody` in `parser.cc`); a `[` followed by
none of the three is a parse error naming what it expected instead.

**Struct/union fields** may be scalar/`string`/enum/struct/union/
`array<T>`/`map<K, V>` thereof, and (v23) `pending_*`/`handle` kinds --
matching real mojom. Map *keys* still cannot be handle-bearing. A by-value
(or optional / `array<T, N>`) struct-or-union field needs a complete C++
type, so a cycle of those is a parse error. `array<T>` and `map<K, V>`
may reference `T` while `T` is still incomplete -- generated as
WASMSafeSpace `CageVector<T>` / `CageMap<K, V>` (cage-backed, and the
incomplete-`T` instantiation this family's libstdc++ 16 / libc++ 23
accept). The generator forward-declares every struct/union and reorders
emission by complete-type dependencies, so a type declared later in the
file can still be used by value. A
`map<K,
V>`'s key type `K` is further restricted to bool/integer/`string`/enum --
never `float`/`double`/struct/union/`array`/`map`/`pending_*` -- matching
real mojom's own map-key rule. **Interfaces have no such restriction**:
every interface name is forward-declared before any full definition (a
`pending_remote<T>` etc. only ever needs `T` to be incomplete), so
interfaces may reference each other in either order.

## Imports

`import "path/to/other.voodoom";` pulls that file's `struct`/`union`/
`enum`/`interface`/`const` declarations into the importing file, visible
unqualified in the *source* (no `other.Foo`-style qualification -- same as
mojom) even though the *generated C++* qualifies them (see below). Like
mojom_bindings_generator.py, `voodoomc` emits one C++ header per `.voodoom`
file, each in its own namespace (that file's own `module` statement -- no
override exists for anything but the entry file), `#include`-ing each of
its own direct imports' generated headers. Unlike mojom_bindings_generator.py
-- which relies on a build system (GN) to already know the whole dependency
graph and its file-name mapping -- `voodoomc` is invoked directly on one
entry file and has to work that mapping out itself: `src/module_loader.h`/
`.cc` resolves the whole import graph (each file keeping its own,
*unmerged* declarations -- see `LoadedGraph`/`LoadedFile`), and `main.cc`
derives every import's generated filename with a fixed convention
(`DeriveGenFilename`: strip directory, strip `.voodoom` or (v22) `.mojom`,
append `_gen.h` -- e.g. `common/types.voodoom` -> `types_gen.h`,
`services/device/public/mojom/battery_status.mojom` ->
`battery_status_gen.h`), overridable per import via
`--import-out=RESOLVED_PATH=NAME`, writing each into `--out-dir=DIR`
(required whenever the entry transitively imports anything; the entry
itself still uses `-o` exactly as before). `src/parser.cc`'s
`SeedFromPrelude` still makes every name loaded earlier in the graph
resolvable unqualified while parsing a later file (a deliberately permissive
simplification, unchanged since before per-file generation: it's *every*
earlier-loaded file, not just this one's own direct imports) -- what's new
is that every resolved cross-file reference also gets stamped with
*whose* namespace it needs qualifying with at codegen time
(`TypeSpec::owner_namespace`/`DefaultValue::named_expr_owner_namespace`),
via a `Prelude::owner_by_name` map module_loader.cc builds alongside its
existing merge. `cpp_generator.cc`'s `NamespacePrefix` turns a non-empty
owner into the `a::b::` prefix actually emitted wherever that type/
interface/const/enum-value is referenced.

Path resolution, per import: try relative to the importing file's own
directory first, then each `--import-dir=DIR` in the order given (like
C/C++'s `#include "..."` search order) -- first match wins. `.`/`..`
segments are collapsed with plain string manipulation (no filesystem
calls), so `import "../common/x.voodoom";` behaves the same on every OS.
A file imported more than once (a "diamond" -- two files both importing a
shared `common.voodoom`) is only parsed, and only gets its own generated
header written, once. An import cycle (`a` imports `b` imports `a`) is a
hard error, reported as the actual chain, not a hang or a silent guess.

[`examples/common/types.voodoom`](examples/common/types.voodoom) is a pure
library file (an enum and a struct, no interface); it's compiled directly.
[`examples/logging/logging.voodoom`](examples/logging/logging.voodoom)
`import`s it (`import "../common/types.voodoom";`, resolved with zero
extra flags) and uses both types plus a `map<Severity, int32>` (the first
example with an enum-typed map key) in its own interface -- compiling it
(see `CMakeLists.txt`'s `wvc_generate_logging` target) produces both
`logging_interface_gen.h` (namespace `logging`) and `types_gen.h`
(namespace `common`), the former `#include`-ing the latter.
[`tests/golden/logging_roundtrip.cc`](tests/golden/logging_roundtrip.cc)
proves the whole chain actually works end-to-end at runtime: it references
`common::LogEntry`/`common::Severity` (not `logging::LogEntry`/
`logging::Severity`) -- these never appear in `logging.voodoom` itself, so
if the import graph resolution, the separate-header generation, or the
cross-file `#include` were broken, this wouldn't even compile.

## Nullable types

`T?` generates `std::optional<T>` wherever `T` would otherwise appear (a
struct field, a method param/response param) -- same by-const-ref passing
rule as the non-nullable containers (`string`/`array`/`map`/struct/union).
The wire format is exactly what you'd expect from this compiler's flat,
sequential, no-offset-indirection style (see "What's here" below): a
`bool` presence flag immediately followed by the value, only if present --
no pointer, no separate "null table", nothing that needs the generic
offset-walking (de)serializer this compiler deliberately doesn't have.
`WriteX`/`ReadX` for a nullable field just wrap the same
scalar/string/struct/union/array/map write/read code every other field
uses, the same way array elements and map entries do -- so a nullable
array's elements, or a nullable struct's own nullable fields, all compose
for free through the same recursive `EmitWriteValue`/`EmitReadInto`.

[`examples/profile/profile.voodoom`](examples/profile/profile.voodoom)
exercises nullable scalars-inside-structs (`Address.zip`), nullable nested
structs (`Profile.address`), and nullable top-level `string`/`array`/`map`
fields, all in the same pair of structs.
[`tests/golden/profile_roundtrip.cc`](tests/golden/profile_roundtrip.cc)
proves every nullable field survives a real `Put()`/`Get()` roundtrip in
both states -- present *and* absent -- including the case where an outer
nullable struct field is present but one of *its own* nullable fields is
absent, proving nullability nests correctly rather than one flag
accidentally controlling more than its own field.

A nullable `pending_remote<T>`/`pending_receiver<T>`/
`pending_associated_remote<T>`/`pending_associated_receiver<T>` method
parameter (v14) works differently from every other nullable kind above:
it does *not* get wrapped in `std::optional<...>` at all. Each of those
four types already has a real, meaningful "empty" state on the C++ side
(`is_valid() == false`, the default-constructed state) -- wrapping that in
`std::optional` would just be a second way to say the same thing.
Instead, `?` on one of these adds a presence-flag byte to that one
parameter's own top-level wire framing
(`EmitTopWritePrelude`/`EmitTopWritePayload`/`EmitTopReadParam` in
cpp_generator.cc, not the generic `EmitWriteValue`/`EmitReadInto` every
other nullable kind goes through -- these four are never legal inside a
struct/union/array/map until v23, when the same framing started applying
there too):
absent, only the flag byte is written, and the reader leaves the parameter
default-constructed without touching `Message::TakeHandles()`'s handle
index at all (so an absent `pending_remote<T>?` doesn't desync a *later*
handle-bearing parameter's own index); present, the flag is followed by
exactly the same bytes (and, for the two non-associated kinds, the same
attached OS handle) a non-nullable parameter of that type would already
send.
[`examples/optional_handles/optional_handles.voodoom`](examples/optional_handles/optional_handles.voodoom)
and
[`tests/golden/optional_handles_roundtrip.cc`](tests/golden/optional_handles_roundtrip.cc)
prove both states, for both a non-associated `pending_remote<Watcher>?`
(a real OS pipe handle, or none) and an associated
`pending_associated_remote<Listener>?` (a real in-process endpoint id, or
none) -- including that the connection keeps working normally for a
second call right after an absent one, proving the handle-index
bookkeeping really does stay in sync.

A nullable `handle`/`handle<message_pipe>`/`handle<data_pipe_consumer>`/
`handle<data_pipe_producer>`/`handle<shared_buffer>` method parameter
(v18) follows the exact same non-`std::optional` design as nullable
`pending_kind` above, for the same reason: `mojo::ScopedHandle` and its
four sibling scoped-handle types already have a real `is_valid()`-based
"empty" state, so `?` just adds the same kind of presence-flag byte to
that parameter's own top-level framing, reusing
`EmitNullablePresenceDecl`/`EmitNullablePayloadGuardOpen`/`Close` --
absent, only the flag byte is written and the reader leaves the
parameter default-constructed without consuming a `TakeHandles()` slot;
present, the flag is followed by the same `AttachHandle`-based transfer a
non-nullable handle parameter already does.
[`examples/handle_types/handle_types.voodoom`](examples/handle_types/handle_types.voodoom)
and
[`tests/golden/handle_types_roundtrip.cc`](tests/golden/handle_types_roundtrip.cc)
prove both states for a bare `handle`, a `handle<message_pipe>`, and a
`handle<shared_buffer>` -- including rewrapping a received bare
`handle`/`handle<message_pipe>` into a real `mojo::PendingReceiver<T>`
and completing a genuine method call over it (proving it's the exact
live pipe endpoint, not a dup or placeholder) and mapping a received
`handle<shared_buffer>` to confirm a byte written on the sender's mapping
is visible through the receiver's own mapping of the same buffer.

## Handle-bearing fields

As of v23, `pending_remote`/`pending_receiver`/`pending_associated_*` and
`handle`/`handle<...>` are legal as struct/union fields, array elements,
and map values -- not just top-level method parameters. Generated
`WriteX`/`ReadX` for a type that (transitively) contains a handle take a
handle-context (`TakeHandles()` vector + index + `MultiplexRouter*`) so
handle-index bookkeeping stays in sync whether the handle sits at the top
level or nested inside a struct. A struct/union/array/map that contains a
handle is move-only on the C++ side (because `PendingRemote<T>` etc. are)
and is passed by value, not `const T&`.
[`examples/handle_fields/handle_fields.voodoom`](examples/handle_fields/handle_fields.voodoom)
and
[`tests/golden/handle_fields_roundtrip.cc`](tests/golden/handle_fields_roundtrip.cc)
prove a live `pending_remote` inside a struct survives a real round-trip
and can still complete a method call on the far side.

## Fixed-size arrays

`array<T, N>` (v19) is real mojom's fixed-size array: `N` (a plain
decimal literal, strictly positive) is a compile-time-known element
count, so -- unlike a plain `array<T>` -- there's no length prefix on the
wire at all; the reader already knows exactly how many elements to read
from the type itself. It generates `std::array<T, N>` instead of
`v8::internal::CageVector<T>` wherever the type appears (a struct field, a method
param/response param), and `EmitWriteValue`/`EmitReadInto`'s `kArray`
case just skips the `uint32_t` size write/read that a plain `array<T>`
always does, looping exactly `N` times instead of a runtime-read count --
every other part of the array codegen (the element write/read call
itself, by-const-ref param passing, nullable wrapping) is identical
between the two, since both are still `TypeKind::kArray`, just with
`TypeSpec::fixed_array_size` set or 0. A nullable `array<T, N>?` works
exactly like a nullable plain array -- wrapped in `std::optional<...>`,
same as every other nullable container (this is *not* like the
`pending_kind`/`handle_kind` special case above -- a fixed array has no
built-in "empty" state of its own to reuse). Elements are otherwise
restricted the same way a plain array's are: no `pending_kind`/
`handle_kind` elements (`CheckNotHandleBearing` applies identically,
regardless of a fixed size), and struct/union declaration-order checking
recurses into a fixed array's element type exactly the way it already did
for a plain array.
[`examples/fixed_array/fixed_array.voodoom`](examples/fixed_array/fixed_array.voodoom)
and
[`tests/golden/fixed_array_roundtrip.cc`](tests/golden/fixed_array_roundtrip.cc)
exercise a fixed array of scalars and a fixed array of a struct (both as
`Grid` struct fields, proving element-type recursion still works), a
nullable fixed array in both present and absent states, and a fixed array
as a bare top-level method parameter -- all round-tripped through real
WASMCadidumBindings.

## Dotted namespaces and field defaults

[`examples/telemetry/telemetry.voodoom`](examples/telemetry/telemetry.voodoom)
exercises a dotted `module wasmvoodoo.telemetry;` name (generated code
lives in `namespace wasmvoodoo { namespace telemetry { ... } }`) together
with every kind of field default this compiler supports, all on one
struct (`Sample`): a bare string literal (`label`), a bare bool literal
(`verified`), a reference to a previously-declared const
(`count = kDefaultCount`), and a reference to one of the field's own
enum's declared values (`level = MEDIUM`) -- deliberately declared
*after* `Sample` in the file, to exercise that this resolution is
order-independent.
[`tests/golden/telemetry_roundtrip.cc`](tests/golden/telemetry_roundtrip.cc)
checks a freshly-constructed `Sample` already has all four defaults
(checking the *resolved values*, e.g. `count == 7`, not just that
something got set) *before* it's ever sent anywhere -- proving they're a
real member-initializer effect, not something that only shows up after a
roundtrip -- fully qualifies every type through `wasmvoodoo::telemetry::`
(which wouldn't compile if the dotted name hadn't actually nested), and
proves a real `Record()`/`GetLast()` roundtrip -- for both a fully-custom
`Sample` and one left at every default -- still works inside that nested
namespace.

## Sync methods

`[Sync] Method(Args...) => (ResponseArgs...);` doesn't touch `Stub_` or the
impl side at all -- blocking is purely the *caller's* concern, so
`Interface`'s pure virtual `Method(...)` and `Stub_::AcceptMethod`/its
`impl_->Method(...)` call are identical whether or not the method is
`[Sync]`. `Proxy_` gets an *additional* overload on top of (never instead
of) the normal `base::OnceCallback`-taking one:

```cpp
[[nodiscard]] bool Method(Args... args, ResponseArgType1* out1, ...);
```

Response values come back through trailing out-pointers; the `bool`
return means "got a response" (`false` = a send failure or the wait itself
erroring out, e.g. the peer closed the pipe -- not distinguished further,
matching how the async path already discards `SendMessage`'s own result).
It's built entirely on
[WASMCadidumBindings](WASMCadidumBindings)' `Connector::SyncWaitFor` --
added there specifically to support this (see that repo's README for what
"blocking" actually means in a stack with no real OS-level wait:
cooperatively pumping `whp::Executor` until the response arrives or the
pipe errors, same as real Mojo's own sync calls in the one way that
matters most -- it can block forever if the peer never responds and the
pipe never errors).

Because this overload only lives on `Proxy_`, not on `Interface` itself, a
caller can't reach it through the ordinary `remote->Method(...)` (that
`operator->()` only ever returns `Interface*`). `Remote<T>`/
`AssociatedRemote<T>` instead expose `proxy()`, returning the concrete
`Interface::Proxy_*` -- also added to WASMCadidumBindings alongside
`SyncWaitFor` -- so **the call site for a sync method is
`remote.proxy()->Method(...)`, not `remote->Method(...)`**.

[`examples/calculator/calculator.voodoom`](examples/calculator/calculator.voodoom)
has two `[Sync]` methods (`Add`, `Divide`) and one ordinary async one
(`Echo`) on the same interface.
[`tests/golden/calculator_roundtrip.cc`](tests/golden/calculator_roundtrip.cc)
calls `Add`/`Divide` through `remote.proxy()->...` with **no manual pump**
anywhere before reading the result -- if the blocking overload only queued
the request and returned early instead of actually blocking, those checks
would see a default-constructed answer, not the real one -- and separately
proves `Echo` still needs its own pump, so `[Sync]` on two methods didn't
somehow make the whole interface synchronous.

## `result<T, E>` responses

`Method(...) => result<T, E>;` (v20) is real mojom's alternative to
`=> (param_list)`: exactly one response value, which is either a success
`T` or a failure `E`. The C++ API is `base::expected<T, E>` -- Bindings is the `base::` rung
(`include/base/expected.h` re-exports HolePunch `whp/base/expected.h`);
CadidumKernel is `sys::`, Thunker is `c::`. Async methods take
`base::OnceCallback` the same way (HolePunch `whp/base/once_callback.h`
via `base/callback.h`). The wire is still a
synthesized two-arm union (`parser.cc`'s `MakeResultUnion`, name
`<Interface>_<Method>Result`, fields `value`/`error`, tags 0/1) --
Proxy_/Stub_ convert at the method boundary.
`T`/`E` follow the same rules an ordinary union field's type already
does -- `CheckNotHandleBearing` (no `pending_kind`/`handle_kind`) and
complete-type acyclicity (a by-value struct/union `T`/`E` may be
declared later in the file; a cycle of those is still a parse error)
both run against them exactly the way
`ParseUnion`'s own field loop already does, and the synthesized union's
own `UnionDecl::decl_index` is assigned at the point the `result<T, E>`
is parsed, so the generator's complete-type sort places it relative to every other struct/
union in the file for `GenerateCppHeader`'s Embed-sort (the synthesized
union's `WriteX`/`ReadX` always ends up emitted after `T`/`E`'s own, and
before the interface class that uses it).
[`examples/result_type/result_type.voodoom`](examples/result_type/result_type.voodoom)
has an async `Get(string key) => result<Item, NotFoundError>;` (both arms
carrying real struct data) and a `[Sync] Divide(...) => result<int32,
string>;` (proving `result<T, E>` composes with a blocking call the same
way an ordinary multi-param response already does).
[`tests/golden/result_type_roundtrip.cc`](tests/golden/result_type_roundtrip.cc)
proves both the success and error arm round-trip correctly, for both the
async and the sync method -- including, for the sync case, that no manual
pump happens anywhere near the blocking call itself, the same proof
`calculator_roundtrip.cc` already established for an ordinary `[Sync]`
response.

## Struct versioning

There's no such thing as a "final" struct in mojom -- every struct is
wire-extensible, always. A field just happens not to have an explicit
`[MinVersion=N]` yet, which is equivalent to `[MinVersion=0]`: present
since the struct's very first version. So `voodoomc` gives *every*
generated struct a real wire header, even one with no `[MinVersion]`
fields at all -- `mojo::internal::StructHeader` (`{uint32_t num_bytes;
uint32_t version;}`, 8 bytes), the exact same wire struct real Mojo's own
generated code uses, already defined byte-for-byte in
WASMCadidumBindings' `message_internal.h`. This is the point where this
compiler's normally strictly-flat, no-indirection wire format (see "Why
`real implementation`" above) makes its one deliberate exception: every
struct instance is now `{header}{fields...}` instead of just
`{fields...}`, because there's no way to give old and new readers/writers
real compatibility without *some* self-describing framing.

**Writing** a struct always writes every field it has -- a writer's
"version" is simply everything its own schema was compiled with, there's
no narrower concept of a writer holding back fields. Since this compiler
computes a message's bytes by appending (no separate size-computation
pass the way real Mojo's generator has), `num_bytes` is filled in by
backpatch: reserve 8 placeholder bytes, write every field normally, then
overwrite the placeholder via `Message::mutable_payload()` now that the
final size is known.

**Reading** a struct reads the header first, computes where this struct's
own region ends from `num_bytes`, then reads each field *only if*
`header.version >= that field's own MinVersion` -- skipping a field
leaves it at its already-value-initialized (or `field_default`'d) state.
Because fields are wire-ordered by `MinVersion` (C++ members stay in
declaration order), the first field a reader's version
check fails on means every later *wire* field fails too -- nothing needs to
compare itself to its neighbors, each field's check stands alone. Finally
-- regardless of how many fields it actually understood -- the read
function unconditionally jumps to the computed end-of-struct offset. That
last step is what makes both directions of version skew work correctly
from the exact same code:

- **An old reader gets a message from a new writer**: the writer's
  `version` is higher than any `MinVersion` the old reader's schema knows
  about, so the old reader's per-field checks all pass -- it reads
  everything IT understands, then the unconditional jump skips past the
  newer fields it doesn't, landing exactly where whatever comes next
  (another field, a sibling array element, ...) actually starts.
- **A new reader gets a message from an old writer**: the writer's
  `version` is lower than some of the new reader's fields' `MinVersion`,
  so those per-field checks fail -- the new reader leaves them at their
  own default (an explicit `field_default` if one was declared, otherwise
  ordinary value-initialization), and the unconditional jump lands at
  exactly the same place the old writer's own struct actually ended (no
  overrun, because `num_bytes` came from the writer, not guessed).

This composes for free through every existing container: a
`[MinVersion]`-bearing struct used as another struct's field, an
`array<StructType>` element, or a `map<K, StructType>` value all go
through the exact same `WriteX`/`ReadX` pair, unmodified -- the versioning
logic lives entirely inside `EmitStruct`, not in the generic
`EmitWriteValue`/`EmitReadInto` dispatch those containers already share.

[`examples/versioning/widget_v1.voodoom`](examples/versioning/widget_v1.voodoom)
and
[`examples/versioning/widget_v2.voodoom`](examples/versioning/widget_v2.voodoom)
declare the *same* `Widget` struct at two points in its evolution --
`v2` adds a `[MinVersion=1] string color = "unknown";` field `v1` never
had -- in different namespaces (`widget_v1::` / `widget_v2::`), so both
generated headers coexist in one test binary with no symbol collision,
simulating two real revisions of one schema the way splitting them into
two actual files would.
[`tests/golden/versioning_roundtrip.cc`](tests/golden/versioning_roundtrip.cc)
proves genuine cross-version compatibility, not just same-schema
round-tripping (which every *other* golden test already proves for its
own structs): a `widget_v2::Widget` written and read back as a
`widget_v1::Widget` (checking a marker value written *after* the struct in
the same message actually lands where expected -- proof the old reader's
offset skip is exact, not approximate), the reverse (an old writer's
message read as `widget_v2::Widget`, checking `color` comes back as its
*declared default*, not garbage or an empty string that happens to look
similar), and the same forward-compat case again through
`array<Widget>`, to prove it composes through a container and isn't just
a special case of the bare top-level call.

## Interface versioning

Interfaces can evolve too -- a method can be added later -- and this is a
genuinely *different* mechanism from struct field versioning above, not
an extension of it. Real mojom's full interface versioning has two
halves, and this compiler now implements both:

1. **Caller-side discipline**: before calling a method added in version
   N, a well-behaved client can confirm (via `QueryVersion()`/
   `RequireVersion()`, sent over two reserved `Message::name()` ordinals
   -- `kRunMessageId`/`kRunOrClosePipeMessageId`, `0xFFFFFFFF`/
   `0xFFFFFFFE`, matching real Mojo's own reserved control-message
   ordinals) that the remote actually supports version N.
2. **Receiver-side tolerance**: an `[Extensible]` interface's `Stub_`
   treats a message ordinal it doesn't recognize as "drop it, don't error"
   instead of a fatal protocol violation.

Both halves live in
[WASMCadidumBindings](WASMCadidumBindings)' `lib/interface_control_messages.h`
(`HandleQueryVersionMessage`/`HandleRequireVersionMessage`) plus a small
amount of always-generated code -- not behind `[Extensible]` or any other
opt-in attribute, since `QueryVersion`/`RequireVersion` are protocol-level
and meaningful on *every* interface, extensible or not. Every generated
`Proxy_` gets `QueryVersion(base::OnceCallback<void(uint32_t)>)` (sends a
`kRunMessageId` request; the callback receives the responder's own
`kVersion` once the response arrives) and `RequireVersion(version)` (sends a `kRunOrClosePipeMessageId`
request, no response -- see below for what happens if it's unsatisfied);
every generated `Stub_::Accept()` checks both reserved ordinals *before*
its own ordinary per-method dispatch. The wire payloads are simplified
relative to real Mojo (which wraps these in an extensible `RunInput`/
`RunOutput` union so more `Run_` subcommands could be added later without
a wire-incompatible change) since nothing outside this codebase's own
generated `Proxy_`/`Stub_` pairs ever talks to these ordinals -- see that
header's own comment for the exact simplified shape.

This is a genuinely *different* mechanism from `MultiplexRouter`'s own
router-level pipe-control-message protocol (see that header's comment) --
that one is about *router-level* control messages for associated-endpoint
lifecycle (peer-closed propagation across a `ScopedInterfaceEndpointHandle`,
distinguished on the wire by targeting the reserved `kInvalidInterfaceId`
rather than a reserved `Message::name()`), a separate wire concept from
these *interface-level*, per-`Message::name()` control messages.
Implementing the interface-level half didn't require the router-level one
-- they were independent gaps, and both are now closed (see "Associated
interface disconnect notification" below for the router-level half).

`[MinVersion=N]` on a **method** is metadata only: it drives
`Interface::version` (max over all methods, 0 if none are marked) and the
generated `static constexpr uint32_t kVersion` constant -- the exact
number `QueryVersion` reports and `RequireVersion` checks against -- but
changes nothing about the wire format or per-method dispatch itself
-- unlike a struct field's `MinVersion`, there's no per-call header for a
method to be gated by (method params/response are still flat, unframed
top-level fields, not wrapped the way struct fields now are). Unlike
struct fields, method `MinVersion` values don't need to be in any
particular declaration order either -- dispatch is by ordinal, not
position, so there's no wire-layout reason to require it.

An explicit method `@N` colliding with either reserved ordinal
(`0xFFFFFFFE`/`0xFFFFFFFF`) is a parse error (`ParseMethod` in
`parser.cc`) -- a real, checked rejection of a real wire collision, not a
guess that no one would ever pick those numbers.

`[Extensible]` on an **interface** is where the real behavior lives:
`Stub_::Accept`'s fallthrough (an incoming message whose ordinal matches
none of the interface's methods) becomes `return true;` (silently drop
that one message) instead of `return false;`. That return value matters
because WASMCadidumBindings' `Connector::ReadAllAvailableMessages`
(`connector.cc`) treats `false` from the receiver as a real protocol
error and calls `RaiseError()` over it -- which stops that connector from
reading anything further off the pipe, *forever*, not just for that one
message. `RaiseError()` is local to whichever side hit it, though: it
doesn't close the underlying pipe, so it doesn't automatically notify the
peer -- unlike the router-level pipe-control-message protocol ("Associated
interface disconnect notification" below), which exists specifically for
associated-endpoint lifecycle and has nothing to do with primary-interface
protocol errors like this one -- the peer only observes the practical
fallout (no more responses, ever) rather than an explicit disconnect
signal.

[`examples/extensibility/greeter_v1_extensible.voodoom`](examples/extensibility/greeter_v1_extensible.voodoom),
[`examples/extensibility/greeter_v1_closed.voodoom`](examples/extensibility/greeter_v1_closed.voodoom)
(identical, minus `[Extensible]`), and
[`examples/extensibility/greeter_v2.voodoom`](examples/extensibility/greeter_v2.voodoom)
(adds a `[MinVersion=1] Farewell()` method neither v1 schema has) set up
the same kind of two-schemas-in-one-binary proof as the struct versioning
example, but for interfaces. Because the three files declare genuinely
different C++ types, [`tests/golden/extensibility_roundtrip.cc`](tests/golden/extensibility_roundtrip.cc)
builds the message pipe by hand
(`PendingRemote<T>(pipe, version)`/`PendingReceiver<T>(pipe)` constructed
directly from raw handles, bypassing the type-checked `Bind()` path) to
connect a `greeter_v2::Greeter` `Remote` -- the "upgraded client" --
straight to each older server in turn, and proves: against the
`[Extensible]` server, calling `Farewell()` (which it doesn't know) is
silently tolerated and a *subsequent* `Greet()` call (which it does know)
still gets a real reply; against the non-extensible server, the identical
`Farewell()` call trips `RaiseError()` (observed on the *server's own*
`Receiver::set_disconnect_handler`, not inferred from a client-side
notification that -- per the mechanism above -- never arrives), and the
subsequent `Greet()` call never gets a response at all.

[`tests/golden/interface_control_messages_roundtrip.cc`](tests/golden/interface_control_messages_roundtrip.cc)
reuses those same two generated headers (no new schema needed, since
`QueryVersion`/`RequireVersion` exist unconditionally on every interface)
to prove real, cross-schema version negotiation: `QueryVersion` against
the `greeter_v1_closed::Greeter` server (`kVersion=0`) reports `0`;
against a `greeter_v2::Greeter` server (`kVersion=1`) reports `1`;
`RequireVersion` with a satisfied requirement leaves the connection
working normally afterward; `RequireVersion(1)` against the `kVersion=0`
server causes *that server's own* `Receiver::set_disconnect_handler` to
fire -- the same "observe it on the side that actually hit it" mechanic
`RaiseError()` already has above -- and the connection to stop responding
to anything further, the same "close the pipe if unsupported" consequence
real Mojo's `RequireVersion` has.

## Associated interface disconnect notification

`MultiplexRouter` (in WASMCadidumBindings) also implements the
router-level half of real Mojo's pipe-control-message protocol --
matching the role of Chromium's `PipeControlMessageHandler` -- for one
specific purpose: telling a peer's router when one specific associated
endpoint has closed, distinct from both mechanisms above (interface-level
`QueryVersion`/`RequireVersion`, and `RaiseError()`'s local-only protocol
errors). No `.voodoom`-level attribute or codegen change was needed for
this: it's exposed directly on `AssociatedReceiver<T>`/`AssociatedRemote<T>`
as `set_disconnect_handler`, which application code (generated or
hand-written, it doesn't matter which) calls the same way `Receiver<T>`/
`Remote<T>` already expose `set_disconnect_handler` for the *primary*
interface's whole-pipe error:

```cpp
mojo::AssociatedReceiver<Listener> listener_receiver(&impl);
listener_receiver.set_disconnect_handler([] {
  // Fires once the peer's AssociatedRemote<Listener> for this same
  // endpoint resets/destructs -- the underlying physical pipe, and any
  // other associated or primary interface sharing it, can still be
  // perfectly healthy.
});
```

Closing a `ScopedInterfaceEndpointHandle` (`AssociatedReceiver<T>::reset()`
/`AssociatedRemote<T>::reset()`, or either object's destructor) sends the
peer's router a best-effort notification carrying the closed id --
distinguished from ordinary interface traffic by targeting the reserved
`kInvalidInterfaceId` as the message's `interface_id()` (never a value any
real primary or associated endpoint could have), rather than by a reserved
`Message::name()` ordinal the way `QueryVersion`/`RequireVersion` are,
since this operates one layer below any single interface's `Stub_`/
`Proxy_` -- see `lib/multiplex_router.h`'s
`NotifyPeerEndpointClosed`/`HandlePipeControlMessage`. A handler
registered *after* the notification has already arrived does not fire
retroactively, matching `set_connection_error_handler`'s own
no-catch-up behavior.

[`tests/golden/echo_roundtrip.cc`](tests/golden/echo_roundtrip.cc)'s
`golden_associated_disconnect_handler_fires_across_generated_code` proves
this against real generated code (`echo_interface_gen.h`, produced from
`examples/echo/echo.voodoom`): the client's `AssociatedReceiver` is told,
via a genuine round trip through the server's own `MultiplexRouter`, the
moment the server resets its `AssociatedRemote<EchoListener>`.

## Union versioning

Unions can grow new variants over time too, and -- unlike interface
versioning above -- this compiler's union support needed no scope cut
relative to struct versioning: every union already gets the size-prefixed
wire framing `[Extensible]` needs, whether or not the schema uses
`[Extensible]` at all.

A union only ever writes its *one* active field, selected by tag -- never
all of them the way a struct writes every field in sequence -- so
`[MinVersion=N]` on a union field is metadata only, the same as on a
method: it drives `UnionDecl::version`/the generated `kVersion` constant,
but there's no positional wire layout for field declaration order to have
to agree with, so (unlike struct fields) union fields have no
non-decreasing-`MinVersion`-order requirement.

The real behavior lives in the wire framing every union now has: `{uint32
size}{int32 tag}{value bytes...}` (backpatched the same way a struct's
header is -- see "Struct versioning"). On read, an unrecognized tag means
this reader's schema doesn't have a field for it -- either because it's
older and the writer added one later, or because the two schemas simply
diverged. What happens next depends on `[Extensible]`:

- **Non-extensible** (the default): the read fails (`return false;`) --
  same as before this compiler had any size framing at all. The size
  prefix doesn't buy a non-extensible union anything on its own, but it
  keeps the wire format identical to an extensible union of the same
  fields, so a schema can flip `[Extensible]` on later without a
  wire-incompatible change.
- **`[Extensible]`**: the read *succeeds*, reporting a synthetic
  `Tag::kUnknown` (`which()` returns it; there's no value to recover, so
  no matching getter exists for it) and skipping forward using the size
  prefix -- exactly the same offset-precision guarantee struct forward-compat
  reads have.

This is the same receiver-tolerance-only scope as `Interface::is_extensible`
above, not full mojom union extensibility (which also has a designated
default-field convention this compiler doesn't implement).

[`examples/union_versioning/status_v1_closed.voodoom`](examples/union_versioning/status_v1_closed.voodoom),
[`examples/union_versioning/status_v1_extensible.voodoom`](examples/union_versioning/status_v1_extensible.voodoom)
(identical, plus `[Extensible]`), and
[`examples/union_versioning/status_v2.voodoom`](examples/union_versioning/status_v2.voodoom)
(adds a `[MinVersion=1] bool retry;` variant neither v1 schema has) set up
the same two-schemas-in-one-binary shape as the struct and interface
versioning examples.
[`tests/golden/union_versioning_roundtrip.cc`](tests/golden/union_versioning_roundtrip.cc)
writes a `status_v2::Status` set to `retry` (plus a marker value
afterward, in the same message) and reads it back as both v1 schemas:
`status_v1_extensible::Status` reports `Tag::kUnknown` and the marker
still reads correctly right after (proving the skip is exact, not
approximate); `status_v1_closed::Status`'s read cleanly fails instead. A
separate check proves an ordinary, both-schemas-know-it tag still
round-trips normally through the extensible union.

## Enum versioning

Unlike struct/interface/union versioning above (all "reader tolerates a
writer that's *ahead*"), enum `[Extensible]` is the odd one out in one
useful way: it's the only versioning mechanism here where the closed
(non-extensible, default) behavior is the *new* one, not a pre-existing
gap being made forward-compatible. Before this feature existed, this
compiler read every enum value completely unchecked -- any `int32_t` on
the wire, known or not, was silently `static_cast` into the C++ enum type
with no validation at all, which is actually closer to real mojom's
*extensible*-enum behavior than its default. `[Extensible]` on an
`enum_decl` is what makes a *non*-extensible enum's read genuinely closed,
matching real mojom: an unrecognized value becomes a real read failure
instead of being silently accepted.

Every enum -- extensible or not -- gets a generated `IsKnown<EnumName>`
helper alongside its `enum class`:

```cpp
enum class Color : int32_t {
  kRed = 0,
  kGreen = 1,
  kBlue = 2,
};

inline bool IsKnownColor(int32_t wvc_value) {
  switch (wvc_value) {
    case 0:
    case 1:
    case 2:
      return true;
    default:
      return false;
  }
}
```

(Two differently-named values sharing the same underlying number -- legal
here, same as real mojom's enum aliasing -- collapse to one `case` label;
duplicates would otherwise be a compile error.) Whether a field/parameter/
array-element/... read of that enum type actually *calls* the helper
depends on `EnumDecl::is_extensible`: a non-extensible enum's `ReadX`
code emits `if (!IsKnownColor(wvc_value)) return false;` right before the
`static_cast`; an extensible one skips that check and keeps the original
unconditional-accept behavior -- the helper is still generated for it
(cheap, and independently useful/testable) but nothing in the read path
calls it. Nothing changes on the *write* side either way: writing back out
whatever a C++ enum variable already holds -- known value or not -- always
just works, the same `static_cast<int32_t>` as before.

[`examples/enum_versioning/color_v1_closed.voodoom`](examples/enum_versioning/color_v1_closed.voodoom),
[`examples/enum_versioning/color_v1_extensible.voodoom`](examples/enum_versioning/color_v1_extensible.voodoom)
(identical, plus `[Extensible]`), and
[`examples/enum_versioning/color_v2.voodoom`](examples/enum_versioning/color_v2.voodoom)
(adds a `kBlue` value neither v1 schema has) set up the same
two-schemas-in-one-binary shape as the struct/interface/union versioning
examples, this time around a `Painter` interface's `SetColor(Color c)`
method rather than a struct field.
[`tests/golden/enum_versioning_roundtrip.cc`](tests/golden/enum_versioning_roundtrip.cc)
sends `color_v2::Color::kBlue` (a value neither v1 schema has ever heard
of) to each v1 server in turn: against the non-extensible server, the read
fails and `Connector::RaiseError()` stops that server from responding to
anything further (observed on the server's own
`Receiver::set_disconnect_handler`, the same "side that actually hit it"
mechanic "Interface versioning" above already established); against the
extensible server, the call reaches the impl (its `Color` field just holds
the raw, unnamed wire value `2`) and a subsequent `Ping()` still gets a
real reply.

## Nested enum and const

An `enum` (v16) or `const` (v17) may be declared inside an `interface` or
`struct` body (not `union` -- real mojom doesn't allow that either),
matching real mojom's own rule: `interface Foo { enum Status { OK, ERROR
}; const int32 kMax = 5; M(Status s); };` or `struct Foo { enum Status {
OK, ERROR }; const int32 kMax = 5; Status s; };`. From *outside* its
declaring interface/struct, reference it with a qualified name --
`Foo.Status` / `Foo.kMax` -- the same dotted syntax real mojom uses
(unrelated to a dotted `module a.b.c;` name, a completely different
grammar position; see parser.cc's ParseTypeSpecInner for the enum/type
form, ResolveConstReference for the const/value form). From *inside* the
declaring interface/struct's own body, the bare name (`Status`/`kMax`)
also works -- and (a deliberate permissiveness beyond real mojom's actual
scoping rule, see "Known simplifications" below) so does a bare *enum*
reference from *anywhere else* in the file, not just the declaring
interface/struct. A nested *const*, unlike a nested enum, stays
order-dependent -- resolvable (bare or qualified) only after its own
declaration point, the same rule a top-level const already has (see
`const_by_name_`'s comment in parser.cc) -- so nested consts don't need
enums' pre-scan; `ConstDecl::decl_index` instead tracks one combined,
whole-file declaration order across top-level *and* nested consts, since a
nested const's value can reference an earlier const regardless of which
one (or neither) is nested, and C++ emission has to follow that same real
order (see cpp_generator.cc's `GenerateCppHeader`).

The generated C++ does **not** use a genuine nested type/member
(`Foo::Status`/`Foo::kMax`). A struct can reference an interface's nested
enum/const, or a *different* interface can reference another interface's,
regardless of which happens to be declared first in the file -- and this
compiler has no cross-kind (struct/union/interface) dependency-ordering
pass the way real mojom's generator does (only structs/unions get one, via
`decl_index`/`embed_order_` -- see "Struct/union declaration order
matters" below). Emitting a true nested type/member would only compile
wherever the declaring interface/struct's *entire* class body -- virtual
methods, `Stub_`, `Proxy_`, all of it -- happens to already be textually
complete, which isn't guaranteed once references can go both directions.
So instead a nested enum/const becomes a plain, name-mangled, namespace-
scope declaration -- `Foo_Status`/`Foo_kMax`, never `Foo::Status`/
`Foo::kMax` -- emitted in the same early, ordering-free pass top-level
enums/consts already get (see `cpp_generator.cc`'s `MangledNestedName`/
`EmitNestedEnumDefinition`/`EmitNestedConstDefinition`), so referencing it
never depends on emission order at all. `Foo.Status`/`Foo.kMax` in
`.voodoom` source still mean exactly what they say; only the *generated
C++* spelling differs from what real mojom's own generator would produce.

[`examples/nested_enum/nested_enum.voodoom`](examples/nested_enum/nested_enum.voodoom)
exercises both cross-container directions for enum: `Job` (a struct)
references `Worker.Status` (an interface's nested enum), and `JobQueue`
(an interface) references `Job.Priority` (a struct's nested enum) --
proving the generator doesn't care which of struct/interface happens to
be emitted first. [`tests/golden/nested_enum_roundtrip.cc`](tests/golden/nested_enum_roundtrip.cc)
compiles and runs it against real WASMCadidumBindings.
[`examples/const_parity/const_parity.voodoom`](examples/const_parity/const_parity.voodoom)
does the same for nested const, plus the widened const/default value
grammar below (float/double/bool/string consts, hex literals): `Config`
(a struct)'s own nested const `kDefaultTimeout` references
`Worker.kMaxJobs` (an interface's nested const).
[`tests/golden/const_parity_roundtrip.cc`](tests/golden/const_parity_roundtrip.cc)
proves it end-to-end.

**Widened const/default value grammar (v17).** A const's own type is no
longer integer-only -- bool/integer/float/double/string/enum are all
legal now (matching real mojom's grammar-unrestricted `const typename
NAME = ...;`, bounded here to the same type set a field default can have,
since a const is really just a named, reusable field-default-shaped
value -- see `ast.h`'s `ConstDecl`, which now reuses `DefaultValue`
wholesale instead of a bare integer). Number literals gained hex
(`0x1F`) and float/double (`1.5`, `.5`, `1e10`, `1.5e-3`) forms at the
lexer level (see `lexer.h`/`.cc`'s `kHexNumber`/`kFloatNumber`), and a
leading-zero multi-digit literal (`0123`) is now a real `LexError`
("octal values are not allowed") rather than silently misread, matching
real mojom's own explicit rejection.

## `feature` declarations

`feature NAME { ... };` (v21) is real mojom's top-level declaration for a
Chromium `base::Feature` runtime flag -- attributes like `[Status=STABLE,
EnabledStateByDefault=ENABLED_BY_DEFAULT]` (accepted and silently
ignored, same as any other unrecognized attribute -- see "Known
simplifications") configure how Chromium's own feature-flag system
treats it at runtime, which has no meaning outside an actual Chromium
build. Real mojom's own `FeatureBody` grammar allows nothing but `const`
inside one (confirmed against the fetched `ast.py` during this project's
own roadmap planning) -- so this compiler generates only those nested
consts, the one part of a `feature` block that's actually meaningful
here, using the exact same mangled-namespace-scope treatment
(`RetryPolicy_kMaxRetries`, never a genuine `RetryPolicy::kMaxRetries`)
an interface's or struct's own nested const already gets -- see "Nested
enum and const" above for the full reasoning (it's identical: no
cross-kind dependency-ordering pass, so a real nested member could only
compile in an emission order this compiler can't guarantee). The
`feature` block itself generates no C++ type of its own -- there's no
`class RetryPolicy { ... };` anywhere in the output, only its consts.
`RetryPolicy.kMaxRetries` (qualified, from outside) and `kMaxRetries`
(bare, from inside another const/field-default expression declared
after it -- nested consts stay order-dependent, same rule a top-level
const already has) both resolve exactly the way a nested `const` inside
an `interface`/`struct` already does, because they go through the exact
same `const_by_name_`/`const_container_` machinery in parser.cc -- a
`feature` is just one more kind of const container alongside `interface`
and `struct`.
[`examples/feature_decl/feature_decl.voodoom`](examples/feature_decl/feature_decl.voodoom)
has `RetryPolicy.kMaxRetries` used both as a struct field default
(`Job.retries`) and passed explicitly as an ordinary method argument, and
[`tests/golden/feature_decl_roundtrip.cc`](tests/golden/feature_decl_roundtrip.cc)
proves the mangled const is a real, correct, usable C++ value at runtime
-- not just a parse-time artifact -- including that the field default it
produces survives a real wire round-trip.

## Real `.mojom` file corpus

[`tests/real_mojom/`](tests/real_mojom/) vendors thirteen small, real,
*unmodified* Chromium `.mojom` files (fetched from
`chromium.googlesource.com/chromium/src/+/main/<path>?format=TEXT`,
base64-decoded, and stored verbatim under their real `chromium/src`-
relative path -- e.g. `services/device/public/mojom/geoposition.mojom` --
so import resolution needs nothing but a single search-directory root),
and [`tests/real_mojom_test.cc`](tests/real_mojom_test.cc) parses each
one through the real `LoadModuleGraph`/parser pipeline (not through
`cpp_generator.cc` -- these files' real-world types like `base::Time`
have no WASMCadidumBindings-side runtime counterpart to compile a golden
test against, so this corpus proves real-file *parsing*, not a full
round-trip the way `tests/golden/` does for this project's own examples).
Two of the original five directly surfaced real grammar gaps (v22, phase 8 of the
project's own real-mojom-parity roadmap), both fixed rather than just
documented:
- `services/device/public/mojom/battery_status.mojom` uses real mojom's
  special float/double constant literals (`double discharging_time =
  double.INFINITY;`) -- see `field_default`'s `float_const` production
  above and `DefaultValue::float_special` in `ast.h`.
- `services/device/public/mojom/geoposition.mojom` uses a dotted,
  cross-file top-level type reference (`mojo_base.mojom.Time timestamp;`)
  -- the v16 `NAME '.' NAME` nested-enum form generalized to
  `NAME ('.' NAME)+`, resolving the last segment against the file whose
  exact `module` statement matches everything before it (reusing the
  same `owner_by_name_` map v15's plain cross-file resolution already
  populates) -- see `nullable_type`'s dotted-name production above and
  `ParseTypeSpecInner` in `parser.cc`.

A later pass added seven more files, specifically chosen to stress
constructs the original six didn't happen to combine: `unguessable_token.mojom`/
`token.mojom` (minimal baselines), `big_buffer.mojom` (a `[Stable]` union
with a struct arm that itself carries a `handle<shared_buffer>` field --
v23 handle-bearing fields nested one level inside a union), `string16.mojom`
(imports `big_buffer.mojom` and uses its `BigBuffer` **union** as an
ordinary struct field type -- cross-file resolution of an imported union,
not just an imported struct), `ui/gfx/geometry/mojom/geometry.mojom`
(fifteen structs, several embedding earlier ones by value, exercising the
struct-declaration-order check at real volume), and
`services/device/public/mojom/sensor.mojom` (a fixed-size
`array<double, 4>` at a different `N` than any synthetic example uses,
plus an interface mixing response-bearing and fire-and-forget methods).
All seven parsed correctly on the first try -- no new gap, just a wider
regression net.

The seventh addition, `mojo/public/mojom/base/values.mojom`, is Chromium's
JSON-like `Value` type: `union Value` has direct by-value
`DictionaryValue`/`ListValue` fields, and both of those structs reference
`Value` back through `map<string, Value>`/`array<Value>`. Generated
`array<T>`/`map<K, T>` are WASMSafeSpace `CageVector`/`CageMap`, which
instantiate while `T` is still incomplete; the generator forward-declares
`Value` and emits `DictionaryValue`/`ListValue` first. That file parses
and the generated header compiles --
`real_mojom_values_parses_mutually_recursive_union_and_structs` and
`tests/golden/real_mojom_values_compile.cc`. A *by-value* cycle
(`struct A { B b; }; struct B { A a; }`) is still a parse error: there is
no `StructPtr<T>` box.

## Usage

```
voodoomc examples/echo/echo.voodoom -o echo_interface_gen.h
voodoomc examples/registry/registry.voodoom -o registry_interface_gen.h
voodoomc examples/logging/logging.voodoom -o logging_interface_gen.h
voodoomc examples/profile/profile.voodoom -o profile_interface_gen.h
voodoomc examples/telemetry/telemetry.voodoom -o telemetry_interface_gen.h
voodoomc examples/calculator/calculator.voodoom -o calculator_interface_gen.h
voodoomc examples/versioning/widget_v2.voodoom -o widget_v2_interface_gen.h
voodoomc examples/extensibility/greeter_v2.voodoom -o greeter_v2_interface_gen.h
voodoomc examples/union_versioning/status_v2.voodoom -o status_v2_interface_gen.h
voodoomc examples/cadmium_voodoo/cadmium_voodoo.voodoom -o cadmium_voodoo_interface_gen.h
```

`--namespace=NAME` overrides the C++ namespace (default: the `.voodoom`
file's `module` statement) -- applies to the entry file only, never to an
import (imports always use their own `module` statement; see "Imports").
`--guard=NAME` overrides the header guard (default: derived from the output
filename) -- also entry-only. `--import-dir=DIR` adds a search directory
for `import` statements (see "Imports" above); it may be repeated.
`--out-dir=DIR` is required whenever the entry file transitively imports
anything: every imported file's own generated header is written there (see
"Imports" for the naming convention and `#include` wiring).
`--import-out=RESOLVED_PATH=NAME` (repeatable) overrides the generated
filename for one specific imported file, in place of the default
`DeriveGenFilename` convention -- `RESOLVED_PATH` must match exactly what
`module_loader.cc` resolves that import to (see "Imports").

[`examples/registry/registry.voodoom`](examples/registry/registry.voodoom)
exercises the surface `echo.voodoom` doesn't: an `enum`, a `struct` (used
both as a by-value param and inside an `array<T>` response), `array<string>`,
and a non-associated `pending_remote<T>` parameter -- a real
`ScopedMessagePipeHandle` handed across the wire via
`Message::AttachHandle`/`TakeHandles` (confirmed to actually transit
`Connector`'s `WriteMessageRaw`/`ReadMessageRaw`, not just an in-process
shortcut) and used independently on the far side.
[`tests/golden/registry_roundtrip.cc`](tests/golden/registry_roundtrip.cc)
proves it end-to-end, the same way `echo_roundtrip.cc` does for the v1
surface.

[`examples/settings/settings.voodoom`](examples/settings/settings.voodoom)
exercises the v3 surface neither of those covers: a `union` (an `Outcome`
with an `int32` arm and a `string` arm) and a `map<string, int32>`, both as
sibling fields of the same struct.
[`tests/golden/settings_roundtrip.cc`](tests/golden/settings_roundtrip.cc)
proves both union arms and the map's contents survive a real
`Put()`/`Get()` roundtrip, the same way `registry_roundtrip.cc` does for
the v2 surface.

Unlike the examples above, which each exist to exercise one codegen
feature, [`examples/cadmium_voodoo/cadmium_voodoo.voodoom`](examples/cadmium_voodoo/cadmium_voodoo.voodoom)
is a real consumer: the IDL-formalized shape of the control channel
between [loki-closure](../loki-closure)'s `cmd/cadmium` and
WASMHolePunch's `whp_cadmium` bridge, which `cmd/cadmium/bridge_ipc.go`
hand-rolls today as a polled `/api/rbi/ipc` HTTP hook (JSON, not the real
Mojo wire format). `VoodooBridge.Attach()` hands over a
`pending_associated_remote<VoodooOrdinalListener>` so Cadmium can push
`OnFire()` the moment it queues an outbound ordinal instead of waiting to
be polled -- the same associated-listener idiom `echo.voodoom` uses --
and `VoodooBridge.Report()` covers the `mojo_pipe`/`mojo_recv`/`mojo_send`
reports the HTTP hook's `POST` handles today. Both methods also get
`QueryVersion`/`RequireVersion` for free (see "Interface versioning"
above and `tests/golden/interface_control_messages_roundtrip.cc`) --
a real control channel, not just two RPCs.
[`tests/golden/cadmium_voodoo_roundtrip.cc`](tests/golden/cadmium_voodoo_roundtrip.cc)
proves `Attach()` (a `pending_associated_remote` param *and* a response in
the same call, which none of the other examples combine), the `OnFire()`
push it sets up, and `Report()`'s struct-by-value param all work against
real `wcb`/`wck`.

## Auxiliary output: `--js-out` / `--gn-out` / `--enable-if` / `--typemap`

Two small, optional, entry-file-only outputs alongside the C++ header
`-o` always writes -- see `tests/generator_test.cc`'s
`generator_js_method_table`/`generator_gn_source_set` for their exact
contract.

`--js-out=FILE` writes a JS method-name table (`GenerateJsHeader`): for
each interface, its mojom method names paired with their camelCase JS
names (`EchoString` -> `"echoString"`), for
[WASMExtWrench](../WASMExtWrench)'s `wew.mojo.Remote` to install without
each binding site hand-writing its own name mapping.

`--gn-out=FILE` writes a minimal Chromium-style GN `source_set()` stanza
(`GenerateGnBuild`) wrapping `-o`'s output path -- a convenience snippet
for a consumer that's a real GN build, not a build system this repo runs
itself (this project's own build is the CMakeLists.txt in this directory).

`--enable-if=FLAG` (repeatable) is the feature set `[EnableIf]` /
`[EnableIfNot]` evaluate against. A decl marked `[EnableIf=linux]` is
kept only when `linux` is passed; `[EnableIf=a|b]` keeps if either flag
is set; `[EnableIf=a&b]` requires both; `[EnableIfNot=is_ios]` keeps
unless `is_ios` is set. Dropped methods keep their assigned ordinals
(holes, not compacted). Chromium's `file_path.mojom` two `path` fields
are the motivating case.

`--typemap=Name=::CppType` (repeatable) plus `--include=header` maps a
`[Native]` (or any) struct/enum onto an existing C++ type. Generated
code emits `using Name = ::CppType` and `Write`/`Read` via
`mojo::NativeTraits<Name>` -- specialize that template in an
`--include`d header. Without a typemap, `[Native] struct Foo;` is still
an empty generated struct.

## What's here

| File | Role |
|---|---|
| `src/lexer.h`/`.cc` | Tokenizer (subset of mojom's `lexer.py`) |
| `src/parser.h`/`.cc` | Recursive-descent parser, split into `Parser::ParseHeader()` (module + imports) and `Parser::ParseBody()` (everything else, optionally seeded with an already-resolved `Prelude`) → `voodoom::Module` AST; also does the struct/union-ordering and enum/struct/union name-resolution checks, (v15) stamps `TypeSpec::owner_namespace`/`DefaultValue::named_expr_owner_namespace` on any cross-file reference, and (v16) `TypeSpec::owner_container`/`DefaultValue::named_expr_owner_container` on any nested-enum reference (`ContainerOf`/`ContainerEnums`) |
| `src/ast.h` | The IR: `Module`, `Interface`, `Method`, `Param`, `StructDecl`, `UnionDecl`, `EnumDecl`, `ConstDecl`, `ImportDecl`, `TypeSpec`, `DefaultValue` -- `StructField::min_version`/`StructDecl::version` carry struct `[MinVersion]` info, `Method::min_version`/`Interface::version`/`Interface::is_extensible` carry interface `[MinVersion]`/`[Extensible]` info, `UnionField::min_version`/`UnionDecl::version`/`UnionDecl::is_extensible` carry the same for unions, `EnumDecl::is_extensible` carries `[Extensible]` for enums, `ImportDecl::resolved_path` (v15) is what a generated header's imports get `#include`d by, `Interface::enums`/`StructDecl::enums` (v16) hold nested enums |
| `src/module_loader.h`/`.cc` | Resolves `import` statements transitively (path search, diamond-dependency dedup, cycle detection) into a `LoadedGraph` of per-file, *unmerged* `Module`s in dependency order (v15; mojom_bindings_generator.py's dependency-graph role) -- still builds a flat accumulator + `owner_by_name` map internally to seed `Parser::ParseBody`'s cross-file name resolution, unchanged in spirit since before v15 |
| `src/cpp_generator.h`/`.cc` | Emits one enum/struct/union/`Proxy_`/`Stub_` header per file (mojom_cpp_generator.py's role), `#include`-ing `imported_headers`, qualifying any cross-file reference via `NamespacePrefix`, and (v16) any nested-enum reference via `MangledNestedEnumName`/`ContainerMangle` |
| `src/main.cc` | `voodoomc` CLI |

It also adds a few small pieces to WASMCadidumBindings, when a `.voodoom`
feature needed runtime support that repo didn't have yet rather than
working around the gap in generated code:
`include/mojo/public/cpp/bindings/lib/wire_primitives.h` -- the
`WriteString`/`ReadString`/`WriteScalar<T>`/`ReadScalar<T>` field-level
helpers that `echo_interface.h` used to define inline per-example; factoring
them out means generated code from *any* `.voodoom` file shares one copy
instead of redefining them per interface. This is still not a generic
struct (de)serializer (see `message.h`'s header comment in
WASMCadidumBindings for why) -- every field is still written/read
individually, in the order the `.voodoom` file declares it, same as before.
A `struct StructName { ... };` declaration gets its own generated
`WriteStructName`/`ReadStructName` free functions (in the same
per-field-in-order style), which is what lets `array<StructName>`,
`map<K, StructName>`, and nested structs reuse the same read/write
machinery as top-level method parameters -- still no *generic*,
reflection-based serializer; every type's (de)serialization is code the
compiler generated specifically for it, the same relationship
`protoc`/`mojom_bindgen` output has to *their* runtimes. A `union
UnionName { ... };` declaration gets the same treatment: a generated
`UnionName` class (a tag enum plus one storage member per field -- not a
real C++ `union`, so every arm's destructor/move stays trivial to reason
about) and `WriteUnionName`/`ReadUnionName` functions that write/switch on
the tag, then dispatch to the same per-field write/read code every other
container uses. Struct versioning (see "Struct versioning" above) is the
one place a struct's own fields are no longer *unconditionally*
written/read: each still goes through the exact same per-field
`WriteX`/`ReadX` code, just wrapped in a `{header}{fields...}` frame and
(on the read side) a per-field `if (header.version >= field's
MinVersion)` guard. `[Sync]` methods (see "Sync methods" above) needed
real new *behavior* added to WASMCadidumBindings, not just more codegen:
`Connector::SyncWaitFor(predicate)` (`connector.h`/`.cc`) is what a
`[Sync]` method's generated blocking `Proxy_` overload actually calls, and
`Remote<T>`/`AssociatedRemote<T>` each gained a `proxy()` accessor
(`remote.h`/`associated_remote.h`) so a caller can reach that overload at
all -- see WASMCadidumBindings' own README for both. Struct versioning,
by contrast, needed *no* WASMCadidumBindings changes at all:
`mojo::internal::StructHeader` (`message_internal.h`) -- byte-for-byte
real Mojo's own struct wire header -- was already there, just never
actually used by any generated code until now. Interface versioning
(`[Extensible]`/method `[MinVersion=N]`) needed none either -- the
receiver-tolerance behavior it adds is entirely a `return true;` vs.
`return false;` choice in generated `Stub_::Accept` code, and
WASMCadidumBindings' `Connector`/`MultiplexRouter` already treat that
return value exactly the way "Interface versioning" above describes;
nothing there had to change to make that meaningful. Union versioning is
entirely self-contained generated code too, for the same underlying
reason struct versioning was: it only needed `mutable_payload()` (for the
backpatch) and `WriteScalar`/`ReadScalar` (already generic templates,
happy to read/write a plain `uint32_t` size the same as anything else),
both already used elsewhere in this project's own generated code.
`QueryVersion`/`RequireVersion` (see "Interface versioning" above) needed
real new WASMCadidumBindings code again, the same way `[Sync]` did:
`lib/interface_control_messages.h`
(`HandleQueryVersionMessage`/`HandleRequireVersionMessage`, plus the
reserved `kRunMessageId`/`kRunOrClosePipeMessageId` ordinals) is what
every generated `Stub_::Accept()` calls into for those two ordinals.
Associated-interface disconnect notification (see "Associated interface
disconnect notification" above) needed real new WASMCadidumBindings code
too, but *no* codegen change at all: `MultiplexRouter::
NotifyPeerEndpointClosed`/`HandlePipeControlMessage`/`SetPeerClosedHandler`
(`lib/multiplex_router.h`/`.cc`) and the resulting `AssociatedReceiver<T>`/
`AssociatedRemote<T>::set_disconnect_handler` are pure runtime API,
reachable from any interface's generated types without a single
`cpp_generator.cc` change -- unlike every other feature in this list, this
one is a WASMCadidumBindings-only gap that happened to be the natural next
thing to close. Enum `[Extensible]` (see "Enum versioning" above) needed
no WASMCadidumBindings changes either -- the generated `IsKnown<EnumName>`
helper and the conditional validation call in `EmitReadInto`'s `kEnumRef`
case are both entirely self-contained `cpp_generator.cc` output, using
only `ReadScalar` (already generic) the same way every other enum read
already did. Nullable `pending_*` parameters (see "Nullable types" above)
are self-contained `cpp_generator.cc` output too -- no WASMCadidumBindings
changes -- but for a different reason than struct/union/enum versioning
were: those needed no *runtime* changes because the feature is pure
metadata or a `return true;`/`false;` choice already meaningful to
existing WCB code; this one needs none because `PendingRemote<T>`
etc.'s `is_valid()`/default-constructed "empty" state the new
presence-flag framing relies on already existed in WCB, unchanged, from
long before this feature -- there was simply nothing left to build there.

## Known simplifications (documented, not hidden)

- **No build-system-fed output-path mapping, unlike real mojom.** Real
  mojom's per-file/cross-namespace generation is a build system (GN)
  feeding `mojom_bindings_generator.py` a precomputed dependency graph and
  its `.mojom` -> generated-header filename mapping; `voodoomc` has no such
  build system, so it works that mapping out itself: `--out-dir=DIR`
  (required whenever the entry file transitively imports anything) plus a
  fixed default naming convention for anything not explicitly named
  (`DeriveGenFilename` in `main.cc`: strip directory, strip `.voodoom`,
  append `_gen.h` -- e.g. `types.voodoom` -> `types_gen.h`), overridable
  per import via `--import-out=RESOLVED_PATH=NAME` (repeatable) when a
  project wants a specific imported file to generate to a specific name --
  see "Imports" above and `CMakeLists.txt`'s `wvc_generate_logging` target,
  which names `common/types.voodoom`'s output `common_types_gen.h` this way.
- **Import path resolution is `#include`-style, not mojom's build-system
  roots.** Relative to the importing file's own directory, then each
  `--import-dir` in order -- no awareness of a "source root" or a GN-style
  target graph. `.`/`..` segments are collapsed with plain string
  manipulation (see `NormalizePath` in `module_loader.cc`); nothing heavier
  (no symlink resolution, no case folding) -- two spellings of the same
  import that don't collapse to the same string via that normalization are
  treated as different files.
- **Nested enum: flat bare-name resolution, and no genuine C++ nested
  type.** Two separate divergences from real mojom's own nested-enum rule
  -- see "Nested enum" above for the full reasoning behind both. (1) A
  nested enum's bare name resolves from *anywhere* in the file, not just
  from within its own declaring interface/struct -- real mojom requires
  the qualified `Container.EnumName` form outside the declaring container;
  this compiler's existing struct/union/enum name resolution has always
  been one flat, file-wide (and, via imports, whole-graph-wide) table with
  no real per-container scoping, and this feature reuses that rather than
  building one just for nested enums. A file that already uses correct
  mojom scoping parses identically either way; one relying on the *extra*
  permissiveness here wouldn't parse under real mojom's own compiler. (2)
  The generated C++ type is `Foo_Status` (name-mangled, namespace-scope)
  plus `using Status = Foo_Status` inside `class`/`struct` Foo so callers
  write `Foo::Status`. A true nested `enum class Foo { enum class Status }`
  would only compile wherever `Foo`'s entire class body already happens to
  be textually complete, which this compiler can't guarantee once a struct
  can reference an interface's nested enum (or vice versa) regardless of
  declaration order, and it has no cross-kind dependency-ordering pass to
  fix that (only structs/unions get one -- see the `decl_index`/
  `embed_order_` bullet below). The `.voodoom` source syntax is unaffected
  either way; only the generated C++ spelling differs from real mojom's.
- **Map keys still cannot be handle-bearing.** A `map<K, V>`'s key `K` is
  still bool/integer/`string`/enum only -- never `pending_*`/`handle`,
  matching real mojom. Values, struct/union fields, and array elements
  **can** be handle-bearing as of v23 (see "Handle-bearing fields" below).
- **`handle<platform>` is `mojo::PlatformHandle`.** CadidumKernel wraps
  Thunker's `MojoPlatformHandle` C ABI as `mojo::PlatformHandle`
  (`mojo/public/cpp/system/platform_handle.h`). `PlatformHandle::Wrap` /
  `Unwrap` call `MojoWrapPlatformHandle` / `MojoUnwrapPlatformHandle`,
  which HolePunch implements as a real handle kind (OS HANDLE on Windows,
  fd elsewhere). Wrapped handles transit the wire like the other
  `handle<*>` kinds (AttachHandle / TakeHandles). Mach ports are opaque
  tokens (`WHP_PLATFORM_HANDLE_TYPE_MACH_PORT`), matching WASMYetiKernel
  `MachSendRight`, not Darwin `mach_port_t`.
- **Map keys are further restricted** to bool/integer/`string`/enum --
  never `float`/`double` (no well-defined equality/hash for `NaN`) and
  never struct/union/`array`/`map`/`pending_*` -- matching real mojom's own
  map-key rule, not a compiler-specific narrowing.
- **`result<T, E>` is `base::expected<T, E>` on the C++ API, a two-arm
  union on the wire.** Bindings is the `base::` rung
  (`include/base/expected.h`); CadidumKernel is `sys::`
  (`mojo/public/cpp/system`); Thunker is `c::`
  (`mojo/public/c/system`). The synthesized `Interface_MethodResult`
  union is still generated for serialization; Proxy_/Stub_ convert to
  and from `base::expected<T, E>` at the method boundary.
- **`feature { ... }` bodies allow nothing but `const`** -- matching real
  mojom's own `FeatureBody` grammar (confirmed against the fetched
  `ast.py`), not a compiler-specific narrowing; a method, field, or
  nested enum inside one is a real parse error. The block itself
  generates no C++ type -- only its nested consts do, mangled
  namespace-scope the same way an interface's/struct's own nested consts
  already are (see "`feature` declarations" above) -- and every
  attribute a real `[Status=..., EnabledStateByDefault=...]`-style
  feature block might carry is accepted and silently ignored, since none
  of it (Chromium's `base::Feature` runtime flag configuration) has any
  meaning outside an actual Chromium build.
- **A fixed-size array's `N` must be a bare positive decimal literal** --
  never hex, never a previously-declared `const` reference (real mojom's
  own `array<T, N>` grammar is `INT_CONST_DEC` specifically, not the
  general `literal`/`typename` rule `const`'s value or type gets -- see
  "Fixed-size arrays" above). `array<T, 0>` and any negative size are
  parse errors, not a zero-length or unsigned-wraparound `std::array`.
- **Consts and struct field defaults are bool/integer/float/double/
  string/enum only** (v17 widened this from integer-only, once the lexer
  gained hex/float literal support -- see "Nested enum and const" above).
  A bool/integer/float/double/string field's default may name a
  previously-declared `const` (`int32 x = kMax;`, or, v17, a qualified
  `Container.kMax`), but **that const must be declared earlier in the
  file** (or in one of this file's imports) -- unlike an ordinary
  (non-defaulted) field's *type*, const lookups for defaults aren't
  pre-scanned the way struct/union/enum names already are (see
  `const_by_name_`'s comment in parser.cc), so this is a real,
  order-dependent restriction, not a guess. An enum field's default
  (`Status s = OK;`) *is* order-independent, resolved against that enum's
  own declared value list (see `CollectEnumValues` in parser.cc) --
  matching how the enum *type* was already order-independent. No defaults
  on nullable fields (they already default to absent, arguably the more
  useful default already) or on struct/union/`array`/`map` fields (would
  need parsing a nested field-default list for struct/union, or resolving
  one for every element/entry for `array`/`map` -- neither implemented). A
  default only changes a freshly-constructed struct's initial member
  value, never the wire format: every field is still always written and
  read unconditionally.
- **Struct/union complete-type cycles are rejected; file order is not.**
  A by-value (or optional / `array<T, N>`) struct-or-union field needs a
  complete C++ type, so `struct A { B b; }; struct B { A a; };` is a parse
  error. `array<T>` and `map<K, T>` do *not* create that edge -- they
  generate WASMSafeSpace `CageVector<T>` / `CageMap<K, T>` (cage-backed
  via `CageAllocator`), which instantiate while `T` is incomplete
  (`vector` is C++17-guaranteed; `unordered_map` is not ISO-guaranteed
  but libstdc++ 16 / libc++ 23, the compilers this stack ships, accept
  it). The generator forward-declares every struct/union and emits in
  complete-type dependency order, so a type declared later in the file
  can still be used by value. Interfaces stay forward-declared up front
  as before.
- **No by-value self-embedding, still no `StructPtr<T>`.** Real mojom
  boxes every struct/union field in `StructPtr<T>`. This compiler does
  not: a direct `Node next;` field of type `Node` is still a cycle.
  Recursion through `array<Node>` / `map<string, Node>` is the supported
  shape --
  [`mojo/public/mojom/base/values.mojom`](tests/real_mojom/mojo/public/mojom/base/values.mojom)
  parses and compiles that way.
- **Scalars, string lengths, and struct/union headers are little-endian**
  (`mojo::internal::WriteScalar` / `WriteStructHeader` / `PatchStructHeader`).
  Hosts that are already LE write the same bytes as memcpy; BE hosts swap.
- **No mixed explicit/implicit ordinals within one interface, or tags
  within one union** (matches mojom's own rule for both) -- enforced as a
  parse error, not silently guessed.
- **Generated unions are a tagged class, not a real C++ `union`.** One
  storage member per field (like a struct's, all default-constructed) plus
  a `Tag` enum and `which()`/`set_x()`/`x()` accessors -- trades a little
  memory for keeping every arm's construction/destruction trivial and
  reusing struct-style codegen, instead of placement-new/manual-lifetime
  tricks a packed C++ `union` of non-trivial members (`std::string`,
  `std::vector`, another struct) would need.
- **`pending_*`/`handle` nullable is `is_valid()`, not `std::optional`.**
  Already implemented (v14/v18): a trailing `?` adds a presence-flag byte
  and reuses the C++ type's own empty state. Legal on method parameters
  **and** (v23) on struct/union fields, array elements, and map values.
- **Acted-on attributes are `[Sync]`, `[MinVersion=N]`, `[Extensible]`,
  `[Native]`, `[EnableIf]`/`[EnableIfNot]`, and `[Default]`.** Wrong
  value shape is still a parse error. Every *other* attribute name
  (`[ServiceSandbox]`, `[Uuid]`, `[Stable]`, ...) is accepted and
  ignored. `[EnableIf]` is not ignored: `--enable-if=FLAG` drops
  non-matching decls. `[Native]` empty structs/enums generate empty
  types unless `--typemap` replaces them. `[Sync]`
  itself has no timeout (see WASMCadidumBindings' `Connector::SyncWaitFor`)
  and no reentrancy guard beyond whatever the pumped tasks happen to
  do -- both match real Mojo's own sync-call semantics rather than being
  an accidental gap.
- **`QueryVersion`/`RequireVersion` use tagged `RunInput`/`RunOutput`
  union payloads** (`uint32_t` tag, then the arm). Tag 0 is QueryVersion /
  QueryVersionResult / RequireVersion. This is not Chromium's generated
  mojom union with named fields, but extra arms can be added without a
  wire-incompatible change. See `interface_control_messages.h`. Since
  nothing outside this codebase's own
  generated `Proxy_`/`Stub_` pairs ever talks to these ordinals, there's
  no real interop cost to that -- but it does mean a hypothetical future
  third control subcommand couldn't be added the way real Mojo's own
  format allows, without changing the wire shape.
- **Enum `[Extensible]` plus per-value `[MinVersion=N]`.** `IsKnownX`
  still accepts every declared value. `IsKnownXAsOf(v, version)` also
  requires `version >=` that value's MinVersion -- used when reading an
  enum stored as a struct field (the struct header's version). Method
  parameters still use `IsKnownX` (no struct header on the call).
- **`RequireVersion` closes the pipe, so the peer sees PEER_CLOSED.**
  Bindings' `Connector::RaiseError()` now resets the local handle;
  HolePunch's trap on `PEER_CLOSED` fires the other side's
  `set_disconnect_handler`. That is the same "close if unsupported"
  consequence real Mojo's `RequireVersion` has.
- **Struct field `MinVersion` reorders the wire, not the C++ members.**
  Fields may be declared in any order. C++ members stay in source order;
  Write/Read emit them in non-decreasing `MinVersion` order so an old
  reader still sees version-0 fields first. Real mojom also packs by
  alignment; this compiler has no C-struct-style padding.
- **Union `[Extensible]` plus `[Default]`.** An unrecognized tag is
  skipped using the size prefix. If a field is marked `[Default]`, the
  reader selects that field (default-constructed) instead of
  `Tag::kUnknown`; without `[Default]`, `which()` reports `kUnknown`.
  At most one field per union may be `[Default]`. There is still no
  interaction with interface-level versioning (a union field being new
  doesn't itself gate whether the *method* carrying it is callable).

## Build (native, Windows)

MinGW g++ or MSVC. `voodoomc` itself has no dependencies beyond the C++
standard library:

```
cmake -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

The golden codegen-roundtrip test (`wvc_golden_tests`) only builds if the
in-tree `WASMCadidumBindings` checkout is present (which in turn needs
`WASMCadidumKernel`, `WASMThunker`, `WASMHolePunch`) -- without it,
`voodoomc` and the frontend unit tests (`wvc_tests`) still build and run.

## License

BSD-3-Clause.
