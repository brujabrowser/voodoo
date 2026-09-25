#include "v8-array-buffer.h"

#include <vector>

#include "v8-context.h"
#include "v8-isolate.h"

namespace v8 {

Local<ArrayBuffer> ArrayBuffer::New(Isolate* isolate, size_t byte_length) {
  std::vector<uint8_t> zeros(byte_length);
  return New(isolate, zeros.data(), byte_length);
}

Local<ArrayBuffer> ArrayBuffer::New(Isolate* isolate, const uint8_t* data, size_t byte_length) {
  JSContext* ctx = CurrentContext(isolate);
  return Local<ArrayBuffer>::Adopt(ctx, JS_NewArrayBufferCopy(ctx, data, byte_length));
}

size_t ArrayBuffer::ByteLength() const {
  size_t n = 0;
  JS_GetArrayBuffer(ctx_, &n, val_);
  return n;
}

void* ArrayBuffer::Data() const {
  size_t n = 0;
  return JS_GetArrayBuffer(ctx_, &n, val_);
}

}  // namespace v8
