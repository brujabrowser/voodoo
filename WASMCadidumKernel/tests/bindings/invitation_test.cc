// Golden test for mojo.* invitation bindings -- mirrors
// tests/system/invitation_roundtrip.cc from JS on the WASMv8bindings facade.
#include <cassert>
#include <cstdio>

#include "mojo/public/c/system/types.h"
#include "v8.h"
#include "wck/bindings/invitation_bindings.h"
#include "wck/bindings/message_pipe_bindings.h"

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

bool RunBool(v8::Local<v8::Context> context, const char* source) {
  v8::Local<v8::String> src =
      v8::String::NewFromUtf8(context->GetIsolate(), source).ToLocalChecked();
  v8::Local<v8::Script> script =
      v8::Script::Compile(context, src).ToLocalChecked();
  v8::Local<v8::Value> result = script->Run(context).ToLocalChecked();
  assert(result->IsBoolean());
  return result.As<v8::Boolean>()->Value();
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

    wck_bindings::InstallInvitationBindings(isolate, context);
    wck_bindings::InstallMessagePipeBindings(isolate, context);

    Run(context, "globalThis.out = mojo.createInvitation()");
    assert(RunNumber(context, "out.result") == MOJO_RESULT_OK);
    assert(RunNumber(context, "out.handle") != MOJO_HANDLE_INVALID);

    Run(context,
        "globalThis.attached = mojo.attachMessagePipe(out.handle, 'conn')");
    assert(RunNumber(context, "attached.result") == MOJO_RESULT_OK);
    assert(RunNumber(context, "attached.handle") != MOJO_HANDLE_INVALID);

    assert(RunNumber(context, "mojo.sendInvitation(out.handle, 4242)") ==
           MOJO_RESULT_OK);

    Run(context, "globalThis.inc = mojo.acceptInvitation(4242)");
    assert(RunNumber(context, "inc.result") == MOJO_RESULT_OK);
    assert(RunNumber(context, "inc.handle") != MOJO_HANDLE_INVALID);

    Run(context,
        "globalThis.extracted = mojo.extractMessagePipe(inc.handle, 'conn')");
    assert(RunNumber(context, "extracted.result") == MOJO_RESULT_OK);
    assert(RunNumber(context, "extracted.handle") != MOJO_HANDLE_INVALID);

    assert(RunNumber(context,
                     "mojo.writeMessageRaw(attached.handle, 'hi')") ==
           MOJO_RESULT_OK);
    Run(context, "globalThis.msg = mojo.readMessageRaw(extracted.handle)");
    assert(RunNumber(context, "msg.result") == MOJO_RESULT_OK);
    assert(RunBool(context, "msg.payload === 'hi'"));

    assert(RunNumber(context, "mojo.close(attached.handle)") == MOJO_RESULT_OK);
    assert(RunNumber(context, "mojo.close(extracted.handle)") ==
           MOJO_RESULT_OK);
    assert(RunNumber(context, "mojo.close(inc.handle)") == MOJO_RESULT_OK);

    std::printf("wck_bindings_invitation_test: ok\n");
  }
  isolate->Dispose();
  return 0;
}
