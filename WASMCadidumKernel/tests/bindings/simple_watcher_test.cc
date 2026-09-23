// Golden test for mojo.* SimpleWatcher bindings -- mirrors
// AutomaticWatcherFiresOnWrite from tests/system/simple_watcher_roundtrip.cc.
#include <cassert>
#include <cstdio>

#include "mojo/public/c/system/types.h"
#include "v8.h"
#include "wck/bindings/message_pipe_bindings.h"
#include "wck/bindings/simple_watcher_bindings.h"

namespace {

double RunNumber(v8::Local<v8::Context> context, const char* source) {
  v8::Local<v8::String> src =
      v8::String::NewFromUtf8(context->GetIsolate(), source).ToLocalChecked();
  v8::Local<v8::Script> script =
      v8::Script::Compile(context, src).ToLocalChecked();
  v8::Local<v8::Value> result = script->Run(context).ToLocalChecked();
  assert(result->IsNumber());
  return result.As<v8::Number>()->Value();
}

void Run(v8::Local<v8::Context> context, const char* source) {
  v8::Local<v8::String> src =
      v8::String::NewFromUtf8(context->GetIsolate(), source).ToLocalChecked();
  v8::Local<v8::Script> script =
      v8::Script::Compile(context, src).ToLocalChecked();
  script->Run(context).ToLocalChecked();
}

}  // namespace

int main() {
  v8::Isolate* isolate = v8::Isolate::New();
  {
    v8::Isolate::Scope isolate_scope(isolate);
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = v8::Context::New(isolate);
    v8::Context::Scope context_scope(context);

    wck_bindings::InstallMessagePipeBindings(isolate, context);
    wck_bindings::InstallSimpleWatcherBindings(isolate, context);

    Run(context, "globalThis.pipe = mojo.createMessagePipe()");
    assert(RunNumber(context, "pipe.result") == MOJO_RESULT_OK);

    Run(context, "globalThis.w = mojo.createWatcher(mojo.ARMING_AUTOMATIC)");
    assert(RunNumber(context, "w") > 0);
    assert(RunNumber(context,
                     "mojo.watch(w, pipe.handle1, mojo.SIGNAL_READABLE)") ==
           MOJO_RESULT_OK);

    Run(context, "globalThis.state0 = mojo.watcherState(w)");
    assert(RunNumber(context, "state0.fired") == 0);

    assert(RunNumber(context, "mojo.writeMessageRaw(pipe.handle0, 'x')") ==
           MOJO_RESULT_OK);
    assert(RunNumber(context, "mojo.runUntilIdle()") == MOJO_RESULT_OK);

    Run(context, "globalThis.state1 = mojo.watcherState(w)");
    assert(RunNumber(context, "state1.fired") == 1);
    assert(RunNumber(context, "state1.lastResult") == MOJO_RESULT_OK);

    assert(RunNumber(context, "mojo.destroyWatcher(w)") == MOJO_RESULT_OK);
    assert(RunNumber(context, "mojo.close(pipe.handle0)") == MOJO_RESULT_OK);
    assert(RunNumber(context, "mojo.close(pipe.handle1)") == MOJO_RESULT_OK);

    std::printf("wck_bindings_simple_watcher_test: ok\n");
  }
  isolate->Dispose();
  return 0;
}
