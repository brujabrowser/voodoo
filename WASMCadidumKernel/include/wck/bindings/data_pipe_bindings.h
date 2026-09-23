// JS bindings for mojo/public/cpp/system/data_pipe.h -- create / write /
// read / close against real WASMCadidumKernel C++ (and through it, wst).
// Exposed as `mojo.*` on a v8::Context's global, running on the
// WASMv8bindings V8 embedder-API facade. Same handle-as-number pattern as
// message_pipe_bindings.h.
#ifndef WCK_BINDINGS_DATA_PIPE_BINDINGS_H_
#define WCK_BINDINGS_DATA_PIPE_BINDINGS_H_

#include "v8.h"

namespace wck_bindings {

// Installs createDataPipe / writeData / readData / close and
// RESULT_OK / HANDLE_INVALID constants onto `mojo` (created if missing).
void InstallDataPipeBindings(v8::Isolate* isolate,
                             v8::Local<v8::Context> context);

}  // namespace wck_bindings

#endif  // WCK_BINDINGS_DATA_PIPE_BINDINGS_H_
