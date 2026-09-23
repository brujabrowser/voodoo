# WASMv8bindings

A strip-port of real V8 source -- not a clean-room rewrite -- targeting the
one piece of V8 that can plausibly run inside `wasm32-wasip1`: **cppgc**
(a.k.a. Oilpan), V8's standalone C++ garbage collector, plus a **V8
embedder-API facade** (`v8::Isolate`/`Context`/`FunctionTemplate`/
`ObjectTemplate`/`Object::Wrap`) on top of it whose actual JS execution runs
on [WASMBruja](../WASMBruja)'s existing quickjs-ng dependency. The goal is a
second codegen backend for `brujac` (alongside its existing quickjs
backend) that emits real V8-shaped C++ -- the same "API-compatible facade
over what actually runs on this target" pattern
[WASMSkia](../WASMSkia)/[WASMBlinker](../WASMBlinker) already use for
Skia/blink.mojom.

```
Web IDL (.bruja)  →  brujac  →  quickjs backend (existing)
                              →  V8 backend (planned) → WASMv8bindings (this repo)
                                   v8::ObjectTemplate/FunctionTemplate
                                            │ Object::Wrap/Unwrap (CppHeapPointerTable)
                                            ▼
                                   cppgc::GarbageCollected<T> impl objects
                                            │
                                   quickjs-ng actually runs the JS
```

## Why cppgc, and why not "compile V8"

Running actual V8 -- TurboFan/Maglev JIT, a multi-threaded concurrent
garbage collector, a JS interpreter built around executable machine-code
pages -- inside a wasm32-wasip1 sandbox is not a porting problem, it's a
different architecture. wasm32-wasip1 is single-threaded, has no writable
JIT pages, no OS virtual-memory reservations, and no growable/scannable
native stack.

