// JS bindings for mojo/public/cpp/system/message_pipe.h -- create / write /
// read / close against real WASMCadidumKernel C++ (and through it, wst).
// Exposed as `mojo.*` on a v8::Context's global, running on the
 // WASMv8bindings V8 embedder-API facade. Same handle-as-number pattern as
 // WASMExtWrench's wew C-API tier: MojoHandle is already an opaque uint32_t,
 // so there is no cppgc object to wrap.
#ifndef WCK_BINDINGS_MESSAGE_PIPE_BINDINGS_H_
#define WCK_BINDINGS_MESSAGE_PIPE_BINDINGS_H_

#include "v8.h"

namespace wck_bindings {

// Installs createMessagePipe / writeMessageRaw / readMessageRaw / close and
 // RESULT_OK / HANDLE_INVALID constants onto `mojo` (created if missing).
void InstallMessagePipeBindings(v8::Isolate* isolate,
                                v8::Local<v8::Context> context);

}  // namespace wck_bindings

#endif  // WCK_BINDINGS_MESSAGE_PIPE_BINDINGS_H_
