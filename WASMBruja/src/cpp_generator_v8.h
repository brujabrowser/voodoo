// Second brujac codegen backend: emits C++ bindings against the
// WASMv8bindings V8 embedder-API facade (v8::ObjectTemplate/FunctionTemplate,
// v8::Object::Wrap/Unwrap via CppHeapPointerTable) instead of
// cpp_generator.h's raw quickjs JSClassID/JSCFunctionListEntry idiom -- see
// cpp_generator_v8.cc's file comment for exactly which subset of the .bruja
// grammar this backend currently covers (methods/attributes/consts on
// non-inheriting interfaces, scalar + DOMString/USVString types only) and
// why the rest throws a clear "not yet supported" error rather than
// silently mis-generating.
#ifndef BRUJA_CPP_GENERATOR_V8_H_
#define BRUJA_CPP_GENERATOR_V8_H_

#include <string>

#include "ast.h"

namespace bruja {

// Same contract as GenerateCppHeader (cpp_generator.h): parse+Resolve()
// the module first, then call this. Throws std::runtime_error (caught by
// main.cc the same way LexError/ParseError/ResolveError already are) if the
// module uses a construct this backend doesn't support yet.
std::string GenerateV8CppHeader(const Module& module,
                                 const std::string& header_guard,
                                 const std::string& cpp_namespace,
                                 const std::string& source_filename);

}  // namespace bruja

#endif  // BRUJA_CPP_GENERATOR_V8_H_
