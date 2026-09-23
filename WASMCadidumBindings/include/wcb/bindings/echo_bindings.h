// JS bindings for the hand-written Echo interface
// (examples/echo/echo_interface.h) -- proves Remote<T>/Receiver<T>
// request/response over a real pipe, driven from JS on the WASMv8bindings
// facade. Same no-throw convention as WASMExtWrench's C-API tier.
#ifndef WCB_BINDINGS_ECHO_BINDINGS_H_
#define WCB_BINDINGS_ECHO_BINDINGS_H_

#include "v8.h"

namespace wcb_bindings {

// Installs `wcb.echo(string) -> string` (identity round-trip through a real
// mojo::Remote/Receiver pair) onto the context's global `wcb` object.
void InstallEchoBindings(v8::Isolate* isolate, v8::Local<v8::Context> context);

}  // namespace wcb_bindings

#endif  // WCB_BINDINGS_ECHO_BINDINGS_H_
