// Shared helpers for every wst_bindings tier. Same shape as
// WASMCadidumKernel's wck/bindings/runtime.h -- GetOrCreateNamespace so
// tiers can install independently, plus no-throw Arg* extractors for the
// WASMv8bindings FunctionCallback convention (no Isolate::ThrowException
// yet; see WASMv8bindings/include/v8-exception.h).
#ifndef WST_BINDINGS_RUNTIME_H_
#define WST_BINDINGS_RUNTIME_H_

#include <optional>
#include <string>

#include "v8.h"

namespace wst_bindings {

// Returns `parent[name]` if it's already an object, otherwise creates a
// fresh plain object, sets `parent[name] = <it>`, and returns that.
v8::Local<v8::Object> GetOrCreateNamespace(v8::Isolate* isolate,
                                           v8::Local<v8::Object> parent,
                                           const char* name);

double ArgNumber(const v8::FunctionCallbackInfo<v8::Value>& info, int i,
                 double default_value = 0);
int ArgInt(const v8::FunctionCallbackInfo<v8::Value>& info, int i,
           int default_value = 0);
std::optional<std::string> ArgOptString(
    v8::Isolate* isolate, const v8::FunctionCallbackInfo<v8::Value>& info,
    int i);
std::string ArgString(v8::Isolate* isolate,
                      const v8::FunctionCallbackInfo<v8::Value>& info, int i);

}  // namespace wst_bindings

#endif  // WST_BINDINGS_RUNTIME_H_
