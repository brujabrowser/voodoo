// FunctionTemplate's constructor path (PrototypeTemplate/InstanceTemplate,
// `new`-calling a FunctionTemplate) uses JS_NewCFunction3 with
// JS_CFUNC_constructor_magic instead of the JS_NewCClosure every other
// callback in this facade is built on. That's not a style inconsistency:
// JS_NewCClosure's constructor calls hand the callback an auto-allocated
// `this_val` whose prototype is NOT new_target.prototype and whose class
// is NOT this facade's wrapper class (confirmed empirically against
// quickjs-ng v0.16.2 -- see the top-level README's "How constructor
// support works" section for the full story), making both
// `instanceof` and Object::Wrap unusable from inside such a callback.
// JS_CFUNC_constructor_magic callbacks receive `new_target` directly, so
// this file reads new_target.prototype itself and builds the instance
// (JS_NewObjectProtoClass, correctly classed and prototyped) before ever
// calling the user's FunctionCallback -- verified against the real
// library, not just plausible-looking code.
//
// JS_NewCFunction3 has no opaque-data slot, so a constructor's
// {Isolate*, FunctionCallback, wrappable} can't travel as a closure the
// way every other callback's does; ConstructorRegistry below is a small
// process-lifetime side table indexed by the integer `magic` parameter
// instead. Entries are never freed (matches this port's GCInfoTable, which
// also grows unboundedly by design -- see that file's comment) since
// constructors are created once per interface type, not per call.
#include "v8-template.h"

#include <string>
#include <vector>

#include "quickjs.h"
#include "src/base/logging.h"
#include "v8-exception.h"
#include "v8-function-callback.h"
#include "v8-function.h"
#include "v8-isolate.h"
#include "v8-object.h"
#include "v8-primitive.h"
#include "v8-property-callback.h"

