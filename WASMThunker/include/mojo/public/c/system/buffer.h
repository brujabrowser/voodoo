#ifndef MOJO_PUBLIC_C_SYSTEM_BUFFER_H_
#define MOJO_PUBLIC_C_SYSTEM_BUFFER_H_

#include "mojo/public/c/system/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t MojoCreateSharedBufferFlags;
typedef struct MojoCreateSharedBufferOptions {
  uint32_t struct_size;
  MojoCreateSharedBufferFlags flags;
} MojoCreateSharedBufferOptions;

typedef uint32_t MojoDuplicateBufferHandleFlags;
typedef struct MojoDuplicateBufferHandleOptions {
  uint32_t struct_size;
  MojoDuplicateBufferHandleFlags flags;
} MojoDuplicateBufferHandleOptions;

typedef uint32_t MojoMapBufferFlags;
typedef struct MojoMapBufferOptions {
  uint32_t struct_size;
  MojoMapBufferFlags flags;
} MojoMapBufferOptions;

typedef uint32_t MojoGetBufferInfoFlags;
typedef struct MojoGetBufferInfoOptions {
  uint32_t struct_size;
  MojoGetBufferInfoFlags flags;
} MojoGetBufferInfoOptions;

typedef struct MojoSharedBufferInfo {
  uint32_t struct_size;
  uint64_t num_bytes;
} MojoSharedBufferInfo;

typedef struct MojoSharedBufferGuid {
  uint64_t high;
  uint64_t low;
} MojoSharedBufferGuid;

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // MOJO_PUBLIC_C_SYSTEM_BUFFER_H_
