// JS bindings for mojo/public/cpp/system/buffer.h -- create / duplicate /
// mapWrite / mapRead against real WASMCadidumKernel shared buffers.
// Exposed as `mojo.*` on a v8::Context's global.
#ifndef WCK_BINDINGS_BUFFER_BINDINGS_H_
#define WCK_BINDINGS_BUFFER_BINDINGS_H_

#include "v8.h"

namespace wck_bindings {

void InstallBufferBindings(v8::Isolate* isolate,
                           v8::Local<v8::Context> context);

}  // namespace wck_bindings

#endif  // WCK_BINDINGS_BUFFER_BINDINGS_H_
