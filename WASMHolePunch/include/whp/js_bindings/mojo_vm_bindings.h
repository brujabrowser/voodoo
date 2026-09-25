// mojovm.Call on the guest. The portfolio catalog is every method mojovm
// accepted. call() parses that method's .mojom and returns both ordinals.
#ifndef WHP_JS_BINDINGS_MOJO_VM_BINDINGS_H_
#define WHP_JS_BINDINGS_MOJO_VM_BINDINGS_H_

#include "v8.h"

namespace whp_js_bindings {

void InstallMojoVm(v8::Isolate* isolate, v8::Local<v8::Context> context);

}  // namespace whp_js_bindings

#endif  // WHP_JS_BINDINGS_MOJO_VM_BINDINGS_H_
