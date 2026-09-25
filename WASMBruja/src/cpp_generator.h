// Turns a parsed (and Resolve()d -- see resolver.h) Module into one
// generated C++ header containing, per interface:
//
//  1. A pure-virtual C++ interface class (the implementation contract),
//     with PascalCase method names (Web IDL's lowerCamelCase "log" becomes
//     C++'s "Log", matching this codebase's existing style -- Echo::EchoString,
//     LocalFrame::Navigate), using real C++ inheritance for `interface X :
//     Y` bodies.
//  2. A `JSValue CreateXBinding(JSContext* ctx, X* impl)` function that
//     wraps a non-owning `X*` in a quickjs object exposing the interface's
//     *and all its ancestors'* methods/attributes/consts under their
//     original (lowerCamelCase) JS names, built via quickjs's own
//     JSCFunctionListEntry/JS_SetPropertyFunctionList idiom (the same
//     pattern quickjs-libc.c uses for its os/std modules). Each concrete
//     interface gets its own JSClassID, so inherited members are
//     regenerated (flattened) once per concrete interface rather than
//     shared -- see the "Binding shape" section of WASMBruja/README.md for
//     why (this generator has no dynamic most-derived-wrapper registry).
//
// `impl` is caller-owned and non-finalized: the generated binding doesn't
// take ownership or call delete on it. Still fully synchronous/callback-
// return-free at the C++ level even though the grammar now has `callback`
// types (e.g. EventTarget's addEventListener) -- invoking a stored JS
// callback is a plain `JS_Call`, no async plumbing. See WASMBruja/README.md.
//
// `any`-typed values follow one ownership rule everywhere they appear --
// as a plain parameter, an attribute setter, or a dictionary field (which
// is really just a parameter one level down, converted by the same
// EmitValueConversion): the JS->C++ conversion `JS_DupValue`s the
// incoming value once, handing the impl a reference it now owns and must
// free exactly once. Returning an `any` (EmitToJs's kAny case) is the
// mirror image: the impl's value is dup'd again on the way *out*, since
// that's a distinct new reference for the caller. See
// tests/golden/dom_roundtrip.cc's CustomEventImpl for this exercised
// through a dictionary field (CustomEventInit.detail) end to end.
#ifndef BRUJA_CPP_GENERATOR_H_
#define BRUJA_CPP_GENERATOR_H_

#include <string>

#include "ast.h"

namespace bruja {

std::string GenerateCppHeader(const Module& module,
                               const std::string& header_guard,
                               const std::string& cpp_namespace,
                               const std::string& source_filename);

}  // namespace bruja

#endif  // BRUJA_CPP_GENERATOR_H_
