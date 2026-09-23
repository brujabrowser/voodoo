# Chromium reference sources

These files are **spec only**. They document the shape of the real Mojo C
System embedder API and are **not compiled** into WASMThunker.

| File | Chromium path |
|---|---|
| `thunks.cc` | `mojo/public/c/system/thunks.cc` |

`src/thunk.cc` is our own implementation of the same `Mojo*` C ABI, wired
directly to WASMHolePunch's `whp::c` layer instead of a swappable
`MojoSystemThunks2` vtable -- see its header comment for what maps 1:1 and
what is `MOJO_RESULT_UNIMPLEMENTED`.

Copyright The Chromium Authors. BSD-style license as in the Chromium tree.
