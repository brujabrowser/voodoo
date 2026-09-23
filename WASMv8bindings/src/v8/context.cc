#include "v8-context.h"

#include "src/base/logging.h"
#include "v8-isolate.h"
#include "v8-object.h"

namespace v8 {

namespace {
thread_local JSContext* g_current_context = nullptr;
}  // namespace

Local<Context> Context::New(Isolate* isolate) {
  JSContext* ctx = JS_NewContext(isolate->runtime_for_wasmv8_internal());
  CHECK(ctx);
  isolate->TrackContext_for_wasmv8_internal(ctx);
  return Local<Context>(ctx);
}

Isolate* Context::GetIsolate() const {
  JSRuntime* rt = JS_GetRuntime(ctx_);
  return static_cast<Isolate*>(JS_GetRuntimeOpaque(rt));
}

Local<Object> Context::Global() const {
  JSValue global = JS_GetGlobalObject(ctx_);
  return Local<Object>::Adopt(ctx_, global);
}

Context::Scope::Scope(Local<Context> context) : previous_(g_current_context) {
  g_current_context = context.context_for_wasmv8_internal();
}

Context::Scope::~Scope() { g_current_context = previous_; }

JSContext* CurrentContext(Isolate*) { return g_current_context; }

}  // namespace v8
