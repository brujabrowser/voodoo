// JS bindings for the Mojo C System message-pipe surface -- create / write /
// read / close against real WASMThunker (and through it, whp::c). Exposed as
// `wst.*` on a v8::Context's global, running on the WASMv8bindings V8
// embedder-API facade. Same handle-as-number pattern as CadidumKernel's
// wck C++ tier: MojoHandle is already an opaque uint32_t, so there is no
// cppgc object to wrap. Namespace is `wst` (not `mojo`) to distinguish from
// CadidumKernel's C++ bindings when both are loaded in one isolate.
#ifndef WST_BINDINGS_MESSAGE_PIPE_BINDINGS_H_
#define WST_BINDINGS_MESSAGE_PIPE_BINDINGS_H_

#include "v8.h"

namespace wst_bindings {

// Installs createMessagePipe / writeMessage / readMessage / close and
// RESULT_OK / HANDLE_INVALID constants onto `wst` (created if missing).
void InstallMessagePipeBindings(v8::Isolate* isolate,
                                v8::Local<v8::Context> context);

}  // namespace wst_bindings

#endif  // WST_BINDINGS_MESSAGE_PIPE_BINDINGS_H_
