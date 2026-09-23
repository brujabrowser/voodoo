// Simplified from V8's include/v8-primitive.h -- String (UTF-8 in and out,
// matching real V8's String::NewFromUtf8/String::Utf8Value idiom), Number,
// Boolean. Real V8 also has BigInt, Symbol, and several string encodings
// (one-byte/two-byte/external); not ported.
#ifndef WASMV8_INCLUDE_V8_PRIMITIVE_H_
#define WASMV8_INCLUDE_V8_PRIMITIVE_H_

#include "quickjs.h"
#include "v8-local-handle.h"
#include "v8-maybe.h"
#include "v8-value.h"

namespace v8 {

class Isolate;

class String final : public Value {
 public:
  String() = default;
  String(JSContext* ctx, JSValue val) : Value(ctx, val) {}

  static MaybeLocal<String> NewFromUtf8(Isolate* isolate, const char* data);

  // Matches real V8's v8::String::Utf8Value RAII idiom: construct from a
  // Local<Value>, then use like a `const char*`.
  class Utf8Value {
   public:
    Utf8Value(Isolate* isolate, Local<Value> value);
    ~Utf8Value();
    Utf8Value(const Utf8Value&) = delete;
    Utf8Value& operator=(const Utf8Value&) = delete;

    const char* operator*() const { return str_ ? str_ : ""; }
    int length() const { return str_ ? static_cast<int>(len_) : 0; }

   private:
    JSContext* ctx_ = nullptr;
    const char* str_ = nullptr;
    size_t len_ = 0;
  };
};

class Number final : public Value {
 public:
  Number() = default;
  // Qualified as ::v8::Value: Number's own Value() method (below) would
  // otherwise hide the base class *type* named Value within this
  // constructor's mem-initializer lookup, the same "member function name
  // hides an enclosing type name" rule that makes `struct S { int S; };`
  // awkward -- unqualified `Value(ctx, val)` here resolves to the method,
  // not the base class, and fails to compile.
  Number(JSContext* ctx, JSValue val) : ::v8::Value(ctx, val) {}

  static Local<Number> New(Isolate* isolate, double value);
  double Value() const;
};

class Boolean final : public Value {
 public:
  Boolean() = default;
  Boolean(JSContext* ctx, JSValue val) : ::v8::Value(ctx, val) {}  // see Number's comment

  static Local<Boolean> New(Isolate* isolate, bool value);
  bool Value() const { return JS_ToBool(ctx_, val_); }
};

}  // namespace v8

#endif  // WASMV8_INCLUDE_V8_PRIMITIVE_H_
