// Minimal stand-in for V8's src/base/build_config.h -- just the one macro
// src/heap/cppgc-internal/globals.h actually reads. No CPPGC_CAGED_HEAP,
// no CPPGC_POINTER_COMPRESSION anywhere in this port: both require an OS
// virtual-memory cage reservation (and, in real V8, feed the sandbox's
// trusted-pointer-table indirection) that wasm32-wasip1's flat linear
// memory has no equivalent of. Every on-heap pointer here is a plain,
// uncompressed native pointer.
#ifndef WASMV8_SRC_BASE_BUILD_CONFIG_H_
#define WASMV8_SRC_BASE_BUILD_CONFIG_H_

#if defined(__wasm64__) || defined(_WIN64) || defined(__LP64__) || \
    defined(__x86_64__) || defined(__aarch64__)
#define V8_HOST_ARCH_64_BIT 1
#else
#define V8_HOST_ARCH_32_BIT 1
#endif

#endif  // WASMV8_SRC_BASE_BUILD_CONFIG_H_