cppgc is the one part of V8 explicitly designed to be usable standalone,
independent of V8's isolate/JS-execution machinery (see the real project's
own description, vendored at
`third_party/v8/include/cppgc/README.md`: "Oilpan is an open-source garbage
collection library for C++ that can be used stand-alone..."). Its own
documented feature list includes **precise GC** ("Allows for collection
with and without considering stack") as a first-class supported mode, not a
hack -- which is exactly what removes the one blocker that looked fatal at
first (no portable conservative stack scan on WASM). `go++`'s own
`src/runtime.hpp` "Oilpan-lite" already reached the same conclusion
independently, hand-writing a small always-precise GC matching cppgc's
public API shape; this repo does the same job as a real strip-port of
upstream's actual source instead of a from-scratch rewrite.

## Layout

```
include/cppgc/            cppgc's public API -- real V8 headers where
                           genuinely portable (macros.h, garbage-collected.h,
                           type-traits.h, trace-trait.h, internal/gc-info.h,
                           platform.h, common.h, sentinel-pointer.h,
                           name-provider.h, source-location.h, copied
                           verbatim); custom-written where the real file
                           pulls in machinery this port doesn't have
                           (member.h, persistent.h, visitor.h, allocation.h,
                           heap.h, heap-handle.h, internal/member-storage.h,
                           internal/pointer-policies.h,
                           internal/persistent-node.h -- each file's header
                           comment says exactly what changed and why).
include/v8*.h, v8.h        The V8 embedder-API facade's public headers --
                           real V8 header names/paths (v8.h, v8-isolate.h,
                           v8-context.h, v8-local-handle.h, v8-value.h,
                           v8-primitive.h, v8-object.h, v8-function.h,
                           v8-function-callback.h, v8-property-callback.h,
                           v8-template.h, v8-script.h, v8-exception.h,
                           v8-global.h, v8-maybe.h, v8-sandbox.h,
                           v8-platform.h, v8config.h, v8-source-location.h)
                           so real-V8-shaped embedder code reads the same
                           against this facade. Each file's header comment
                           says what's simplified vs. real V8's own version.
src/base/                  Minimal src/base/* stand-ins (logging, bit-field,
                           mutex, lazy-instance, bits, macros) -- just
                           enough for the ported files below to compile.
src/heap/internal/         cppgc's real internals: heap-object-header.h
                           (near-verbatim strip-port of the real bit-packed
                           header), gc-info-table.h/.cc (near-verbatim),
                           gc-info.cc (verbatim), free-list.h/.cc
                           (near-verbatim bucketed freelist), platform.cc,
                           normal-page.h (this port's own page type, real
                           kPageSize/bump-allocation shape, see its file
                           comment for why it's a separate object rather
                           than embedding its header in the payload like
                           real cppgc), marking-visitor.h + heap.h/.cc
                           (this port's own atomic mark-sweep + allocator
                           driver, wiring the pieces above together -- not
                           upstream code, see heap.h's file comment).
src/sandbox/               CppHeapPointerTable -- the indirect-handle/
                           tag-range mechanism Object::Wrap()/Unwrap() use
                           to tie a JS wrapper object to a cppgc-managed C++
                           object. See its own file comment for what's real
                           vs. simplified (no segmented OS-reserved memory,
                           no table-level compaction; the type-confusion
                           protection itself is fully real).
src/v8/                    The facade's implementation (isolate.cc,
                           context.cc, object.cc, template.cc, function.cc,
                           script.cc, primitive.cc), backed by quickjs-ng
                           (FetchContent'd in CMakeLists.txt, same
                           dependency and pinned version WASMBruja's golden
                           tests already use).
third_party/v8/             Spec-only reference copies of the real upstream
                           V8 source this port is strip-ported from or
                           consulted against -- never compiled, same
                           convention as WASMExtWrench's
                           third_party/chromium/. Includes the full public
                           include/cppgc/ tree, the internal
                           src/heap/cppgc-internal/ files this port draws
                           from, and src/sandbox/{cppheap-pointer-table,
                           trusted-pointer-table,...} that src/sandbox/'s
                           own (simplified) implementation was designed
                           against.
tests/cppgc_gc_test.cc     Golden test: a real mark-sweep cycle -- reachable
                           chain survives, an unreachable tail is actually
                           finalized and freed, a WeakMember<T> edge is
                           cleared once its target is otherwise
                           unreachable, dropping the last root collects
                           everything.
tests/cppgc_allocator_test.cc  White-box test (reaches into
                           src/heap/internal/heap.h directly, not just the
                           public API) proving the real page/freelist
                           allocator actually spans multiple pages under
                           load, actually destroys a page that ends up
                           fully empty, and actually reuses that freed
                           space for a subsequent allocation batch instead
                           of growing unboundedly.
tests/cppheap_pointer_table_test.cc  Golden test: exact-tag and tag-range
                           lookups succeed, a type-confused access returns a
                           clean nullptr (never the raw pointer), freed
                           entries are reused via the freelist.
tests/v8_facade_console_test.cc  Golden test tying the whole stack
                           together: a real cppgc-managed C++ `Console`
                           object is wrapped into a JS object via
                           `Object::Wrap`; JS source (actually run by
                           quickjs-ng via `Script::Run`) calls
                           `console.log(...)`, which `Object::Unwrap`s back
                           to the real C++ object and records the message --
                           proving genuine end-to-end JS-to-C++ dispatch,
                           not just wiring that nothing exercises.
tests/v8_facade_accessor_test.cc  Golden test for
                           `ObjectTemplate::SetAccessor`: a readonly
                           attribute reads the live C++ value and genuinely
                           rejects assignment in strict mode (a real
                           TypeError), a read-write attribute's setter
                           actually mutates the wrapped C++ object.
tests/v8_facade_constructor_test.cc  Golden test for `FunctionTemplate` as
                           a `new`-able constructor: `new Point(3, 4)`
                           yields a real `instanceof Point` object with its
                           own cppgc-managed C++ `Point` allocated and
                           wrapped during construction, readonly attributes
                           and a method installed on the shared
                           `PrototypeTemplate` resolve correctly per
                           instance, two instances stay independently
                           backed, and calling the constructor without
                           `new` is rejected.
tests/v8_facade_trycatch_test.cc  Golden test for `TryCatch`: a thrown
                           `Error` and a thrown plain value are both
                           captured with their real content, a nested
                           `TryCatch` doesn't leak into its outer scope, a
                           throwing script with no `TryCatch` active still
                           just fails cleanly (unchanged pre-`TryCatch`
                           behavior), and a non-throwing script inside a
                           `TryCatch` reports nothing caught.
tests/v8_facade_global_test.cc  Golden test for `Global<T>`: a JS function
                           stored as a `Global` genuinely survives past the
                           `Local<Function>` and the scope that created it,
                           and is still callable later -- the shape a real
                           `addEventListener` needs.
tests/v8_facade_inherit_test.cc  Golden test for
                           `FunctionTemplate::Inherit`: `new Element()` is
                           both `instanceof Element` and `instanceof Node`,
                           an inherited prototype method resolves, an
                           Element-only method doesn't leak onto a plain
                           Node, and static-property inheritance
                           (`Object.getPrototypeOf(Element) === Node`)
                           holds -- see "A second real bug this hardening
                           pass found" below for what this test caught.
examples/cppgc_hello/      Minimal cppgc usage, the shape of real cppgc's
                           own samples/cppgc/hello-world.cc.
examples/v8_hello/         Minimal facade usage: create an isolate/context,
                           run a JS source string, print the result.
```

## What's real vs. simplified

| Area | Status |
|---|---|
| `HeapObjectHeader` bit layout (GCInfoIndex/mark-bit/in-construction encoding) | Near-verbatim strip-port of the real file. `CPPGC_CAGED_HEAP`'s inline `next_unfinalized_` field is the only thing cut (dead code here -- see "Cages, compressed pointers, TPT" below) |
| `GCInfoTable`/`GCInfoTrait`/`FinalizerTrait`/`NameTrait`/`TraceTrait` (the real trace/finalize/name dispatch mechanism, one registered `GCInfo` per type) | Real, near-verbatim. `GCInfoFolding` (an optimization that shares one `GCInfo` across a hierarchy with identical trace/finalize behavior) is not ported -- every concrete type gets its own entry; correct, just not memory-optimal |
| `GarbageCollected<T>`, `MakeGarbageCollected<T>()`, `Trace()` dispatch | Real, matching upstream's actual mechanism (allocate header, placement-new T, mark fully-constructed) |
| `Member<T>`/`WeakMember<T>`/`Persistent<T>`/`WeakPersistent<T>` | Real semantics (strong/weak edges, weak clearing during a dedicated post-mark pass), custom-written against a trimmed `BasicMember`/`BasicPersistent` rather than upstream's ~700-line files (which also cover cross-thread and tagged/compressed variants) |
| GC algorithm | **Atomic, non-incremental, non-concurrent, non-compacting, precise-only** mark-sweep -- one of cppgc's own documented supported configurations (see `third_party/v8/include/cppgc/README.md`), not an invented mode. No conservative stack scan: every live object must be reachable through a `Persistent<T>` root or a traced `Member<T>` edge, exactly like `go++`'s own Oilpan-lite already requires |
| Object allocator | **Now real**: a bump-pointer linear allocation buffer over a `NormalPage` (fixed `kPageSize`, matching real cppgc), refilled from a real bucketed `FreeList` (near-verbatim strip-port of `free-list.cc`) or a freshly allocated page, exactly matching real cppgc's `ObjectAllocator::AllocateObjectOnSpace` fast path. One flat large-object list (not real cppgc's several size-classed `NormalPageSpace`s or a distinct `LargePage` type) -- see `heap.h`'s file comment. `tests/cppgc_allocator_test.cc` proves multi-page allocation, empty-page reclamation, and freed-space reuse actually happen, not just compile |
| `GarbageCollectedMixin` (multiple-inheritance mixins) | **Not supported** -- `TraceTraitFromInnerAddressImpl::GetTraceDescriptor` (needed to recover a mixin's true object start from an inner pointer) is declared but not defined; using a mixin fails to *link*, loudly, rather than misbehaving silently. Real support needs the page/`ObjectStartBitmap` machinery this port's allocator deliberately skipped |
| Custom spaces, `iterable<T>`-style ephemerons, custom weak callbacks | Not ported (no `HeapOptions::custom_spaces`, no `Visitor::Trace(const EphemeronPair&)`) |
| Heap instances | **Exactly one live `Heap` at a time** -- `Heap::Create()` `CHECK`-fails on a second concurrent instance. `Persistent<T>`/`WeakPersistent<T>` rely on this to resolve "which heap's `PersistentRegion`" without upstream's `BasePage`-derived-from-pointer lookup (see `persistent.h`'s file comment) |
| Cross-thread persistents | Not ported -- single-threaded target, see `persistent-node.h`'s file comment |
| `CppHeapPointerTable` (JS↔C++ handle indirection, see below) | Real tag-range type-confusion protection; now built on [WASMSafeSpace](../WASMSafeSpace)'s `v8::internal::ExternalEntityTable<Entry>` (the same shared base real V8's `TrustedPointerTable`/`CodePointerTable`/`ExternalPointerTable`/`CppHeapPointerTable` all derive from) instead of a standalone freelist -- entries still live in a plain `std::vector` rather than segmented OS-reserved memory, and entry lifetime is still caller-managed (`FreeEntry`, wired to quickjs's own wrapper-object finalizer) rather than reclaimed by a table-level mark pass (`SweepAndCompact`) -- see `src/sandbox/cppheap-pointer-table.h`'s file comment for why that's sufficient here. `Compact()` is real: it trims a *trailing* run of freed entries off the backing vector (bounding memory growth, the actual property `SweepAndCompact` provides for this port's usage pattern) without needing a mark phase, since liveness is always already known from the freelist -- it deliberately never relocates a still-live entry to close an earlier hole, proven by `cppheap_pointer_table_test.cc`'s hole-vs-trailing-run cases |
| V8 embedder-API facade (`Isolate`/`Context`/`Local<T>`/`FunctionTemplate`/`ObjectTemplate`/`Object::Wrap`/`Script`/`TryCatch`) | Real JS-to-C++ dispatch, proven end to end by `tests/v8_facade_console_test.cc`. `Local<T>` is a self-sufficient RAII handle backed by quickjs's own JSValue refcounting rather than a real V8 `HandleScope` slot-block allocator (`HandleScope` is still present and should still be used, for API-shape fidelity and as a hook for a future non-refcounted representation -- see `v8-local-handle.h`). `Script::Compile`/`Run` don't have a real separate compile step (`Run` re-parses via `JS_Eval` every call). `ObjectTemplate::SetAccessor` (getter/setter properties) is real, including genuine readonly-property strict-mode-throw semantics -- see `tests/v8_facade_accessor_test.cc`. `FunctionTemplate` as a `new`-able constructor (`PrototypeTemplate()`/`InstanceTemplate()`) is real -- genuine `instanceof`, shared-prototype method/accessor resolution, a fresh cppgc object per instance, non-`new` calls rejected -- see `tests/v8_facade_constructor_test.cc` and template.cc's file comment for the quickjs-ng mechanism (`JS_CFUNC_constructor_magic` + `new_target`) it needed, different from every other callback here. `TryCatch` is real -- every JS-exception-discarding call site in the facade (`Script::Run`, `Function::Call`, `FunctionTemplate::GetFunction`, `ObjectTemplate`'s member installation, `String::NewFromUtf8`) now reports through it instead, captured value and all, proven by `tests/v8_facade_trycatch_test.cc` including nested `TryCatch` scoping and the no-`TryCatch`-active fallback. Real V8's `Message()`/`StackTrace()` (structured location info) aren't ported -- only the raw thrown value. `Global<T>` is real -- a JS value genuinely outlives the scope (and the `Local<T>`) that created it, proven by `tests/v8_facade_global_test.cc` storing and later invoking a JS function the way an event-listener registration would; underneath it's the same refcounted `Local<T>` wearing real V8's move-only API shape, not a second handle mechanism (see `v8-global.h`) -- `SetWeak()` isn't ported. `FunctionTemplate::Inherit` is real -- genuine ES6-class-extends-shaped prototype-chain inheritance (`instanceof` walks multiple levels, an inherited method resolves, a subclass-only method doesn't leak to the base, static-property inheritance via `Object.getPrototypeOf(Child) === Parent`), proven by `tests/v8_facade_inherit_test.cc`; getting it right also surfaced and fixed a real bug in `GetFunction()` (see below). **Not ported**: microtasks; `Persistent<T>`'s pre-`Global<T>` weak-callback API (superseded by `Global<T>` in real modern V8 too); `Number`/`Array`/`Promise`/`Symbol`/`BigInt`; snapshots |

