#include "whp/js_bindings/chrome_bindings.h"
#include "whp/js_bindings/chrome_devtools_bindings.h"
#include "whp/js_bindings/mojo_js_bindings.h"
#include "whp/js_bindings/mojo_vm_bindings.h"

#include <string>

#include "whp/js_bindings/runtime.h"

namespace whp_js_bindings {
namespace {

JSContext* g_isolated = nullptr;

using v8::FunctionCallbackInfo;
using v8::FunctionTemplate;
using v8::Isolate;
using v8::Local;
using v8::Object;
using v8::String;
using v8::Value;

void InstallFn(Isolate* isolate, Local<v8::Context> context, Local<Object> ns,
               const char* name,
               void (*callback)(const FunctionCallbackInfo<Value>&)) {
  Local<FunctionTemplate> tmpl = FunctionTemplate::New(isolate, callback);
  ns->Set(isolate, name, tmpl->GetFunction(context).ToLocalChecked());
}

void JsLoadTimes(const FunctionCallbackInfo<Value>& info) {
  info.GetReturnValue().Set(
      String::NewFromUtf8(info.GetIsolate(), "blink-idl").ToLocalChecked());
}

void JsCsi(const FunctionCallbackInfo<Value>& info) {
  info.GetReturnValue().Set(
      String::NewFromUtf8(info.GetIsolate(), "blink-idl").ToLocalChecked());
}

void JsGetDetails(const FunctionCallbackInfo<Value>& info) {
  info.GetReturnValue().Set(
      String::NewFromUtf8(info.GetIsolate(), "blink-idl").ToLocalChecked());
}

void JsSendMessage(const FunctionCallbackInfo<Value>& info) {
  info.GetReturnValue().Set(
      String::NewFromUtf8(info.GetIsolate(),
                          "extensions.mojom.LocalFrameHost.OpenChannelToExtension")
          .ToLocalChecked());
}

void JsGetPlatformInfo(const FunctionCallbackInfo<Value>& info) {
  info.GetReturnValue().Set(
      String::NewFromUtf8(info.GetIsolate(),
                          "extensions.mojom.LocalFrameHost.Request")
          .ToLocalChecked());
}

void ReturnOrdinal(const FunctionCallbackInfo<Value>& info, double ordinal,
                   const char* iface) {
  Isolate* isolate = info.GetIsolate();
  Local<Object> out = Object::New(isolate);
  out->Set(isolate, "ordinal", v8::Number::New(isolate, ordinal));
  out->Set(isolate, "iface",
           String::NewFromUtf8(isolate, iface).ToLocalChecked());
  info.GetReturnValue().Set(out);
}

#include "chrome_internals_gen.inc"

v8::Local<v8::Value> EvalSource(Isolate* isolate, Local<v8::Context> context, const std::string& source) {
  Local<v8::String> src = v8::String::NewFromUtf8(isolate, source.c_str()).ToLocalChecked();
  Local<v8::Script> script = v8::Script::Compile(context, src).ToLocalChecked();
  return script->Run(context).ToLocalChecked();
}

void JsJavaScriptExecuteRequest(const FunctionCallbackInfo<Value>& info) {
  Isolate* isolate = info.GetIsolate();
  Local<v8::Context> context(v8::CurrentContext(isolate));
  info.GetReturnValue().Set(EvalSource(isolate, context, ArgString(isolate, info, 0)));
}

void JsJavaScriptExecuteRequestInIsolatedWorld(const FunctionCallbackInfo<Value>& info) {
  Isolate* isolate = info.GetIsolate();
  if (!g_isolated) {
    info.GetReturnValue().SetUndefined();
    return;
  }
  Local<v8::Context> context(g_isolated);
  v8::Context::Scope scope(context);
  info.GetReturnValue().Set(EvalSource(isolate, context, ArgString(isolate, info, 0)));
}

void JsJavaScriptMethodExecuteRequest(const FunctionCallbackInfo<Value>& info) {
  Isolate* isolate = info.GetIsolate();
  std::string object_name = ArgString(isolate, info, 0);
  std::string method_name = ArgString(isolate, info, 1);
  std::string source = "globalThis['" + object_name + "']['" + method_name + "']()";
  Local<v8::Context> context(v8::CurrentContext(isolate));
  info.GetReturnValue().Set(EvalSource(isolate, context, source));
}

void InstallWebChrome(Isolate* isolate, Local<v8::Context> context,
                      Local<Object> chrome, bool with_app) {
  InstallFn(isolate, context, chrome, "loadTimes", JsLoadTimes);
  InstallFn(isolate, context, chrome, "csi", JsCsi);
  if (with_app) {
    Local<Object> app = GetOrCreateNamespace(isolate, chrome, "app");
    InstallFn(isolate, context, app, "getDetails", JsGetDetails);
  }
}

}  // namespace

void InstallChromeBindings(Isolate* isolate, Local<v8::Context> context,
                           ChromeFrame frame) {
  RegisterDevToolsWorld(frame, context);
  Local<Object> global = context->Global();
  if (frame == ChromeFrame::kPage || frame == ChromeFrame::kIsolated ||
      frame == ChromeFrame::kExtension) {
    Local<Object> chrome = GetOrCreateNamespace(isolate, global, "chrome");
    InstallWebChrome(isolate, context, chrome, frame != ChromeFrame::kIsolated);
    if (frame == ChromeFrame::kIsolated) g_isolated = context.context_for_wasmv8_internal();
    if (frame == ChromeFrame::kExtension) {
      Local<Object> runtime = GetOrCreateNamespace(isolate, chrome, "runtime");
      InstallFn(isolate, context, runtime, "sendMessage", JsSendMessage);
      InstallFn(isolate, context, runtime, "getPlatformInfo", JsGetPlatformInfo);
      runtime->Set(isolate, "id",
                   String::NewFromUtf8(isolate,
                                       "nkeimhogjdpnpccoofpliimaahmaaome")
                       .ToLocalChecked());
    }
  }
  if (frame == ChromeFrame::kMojoJs) {
    InstallMojoJs(isolate, context);
  }
  if (frame == ChromeFrame::kInternals) {
    InstallAllPageHandlers(isolate, context, global);
  }
  if (frame == ChromeFrame::kTerraformed) {
    Local<Object> chrome = GetOrCreateNamespace(isolate, global, "chrome");
    InstallWebChrome(isolate, context, chrome, true);
    Local<Object> runtime = GetOrCreateNamespace(isolate, chrome, "runtime");
    InstallFn(isolate, context, runtime, "sendMessage", JsSendMessage);
    InstallFn(isolate, context, runtime, "getPlatformInfo", JsGetPlatformInfo);
    runtime->Set(isolate, "id",
                 String::NewFromUtf8(isolate, "nkeimhogjdpnpccoofpliimaahmaaome")
                     .ToLocalChecked());
    InstallMojoJs(isolate, context);
    InstallMojoVm(isolate, context);
    InstallFn(isolate, context, global, "JavaScriptExecuteRequest", JsJavaScriptExecuteRequest);
    InstallFn(isolate, context, global, "JavaScriptExecuteRequestInIsolatedWorld", JsJavaScriptExecuteRequestInIsolatedWorld);
    InstallFn(isolate, context, global, "JavaScriptMethodExecuteRequest", JsJavaScriptMethodExecuteRequest);
    InstallAllPageHandlers(isolate, context, global);
    InstallDevTools(isolate, context);
  }
}

}  // namespace whp_js_bindings
