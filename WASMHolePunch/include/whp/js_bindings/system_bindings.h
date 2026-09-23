#ifndef WHP_JS_BINDINGS_SYSTEM_BINDINGS_H_
#define WHP_JS_BINDINGS_SYSTEM_BINDINGS_H_

#include "v8.h"

namespace whp_js_bindings {

void InstallSystemBindings(v8::Isolate* isolate,
                           v8::Local<v8::Context> context);

}  // namespace whp_js_bindings

#endif  // WHP_JS_BINDINGS_SYSTEM_BINDINGS_H_
