// v8::ArrayBuffer on quickjs JS_NewArrayBufferCopy / JS_GetArrayBuffer.
#ifndef WASMV8_INCLUDE_V8_ARRAY_BUFFER_H_
#define WASMV8_INCLUDE_V8_ARRAY_BUFFER_H_

#include <stddef.h>
#include <stdint.h>

#include "v8-object.h"

namespace v8 {

class Isolate;

class ArrayBuffer : public Object {
 public:
  ArrayBuffer() = default;
  ArrayBuffer(JSContext* ctx, JSValue val) : Object(ctx, val) {}

  static Local<ArrayBuffer> New(Isolate* isolate, size_t byte_length);
  static Local<ArrayBuffer> New(Isolate* isolate, const uint8_t* data, size_t byte_length);

  size_t ByteLength() const;
  void* Data() const;
};

}  // namespace v8

#endif  // WASMV8_INCLUDE_V8_ARRAY_BUFFER_H_
