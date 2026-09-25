// MojoJS bootstrap on the HolePunch pipe. createMessagePipe and
// bindInterface match the public MojoJS names. The reply codec needs
// v8::Promise and an ArrayBuffer embedder type, which this facade does
// not have.
#ifndef WHP_JS_BINDINGS_MOJO_JS_BINDINGS_H_
#define WHP_JS_BINDINGS_MOJO_JS_BINDINGS_H_

#include "v8.h"

namespace whp_js_bindings {

void InstallMojoJs(v8::Isolate* isolate, v8::Local<v8::Context> context);

}  // namespace whp_js_bindings

#endif  // WHP_JS_BINDINGS_MOJO_JS_BINDINGS_H_
