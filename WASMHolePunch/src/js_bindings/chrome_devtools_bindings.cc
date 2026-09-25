#include "whp/js_bindings/chrome_devtools_bindings.h"

#include <string>

#include "whp/js_bindings/runtime.h"

namespace whp_js_bindings {
namespace {

using v8::FunctionCallbackInfo;
using v8::FunctionTemplate;
using v8::Isolate;
using v8::Local;
using v8::Object;
using v8::String;
using v8::Value;

JSContext* g_worlds[6] = {};
int g_attached = -1;
bool g_trusted = false;
int g_next_breakpoint = 1;
std::string g_paused;

const char* kNames[] = {"page", "isolated", "extension", "mojojs", "internals", "terraformed"};

void InstallFn(Isolate* isolate, Local<v8::Context> context, Local<Object> ns,
               const char* name,
               void (*callback)(const FunctionCallbackInfo<Value>&)) {
  Local<FunctionTemplate> tmpl = FunctionTemplate::New(isolate, callback);
  ns->Set(isolate, name, tmpl->GetFunction(context).ToLocalChecked());
}

int FrameIndex(const std::string& id) {
  for (int i = 0; i < 6; ++i) {
    if (id == kNames[i]) return i;
  }
  return -1;
}

const char* TargetType(int i) {
  if (i == 2) return "background_page";
  return "page";
}

const char* TargetUrl(int i) {
  if (i == 0) return "about:blank";
  if (i == 2) return "chrome-extension://nkeimhogjdpnpccoofpliimaahmaaome/";
  if (i == 1) return "isolated://world";
  return "guest://";
}

void JsListTargets(const FunctionCallbackInfo<Value>& info) {
  info.GetReturnValue().Set(
      String::NewFromUtf8(info.GetIsolate(),
                          "page,isolated,extension,mojojs,internals,terraformed")
          .ToLocalChecked());
}

void JsAttach(const FunctionCallbackInfo<Value>& info) {
  Isolate* isolate = info.GetIsolate();
  int index = FrameIndex(ArgString(isolate, info, 0));
  g_attached = index;
  g_trusted = index >= 0;
  g_paused.clear();
  const char* name = index < 0 ? "missing" : kNames[index];
  info.GetReturnValue().Set(String::NewFromUtf8(isolate, name).ToLocalChecked());
}

void JsAttachSession(const FunctionCallbackInfo<Value>& info) {
  Isolate* isolate = info.GetIsolate();
  int index = FrameIndex(ArgString(isolate, info, 0));
  g_attached = index;
  Local<Value> flag = info[1];
  g_trusted = index >= 0 && flag->IsBoolean() && flag.As<v8::Boolean>()->Value();
  g_paused.clear();
  const char* name = index < 0 ? "missing" : (g_trusted ? "trusted" : "untrusted");
  info.GetReturnValue().Set(String::NewFromUtf8(isolate, name).ToLocalChecked());
}

void JsTrusted(const FunctionCallbackInfo<Value>& info) {
  info.GetReturnValue().Set(
      String::NewFromUtf8(info.GetIsolate(), g_trusted ? "true" : "false").ToLocalChecked());
}

void JsDispatch(const FunctionCallbackInfo<Value>& info) {
  Isolate* isolate = info.GetIsolate();
  if (g_attached < 0) {
    info.GetReturnValue().Set(String::NewFromUtf8(isolate, "detached").ToLocalChecked());
    return;
  }
  std::string method = ArgString(isolate, info, 0);
  if (method.rfind("Database.", 0) == 0 && !g_trusted) {
    info.GetReturnValue().Set(String::NewFromUtf8(isolate, "denied").ToLocalChecked());
    return;
  }
  info.GetReturnValue().Set(String::NewFromUtf8(isolate, "ok").ToLocalChecked());
}

void JsTarget(const FunctionCallbackInfo<Value>& info) {
  Isolate* isolate = info.GetIsolate();
  int index = FrameIndex(ArgString(isolate, info, 0));
  std::string text = "missing";
  if (index >= 0) {
    text = std::string(TargetType(index)) + " " + TargetUrl(index);
  }
  info.GetReturnValue().Set(String::NewFromUtf8(isolate, text.c_str()).ToLocalChecked());
}

void JsEvaluate(const FunctionCallbackInfo<Value>& info) {
  Isolate* isolate = info.GetIsolate();
  std::string expr = ArgString(isolate, info, 0);
  if (g_attached < 0 || g_worlds[g_attached] == nullptr) {
    info.GetReturnValue().Set(String::NewFromUtf8(isolate, "detached").ToLocalChecked());
    return;
  }
  if (expr.find("debugger") != std::string::npos) {
    g_paused = "debuggerStatement";
    info.GetReturnValue().Set(String::NewFromUtf8(isolate, "paused").ToLocalChecked());
    return;
  }
  Local<v8::Context> context(g_worlds[g_attached]);
  v8::Context::Scope scope(context);
  Local<String> src = String::NewFromUtf8(isolate, expr.c_str()).ToLocalChecked();
  Local<v8::Script> script = v8::Script::Compile(context, src).ToLocalChecked();
  Local<Value> result = script->Run(context).ToLocalChecked();
  v8::String::Utf8Value text(isolate, result);
  g_paused.clear();
  info.GetReturnValue().Set(String::NewFromUtf8(isolate, *text).ToLocalChecked());
}

void JsSetBreakpointByUrl(const FunctionCallbackInfo<Value>& info) {
  Isolate* isolate = info.GetIsolate();
  std::string id = std::to_string(g_next_breakpoint++);
  info.GetReturnValue().Set(String::NewFromUtf8(isolate, id.c_str()).ToLocalChecked());
}

void JsPaused(const FunctionCallbackInfo<Value>& info) {
  info.GetReturnValue().Set(
      String::NewFromUtf8(info.GetIsolate(), g_paused.c_str()).ToLocalChecked());
}

void JsResume(const FunctionCallbackInfo<Value>& info) {
  g_paused.clear();
  info.GetReturnValue().Set(String::NewFromUtf8(info.GetIsolate(), "resumed").ToLocalChecked());
}

}  // namespace

void RegisterDevToolsWorld(ChromeFrame frame, Local<v8::Context> context) {
  int index = static_cast<int>(frame);
  if (index >= 0 && index < 6) {
    g_worlds[index] = context.context_for_wasmv8_internal();
  }
}

void InstallDevTools(Isolate* isolate, Local<v8::Context> context) {
  Local<Object> ns = GetOrCreateNamespace(isolate, context->Global(), "DevTools");
  InstallFn(isolate, context, ns, "listTargets", JsListTargets);
  InstallFn(isolate, context, ns, "attach", JsAttach);
  InstallFn(isolate, context, ns, "attachSession", JsAttachSession);
  InstallFn(isolate, context, ns, "trusted", JsTrusted);
  InstallFn(isolate, context, ns, "dispatch", JsDispatch);
  InstallFn(isolate, context, ns, "target", JsTarget);
  InstallFn(isolate, context, ns, "evaluate", JsEvaluate);
  InstallFn(isolate, context, ns, "setBreakpointByUrl", JsSetBreakpointByUrl);
  InstallFn(isolate, context, ns, "paused", JsPaused);
  InstallFn(isolate, context, ns, "resume", JsResume);
}

}  // namespace whp_js_bindings
