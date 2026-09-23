#ifndef WHP_JS_BINDINGS_RUNTIME_H_
#define WHP_JS_BINDINGS_RUNTIME_H_

#include <optional>
#include <string>

#include "v8.h"

namespace whp_js_bindings {

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

}  // namespace whp_js_bindings

#endif  // WHP_JS_BINDINGS_RUNTIME_H_
