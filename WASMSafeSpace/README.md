# WASMSafeSpace

A strip-port of real V8 source -- not a clean-room rewrite, same convention
as [WASMv8bindings](../WASMv8bindings) -- targeting V8's **Sandbox**: the
security boundary sometimes called "V8 Cages," a large in-process memory
region that most V8 heap objects and buffers live inside, combined with
tag-checked indirection tables (trusted pointers, code pointers, external
pointers) that let objects inside the cage safely reference things outside
it. The premise, straight from real V8's own doc comment (vendored at
`third_party/v8/src/sandbox/sandbox.h`): assume an attacker can, via a V8
memory-safety bug, corrupt memory anywhere inside the sandbox arbitrarily;
the sandbox's job is to stop that corruption from reaching anything
*outside* it, or from being reinterpreted as the wrong type of object.

This repo implements that mechanism -- the cage itself, cage-relative
compressed pointers, and the three real tag-checked indirection tables --
against `wasm32-wasip1`, [WASMv8bindings](../WASMv8bindings)'s own target.
Where that repo strip-ports cppgc (V8's garbage collector) and an embedder
API facade, this one strip-ports the other half of "what makes V8 a
sandbox": the memory-safety boundary itself.

## Why this is a *second* boundary, not *the* boundary

wasm32-wasip1 has no OS virtual-memory reservation primitive to call (no
`mmap`/`VirtualAlloc` with `PROT_NONE` guard pages) -- a module's entire
linear memory already *is* a single, flat, bounded 32-bit address space,
and it's wasmtime's own sandboxing that stops that linear memory from
reaching anything outside the host process. That's a real, already-provided
boundary, but it's a different one from what `Sandbox` here provides: this
repo's cage is a second, narrower boundary purely for defense-in-depth
against a bug *inside* this port's own object model -- exactly the same
threat real V8's Sandbox defends against (V8 already runs inside the OS's
process boundary too; the Sandbox exists because *that* boundary is too
coarse to stop an in-process V8 bug from corrupting the rest of the
process). See `src/sandbox/sandbox.h`'s file comment for the full
reasoning, including why this port deliberately uses the same
heap-allocation-backed implementation on native builds too, rather than
forking in real OS guard-page reservation behind a native-only `#ifdef`.

## Layout

