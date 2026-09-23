// Minimal stand-in for V8's top-level include/v8config.h -- just the macros
// the ported sandbox headers actually reference. Identical in spirit to
// ../WASMv8bindings/include/v8config.h (same family, same reasoning): this
// is a static lib, so there is no component-build shared-library boundary to
// cross, and V8_EXPORT/V8_EXPORT_PRIVATE are no-ops rather than dllexport/
// visibility attributes.
#ifndef WASMSAFESPACE_INCLUDE_V8CONFIG_H_
#define WASMSAFESPACE_INCLUDE_V8CONFIG_H_

#define V8_EXPORT
#define V8_EXPORT_PRIVATE
#define V8_INLINE inline
#define V8_NOINLINE
#define V8_PRESERVE_MOST

#if defined(__GNUC__) || defined(__clang__)
#define V8_LIKELY(x) __builtin_expect(!!(x), 1)
#define V8_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define V8_LIKELY(x) (x)
#define V8_UNLIKELY(x) (x)
#endif

#if defined(__clang__)
#define V8_TRIVIAL_ABI [[clang::trivial_abi]]
#else
#define V8_TRIVIAL_ABI
#endif

#define V8_CLANG_NO_SANITIZE(what)

#if defined(_MSC_VER) && !defined(__clang__)
#define V8_CC_MSVC 1
#else
#define V8_CC_GNU 1
#endif

#if defined(__wasm64__) || defined(_WIN64) || defined(__LP64__) || \
    defined(__x86_64__) || defined(__aarch64__)
#define V8_HOST_ARCH_64_BIT 1
#else
#define V8_HOST_ARCH_32_BIT 1
#endif

#endif  // WASMSAFESPACE_INCLUDE_V8CONFIG_H_
