#include "whp/js_bindings/mojo_vm_bindings.h"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>

#include "mojovm.h"
#include "whp/js_bindings/runtime.h"

namespace whp_js_bindings {
namespace {

using v8::FunctionCallbackInfo;
using v8::FunctionTemplate;
using v8::Isolate;
using v8::Local;
using v8::Number;
using v8::Object;
using v8::String;
using v8::Value;

const char kMojomRoot[] =
    "C:\\Users\\grego\\WASMVoodooCompile\\Bruja\\third_party\\chromium-src";
const char kPortfolio[] =
    "C:\\Users\\grego\\WASMVoodooCompile\\Bruja\\out\\mojovm-portfolio.txt";

struct Row {
  double voodoo = 0;
  double mojo = 0;
};

std::unordered_map<std::string, Row>& Catalog() {
  static std::unordered_map<std::string, Row> rows;
  static bool loaded = false;
  if (loaded) return rows;
  loaded = true;
  std::ifstream in(kPortfolio);
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    std::string col[3];
    int n = 0;
    std::string cur;
    for (char c : line) {
      if (c == '\t') {
        if (n < 3) col[n++] = cur;
        cur.clear();
        if (n == 3) break;
      } else {
        cur.push_back(c);
      }
    }
    if (n < 2 || col[0].empty() || col[0][0] == '#') continue;
    if (n == 2) col[2] = cur;
    Row row;
    row.voodoo = std::strtod(col[1].c_str(), nullptr);
    row.mojo = std::strtod(col[2].c_str(), nullptr);
    rows.emplace(col[0], row);
  }
  return rows;
}

mojovm::Vm& Vm() {
  static mojovm::Vm vm(kMojomRoot);
  return vm;
}

void InstallFn(Isolate* isolate, Local<v8::Context> context, Local<Object> ns,
               const char* name,
               void (*callback)(const FunctionCallbackInfo<Value>&)) {
  Local<FunctionTemplate> tmpl = FunctionTemplate::New(isolate, callback);
  ns->Set(isolate, name, tmpl->GetFunction(context).ToLocalChecked());
}

void JsCall(const FunctionCallbackInfo<Value>& info) {
  Isolate* isolate = info.GetIsolate();
  std::string api = ArgString(isolate, info, 0);
  std::string arg = ArgString(isolate, info, 1);
  std::string reply = Vm().Call(api, arg);
  info.GetReturnValue().Set(String::NewFromUtf8(isolate, reply.c_str()).ToLocalChecked());
}

void JsLookup(const FunctionCallbackInfo<Value>& info) {
  Isolate* isolate = info.GetIsolate();
  std::string api = ArgString(isolate, info, 0);
  auto& cat = Catalog();
  auto it = cat.find(api);
  Local<Object> out = Object::New(isolate);
  if (it == cat.end()) {
    out->Set(isolate, "ok", v8::Boolean::New(isolate, false));
    info.GetReturnValue().Set(out);
    return;
  }
  out->Set(isolate, "ok", v8::Boolean::New(isolate, true));
  out->Set(isolate, "voodoo", Number::New(isolate, it->second.voodoo));
  out->Set(isolate, "mojo", Number::New(isolate, it->second.mojo));
  info.GetReturnValue().Set(out);
}

#include "runtime_bindings_gen.inc"

}  // namespace

void InstallMojoVm(Isolate* isolate, Local<v8::Context> context) {
  Local<Object> global = context->Global();
  Local<Object> ns = GetOrCreateNamespace(isolate, global, "MojoVM");
  InstallFn(isolate, context, ns, "call", JsCall);
  InstallFn(isolate, context, ns, "lookup", JsLookup);
  ns->Set(isolate, "count", Number::New(isolate, static_cast<double>(Catalog().size())));
  InstallRuntimeBindings(isolate, context, global);
}

}  // namespace whp_js_bindings
