// Golden test for FunctionTemplate::Inherit -- the piece a DOM-shaped
// interface hierarchy (Node -> Element, see WASMBruja's own DOM example)
// needs from a V8 backend. Proves real ES6-class-extends-shaped
// inheritance: `new Element()` is both `instanceof Element` and
// `instanceof Node`, Node's prototype method is reachable on an Element
// instance through the prototype chain, an Element-only method isn't
// reachable on a plain Node instance, and static-property inheritance
// (`Element.__proto__ === Node`) holds too.
#include <cassert>
#include <cstdio>

#include "cppgc/allocation.h"
#include "cppgc/garbage-collected.h"
#include "cppgc/heap.h"
#include "cppgc/visitor.h"
#include "v8.h"

namespace {

constexpr v8::CppHeapPointerTag kNodeTag = static_cast<v8::CppHeapPointerTag>(1);

class Node final : public cppgc::GarbageCollected<Node> {
 public:
  int node_type = 1;
  void Trace(cppgc::Visitor*) const {}
};

cppgc::AllocationHandle* g_handle = nullptr;

void NodeConstructor(const v8::FunctionCallbackInfo<v8::Value>& info) {
  Node* node = cppgc::MakeGarbageCollected<Node>(*g_handle);
  v8::Object::Wrap(info.GetIsolate(), info.This(), node, kNodeTag);
}

void NodeType(const v8::FunctionCallbackInfo<v8::Value>& info) {
  Node* node = v8::Object::Unwrap<Node>(info.This(), kNodeTag);
  assert(node != nullptr);
  info.GetReturnValue().Set(static_cast<double>(node->node_type));
}

void ElementTagName(const v8::FunctionCallbackInfo<v8::Value>& info) {
  info.GetReturnValue().Set(
      v8::String::NewFromUtf8(info.GetIsolate(), "DIV").ToLocalChecked());
}

bool RunBool(v8::Local<v8::Context> context, const char* src) {
  v8::Isolate* isolate = context->GetIsolate();
  v8::Local<v8::String> source = v8::String::NewFromUtf8(isolate, src).ToLocalChecked();
  v8::Local<v8::Script> script = v8::Script::Compile(context, source).ToLocalChecked();
  v8::Local<v8::Value> result = script->Run(context).ToLocalChecked();
  assert(result->IsBoolean());
  return result.As<v8::Boolean>()->Value();
}

}  // namespace

int main() {
  auto cppgc_heap = cppgc::Heap::Create(nullptr);
  cppgc::AllocationHandle& handle = cppgc_heap->GetAllocationHandle();
  g_handle = &handle;

  v8::Isolate* isolate = v8::Isolate::New();
  {
    v8::Isolate::Scope isolate_scope(isolate);
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = v8::Context::New(isolate);
    v8::Context::Scope context_scope(context);

    v8::Local<v8::FunctionTemplate> node_ctor =
        v8::FunctionTemplate::New(isolate, NodeConstructor);
    node_ctor->InstanceTemplate()->SetInternalFieldCount(1);
    node_ctor->PrototypeTemplate()->Set("nodeType", v8::FunctionTemplate::New(isolate, NodeType));

    v8::Local<v8::FunctionTemplate> element_ctor =
        v8::FunctionTemplate::New(isolate, NodeConstructor);
    element_ctor->InstanceTemplate()->SetInternalFieldCount(1);
    element_ctor->PrototypeTemplate()->Set("tagName",
                                           v8::FunctionTemplate::New(isolate, ElementTagName));
    element_ctor->Inherit(node_ctor);

    v8::Local<v8::Function> node_fn = node_ctor->GetFunction(context).ToLocalChecked();
    v8::Local<v8::Function> element_fn = element_ctor->GetFunction(context).ToLocalChecked();
    context->Global()->Set(isolate, "Node", node_fn);
    context->Global()->Set(isolate, "Element", element_fn);

    // Real prototype-chain inheritance, not a copy of Node's methods.
    assert(RunBool(context, "var e = new Element(); e instanceof Element"));
    assert(RunBool(context, "e instanceof Node"));
    assert(RunBool(context, "e.nodeType() === 1"));   // inherited from Node
    assert(RunBool(context, "e.tagName() === 'DIV'"));  // Element's own

    // A plain Node does NOT get Element's method.
    assert(RunBool(context, "var n = new Node(); typeof n.tagName === 'undefined'"));
    assert(RunBool(context, "!(n instanceof Element)"));

    // Static-property inheritance: Element's own [[Prototype]] is Node
    // itself (the function object), matching ES6 `class Element extends
    // Node {}`.
    assert(RunBool(context, "Object.getPrototypeOf(Element) === Node"));
  }

  isolate->Dispose();
  cppgc_heap->ForceGarbageCollectionSlow("test", "shutdown");
  cppgc_heap.reset();

  std::printf("v8_facade_inherit_test: OK\n");
  return 0;
}
