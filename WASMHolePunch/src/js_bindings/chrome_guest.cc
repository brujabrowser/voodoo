#include "whp/js_bindings/chrome_guest.h"

#include "v8.h"

namespace whp_js_bindings {
namespace {

int Index(ChromeFrame frame) { return static_cast<int>(frame); }

v8::Local<v8::Value> Eval(v8::Isolate* isolate, JSContext* world, const char* source) {
  v8::Local<v8::Context> context(world);
  v8::Context::Scope scope(context);
  v8::Local<v8::String> src = v8::String::NewFromUtf8(isolate, source).ToLocalChecked();
  v8::Local<v8::Script> script = v8::Script::Compile(context, src).ToLocalChecked();
  return script->Run(context).ToLocalChecked();
}

}  // namespace

ChromeGuest::ChromeGuest() {
  isolate_ = v8::Isolate::New();
  v8::Isolate::Scope isolate_scope(isolate_);
  v8::HandleScope handle_scope(isolate_);
  const ChromeFrame frames[] = {
      ChromeFrame::kPage, ChromeFrame::kIsolated, ChromeFrame::kExtension,
      ChromeFrame::kMojoJs, ChromeFrame::kInternals, ChromeFrame::kTerraformed,
  };
  for (ChromeFrame frame : frames) {
    v8::Local<v8::Context> context = v8::Context::New(isolate_);
    v8::Context::Scope scope(context);
    InstallChromeBindings(isolate_, context, frame);
    worlds_[Index(frame)] = context.context_for_wasmv8_internal();
  }
}

ChromeGuest::~ChromeGuest() {
  if (isolate_) isolate_->Dispose();
}

JSContext* ChromeGuest::World(ChromeFrame frame) const { return worlds_[Index(frame)]; }

void ChromeGuest::Run(ChromeFrame frame, const char* source) {
  v8::Isolate::Scope isolate_scope(isolate_);
  v8::HandleScope handle_scope(isolate_);
  Eval(isolate_, World(frame), source);
}

std::string ChromeGuest::RunString(ChromeFrame frame, const char* source) {
  v8::Isolate::Scope isolate_scope(isolate_);
  v8::HandleScope handle_scope(isolate_);
  v8::Local<v8::Value> result = Eval(isolate_, World(frame), source);
  v8::String::Utf8Value text(isolate_, result);
  return std::string(*text, static_cast<size_t>(text.length()));
}

double ChromeGuest::RunNumber(ChromeFrame frame, const char* source) {
  v8::Isolate::Scope isolate_scope(isolate_);
  v8::HandleScope handle_scope(isolate_);
  v8::Local<v8::Value> result = Eval(isolate_, World(frame), source);
  return result.As<v8::Number>()->Value();
}

void ChromeGuest::Checkpoint() {
  v8::Isolate::Scope isolate_scope(isolate_);
  isolate_->PerformMicrotaskCheckpoint();
}

}  // namespace whp_js_bindings
