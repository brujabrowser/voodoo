// Minimal stand-in for V8's src/base/macros.h -- only the handful of
// macros the ported cppgc-internal files actually use. No component-build
// export/import dance (this is a static lib, like brujac/wck/wcb are), so
// V8_EXPORT / V8_EXPORT_PRIVATE / V8_EXPORT_ENUM_CLASS are all no-ops.
#ifndef WASMV8_SRC_BASE_MACROS_H_
#define WASMV8_SRC_BASE_MACROS_H_

#include <cstddef>

#define V8_EXPORT
#define V8_EXPORT_PRIVATE
#define V8_INLINE inline
#define V8_NOINLINE

#define DISALLOW_NEW_AND_DELETE()                    \
 public:                                              \
  void* operator new(size_t) = delete;                \
  void operator delete(void*) = delete;                \
  void* operator new[](size_t) = delete;               \
  void operator delete[](void*) = delete;               \
  static_assert(true, "Force semicolon.")

#define DISALLOW_COPY_AND_ASSIGN(TypeName) \
  TypeName(const TypeName&) = delete;      \
  TypeName& operator=(const TypeName&) = delete

template <typename T, size_t N>
constexpr size_t arraysize(const T (&)[N]) {
  return N;
}

#endif  // WASMV8_SRC_BASE_MACROS_H_
