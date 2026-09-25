#include <cassert>
#include <cstdio>
#include <string>

#include "v8.h"
#include "whp/js_bindings/chrome_bindings.h"
#include "whp/js_bindings/chrome_internals_table.h"

namespace {

std::string RunString(v8::Local<v8::Context> context, const char* source) {
  v8::Local<v8::String> src =
      v8::String::NewFromUtf8(context->GetIsolate(), source).ToLocalChecked();
  v8::Local<v8::Script> script =
      v8::Script::Compile(context, src).ToLocalChecked();
  v8::Local<v8::Value> result = script->Run(context).ToLocalChecked();
  v8::String::Utf8Value text(context->GetIsolate(), result);
  return std::string(*text, static_cast<size_t>(text.length()));
}

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

v8::Local<v8::Context> NewContext(v8::Isolate* isolate) {
  return v8::Context::New(isolate);
}

}  // namespace

int main() {
  v8::Isolate* isolate = v8::Isolate::New();
  {
    v8::Isolate::Scope isolate_scope(isolate);
    v8::HandleScope handle_scope(isolate);

    {
      v8::Local<v8::Context> page = NewContext(isolate);
      v8::Context::Scope scope(page);
      whp_js_bindings::InstallChromeBindings(
          isolate, page, whp_js_bindings::ChromeFrame::kPage);
      assert(RunString(page, "typeof chrome.loadTimes") == "function");
      assert(RunString(page, "typeof chrome.runtime") == "undefined");
      assert(RunString(page, "typeof Mojo") == "undefined");
      assert(RunString(page, "chrome.loadTimes()") == "blink-idl");
    }

    {
      v8::Local<v8::Context> isolated = NewContext(isolate);
      v8::Context::Scope scope(isolated);
      whp_js_bindings::InstallChromeBindings(
          isolate, isolated, whp_js_bindings::ChromeFrame::kIsolated);
      assert(RunString(isolated, "typeof chrome.csi") == "function");
      assert(RunString(isolated, "typeof chrome.app") == "undefined");
    }

    {
      v8::Local<v8::Context> extension = NewContext(isolate);
      v8::Context::Scope scope(extension);
      whp_js_bindings::InstallChromeBindings(
          isolate, extension, whp_js_bindings::ChromeFrame::kExtension);
      assert(RunString(extension, "chrome.runtime.id") ==
             "nkeimhogjdpnpccoofpliimaahmaaome");
      assert(RunString(extension, "chrome.runtime.sendMessage('ping')") ==
             "extensions.mojom.LocalFrameHost.OpenChannelToExtension");
      assert(RunString(extension, "chrome.runtime.getPlatformInfo()") ==
             "extensions.mojom.LocalFrameHost.Request");
    }

    {
      v8::Local<v8::Context> mojojs = NewContext(isolate);
      v8::Context::Scope scope(mojojs);
      whp_js_bindings::InstallChromeBindings(
          isolate, mojojs, whp_js_bindings::ChromeFrame::kMojoJs);
      assert(RunString(mojojs, "typeof chrome") == "undefined");
      assert(RunNumber(mojojs, "Mojo.BROWSER_INTERFACE_BROKER") == 131441659.0);
      Run(mojojs, "globalThis.pipe = Mojo.createMessagePipe()");
      assert(RunNumber(mojojs, "pipe.result") == 0);
      Run(mojojs, "globalThis.bound = Mojo.bindInterface('blink.mojom.BrowserInterfaceBroker', pipe.handle0); bound.then(function(v) { globalThis.boundResult = v; })");
      isolate->PerformMicrotaskCheckpoint();
      assert(RunNumber(mojojs, "boundResult") == 0);
      Run(mojojs, "globalThis.msg = pipe.handle1.readMessage()");
      assert(RunNumber(mojojs, "msg.result") == 0);
      assert(RunString(mojojs, "msg.payload") == "blink.mojom.BrowserInterfaceBroker");
    }

    {
      v8::Local<v8::Context> internals = NewContext(isolate);
      v8::Context::Scope scope(internals);
      whp_js_bindings::InstallChromeBindings(
          isolate, internals, whp_js_bindings::ChromeFrame::kInternals);
      assert(whp_js_bindings::kChromeInternalsCount == 1236);
      for (int i = 0; i < whp_js_bindings::kChromeInternalsCount; ++i) {
        const whp_js_bindings::ChromeInternalsRow& row = whp_js_bindings::kChromeInternals[i];
        std::string call = std::string("globalThis['") + row.iface + "']['" + row.method + "']()";
        assert(RunNumber(internals, (call + ".ordinal").c_str()) == row.ordinal);
        assert(RunString(internals, (call + ".iface").c_str()) == row.iface);
      }
      assert(RunString(internals, "typeof chrome") == "undefined");
    }

    std::printf("whp_bindings_chrome_test: ok\n");
  }
  isolate->Dispose();
  return 0;
}
