#include <cassert>
#include <cstdio>
#include <string>

#include "whp/js_bindings/chrome_guest.h"

int main() {
  whp_js_bindings::ChromeGuest guest;

  assert(guest.RunString(whp_js_bindings::ChromeFrame::kPage, "chrome.loadTimes()") == "blink-idl");
  assert(guest.RunString(whp_js_bindings::ChromeFrame::kPage, "typeof Mojo") == "undefined");
  assert(guest.RunString(whp_js_bindings::ChromeFrame::kIsolated, "typeof chrome.app") == "undefined");
  assert(guest.RunString(whp_js_bindings::ChromeFrame::kExtension, "chrome.runtime.sendMessage('ping')") ==
         "extensions.mojom.LocalFrameHost.OpenChannelToExtension");

  guest.Run(whp_js_bindings::ChromeFrame::kMojoJs, "globalThis.pipe = Mojo.createMessagePipe()");
  assert(guest.RunNumber(whp_js_bindings::ChromeFrame::kMojoJs, "pipe.result") == 0);
  guest.Run(whp_js_bindings::ChromeFrame::kMojoJs,
            "globalThis.bound = Mojo.bindInterface('blink.mojom.BrowserInterfaceBroker', pipe.handle0);"
            "bound.then(function(v) { globalThis.boundResult = v; })");
  guest.Checkpoint();
  assert(guest.RunNumber(whp_js_bindings::ChromeFrame::kMojoJs, "boundResult") == 0);
  guest.Run(whp_js_bindings::ChromeFrame::kMojoJs, "globalThis.msg = pipe.handle1.readMessage()");
  assert(guest.RunString(whp_js_bindings::ChromeFrame::kMojoJs, "msg.payload") ==
         "blink.mojom.BrowserInterfaceBroker");

  assert(guest.RunNumber(whp_js_bindings::ChromeFrame::kInternals,
                         "globalThis['access_code_cast.mojom.PageHandler']['AddSink']().ordinal") ==
         2074422509.0);
  assert(guest.RunString(whp_js_bindings::ChromeFrame::kInternals, "typeof chrome") == "undefined");

  assert(guest.RunString(whp_js_bindings::ChromeFrame::kTerraformed, "typeof Mojo.bindInterface") == "function");
  assert(guest.RunString(whp_js_bindings::ChromeFrame::kTerraformed, "typeof chrome.runtime.sendMessage") == "function");
  assert(guest.RunNumber(whp_js_bindings::ChromeFrame::kTerraformed,
                         "globalThis['access_code_cast.mojom.PageHandler']['AddSink']().ordinal") ==
         2074422509.0);
  guest.Run(whp_js_bindings::ChromeFrame::kTerraformed, "globalThis.pipe = Mojo.createMessagePipe()");
  guest.Run(whp_js_bindings::ChromeFrame::kTerraformed,
            "globalThis.bound = Mojo.bindInterface('blink.mojom.BrowserInterfaceBroker', pipe.handle0);"
            "bound.then(function(v) { globalThis.boundResult = v; })");
  guest.Checkpoint();
  assert(guest.RunNumber(whp_js_bindings::ChromeFrame::kTerraformed, "boundResult") == 0);
  assert(guest.RunNumber(whp_js_bindings::ChromeFrame::kTerraformed,
                         "globalThis['extensions.mojom.MessagePort']['dispatchDisconnect']()") ==
         2592569);
  assert(guest.RunNumber(whp_js_bindings::ChromeFrame::kTerraformed, "JavaScriptExecuteRequest('1+1')") == 2);
  assert(guest.RunString(whp_js_bindings::ChromeFrame::kTerraformed,
                         "JavaScriptExecuteRequestInIsolatedWorld('typeof chrome.app')") ==
         "undefined");
  assert(guest.RunNumber(whp_js_bindings::ChromeFrame::kTerraformed,
                         "JavaScriptMethodExecuteRequest('extensions.mojom.MessagePort', 'dispatchDisconnect')") ==
         2592569);
  assert(guest.RunNumber(whp_js_bindings::ChromeFrame::kTerraformed, "MojoVM.count") == 9062);
  assert(guest.RunNumber(whp_js_bindings::ChromeFrame::kTerraformed,
                         "MojoVM.lookup('extensions.mojom.MessagePort.DispatchDisconnect').mojo") ==
         2592569);
  assert(guest.RunString(whp_js_bindings::ChromeFrame::kTerraformed,
                         "MojoVM.call('extensions.mojom.MessagePort.DispatchDisconnect')")
             .find("2592569") != std::string::npos);

  std::printf("whp_bindings_chrome_guest_test: ok\n");
  return 0;
}
