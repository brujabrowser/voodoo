// JS bindings for mojo::SimpleWatcher. First tier uses a pollable fire
// counter (no JS Function callback yet) plus mojo.runUntilIdle() so the
// golden test can drive whp::Executor the same way the C++ roundtrip does.
#include "wck/bindings/simple_watcher_bindings.h"

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include "mojo/public/c/system/core.h"
#include "mojo/public/cpp/system/message_pipe.h"
#include "mojo/public/cpp/system/simple_watcher.h"
#include "wck/bindings/runtime.h"
#include "whp/base/executor.h"

namespace wck_bindings {
namespace {

using v8::FunctionCallbackInfo;
using v8::FunctionTemplate;
using v8::Isolate;
using v8::Local;
using v8::Number;
using v8::Object;
using v8::Value;

struct WatcherSlot {
  std::unique_ptr<mojo::SimpleWatcher> watcher;
  int fired = 0;
  MojoResult last_result = MOJO_RESULT_UNKNOWN;
  // Kept so Automatic policy can drain READABLE inside the C++ callback
  // (mirrors simple_watcher_roundtrip.cc) without needing a JS Function.
  MojoHandle watched = MOJO_HANDLE_INVALID;
  bool auto_drain = false;
};

std::unordered_map<int, WatcherSlot>& Table() {
  static std::unordered_map<int, WatcherSlot> table;
  return table;
}

int next_id = 1;

MojoHandle ArgHandle(const FunctionCallbackInfo<Value>& info, int i) {
  return static_cast<MojoHandle>(ArgUInt32(info, i, MOJO_HANDLE_INVALID));
}

void SetResult(const FunctionCallbackInfo<Value>& info, MojoResult r) {
  info.GetReturnValue().Set(static_cast<double>(r));
}

WatcherSlot* Lookup(int id) {
  auto it = Table().find(id);
  if (it == Table().end()) return nullptr;
  return &it->second;
}

void JsCreateWatcher(const FunctionCallbackInfo<Value>& info) {
  int policy_arg = ArgInt(info, 0, 0);  // 0 = automatic, 1 = manual
  auto policy = policy_arg == 0 ? mojo::SimpleWatcher::ArmingPolicy::kAutomatic
                                : mojo::SimpleWatcher::ArmingPolicy::kManual;
  int id = next_id++;
  WatcherSlot slot;
  slot.watcher = std::make_unique<mojo::SimpleWatcher>(policy);
  slot.auto_drain = (policy == mojo::SimpleWatcher::ArmingPolicy::kAutomatic);
  Table().emplace(id, std::move(slot));
  info.GetReturnValue().Set(static_cast<double>(id));
}

void JsWatch(const FunctionCallbackInfo<Value>& info) {
  int id = ArgInt(info, 0, 0);
  MojoHandle handle = ArgHandle(info, 1);
  MojoHandleSignals signals =
      static_cast<MojoHandleSignals>(ArgInt(info, 2, MOJO_HANDLE_SIGNAL_READABLE));
  WatcherSlot* slot = Lookup(id);
  if (!slot || !slot->watcher) {
    SetResult(info, MOJO_RESULT_INVALID_ARGUMENT);
    return;
  }
  slot->watched = handle;
  slot->fired = 0;
  slot->last_result = MOJO_RESULT_UNKNOWN;
  MojoResult r = slot->watcher->Watch(
      mojo::Handle(handle), signals, [id](MojoResult result) {
        WatcherSlot* s = Lookup(id);
        if (!s) return;
        ++s->fired;
        s->last_result = result;
        if (s->auto_drain && s->watched != MOJO_HANDLE_INVALID) {
          std::vector<uint8_t> payload;
          std::vector<MojoHandle> handles;
          mojo::ReadMessageRaw(mojo::MessagePipeHandle(s->watched), &payload,
                               &handles, MOJO_READ_MESSAGE_FLAG_NONE);
          for (MojoHandle h : handles) {
            if (h != MOJO_HANDLE_INVALID) MojoClose(h);
          }
        }
      });
  SetResult(info, r);
}

void JsArm(const FunctionCallbackInfo<Value>& info) {
  Isolate* isolate = info.GetIsolate();
  int id = ArgInt(info, 0, 0);
  WatcherSlot* slot = Lookup(id);
  Local<Object> out = Object::New(isolate);
  if (!slot || !slot->watcher) {
    out->Set(isolate, "result",
             Number::New(isolate, static_cast<double>(MOJO_RESULT_INVALID_ARGUMENT)));
    out->Set(isolate, "ready",
             Number::New(isolate, static_cast<double>(MOJO_RESULT_UNKNOWN)));
    info.GetReturnValue().Set(out);
    return;
  }
  MojoResult ready = MOJO_RESULT_UNKNOWN;
  MojoResult r = slot->watcher->Arm(&ready);
  out->Set(isolate, "result", Number::New(isolate, static_cast<double>(r)));
  out->Set(isolate, "ready", Number::New(isolate, static_cast<double>(ready)));
  info.GetReturnValue().Set(out);
}

void JsCancelWatcher(const FunctionCallbackInfo<Value>& info) {
  int id = ArgInt(info, 0, 0);
  WatcherSlot* slot = Lookup(id);
  if (!slot || !slot->watcher) {
    SetResult(info, MOJO_RESULT_INVALID_ARGUMENT);
    return;
  }
  slot->watcher->Cancel();
  SetResult(info, MOJO_RESULT_OK);
}

void JsDestroyWatcher(const FunctionCallbackInfo<Value>& info) {
  int id = ArgInt(info, 0, 0);
  Table().erase(id);
  SetResult(info, MOJO_RESULT_OK);
}

void JsWatcherState(const FunctionCallbackInfo<Value>& info) {
  Isolate* isolate = info.GetIsolate();
  int id = ArgInt(info, 0, 0);
  WatcherSlot* slot = Lookup(id);
  Local<Object> out = Object::New(isolate);
  if (!slot) {
    out->Set(isolate, "fired", Number::New(isolate, -1));
    out->Set(isolate, "lastResult",
             Number::New(isolate, static_cast<double>(MOJO_RESULT_UNKNOWN)));
  } else {
    out->Set(isolate, "fired",
             Number::New(isolate, static_cast<double>(slot->fired)));
    out->Set(isolate, "lastResult",
             Number::New(isolate, static_cast<double>(slot->last_result)));
  }
  info.GetReturnValue().Set(out);
}

void JsRunUntilIdle(const FunctionCallbackInfo<Value>& info) {
  whp::Executor::Current().RunUntilIdle();
  SetResult(info, MOJO_RESULT_OK);
}

void InstallFn(Isolate* isolate, Local<v8::Context> context, Local<Object> ns,
               const char* name,
               void (*callback)(const FunctionCallbackInfo<Value>&)) {
  Local<FunctionTemplate> tmpl = FunctionTemplate::New(isolate, callback);
  ns->Set(isolate, name, tmpl->GetFunction(context).ToLocalChecked());
}

}  // namespace

void InstallSimpleWatcherBindings(Isolate* isolate,
                                  Local<v8::Context> context) {
  Local<Object> global = context->Global();
  Local<Object> mojo = GetOrCreateNamespace(isolate, global, "mojo");

  InstallFn(isolate, context, mojo, "createWatcher", JsCreateWatcher);
  InstallFn(isolate, context, mojo, "watch", JsWatch);
  InstallFn(isolate, context, mojo, "armWatcher", JsArm);
  InstallFn(isolate, context, mojo, "cancelWatcher", JsCancelWatcher);
  InstallFn(isolate, context, mojo, "destroyWatcher", JsDestroyWatcher);
  InstallFn(isolate, context, mojo, "watcherState", JsWatcherState);
  InstallFn(isolate, context, mojo, "runUntilIdle", JsRunUntilIdle);

  mojo->Set(isolate, "RESULT_OK",
            Number::New(isolate, static_cast<double>(MOJO_RESULT_OK)));
  mojo->Set(isolate, "RESULT_FAILED_PRECONDITION",
            Number::New(isolate,
                        static_cast<double>(MOJO_RESULT_FAILED_PRECONDITION)));
  mojo->Set(isolate, "HANDLE_INVALID",
            Number::New(isolate, static_cast<double>(MOJO_HANDLE_INVALID)));
  mojo->Set(isolate, "SIGNAL_READABLE",
            Number::New(isolate,
                        static_cast<double>(MOJO_HANDLE_SIGNAL_READABLE)));
  mojo->Set(isolate, "ARMING_AUTOMATIC", Number::New(isolate, 0));
  mojo->Set(isolate, "ARMING_MANUAL", Number::New(isolate, 1));
}

}  // namespace wck_bindings