namespace v8 {

namespace {

struct CallbackData {
  Isolate* isolate;
  FunctionCallback callback;
};

void FreeCallbackData(void* opaque) { delete static_cast<CallbackData*>(opaque); }

JSValue InvokeCallback(JSContext* ctx, JSValueConst this_val, int argc,
                       JSValueConst* argv, int /*magic*/, void* opaque) {
  auto* data = static_cast<CallbackData*>(opaque);
  JSValue return_value = JS_UNDEFINED;
  FunctionCallbackInfo<Value> info(data->isolate, ctx, this_val, argc,
                                   const_cast<JSValue*>(argv), &return_value);
  data->callback(info);
  return return_value;
}

struct AccessorData {
  Isolate* isolate;
  AccessorGetterCallback getter;
  AccessorSetterCallback setter;
  std::string name;
};

void FreeAccessorData(void* opaque) { delete static_cast<AccessorData*>(opaque); }

JSValue InvokeGetter(JSContext* ctx, JSValueConst this_val, int /*argc*/,
                     JSValueConst* /*argv*/, int /*magic*/, void* opaque) {
  auto* data = static_cast<AccessorData*>(opaque);
  JSValue return_value = JS_UNDEFINED;
  Local<String> prop_name =
      String::NewFromUtf8(data->isolate, data->name.c_str()).ToLocalChecked();
  PropertyCallbackInfo<Value> info(data->isolate, ctx, this_val, &return_value);
  data->getter(prop_name, info);
  return return_value;
}

JSValue InvokeSetter(JSContext* ctx, JSValueConst this_val, int argc,
                     JSValueConst* argv, int /*magic*/, void* opaque) {
  auto* data = static_cast<AccessorData*>(opaque);
  Local<String> prop_name =
      String::NewFromUtf8(data->isolate, data->name.c_str()).ToLocalChecked();
  Local<Value> value = Local<Value>::Adopt(
      ctx, JS_DupValue(ctx, argc > 0 ? argv[0] : JS_UNDEFINED));
  PropertyCallbackInfo<void> info(data->isolate, ctx, this_val);
  data->setter(prop_name, value, info);
  return JS_UNDEFINED;
}

struct ConstructorEntry {
  Isolate* isolate;
  FunctionCallback callback;
  bool wrappable;
};

std::vector<ConstructorEntry>& ConstructorRegistry() {
  static std::vector<ConstructorEntry> registry;
  return registry;
}

JSValue InvokeConstructor(JSContext* ctx, JSValueConst new_target, int argc,
                         JSValueConst* argv, int magic) {
  const ConstructorEntry& entry = ConstructorRegistry()[static_cast<size_t>(magic)];

  JSValue proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  JSValue instance =
      entry.wrappable
          ? JS_NewObjectProtoClass(ctx, proto,
                                   entry.isolate->wrapper_class_id_for_wasmv8_internal())
          : JS_NewObjectProto(ctx, proto);
  JS_FreeValue(ctx, proto);
  if (JS_IsException(instance)) return instance;

  JSValue return_value = JS_UNDEFINED;  // unused: a constructor always
                                        // yields `instance` itself (see
                                        // file comment) -- offered anyway
                                        // so FunctionCallbackInfo stays
                                        // the one shape every callback
                                        // in this facade uses.
  FunctionCallbackInfo<Value> info(entry.isolate, ctx, instance, argc,
                                   const_cast<JSValue*>(argv), &return_value);
  entry.callback(info);
  JS_FreeValue(ctx, return_value);
  return instance;
}

}  // namespace

Local<FunctionTemplate> FunctionTemplate::New(Isolate* isolate,
                                              FunctionCallback callback) {
  auto impl = std::make_shared<FunctionTemplate>();
  impl->isolate_ = isolate;
  impl->callback_ = callback;
  return Local<FunctionTemplate>::Adopt(std::move(impl));
}

FunctionTemplate::~FunctionTemplate() {
  if (cached_ctx_ && !JS_IsUndefined(cached_fn_)) {
    JS_FreeValue(cached_ctx_, cached_fn_);
  }
}

Local<ObjectTemplate> FunctionTemplate::PrototypeTemplate() {
  touched_ = true;
  if (prototype_template_.IsEmpty()) prototype_template_ = ObjectTemplate::New(isolate_);
  return prototype_template_;
}

Local<ObjectTemplate> FunctionTemplate::InstanceTemplate() {
  touched_ = true;
  if (instance_template_.IsEmpty()) instance_template_ = ObjectTemplate::New(isolate_);
  return instance_template_;
}

void FunctionTemplate::Inherit(Local<FunctionTemplate> parent) {
  touched_ = true;
  parent_ = std::move(parent);
}

MaybeLocal<Function> FunctionTemplate::GetFunction(Local<Context> context) {
  CHECK(callback_);
  JSContext* ctx = context.context_for_wasmv8_internal();

  if (cached_ctx_ == ctx && !JS_IsUndefined(cached_fn_)) {
    return Local<Function>::Adopt(ctx, JS_DupValue(ctx, cached_fn_));
  }

  if (!touched_) {
    // Plain callable function -- e.g. Console.log. No constructor
    // machinery needed.
    auto* data = new CallbackData{isolate_, callback_};
    JSValue fn = JS_NewCClosure(ctx, InvokeCallback, "", FreeCallbackData, 0, 0, data);
    if (JS_IsException(fn)) {
      delete data;
      TryCatch::ReportException_for_wasmv8_internal(ctx);
      return MaybeLocal<Function>();
    }
    cached_ctx_ = ctx;
    cached_fn_ = JS_DupValue(ctx, fn);
    return Local<Function>::Adopt(ctx, fn);
  }

  // Constructible: register {isolate, callback, wrappable} and hand out the
  // registry index as `magic` -- see file comment for why (JS_NewCFunction3
  // has no opaque-data slot).
  const bool wrappable =
      !instance_template_.IsEmpty() && instance_template_->wrappable_for_wasmv8_internal();
  ConstructorRegistry().push_back({isolate_, callback_, wrappable});
  const int magic = static_cast<int>(ConstructorRegistry().size() - 1);

  JSValue fn = JS_NewCFunction3(ctx, reinterpret_cast<JSCFunction*>(InvokeConstructor),
                                "", 0, JS_CFUNC_constructor_magic, magic,
                                JS_UNDEFINED, 0);
  if (JS_IsException(fn)) {
    TryCatch::ReportException_for_wasmv8_internal(ctx);
    return MaybeLocal<Function>();
  }

  // Inherit(parent): materialize the parent constructor too (recursing up
  // the chain as needed) and chain both the function object itself
  // (`child.__proto__ === parent`, static-property inheritance) and the
  // about-to-be-built prototype object (`child.prototype.__proto__ ===
  // parent.prototype`, instance method/accessor inheritance) -- the same
  // two links ES6 `class Child extends Parent` sets up.
  JSValue parent_proto = JS_UNDEFINED;
  if (!parent_.IsEmpty()) {
    MaybeLocal<Function> parent_fn = parent_->GetFunction(context);
    if (parent_fn.IsEmpty()) {
      JS_FreeValue(ctx, fn);
      return MaybeLocal<Function>();
    }
    // Kept alive as a named local (not a temporary) for as long as
    // `parent_fn_val` is used below -- Local<Function>'s destructor frees
    // its JSValue, so a temporary would leave parent_fn_val dangling
    // before JS_SetPrototype/JS_GetPropertyStr ever ran.
    Local<Function> parent_function = parent_fn.ToLocalChecked();
    JSValue parent_fn_val = parent_function.value_for_wasmv8_internal();
    JS_SetPrototype(ctx, fn, parent_fn_val);
    parent_proto = JS_GetPropertyStr(ctx, parent_fn_val, "prototype");
  }

  JSValue proto =
      JS_IsUndefined(parent_proto) ? JS_NewObject(ctx) : JS_NewObjectProto(ctx, parent_proto);
  JS_FreeValue(ctx, parent_proto);
  if (!prototype_template_.IsEmpty() &&
      !prototype_template_->InstallMembersOn_for_wasmv8_internal(context, proto)) {
    JS_FreeValue(ctx, proto);
    JS_FreeValue(ctx, fn);
    return MaybeLocal<Function>();
  }
  JS_DefinePropertyValueStr(ctx, fn, "prototype", proto, 0);

  cached_ctx_ = ctx;
  cached_fn_ = JS_DupValue(ctx, fn);
  return Local<Function>::Adopt(ctx, fn);
}

Local<ObjectTemplate> ObjectTemplate::New(Isolate* isolate) {
  auto impl = std::make_shared<ObjectTemplate>();
  impl->isolate_ = isolate;
  return Local<ObjectTemplate>::Adopt(std::move(impl));
}

void ObjectTemplate::Set(const char* name, Local<FunctionTemplate> function_template) {
  methods_.push_back({name, std::move(function_template)});
}

void ObjectTemplate::SetAccessor(const char* name, AccessorGetterCallback getter,
                                 AccessorSetterCallback setter) {
  accessors_.push_back({name, getter, setter});
}

void ObjectTemplate::SetInternalFieldCount(int count) { wrappable_ = count > 0; }

bool ObjectTemplate::InstallMembersOn_for_wasmv8_internal(Local<Context> context,
                                                          JSValueConst target) {
  JSContext* ctx = context.context_for_wasmv8_internal();
  Local<Object> target_obj = Local<Object>::Adopt(ctx, JS_DupValue(ctx, target));

  for (auto& method : methods_) {
    MaybeLocal<Function> fn = method.function_template->GetFunction(context);
    if (fn.IsEmpty()) return false;
    target_obj->Set(isolate_, method.name.c_str(), fn.ToLocalChecked());
  }

  for (auto& accessor : accessors_) {
    auto* getter_data = new AccessorData{isolate_, accessor.getter, nullptr, accessor.name};
    JSValue getter_fn =
        JS_NewCClosure(ctx, InvokeGetter, "", FreeAccessorData, 0, 0, getter_data);
    if (JS_IsException(getter_fn)) {
      delete getter_data;
      TryCatch::ReportException_for_wasmv8_internal(ctx);
      return false;
    }
    JSValue setter_fn = JS_UNDEFINED;
    if (accessor.setter) {
      auto* setter_data =
          new AccessorData{isolate_, nullptr, accessor.setter, accessor.name};
      setter_fn =
          JS_NewCClosure(ctx, InvokeSetter, "", FreeAccessorData, 1, 0, setter_data);
      if (JS_IsException(setter_fn)) {
        delete setter_data;
        JS_FreeValue(ctx, getter_fn);
        TryCatch::ReportException_for_wasmv8_internal(ctx);
        return false;
      }
    }
    JSAtom atom = JS_NewAtom(ctx, accessor.name.c_str());
    JS_DefinePropertyGetSet(ctx, target, atom, getter_fn, setter_fn,
                            JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE);
    JS_FreeAtom(ctx, atom);
  }
  return true;
}

MaybeLocal<Object> ObjectTemplate::NewInstance(Local<Context> context) {
  JSContext* ctx = context.context_for_wasmv8_internal();
  JSValue obj = wrappable_
                    ? JS_NewObjectClass(ctx, isolate_->wrapper_class_id_for_wasmv8_internal())
                    : JS_NewObject(ctx);
  if (JS_IsException(obj)) {
    TryCatch::ReportException_for_wasmv8_internal(ctx);
    return MaybeLocal<Object>();
  }
  if (!InstallMembersOn_for_wasmv8_internal(context, obj)) {
    JS_FreeValue(ctx, obj);
    return MaybeLocal<Object>();
  }
  return Local<Object>::Adopt(ctx, obj);
}

}  // namespace v8
