// Shared helpers for every wck_bindings tier. Same shape as
 // WASMExtWrench's wew/bindings/runtime.h -- GetOrCreateNamespace so tiers
 // can install independently, plus no-throw Arg* extractors for the
 // WASMv8bindings FunctionCallback convention (no Isolate::ThrowException
 // yet; see WASMv8bindings/include/v8-exception.h).
#ifndef WCK_BINDINGS_RUNTIME_H_
#define WCK_BINDINGS_RUNTIME_H_

#include <cstdint>
#include <optional>
#include <string>

#include "v8.h"

namespace wck_bindings {

// Returns `parent[name]` if it's already an object, otherwise creates a
// fresh plain object, sets `parent[name] = <it>`, and returns that.
v8::Local<v8::Object> GetOrCreateNamespace(v8::Isolate* isolate,
                                           v8::Local<v8::Object> parent,
                                           const char* name);

double ArgNumber(const v8::FunctionCallbackInfo<v8::Value>& info, int i,
                 double default_value = 0);
int ArgInt(const v8::FunctionCallbackInfo<v8::Value>& info, int i,
           int default_value = 0);
// Prefer this for MojoHandle / any uint32 opaque id -- ArgInt truncates
// invitation handles (base 0xF0000000) via signed int overflow UB.
uint32_t ArgUInt32(const v8::FunctionCallbackInfo<v8::Value>& info, int i,
                   uint32_t default_value = 0);
std::optional<std::string> ArgOptString(
    v8::Isolate* isolate, const v8::FunctionCallbackInfo<v8::Value>& info,
    int i);
std::string ArgString(v8::Isolate* isolate,
                      const v8::FunctionCallbackInfo<v8::Value>& info, int i);

}  // namespace wck_bindings

#endif  // WCK_BINDINGS_RUNTIME_H_
