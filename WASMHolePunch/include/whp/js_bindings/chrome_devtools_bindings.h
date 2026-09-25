// Chrome DevTools Protocol names, bound to the in-process guest worlds.
// listTargets / attach / evaluate do not open a DevTools socket.
#ifndef WHP_JS_BINDINGS_CHROME_DEVTOOLS_BINDINGS_H_
#define WHP_JS_BINDINGS_CHROME_DEVTOOLS_BINDINGS_H_

#include "v8.h"

#include "whp/js_bindings/chrome_bindings.h"

namespace whp_js_bindings {

void RegisterDevToolsWorld(ChromeFrame frame, v8::Local<v8::Context> context);
void InstallDevTools(v8::Isolate* isolate, v8::Local<v8::Context> context);

}  // namespace whp_js_bindings

#endif  // WHP_JS_BINDINGS_CHROME_DEVTOOLS_BINDINGS_H_