## Cages, compressed pointers, and the trusted-pointer table

V8's real `CPPGC_CAGED_HEAP`/`CPPGC_POINTER_COMPRESSION` reserve a large,
alignment-fixed virtual-memory region via the OS (`src/heap/cppgc-internal/
caged-heap.cc`) so a 64-bit heap pointer can be truncated to a 32-bit
cage-relative offset. `CppHeapPointerTable` goes further: it indirects
references to cppgc objects through a tagged, bounds-checked handle table,
the same "external entity table" pattern V8's sandbox uses for its
trusted-pointer table (TPT) -- defense in depth against a forged/corrupted
pointer.

wasm32-wasip1 has no OS virtual-memory reservation to build a real cage
from, but linear memory is itself already a single, bounded, 32-bit address
space -- in a real sense wasm32 is *already* "caged." That's why
`include/cppgc/internal/member-storage.h`'s `CompressedPointer` here is a
real (if degenerate, shift=0) instance of the same algorithm, not a stub:
see that file's header comment.

**Now built**: `src/sandbox/cppheap-pointer-table.h`/`.cc` implements the
indirection layer (a real, tagged, bounds-checked handle table -- since the
[WASMSafeSpace](../WASMSafeSpace) wiring below, built on that repo's
`ExternalEntityTable<Entry>`/`TaggedPayload<Scheme>` rather than a
standalone freelist), and `v8-object.h`'s `Object::Wrap()`/
`Object::Unwrap<T>()` use it to tie a JS wrapper object to a cppgc-managed
C++ object, exactly the mechanism real V8 uses for `v8::Object::Wrap()`
when pointer compression is on. `include/v8-sandbox.h` carries the
tag/tag-range types (`CppHeapPointerTag`, `CppHeapPointerTagRange`), and
`internal::TagRange<Tag>` itself now comes from WASMSafeSpace's
`include/v8-internal.h` (see "Sandbox composition" below) rather than being
defined a second time here -- including the real post-order tag-assignment
algorithm for type hierarchies (see `v8-sandbox.h`'s comment) -- which a
future `brujac` V8 backend can compute from the exact same descendant-set
information its `resolver.cc` already builds today for its quickjs
backend's `Node*`-descendant argument checks.

## Sandbox composition (WASMSafeSpace)

[WASMSafeSpace](../WASMSafeSpace)'s `Sandbox` (the cage) and
`ExternalEntityTable<Entry>` (the shared table base) are now real sibling
dependencies (`WV8_WSS_DIR` in `CMakeLists.txt`), not just a described-but-
unwired composition:

- `CppHeapPointerTable` derives from `v8::internal::ExternalEntityTable<CppHeapPointerTableEntry>`
  instead of its own ad hoc `std::vector`+freelist -- same shape as that
  repo's own `TrustedPointerTable`, same bit-layout widths (15-bit tag |
  1 mark bit | 48-bit payload) since `CppHeapPointerTag` is also a 15-bit-
  bounded `uint16_t` enum.
