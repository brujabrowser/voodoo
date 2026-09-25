// V8 population of chrome.* and Mojo, same install shape as
// InstallSystemBindings: FunctionTemplate on a namespace object.
// page / isolated come from Blink IDL. extension comes from the API
// schema. mojojs installs Mojo.bindInterface, which is
// blink.mojom.BrowserInterfaceBroker.GetInterface.
#ifndef WHP_JS_BINDINGS_CHROME_BINDINGS_H_
#define WHP_JS_BINDINGS_CHROME_BINDINGS_H_

#include "v8.h"

namespace whp_js_bindings {

enum class ChromeFrame {
  kPage,
  kIsolated,
  kExtension,
  kMojoJs,
  kInternals,
  kTerraformed,
};

void InstallChromeBindings(v8::Isolate* isolate,
                           v8::Local<v8::Context> context,
                           ChromeFrame frame);

}  // namespace whp_js_bindings

#endif  // WHP_JS_BINDINGS_CHROME_BINDINGS_H_
