#include "v8-function.h"

#include <vector>

#include "quickjs.h"
#include "v8-exception.h"

namespace v8 {

MaybeLocal<Value> Function::Call(Local<Context> context, Local<Value> recv,
                                 int argc, Local<Value> argv[]) const {
  JSContext* ctx = context.context_for_wasmv8_internal();
  std::vector<JSValue> args;
  args.reserve(static_cast<size_t>(argc));
  for (int i = 0; i < argc; ++i) {
    args.push_back(argv[i].value_for_wasmv8_internal());
  }
  JSValue result =
      JS_Call(ctx, val_, recv.value_for_wasmv8_internal(), argc, args.data());
  if (JS_IsException(result)) {
    TryCatch::ReportException_for_wasmv8_internal(ctx);
    return MaybeLocal<Value>();
  }
  return Local<Value>::Adopt(ctx, result);
}

}  // namespace v8