- `src/heap/internal/normal-page.h`'s `NormalPage::Create()` takes a
  `v8::internal::Sandbox*` (`Sandbox::current()` at its one call site in
  `heap.cc`) and, when one is active, allocates its `kPageSize` payload via
  `Sandbox::Allocate()` instead of a plain `new[]` -- so cppgc-managed
  objects genuinely live inside the cage, while `CppHeapPointerTable`'s own
  storage (a plain `std::vector`, inherited from `ExternalEntityTable`)
  stays outside it, exactly real V8's actual shape. `tests/cppgc_sandbox_test.cc`
  proves every object a multi-page allocation batch produces satisfies
  `Sandbox::Contains()`, including after a GC cycle reclaims most of them.
  No `Sandbox` is required (`Sandbox::current()` defaults to `nullptr`,
  preserving the exact prior behavior) -- every pre-existing test still
  constructs a `Heap` with no sandbox involved at all.
- `include/v8-sandbox.h`'s `TagRange<Tag>` is no longer defined here; it's
  included from WASMSafeSpace's `include/v8-internal.h` (matching real V8's
  own file layout, where `TagRange` lives in `v8-internal.h` too).

Known tradeoff: `Sandbox::Allocate()` has no `Free()`, so a sandbox-backed
`NormalPage`'s bytes are never reclaimed even once the page is destroyed as
empty during GC -- the same "no table-level reclamation, only caller-managed
freeing" simplification already documented for `CppHeapPointerTable`/
`ExternalEntityTable` elsewhere in this repo and in WASMSafeSpace. It also
`CHECK`-fails (hard abort) on cage exhaustion rather than returning null,
matching real V8's own treatment of cage exhaustion as fatal but different
from this port's prior always-graceful `new(std::nothrow)` path. Large
objects (`AllocateLarge`) are **not** routed through the Sandbox -- left as
a follow-up, consistent with that path already being a documented
simplification (one flat list, not real cppgc's `LargePage`).

## Universal package for the WASM\* browser stack

This repo is the **only** place sibling projects should obtain a JS
runtime. Pull `wasmv8_facade` via a guarded `add_subdirectory` (see
WASMExtWrench / WASMCadidumKernel / WASMSkia / WASMThunker /
WASMCadidumBindings). Do not FetchContent quickjs-ng in those repos.

## Build (native, Windows)

Requires a sibling [WASMSafeSpace](../WASMSafeSpace) checkout (`../WASMSafeSpace`
relative to this repo) -- `wasmv8_sandbox` and `wasmv8_cppgc` both link its
`wasmsafe_sandbox` target now (see "Sandbox composition" above); CMake
`FATAL_ERROR`s with instructions if it's missing. `wasmv8_facade` (the V8 API
layer) `FetchContent`s quickjs-ng on first configure (same pinned version
WASMBruja's own golden tests use) -- needs network access that first time.

```
cmake -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

```
build/cppgc_example.exe
build/v8_example.exe
```

## WASI (wasm32-wasip1) -- the actual target this repo exists for

```
cmake -B build-wasi -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/wasi-sdk.cmake -DWASI_SDK_PATH=<path to wasi-sdk>
cmake --build build-wasi --target wasmv8_everything
wasmtime run build-wasi/v8_facade_console_test.wasm
```

(`WASI_SDK_PATH` defaults to `%USERPROFILE%/wasi-sdk` / `$WASI_SDK_PATH` if unset. `--target wasmv8_everything` builds exactly this repo's own libraries/tests/examples, not quickjs-ng's own CLI/dev tools -- see `CMakeLists.txt`'s comment on why those are `EXCLUDE_FROM_ALL`: `run-test262` specifically doesn't even compile under wasm32-wasip1, no WASI thread primitives for its worker-agent support, a real quickjs-ng gap unrelated to this repo.)

Every test and example in this repo has been run for real under `wasmtime`
on wasm32-wasip1, not just compiled -- `cppgc_gc_test`, `cppgc_allocator_test`
(the real page allocator, multi-page, under WASI's own memory model),
`cppheap_pointer_table_test`, `cppgc_sandbox_test` (every object across a
multi-page allocation batch, and after a GC cycle, still satisfies a real
`Sandbox::Contains()`), and every `v8_facade_*_test` (console,
accessor, constructor, trycatch, global, inherit) -- the entire chain,
cppgc through the V8 facade through quickjs-ng actually executing JS
source, genuinely working on the target platform this whole project is
for, not assumed to. Re-run after each subsequent addition to this repo,
not just once at the start.

Getting there needed one real fix, already applied in
`cmake/wasi-sdk.cmake`: plain `clang++ --target=wasm32-wasip1` resolves
`<string>`/`<atomic>`/etc. against a broken mix of wasi-sdk 34 (LLVM 23)'s
no-exceptions ("noeh") and full libc++ include directories and defaults to
`-fexceptions` even though noeh `libc++abi` has no `__cxa_throw`.
[`~/go++`](../go++) (`docs/design-log.md`, "Compile to wasm") already
diagnosed this exact issue for its own C++-to-wasm pipeline; the fix
reused here verbatim is `-fno-exceptions -nostdinc++` plus explicit
`-isystem` flags adding back only noeh libc++ then wasi-libc, in that
order. (Also needed: an explicit `#include <string>` in `cppgc/platform.h`
that happened to be masked by transitive includes on the native build but
not this stricter one -- a real, if minor, gap the WASI build itself
surfaced.)

