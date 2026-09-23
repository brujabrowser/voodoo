// JS bindings for mojo::SimpleWatcher. First tier uses a pollable fire
 // counter (no JS Function callback yet) plus mojo.runUntilIdle() so the
 // golden test can drive whp::Executor the same way the C++ roundtrip does.
#ifndef WCK_BINDINGS_SIMPLE_WATCHER_BINDINGS_H_
#define WCK_BINDINGS_SIMPLE_WATCHER_BINDINGS_H_

#include "v8.h"

namespace wck_bindings {

void InstallSimpleWatcherBindings(v8::Isolate* isolate,
                                  v8::Local<v8::Context> context);

}  // namespace wck_bindings

#endif  // WCK_BINDINGS_SIMPLE_WATCHER_BINDINGS_H_
