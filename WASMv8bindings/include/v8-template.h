// Simplified from V8's include/v8-template.h.
//
// A real V8 Template is a C++-side "recipe" object, not itself a JS value
// visible to script -- GetFunction()/NewInstance() materialize one into an
// actual JS value. That means, unlike Object/String/Function,
// FunctionTemplate/ObjectTemplate aren't backed by a quickjs JSValue at
// all, so Local<FunctionTemplate>/Local<ObjectTemplate> are full
// specializations backed by a std::shared_ptr instead of the generic
// dup/free-a-JSValue Local<T> from v8-local-handle.h (same reasoning as
// Local<Context>, see v8-context.h's file comment). The two specializations
// are declared up front, each against only a forward-declared class (safe:
// their bodies never need the pointee complete, only whoever actually
// constructs one via Adopt() does) -- ObjectTemplate::Method needs a
// complete Local<FunctionTemplate> and FunctionTemplate needs a complete
// Local<ObjectTemplate>, so neither full class can come first otherwise.
//
// SetAccessor() (getter/setter properties, matching brujac's Web IDL
// `[readonly] attribute`/read-write attribute surface directly) and
// constructor support (FunctionTemplate::PrototypeTemplate/InstanceTemplate,
// `new`-calling a FunctionTemplate) are both real -- see template.cc's file
// comment for the quickjs-ng mechanism constructors needed
// (JS_CFUNC_constructor_magic + new_target, not the JS_NewCClosure every
// other callback here uses; see the top-level README for why plain
// JS_NewCClosure doesn't work for this one case).
#ifndef WASMV8_INCLUDE_V8_TEMPLATE_H_
#define WASMV8_INCLUDE_V8_TEMPLATE_H_

#include <memory>
#include <string>
#include <vector>

#include "quickjs.h"
#include "v8-context.h"
#include "v8-function-callback.h"
#include "v8-local-handle.h"
#include "v8-maybe.h"
#include "v8-property-callback.h"

namespace v8 {

class Isolate;
class Function;
class Object;
class String;
class FunctionTemplate;
class ObjectTemplate;

template <>
class Local<FunctionTemplate> {
 public:
  Local() = default;

  bool IsEmpty() const { return !impl_; }
  void Clear() { impl_.reset(); }

  FunctionTemplate* operator->() const { return impl_.get(); }
  FunctionTemplate& operator*() const { return *impl_; }

  static Local<FunctionTemplate> Adopt(std::shared_ptr<FunctionTemplate> impl) {
    Local<FunctionTemplate> local;
    local.impl_ = std::move(impl);
    return local;
  }

 private:
  std::shared_ptr<FunctionTemplate> impl_;
};

template <>
class Local<ObjectTemplate> {
 public:
  Local() = default;

  bool IsEmpty() const { return !impl_; }
  void Clear() { impl_.reset(); }

  ObjectTemplate* operator->() const { return impl_.get(); }
  ObjectTemplate& operator*() const { return *impl_; }

  static Local<ObjectTemplate> Adopt(std::shared_ptr<ObjectTemplate> impl) {
    Local<ObjectTemplate> local;
    local.impl_ = std::move(impl);
    return local;
  }

 private:
  std::shared_ptr<ObjectTemplate> impl_;
};

class ObjectTemplate final {
 public:
  static Local<ObjectTemplate> New(Isolate* isolate);

  // Installs a method named `name`, callable from JS on any instance
  // created from this template (ObjectTemplate::NewInstance), or shared
  // through the prototype chain of instances created by a FunctionTemplate
  // whose PrototypeTemplate() this is.
  void Set(const char* name, Local<FunctionTemplate> function_template);

  // Installs a getter/setter accessor property named `name`. Omitting
  // `setter` (the default) makes it a readonly property -- assigning to it
  // from JS silently does nothing in sloppy mode / throws in strict mode,
  // matching real quickjs's own getter-only-accessor behavior, which is
  // exactly Web IDL's `[readonly] attribute` semantics.
  void SetAccessor(const char* name, AccessorGetterCallback getter,
                   AccessorSetterCallback setter = nullptr);

  // Marks instances as wrappable (usable with Object::Wrap/Unwrap). Real
  // V8's count selects how many raw internal-field slots an instance gets;
  // this port only needs "wrappable or not" (>= 1), since wrapping goes
  // through the CppHeapPointerTable rather than a raw internal-field
  // array -- see v8-object.h's Wrap()/Unwrap().
  void SetInternalFieldCount(int count);