## Roadmap

The "genuinely optional polish" items from the previous milestone --
table-level reclamation for `CppHeapPointerTable`, `Global<T>`, and
`FunctionTemplate::Inherit` -- are now done (see the status table above).
What's left:

1. **Done, full DOM-shaped grammar parity.** `brujac` (WASMBruja) now has a
   second backend, `cpp_generator_v8.cc`, alongside its original
   `cpp_generator.cc` (quickjs) one -- `--backend=v8` emits
   `ObjectTemplate`/`FunctionTemplate`/`Object::Wrap`/`Unwrap` calls against
   this facade directly. What started as a narrow slice (flat interfaces,
   methods/attributes/consts, scalar + DOMString/USVString types -- exactly
   WASMv16's four real `.bruja` files: console/navigator/document/processes)
   has grown to full parity with the quickjs backend's grammar: interface
   inheritance, interface-typed/nullable values, `interface mixin`/
   `includes`, dictionaries (incl. inheritance), `callback`, `any`,
   `sequence<T>`, unions, `enum`, variadic/optional params, `Promise<T>`,
   and `constructor`. Runtime tag allocation grew alongside it from a single
   per-interface tag (`AllocateCppHeapPointerTag()`) to a real, contiguous
   per-hierarchy tag-range allocator (`AllocateCppHeapPointerTagRange(count)`
   in `include/v8-sandbox.h`), assigned via a pre-order walk of each
   `.bruja` file's interface tree -- the same range-assignment algorithm
   real V8 uses, so a tag-range probe on `Unwrap<Base>()` safely accepts any
   subtype pointer. Proven end to end against a real `v8::Isolate` by ten
   golden tests, including `dom_v8_roundtrip.cc` -- the full 32-interface,
   5-level-deep `examples/dom/dom.bruja` tree (the same one the quickjs
   backend's `dom_roundtrip.cc` exercises), reusing `dom_impl.h`'s real impl
   classes almost verbatim. Only variadic constructor parameters remain
   unsupported. See WASMBruja/README.md's "What's supported" section and
   `cpp_generator_v8.cc`'s file comment.
2. `Symbol`/`BigInt`, `TryCatch::Message()`/`StackTrace()` (structured error
   location), microtasks as a first-class facade API (today `sequence<T>`
   and `Promise<T>` in the V8-backend codegen, and WASMv16's own Promise/job
   handling, reach past the facade into raw quickjs as a documented escape
   hatch -- see WASMBruja/README.md).
3. Size-classed `NormalPageSpace`s (real cppgc uses several, bucketed by
   object size, to cut fragmentation) instead of this port's one implicit
   normal space -- a pure density optimization, not a correctness gap.

## A second real bug this hardening pass found

Implementing `FunctionTemplate::Inherit` surfaced a real bug in
`GetFunction()`: it rebuilt a brand-new JS function (and a brand-new
prototype object) on *every* call, rather than being idempotent per
`Context` the way real V8's is. That's invisible for a template only ever
materialized once, but `Inherit()` calls `parent->GetFunction(context)`
internally to fetch the parent's prototype -- so a child template calling
`GetFunction()` after the caller had already separately called
`parent->GetFunction()` (the ordinary way to expose a constructor to
script) silently built a *second*, disconnected copy of the parent
constructor. The child's prototype chain pointed at that orphaned copy's
prototype object, not the one the caller actually exposed as `window.Node`
(or whatever), so `instanceof` against the real, exposed constructor
quietly returned `false`. Fixed by caching the built function (keyed on
`Context`) on the `FunctionTemplate` itself, freed in its now-added
destructor. `tests/v8_facade_inherit_test.cc`'s `instanceof` assertions
are exactly what caught this.

## How constructor support works (a real quickjs-ng wrinkle, not guesswork)

`FunctionTemplate` as a `new`-able constructor (`interface X { constructor(...); }`
in brujac's own IDL -- see its DOM example) needs a JS-visible constructor
function whose `new`-constructed instances (a) inherit `X.prototype` (for
`instanceof`/method lookup) and (b) belong to this facade's own wrappable
quickjs class (so `Object::Wrap` can attach a cppgc object to them).
Empirically probing quickjs-ng v0.16.2 against the actual built library
(not just reading headers) found that `JS_NewCClosure` -- the
opaque-data-carrying closure mechanism every *other* callback in this
facade uses (`FunctionTemplate::GetFunction`'s plain-function path,
`ObjectTemplate::SetAccessor`) -- does *not* propagate `new_target.prototype`
to the auto-allocated `this_val` the way a plain JS function/class does:
`this_val`'s prototype inside a `JS_NewCClosure` constructor call is not
`Ctor.prototype`, breaking both `instanceof` and prototype-chain method
lookup, and its class id isn't this facade's wrapper class either (so
`Object::Wrap` fails outright even ignoring the prototype problem).

The actual fix, in `template.cc`: `JS_NewCFunction3` with
`JS_CFUNC_constructor_magic`, whose callback signature takes `new_target`
directly (`JSValue (*)(JSContext*, JSValueConst new_target, int argc,
JSValueConst* argv, int magic)`) instead of `this_val` -- reading
`new_target.prototype` via `JS_GetPropertyStr` sidesteps the broken
`this_val` entirely, and building the instance with
`JS_NewObjectProtoClass(ctx, that_prototype, wrapper_class_id)` gives it
both the right prototype and the right (wrappable) class before the user's
`FunctionCallback` ever runs. That API has no `void* opaque` slot, so
`ConstructorRegistry` in `template.cc` carries each constructor's
`{Isolate*, FunctionCallback, wrappable}` in a small process-lifetime side
table indexed by the integer `magic` parameter instead of a heap-allocated
closure -- the one callback shape in this facade that isn't
`JS_NewCClosure`-based, for this specific, verified reason. `tests/v8_facade_constructor_test.cc`
proves the whole chain: real `instanceof`, shared-prototype method/accessor
resolution, independently backed instances, and non-`new` calls rejected.

## A real bug this hardening pass found

`tests/cppgc_allocator_test.cc` (many objects across several pages, most
becoming unreachable, then a fresh allocation batch reusing that space) is
what real cppgc's own conformance testing would call a stress test, and it
caught a real one: `SweepNormalPage` was adding a page's coalesced
dead/free byte ranges straight into `free_list_` *during* the sweep walk,
before the caller had decided whether that page survives. For a page that
turned out to be fully empty, those freelist entries pointed into memory
`NormalPage::Destroy` then immediately `delete[]`'d -- a real
use-after-free, reliably reachable at multi-page scale (confirmed with
Windows' own heap-corruption detector via a debugger backtrace, not just
inferred). The fix: `SweepNormalPage` now stages a page's free runs into a
caller-provided list and returns live-byte count; `CollectGarbage` only
actually adds those runs to `free_list_` for a page that survives,
discarding them (along with the page) otherwise. Mentioned here rather
than silently squashed because it's the kind of bug an allocator strip-port
is exactly supposed to catch by being exercised for real, not just
compiled.

## License

New code is BSD-3-Clause. Files under `third_party/v8/` remain Copyright
the V8 project authors, also BSD-3-Clause.