```
include/v8-internal.h      Address typedef, KB/MB/GB constants, and
                            TagRange<Tag> (verbatim from real V8's
                            include/v8-internal.h) shared by every tag type
                            below.
include/v8-sandbox.h       Not present as a separate embedder-facing file
                            here (unlike WASMv8bindings' CppHeapPointerTag) --
                            this repo's tag types live in src/sandbox/ since
                            nothing here exposes an embedder API surface;
                            see each table header for its tag type.
include/v8config.h         Same minimal V8_INLINE/V8_EXPORT_PRIVATE/
                            V8_LIKELY stand-in as WASMv8bindings' copy (this
                            is a static lib, no component-build boundary).
src/base/                  logging.h (CHECK/DCHECK), bits.h (IsPowerOfTwo) --
                            copied unchanged from WASMv8bindings' own
                            src/base/, same minimal stand-ins.
src/sandbox/
  sandbox.h / .cc           The cage itself: Initialize/TearDown, base()/
                            end()/size()/Contains(), a bump allocator for
                            objects that must genuinely live inside the
                            cage, and the single-active-sandbox current()/
                            set_current() real V8 also maintains.
  sandboxed-pointer.h        SandboxedPointer_t: a cage-relative compressed
                            pointer. Real algorithm (offset from base,
                            decode = base + offset), adapted so decoding is
                            a *total* function that lands inside the cage
                            for literally every possible input -- see its
                            file comment for why that's actually a stronger
                            guarantee than upstream's shift-only scheme for
                            this port's guard-region-less setup.
  tagged-payload.h            Near-verbatim strip-port of real V8's generic
                            {tag, mark bit, payload} single-word packing --
                            pure bit arithmetic, needed almost no
                            simplification. Shared by all three tables
                            below via a templated tagging-scheme parameter,
                            exactly like upstream.
  external-entity-table.h     This port's shared freelist/allocation base
                            (custom-written, real *shape*: one real V8 base
                            class, factored out once instead of copy-pasted
                            three times, matching upstream's own
                            ExternalEntityTable). See file comment for what
                            upstream's segmented-OS-reservation machinery
                            this drops in favor of a plain
                            std::vector<Entry> + freelist.
  indirect-pointer-tag.h       Trimmed strip-port of real V8's tag enum and
                            fast-tag bit-math helpers (verbatim where pure
                            arithmetic); V8-object-model-specific tags and
                            InstanceType switch dropped, replaced with a
                            small self-contained tag set.
  trusted-pointer-table.h/.cc  TrustedPointerTable: tag-checked indirection
                            to trusted objects living outside the cage.
  code-pointer-table.h/.cc     CodePointerTable: control-flow-integrity
                            indirection to callable entrypoints -- ported
                            from V8 tag 12.0.267 (upstream later replaced
                            this mechanism's role for JS functions with
                            JSDispatchTable "leaptiering"; this simpler,
                            earlier design is what's implemented here).
  external-pointer-table.h/.cc ExternalPointerTable: tag-checked indirection
                            to raw external (non-V8-object) pointers, same
                            mechanism as TrustedPointerTable with an
                            independently-chosen bit layout and tag space.
  indirect-pointer-table.h/.cc IndirectPointerTable (IPT): a new table in
                            this family, not a strip-port of anything
                            upstream -- real V8 has no equivalent, since it
                            never needs to reference guest physical memory
                            inside a hardware-virtualized partition. Same
                            ExternalEntityTable/TaggedPayload mechanism as
                            TrustedPointerTable/ExternalPointerTable, its
                            own independently-chosen bit layout and a
                            GuestPointerTag catalog, but indirecting to a
                            uint64_t guest physical address rather than a
                            host pointer -- what
                            ../WASMTurboSpace/src/hv/hv-partition.cc uses
                            to reference every guest physical address its
                            own code touches (the GDT/IDT tables, IDT
                            stubs, exception handler, ...) instead of raw
                            constants. See that repo's own README ("Real
                            hypervisor-backed execution") for the actual
                            consumer.
third_party/v8/             Reference copies of the real upstream V8 source
                            this port is strip-ported from -- never
                            compiled, same convention as WASMv8bindings'
                            third_party/v8/. Includes real
                            external-entity-table.h, indirect-pointer-tag.h,
                            indirect-pointer.h, trusted-pointer-table.h/.cc
                            (shared with that repo's own vendoring), plus
                            this repo's own additions: sandbox.h/.cc,
                            sandboxed-pointer.h/-inl.h, tagged-payload.h,
                            external-pointer-table.h/.cc/-inl.h,
                            external-pointer.h/-inl.h,
                            code-pointer-table.h/.cc/-inl.h (from v8 tag
                            12.0.267), and include/v8-sandbox.h.
tests/sandbox_test.cc              Golden test: Contains() is exact (true
                            inside including the boundary byte, false for
                            end() itself and anything beyond), the bump
                            allocator only ever hands out in-cage,
                            correctly-aligned memory, two independent
                            sandboxes have disjoint ranges, TearDown()
                            actually invalidates containment.
tests/sandboxed_pointer_test.cc    Golden test: genuine round-trip for
                            real in-cage addresses, and -- the actual
                            property -- every possible 32-bit encoded
                            value, including adversarial ones like
                            0xFFFFFFFF, decodes to an address still
                            satisfying Contains().
tests/trusted_pointer_table_test.cc  Golden test: exact-tag and tag-range
                            lookups succeed, a type-confused access returns
                            a clean null (never the real pointer), freed
                            entries are reused via the freelist, Compact()
                            trims only a genuine trailing run.
tests/code_pointer_table_test.cc     Golden test: an entrypoint recovered
                            from the table is genuinely callable (real
                            indirect call through a real function
                            pointer), SetEntrypoint retargets an entry the
                            way a tiering compiler would, corrupted/
                            out-of-range handles yield a safe null.
tests/external_pointer_table_test.cc Golden test: same shape as the trusted-
                            pointer-table test, applied to raw external
                            pointers.
tests/sandbox_integration_test.cc    Golden test tying the whole cage
                            together: a real in-cage "ArrayBuffer" backing
                            store reached through a SandboxedPointer, real
                            "trusted metadata" living genuinely outside the
                            cage reached through a TrustedPointerTable
                            handle, a real callable "detach handler"
                            reached through a CodePointerTable handle --
                            then proves an attacker confined to corrupting
                            sandboxed memory cannot use a fully-forged
                            SandboxedPointer to reach outside the cage, and
                            cannot use a forged/wrong-tagged handle to
                            reinterpret one object as another type.
examples/sandbox_hello/     Minimal usage: reserve a cage, allocate inside
                            it, wrap an outside-the-cage pointer in a
                            tag-checked handle, show a wrong-tag access
                            rejected.
```

