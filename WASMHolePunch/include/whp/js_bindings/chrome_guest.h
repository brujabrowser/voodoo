// One booted Chrome guest. Each ChromeFrame is its own V8 context on
// one isolate: page, isolated, extension, MojoJS, and browser internals.
#ifndef WHP_JS_BINDINGS_CHROME_GUEST_H_
#define WHP_JS_BINDINGS_CHROME_GUEST_H_

#include <string>

#include "whp/js_bindings/chrome_bindings.h"

struct JSContext;

namespace whp_js_bindings {

class ChromeGuest {
 public:
  ChromeGuest();
  ~ChromeGuest();

  ChromeGuest(const ChromeGuest&) = delete;
  ChromeGuest& operator=(const ChromeGuest&) = delete;

  std::string RunString(ChromeFrame frame, const char* source);
  double RunNumber(ChromeFrame frame, const char* source);
  void Run(ChromeFrame frame, const char* source);
  void Checkpoint();

 private:
  JSContext* World(ChromeFrame frame) const;

  v8::Isolate* isolate_ = nullptr;
  JSContext* worlds_[6] = {};
};

}  // namespace whp_js_bindings

#endif  // WHP_JS_BINDINGS_CHROME_GUEST_H_
