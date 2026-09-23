#include "v8-exception.h"

namespace v8 {

namespace {
thread_local TryCatch* g_current_try_catch = nullptr;
}  // namespace

TryCatch::TryCatch(Isolate* isolate)
    : isolate_(isolate), previous_(g_current_try_catch) {
  g_current_try_catch = this;
}

TryCatch::~TryCatch() { g_current_try_catch = previous_; }

void TryCatch::ReportException_for_wasmv8_internal(JSContext* ctx) {
  JSValue exc = JS_GetException(ctx);
  if (g_current_try_catch) {
    g_current_try_catch->has_caught_ = true;
    g_current_try_catch->exception_ = Local<Value>::Adopt(ctx, exc);
  } else {
    JS_FreeValue(ctx, exc);
  }
}

}  // namespace v8
