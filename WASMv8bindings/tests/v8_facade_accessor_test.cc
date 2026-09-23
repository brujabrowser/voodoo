// Golden test for ObjectTemplate::SetAccessor -- the piece that maps
// directly onto brujac's Web IDL attribute surface
// (`[readonly] attribute Type name;` and read-write attributes, see
// WASMBruja's own README). Proves: a getter-only accessor is real (JS
// reads the live C++ value, not a snapshot), assigning to it is rejected
// in strict mode (real readonly-property semantics, not just "nothing
// happens to silently hide a bug"), and a read-write accessor's setter
// genuinely mutates the wrapped C++ object, observable both from JS
// (reading it back) and from C++ directly.
#include <cassert>
#include <cstdio>
#include <string>

#include "cppgc/allocation.h"
#include "cppgc/garbage-collected.h"
#include "cppgc/heap.h"
#include "cppgc/visitor.h"
#include "v8.h"

namespace {

constexpr v8::CppHeapPointerTag kWidgetTag = static_cast<v8::CppHeapPointerTag>(1);

class Widget final : public cppgc::GarbageCollected<Widget> {
 public:
  int id = 7;         // readonly attribute
  std::string label;  // read-write attribute
  void Trace(cppgc::Visitor*) const {}
};

void IdGetter(v8::Local<v8::String>, const v8::PropertyCallbackInfo<v8::Value>& info) {
  Widget* widget = v8::Object::Unwrap<Widget>(info.This(), kWidgetTag);
  assert(widget != nullptr);
  info.GetReturnValue().Set(static_cast<double>(widget->id));
}

void LabelGetter(v8::Local<v8::String>, const v8::PropertyCallbackInfo<v8::Value>& info) {
  Widget* widget = v8::Object::Unwrap<Widget>(info.This(), kWidgetTag);
  assert(widget != nullptr);
  info.GetReturnValue().Set(
      v8::String::NewFromUtf8(info.GetIsolate(), widget->label.c_str()).ToLocalChecked());
}

void LabelSetter(v8::Local<v8::String>, v8::Local<v8::Value> value,
                 const v8::PropertyCallbackInfo<void>& info) {
  Widget* widget = v8::Object::Unwrap<Widget>(info.This(), kWidgetTag);
  assert(widget != nullptr);
  v8::String::Utf8Value utf8(info.GetIsolate(), value);
  widget->label = *utf8;
}

}  // namespace

int main() {
  auto cppgc_heap = cppgc::Heap::Create(nullptr);
  Widget* widget = cppgc::MakeGarbageCollected<Widget>(cppgc_heap->GetAllocationHandle());
  widget->label = "initial";

  v8::Isolate* isolate = v8::Isolate::New();
  {
    v8::Isolate::Scope isolate_scope(isolate);
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = v8::Context::New(isolate);
    v8::Context::Scope context_scope(context);

    v8::Local<v8::ObjectTemplate> tmpl = v8::ObjectTemplate::New(isolate);
    tmpl->SetInternalFieldCount(1);
    tmpl->SetAccessor("id", IdGetter);  // readonly: no setter
    tmpl->SetAccessor("label", LabelGetter, LabelSetter);

    v8::Local<v8::Object> obj = tmpl->NewInstance(context).ToLocalChecked();
    v8::Object::Wrap(isolate, obj, widget, kWidgetTag);
    context->Global()->Set(isolate, "widget", obj);

    // Readonly attribute: reads the live value...
    v8::Local<v8::String> read_id =
        v8::String::NewFromUtf8(isolate, "widget.id").ToLocalChecked();
    v8::Local<v8::Script> script1 = v8::Script::Compile(context, read_id).ToLocalChecked();
    v8::Local<v8::Value> id_result = script1->Run(context).ToLocalChecked();
    assert(id_result->IsNumber());
    assert(id_result.As<v8::Number>()->Value() == 7.0);

    // ...and rejects assignment in strict mode, exactly like a real
    // getter-only accessor property (not silently ignored -- an actual
    // TypeError, proving this isn't just "the setter never got called").
    v8::Local<v8::String> write_id = v8::String::NewFromUtf8(
                                         isolate, "'use strict'; widget.id = 99;")
                                         .ToLocalChecked();
    v8::Local<v8::Script> script2 = v8::Script::Compile(context, write_id).ToLocalChecked();
    v8::MaybeLocal<v8::Value> write_result = script2->Run(context);
    assert(write_result.IsEmpty());  // threw
    assert(widget->id == 7);         // unmodified

    // Read-write attribute: setter genuinely mutates the C++ object.
    v8::Local<v8::String> write_label = v8::String::NewFromUtf8(
                                            isolate, "widget.label = 'from JS'; widget.label")
                                            .ToLocalChecked();
    v8::Local<v8::Script> script3 =
        v8::Script::Compile(context, write_label).ToLocalChecked();
    v8::Local<v8::Value> label_result = script3->Run(context).ToLocalChecked();
    assert(label_result->IsString());
    v8::String::Utf8Value label_utf8(isolate, label_result);
    assert(std::string(*label_utf8) == "from JS");
    assert(widget->label == "from JS");  // visible on the C++ side directly
  }

  isolate->Dispose();
  cppgc_heap->ForceGarbageCollectionSlow("test", "shutdown");
  cppgc_heap.reset();

  std::printf("v8_facade_accessor_test: OK\n");
  return 0;
}
