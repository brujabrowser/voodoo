#ifndef MOJO_PUBLIC_CPP_SYSTEM_BUFFER_H_
#define MOJO_PUBLIC_CPP_SYSTEM_BUFFER_H_

#include "mojo/public/c/system/core.h"
#include "mojo/public/cpp/system/handle.h"

#include <cstdint>
#include <memory>

namespace mojo {

class SharedBufferHandle : public Handle {
 public:
  SharedBufferHandle() = default;
  explicit SharedBufferHandle(MojoHandle value) : Handle(value) {}
};
using ScopedSharedBufferHandle = ScopedHandleBase<SharedBufferHandle>;

class ScopedSharedBufferMapping;
MojoResult MapBuffer(SharedBufferHandle buffer,
                     uint64_t offset,
                     uint64_t num_bytes,
                     ScopedSharedBufferMapping* mapping);

// RAII wrapper around MojoMapBuffer/MojoUnmapBuffer, matching Mojo's
// ScopedSharedBufferMapping.
class ScopedSharedBufferMapping {
 public:
  ScopedSharedBufferMapping() = default;
  ScopedSharedBufferMapping(ScopedSharedBufferMapping&& other) noexcept
      : buffer_(other.buffer_) {
    other.buffer_ = nullptr;
  }
  ScopedSharedBufferMapping& operator=(
      ScopedSharedBufferMapping&& other) noexcept {
    if (this != &other) {
      reset();
      buffer_ = other.buffer_;
      other.buffer_ = nullptr;
    }
    return *this;
  }
  ScopedSharedBufferMapping(const ScopedSharedBufferMapping&) = delete;
  ScopedSharedBufferMapping& operator=(const ScopedSharedBufferMapping&) =
      delete;

  ~ScopedSharedBufferMapping() { reset(); }

  void* get() const { return buffer_; }
  bool is_valid() const { return buffer_ != nullptr; }

  void reset() {
    if (buffer_) {
      MojoUnmapBuffer(buffer_);
      buffer_ = nullptr;
    }
  }

 private:
  friend MojoResult MapBuffer(SharedBufferHandle buffer,
                              uint64_t offset,
                              uint64_t num_bytes,
                              ScopedSharedBufferMapping* mapping);
  void* buffer_ = nullptr;
};

inline MojoResult CreateSharedBuffer(uint64_t num_bytes,
                                     const MojoCreateSharedBufferOptions* options,
                                     ScopedSharedBufferHandle* handle) {
  MojoHandle h = MOJO_HANDLE_INVALID;
  MojoResult result = MojoCreateSharedBuffer(num_bytes, options, &h);
  if (result == MOJO_RESULT_OK) {
    handle->reset(SharedBufferHandle(h));
  }
  return result;
}

inline MojoResult DuplicateBuffer(
    SharedBufferHandle buffer,
    const MojoDuplicateBufferHandleOptions* options,
    ScopedSharedBufferHandle* new_handle) {
  MojoHandle h = MOJO_HANDLE_INVALID;
  MojoResult result = MojoDuplicateBufferHandle(buffer.value(), options, &h);
  if (result == MOJO_RESULT_OK) {
    new_handle->reset(SharedBufferHandle(h));
  }
  return result;
}

inline MojoResult GetBufferInfo(SharedBufferHandle buffer,
                                MojoSharedBufferInfo* info) {
  return MojoGetBufferInfo(buffer.value(), nullptr, info);
}

inline MojoResult MapBuffer(SharedBufferHandle buffer,
                            uint64_t offset,
                            uint64_t num_bytes,
                            ScopedSharedBufferMapping* mapping) {
  void* data = nullptr;
  MojoResult result =
      MojoMapBuffer(buffer.value(), offset, num_bytes, nullptr, &data);
  if (result == MOJO_RESULT_OK) {
    mapping->reset();
    mapping->buffer_ = data;
  }
  return result;
}

}  // namespace mojo

#endif  // MOJO_PUBLIC_CPP_SYSTEM_BUFFER_H_