## What's real vs. simplified

| Area | Status |
|---|---|
| `Sandbox::Contains()`/`base()`/`end()`/`size()` | Real, exact containment check -- not approximate. What's cut: OS virtual-memory reservation and guard regions (nothing on wasm32-wasip1 to reserve them from -- see "Why this is a *second* boundary" above), the partially-reserved-sandbox fallback, multi-cage support. One heap allocation stands in for the reservation, on every build target including native. |
| `TaggedPayload<Scheme>` ({tag, mark bit, payload} single-word packing) | Near-verbatim strip-port -- pure bit arithmetic, no OS dependency, so almost nothing needed to change. Widened to a fixed `uint64_t` regardless of target pointer width (wasm32's 32-bit `Address` has no spare bits above a pointer's significant bits the way a 64-bit host's does) -- see `tagged-payload.h`'s file comment. |
| `TrustedPointerTable` | Real tag-range type-confusion protection (same `kTrustedPointerTable*` bit-layout constants as real V8, copied verbatim in `indirect-pointer-tag.h`). Entries live in a plain `std::vector` with a freelist rather than segmented OS-reserved memory; `Compact()` trims a *trailing* run of freed entries (no mark phase needed, liveness is always already known from the freelist) rather than real `SweepAndCompact`'s segment-level relocation -- same simplification, same reasoning, as [WASMv8bindings](../WASMv8bindings)'s `CppHeapPointerTable`, which this repo's `ExternalEntityTable<Entry>` base generalizes. |
| `CodePointerTable` | Real control-flow-integrity property: an entrypoint recovered through a handle can only ever be one this program itself legitimately registered, proven by `code_pointer_table_test.cc` performing a genuine indirect call. Real, documented scope preserved unmodified from upstream: this stops redirection to forged/injected code, but a corrupted-yet-in-bounds handle can still select a *different, still-legitimate* entry -- that's what CPT-based CFI actually promises upstream too, not a gap introduced here. Entry layout doesn't rely on Code-object pointer alignment to steal a mark bit from the LSB (this port's `code_object_` has no such alignment guarantee) -- see `code-pointer-table.h`'s file comment. |
| `ExternalPointerTable` | Same tag-checked-indirection mechanism as `TrustedPointerTable`, independently-chosen bit layout and a self-contained example tag set standing in for upstream's much larger real tag catalog (tied to V8-specific embedder resource types this port doesn't have). |
| `IndirectPointerTable` | Real, same `ExternalEntityTable`/`TaggedPayload` mechanism as `TrustedPointerTable`/`ExternalPointerTable` (own independently-chosen bit layout, `indirect_pointer_table_test.cc` proving the same exact-tag/tag-range/type-confusion/freelist/`Compact()` properties), but not a strip-port of anything upstream -- a genuinely new table this family needed and real V8 has no equivalent of, since V8 never indirects to a *guest physical address* the way `../WASMTurboSpace/src/hv/hv-partition.cc` needs to for its virtual-processor-backed execution. See `indirect-pointer-table.h`'s file comment. |
| `SandboxedPointer` | Real algorithm (`offset = pointer - base`, `decode = base + offset`). This port's encode/decode wrap the offset modulo the cage's actual size instead of upstream's shift-plus-huge-guard-regions scheme -- for this port's guard-region-less cage, that makes `DecodeSandboxedPointer` a genuinely *total* function (proven for adversarial inputs by `sandboxed_pointer_test.cc`), which is the real-world property a sandboxed pointer is supposed to guarantee, rather than "true as long as the guard regions are big enough." |
| `IndirectPointerTag` | Bit layout and fast-tag helpers (`IsFastIndirectPointerTag` etc.) copied verbatim -- pure bit arithmetic. V8-object-model-specific tag catalog and `IndirectPointerTagFromInstanceType()` (switches on real V8's `InstanceType`) dropped; replaced with a small self-contained tag set, same reasoning as the CppHeapPointerTag catalog trim in WASMv8bindings. |
| `ExternalEntityTable<Entry>` | Custom-written (this port's own factoring, not upstream's exact segmented-table code) but real *shape*: one shared base class for all three tables, matching upstream's actual architecture (real V8's `TrustedPointerTable`/`CodePointerTable`/`ExternalPointerTable`/`CppHeapPointerTable` all derive from the same real `ExternalEntityTable`). |

## Build (native, Windows)

No sibling checkouts or network access required (unlike WASMv8bindings'
`wasmv8_facade`, nothing here `FetchContent`s a dependency).

```
cmake -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

```
build/sandbox_example.exe
```

## WASI (wasm32-wasip1) -- the actual target this repo exists for

```
cmake -B build-wasi -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/wasi-sdk.cmake -DWASI_SDK_PATH=<path to wasi-sdk>
cmake --build build-wasi --target wasmsafe_everything
wasmtime run build-wasi/sandbox_test.wasm
```

(`WASI_SDK_PATH` defaults to `%USERPROFILE%/wasi-sdk` / `$WASI_SDK_PATH` if
unset. The wasi-sdk 34/LLVM 23 noeh/libc++ fix in `cmake/wasi-sdk.cmake` is
reused verbatim from WASMv8bindings' own toolchain file -- see its comment
for the full explanation.)

Every test and example in this repo has been run for real under `wasmtime`
on wasm32-wasip1, not just compiled -- `sandbox_test`,
`sandboxed_pointer_test`, `trusted_pointer_table_test`,
`code_pointer_table_test`, `external_pointer_table_test`,
`sandbox_integration_test`, and `sandbox_example`. Re-run after each
subsequent addition to this repo, not just once at the start.

## A real bug this actually running under wasmtime found

`Sandbox::Allocate()`'s bump allocator originally aligned only the running
*offset* (`(bump_offset_ + alignment - 1) & ~(alignment - 1)`) rather than
the resulting *absolute* address. That's only correct if the cage's own
base address happens to already satisfy the requested alignment -- which
it did, by pure coincidence, under the native build's `malloc`, so
`sandbox_test.cc`'s 64-byte-alignment assertion passed there. It failed
immediately under `wasmtime`'s own allocator, whose `base()` wasn't
64-aligned: `reinterpret_cast<uintptr_t>(c) % 64 == 0` tripped, a genuine
`assert()` failure caught only by actually executing the compiled `.wasm`
binary, not by compiling it. Fixed by aligning `base_addr + bump_offset_`
(the real absolute address the caller receives) and deriving the offset
from that, in `sandbox.cc`. Mentioned here rather than silently squashed
because it's exactly the kind of bug this repo's own "actually run under
wasmtime, not just compiled" testing discipline (inherited from
WASMv8bindings) exists to catch.

## Roadmap

1. `ExternalPointerTable`'s tag catalog and `CodePointerTable`'s entry
   layout are both intentionally minimal placeholders (see the status
   table) -- an embedder integrating this port for real would extend the
   tag catalogs the same way real V8's own are extended per subsystem.
2. **Done**: this port's tables are wired into [WASMv8bindings](../WASMv8bindings)'s
   cppgc/facade layer -- `Object::Wrap()`'s `CppHeapPointerTable` now
   derives from this repo's `ExternalEntityTable<Entry>` base instead of its
   own ad hoc freelist, and that repo's `NormalPage` allocations get a real
   `Sandbox` to live inside via `Sandbox::Allocate()` when one is active
   (`Sandbox::current()`) -- so the two repos' "V8 Cages" and "cppgc" halves
   compose the way real V8's actually do. See
   [WASMv8bindings' README](../WASMv8bindings/README.md#sandbox-composition-wasmsafespace)
   for the wiring detail and `WASMv8bindings/tests/cppgc_sandbox_test.cc`
   for the golden test (every object in a multi-page allocation batch, and
   after a GC cycle, satisfies `Sandbox::Contains()`). This repo's own tests
   are unaffected -- `ExternalEntityTable`/`Sandbox` gained a consumer, no
   behavior changed here.
3. Table-level compaction beyond a trailing-run trim (real `SweepAndCompact`
   relocates still-live entries to close interior holes) -- not attempted
   here, same reasoning as WASMv8bindings' `CppHeapPointerTable::Compact()`.

## License

New code is BSD-3-Clause. Files under `third_party/v8/` remain Copyright
the V8 project authors, also BSD-3-Clause.
