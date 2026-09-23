// C++ wrapping of WASMThunker's Mojo C platform-handle ABI
// (mojo/public/c/system/platform_handle.h). CadidumKernel is the sys::
// rung; a `handle<platform>` in .voodoom/.mojom maps to this type, the
// same way Chromium's mojom generator maps it to mojo::PlatformHandle.
#ifndef MOJO_PUBLIC_CPP_SYSTEM_PLATFORM_HANDLE_H_
#define MOJO_PUBLIC_CPP_SYSTEM_PLATFORM_HANDLE_H_

#include "mojo/public/c/system/platform_handle.h"
#include "mojo/public/cpp/system/handle.h"

#include <utility>

namespace mojo {

// Move-only wrapper around a Mojo handle that owns a wrapped OS primitive
// (HANDLE on Windows, fd elsewhere). Wrap/Unwrap call Thunker's
// MojoWrapPlatformHandle / MojoUnwrapPlatformHandle. On the wire the type
// transits like the other handle<*> kinds (AttachHandle / TakeHandles).
class PlatformHandle {
 public:
  PlatformHandle() = default;
  explicit PlatformHandle(ScopedHandle handle) : handle_(std::move(handle)) {}
  explicit PlatformHandle(Handle handle) : handle_(handle) {}

  PlatformHandle(PlatformHandle&&) noexcept = default;
  PlatformHandle& operator=(PlatformHandle&&) noexcept = default;
  PlatformHandle(const PlatformHandle&) = delete;
  PlatformHandle& operator=(const PlatformHandle&) = delete;

  bool is_valid() const { return handle_.is_valid(); }

  Handle release() { return handle_.release(); }

  ScopedHandle TakeHandle() { return std::move(handle_); }

  // Takes ownership of the OS primitive in `native`.
  static PlatformHandle Wrap(MojoPlatformHandle native) {
    MojoHandle mh = MOJO_HANDLE_INVALID;
    if (MojoWrapPlatformHandle(&native, nullptr, &mh) != MOJO_RESULT_OK) {
      return PlatformHandle();
    }
    return PlatformHandle(ScopedHandle(Handle(mh)));
  }

  // Consumes this Mojo handle and writes the OS primitive into `native`
  // without closing it.
  bool Unwrap(MojoPlatformHandle* native) && {
    if (!native || !handle_.is_valid()) {
      return false;
    }
    native->struct_size = sizeof(*native);
    native->type = MOJO_PLATFORM_HANDLE_TYPE_INVALID;
    native->value = 0;
    MojoHandle mh = handle_.release().value();
    return MojoUnwrapPlatformHandle(mh, nullptr, native) == MOJO_RESULT_OK;
  }

 private:
  ScopedHandle handle_;
};

}  // namespace mojo

#endif  // MOJO_PUBLIC_CPP_SYSTEM_PLATFORM_HANDLE_H_