  MaybeLocal<Object> NewInstance(Local<Context> context);

  bool wrappable_for_wasmv8_internal() const { return wrappable_; }
  Isolate* isolate_for_wasmv8_internal() const { return isolate_; }

  // Installs this template's methods/accessors onto an already-created
  // JS object (`target`) -- shared by NewInstance() (installing onto a
  // fresh plain/wrappable instance) and FunctionTemplate's constructor
  // path (installing onto the shared prototype object instead). Returns
  // false on a real JS-side failure (an installed method's underlying
  // GetFunction() failed); the caller is responsible for turning that into
  // whatever error signal fits its own return type.
  bool InstallMembersOn_for_wasmv8_internal(Local<Context> context, JSValueConst target);

 private:
  struct Method {
    std::string name;
    Local<FunctionTemplate> function_template;
  };
  struct Accessor {
    std::string name;
    AccessorGetterCallback getter;
    AccessorSetterCallback setter;
  };

  Isolate* isolate_ = nullptr;
  std::vector<Method> methods_;
  std::vector<Accessor> accessors_;
  bool wrappable_ = false;

  friend class Local<ObjectTemplate>;
};

class FunctionTemplate final {
 public:
  static Local<FunctionTemplate> New(Isolate* isolate,
                                     FunctionCallback callback = nullptr);
  ~FunctionTemplate();

  // Plain call: materializes a callable JS function invoking `callback`.
  // If PrototypeTemplate()/InstanceTemplate() were ever touched on this
  // template, the returned function is also `new`-able (see template.cc).
  // Idempotent per Context: a second call returns the *same* underlying
  // JS function rather than building a disconnected duplicate -- real V8
  // callers rely on this too, and Inherit() (template.cc) depends on it
  // directly: it calls parent->GetFunction() again internally, and without
  // caching that would silently produce a second copy of the parent
  // constructor with its own separate prototype object, breaking
  // `instanceof` against whatever the caller already exposed to script.
  MaybeLocal<Function> GetFunction(Local<Context> context);

  // The ObjectTemplate whose Set()-installed methods end up on the shared
  // prototype of every instance this FunctionTemplate constructs via
  // `new` -- real V8's FunctionTemplate::PrototypeTemplate. Touching this
  // (even just to call it) marks the template constructible.
  Local<ObjectTemplate> PrototypeTemplate();

  // The ObjectTemplate configuring instances created via `new` --
  // currently only SetInternalFieldCount() matters (wrappability). Real
  // V8's FunctionTemplate::InstanceTemplate. Touching this also marks the
  // template constructible.
  Local<ObjectTemplate> InstanceTemplate();

  // Chains this constructor's prototype to `parent`'s, matching real V8's
  // FunctionTemplate::Inherit / ES6 `class Child extends Parent`: once
  // GetFunction() materializes both, `child.prototype.__proto__ ===
  // parent.prototype` (so `new Child()` instances inherit Parent's
  // PrototypeTemplate methods/accessors too) and `child.__proto__ ===
  // parent` (so static properties on the parent constructor function are
  // visible on the child constructor as well, the same ES6 class-extends
  // detail). Calling this also marks this template constructible, same as
  // touching PrototypeTemplate()/InstanceTemplate().
  void Inherit(Local<FunctionTemplate> parent);

  Isolate* isolate_for_wasmv8_internal() const { return isolate_; }
  FunctionCallback callback_for_wasmv8_internal() const { return callback_; }
  Local<FunctionTemplate> parent_for_wasmv8_internal() const { return parent_; }

 private:
  Isolate* isolate_ = nullptr;
  FunctionCallback callback_ = nullptr;
  Local<ObjectTemplate> prototype_template_;
  Local<ObjectTemplate> instance_template_;
  Local<FunctionTemplate> parent_;
  bool touched_ = false;

  // Cached at the raw JSValue level (not Local<Function>) so this header
  // doesn't need Function complete -- see this file's top comment on why
  // FunctionTemplate/ObjectTemplate are declared in this specific order.
  JSContext* cached_ctx_ = nullptr;
  JSValue cached_fn_ = JS_UNDEFINED;
};

}  // namespace v8

#endif  // WASMV8_INCLUDE_V8_TEMPLATE_H_
