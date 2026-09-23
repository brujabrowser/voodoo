#include "v8-script.h"

#include <memory>

#include "quickjs.h"
#include "v8-context.h"
#include "v8-exception.h"

namespace v8 {

MaybeLocal<Script> Script::Compile(Local<Context> context, Local<String> source) {
  String::Utf8Value utf8(context->GetIsolate(), source);
  auto impl = std::make_shared<Script>();
  impl->source_.assign(*utf8, static_cast<size_t>(utf8.length()));
  return Local<Script>::Adopt(std::move(impl));
}

MaybeLocal<Value> Script::Run(Local<Context> context) {
  JSContext* ctx = context.context_for_wasmv8_internal();
  JSValue result = JS_Eval(ctx, source_.c_str(), source_.size(), "<script>",
                          JS_EVAL_TYPE_GLOBAL);
  if (JS_IsException(result)) {
    TryCatch::ReportException_for_wasmv8_internal(ctx);
    return MaybeLocal<Value>();
  }
  return Local<Value>::Adopt(ctx, result);
}

}  // namespace v8
