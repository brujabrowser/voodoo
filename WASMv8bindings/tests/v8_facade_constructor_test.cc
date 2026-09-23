// Golden test for FunctionTemplate as a `new`-able constructor -- the
// piece brujac's `interface X { constructor(...); }` (see its DOM example)
// needs a V8 backend to target. Proves: `new Point(3, 4)` produces a real
// `instanceof Point` object (not just an object that happens to work),
// with a cppgc-managed C++ Point genuinely allocated and wrapped during
// construction, its readonly attributes (installed on the shared
// PrototypeTemplate, not per-instance) reading the live C++ fields, and a
// prototype method computing from them.
#include <cassert>
#include <cmath>
#include <cstdio>

#include "cppgc/allocation.h"
#include "cppgc/garbage-collected.h"
#include "cppgc/heap.h"
#include "cppgc/visitor.h"
#include "v8.h"

namespace {

constexpr v8::CppHeapPointerTag kPointTag = static_cast<v8::CppHeapPointerTag>(1);

class Point final : public cppgc::GarbageCollected<Point> {
 public:
  double x = 0;
  double y = 0;
  void Trace(cppgc::Visitor*) const {}
};

cppgc::AllocationHandle* g_handle = nullptr;

void PointConstructor(const v8::FunctionCallbackInfo<v8::Value>& info) {
  v8::Isolate* isolate = info.GetIsolate();
  Point* point = cppgc::MakeGarbageCollected<Point>(*g_handle);
  point->x = info[0].As<v8::Number>()->Value();
  point->y = info[1].As<v8::Number>()->Value();
  v8::Object::Wrap(isolate, info.This(), point, kPointTag);
}

void PointGetX(v8::Local<v8::String>, const v8::PropertyCallbackInfo<v8::Value>& info) {
  Point* point = v8::Object::Unwrap<Point>(info.This(), kPointTag);
  assert(point != nullptr);
  info.GetReturnValue().Set(point->x);
}

void PointGetY(v8::Local<v8::String>, const v8::PropertyCallbackInfo<v8::Value>& info) {
  Point* point = v8::Object::Unwrap<Point>(info.This(), kPointTag);
  assert(point != nullptr);
  info.GetReturnValue().Set(point->y);
}

void PointDistance(const v8::FunctionCallbackInfo<v8::Value>& info) {
  Point* point = v8::Object::Unwrap<Point>(info.This(), kPointTag);
  assert(point != nullptr);
  info.GetReturnValue().Set(std::sqrt(point->x * point->x + point->y * point->y));
}

double RunNumber(v8::Local<v8::Context> context, const char* src) {
  v8::Isolate* isolate = context->GetIsolate();
  v8::Local<v8::String> source = v8::String::NewFromUtf8(isolate, src).ToLocalChecked();
  v8::Local<v8::Script> script = v8::Script::Compile(context, source).ToLocalChecked();
  v8::Local<v8::Value> result = script->Run(context).ToLocalChecked();
  assert(result->IsNumber());
  return result.As<v8::Number>()->Value();
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

    v8::Local<v8::FunctionTemplate> point_ctor =
        v8::FunctionTemplate::New(isolate, PointConstructor);
    point_ctor->InstanceTemplate()->SetInternalFieldCount(1);
    point_ctor->PrototypeTemplate()->SetAccessor("x", PointGetX);
    point_ctor->PrototypeTemplate()->SetAccessor("y", PointGetY);
    point_ctor->PrototypeTemplate()->Set(
        "distance", v8::FunctionTemplate::New(isolate, PointDistance));

    v8::Local<v8::Function> ctor_fn = point_ctor->GetFunction(context).ToLocalChecked();
    context->Global()->Set(isolate, "Point", ctor_fn);

    // Real `instanceof`, real prototype-chain method/accessor lookup --
    // not just an object that happens to have the right shape.
    assert(RunBool(context, "var p = new Point(3, 4); p instanceof Point"));
    assert(RunNumber(context, "p.x") == 3.0);
    assert(RunNumber(context, "p.y") == 4.0);
    assert(RunNumber(context, "p.distance()") == 5.0);

    // A second instance is independently backed by its own cppgc object.
    assert(RunBool(context,
                   "var q = new Point(6, 8); q.distance() === 10 && p.x === 3"));

    // Calling the constructor without `new` is rejected (real quickjs-ng
    // constructor-bit enforcement, exercised here rather than assumed).
    v8::Local<v8::String> bad_call =
        v8::String::NewFromUtf8(isolate, "Point(1, 2)").ToLocalChecked();
    v8::Local<v8::Script> bad_script =
        v8::Script::Compile(context, bad_call).ToLocalChecked();
    assert(bad_script->Run(context).IsEmpty());
  }

  isolate->Dispose();
  cppgc_heap->ForceGarbageCollectionSlow("test", "shutdown");
  cppgc_heap.reset();

  std::printf("v8_facade_constructor_test: OK\n");
  return 0;
}
