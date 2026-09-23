// Simplified from V8's include/v8-function.h -- just Call(). Real V8 also
// has NewInstance() (construct via `new`), SetName(), GetName(); not ported.
#ifndef WASMV8_INCLUDE_V8_FUNCTION_H_
#define WASMV8_INCLUDE_V8_FUNCTION_H_

#include "quickjs.h"
#include "v8-context.h"
#include "v8-local-handle.h"
#include "v8-maybe.h"
#include "v8-object.h"
#include "v8-value.h"

namespace v8 {

class Function final : public Object {
 public:
  Function() = default;
  Function(JSContext* ctx, JSValue val) : Object(ctx, val) {}

  MaybeLocal<Value> Call(Local<Context> context, Local<Value> recv, int argc,
                        Local<Value> argv[]) const;
};

}  // namespace v8

#endif  // WASMV8_INCLUDE_V8_FUNCTION_H_
