#ifndef MOJO_PUBLIC_C_SYSTEM_PLATFORM_HANDLE_H_
#define MOJO_PUBLIC_C_SYSTEM_PLATFORM_HANDLE_H_

#include "mojo/public/c/system/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t MojoPlatformHandleType;
#define MOJO_PLATFORM_HANDLE_TYPE_INVALID ((MojoPlatformHandleType)0)
#define MOJO_PLATFORM_HANDLE_TYPE_FILE_DESCRIPTOR ((MojoPlatformHandleType)1)
#define MOJO_PLATFORM_HANDLE_TYPE_WINDOWS_HANDLE ((MojoPlatformHandleType)2)
#define MOJO_PLATFORM_HANDLE_TYPE_MACH_PORT ((MojoPlatformHandleType)3)

typedef struct MojoPlatformHandle {
  uint32_t struct_size;
  MojoPlatformHandleType type;
  uint64_t value;
} MojoPlatformHandle;

typedef struct MojoPlatformProcessHandle {
  uint32_t struct_size;
  uint64_t value;
} MojoPlatformProcessHandle;

typedef uint32_t MojoWrapPlatformHandleFlags;
typedef struct MojoWrapPlatformHandleOptions {
  uint32_t struct_size;
  MojoWrapPlatformHandleFlags flags;
} MojoWrapPlatformHandleOptions;

typedef uint32_t MojoUnwrapPlatformHandleFlags;
typedef struct MojoUnwrapPlatformHandleOptions {
  uint32_t struct_size;
  MojoUnwrapPlatformHandleFlags flags;
} MojoUnwrapPlatformHandleOptions;

typedef uint32_t MojoPlatformSharedMemoryRegionAccessMode;
#define MOJO_PLATFORM_SHARED_MEMORY_REGION_ACCESS_MODE_UNSAFE \
  ((MojoPlatformSharedMemoryRegionAccessMode)0)
#define MOJO_PLATFORM_SHARED_MEMORY_REGION_ACCESS_MODE_READ_ONLY \
  ((MojoPlatformSharedMemoryRegionAccessMode)1)
#define MOJO_PLATFORM_SHARED_MEMORY_REGION_ACCESS_MODE_WRITABLE \
  ((MojoPlatformSharedMemoryRegionAccessMode)2)

typedef uint32_t MojoWrapPlatformSharedMemoryRegionFlags;
typedef struct MojoWrapPlatformSharedMemoryRegionOptions {
  uint32_t struct_size;
  MojoWrapPlatformSharedMemoryRegionFlags flags;
} MojoWrapPlatformSharedMemoryRegionOptions;

typedef uint32_t MojoUnwrapPlatformSharedMemoryRegionFlags;
typedef struct MojoUnwrapPlatformSharedMemoryRegionOptions {
  uint32_t struct_size;
  MojoUnwrapPlatformSharedMemoryRegionFlags flags;
} MojoUnwrapPlatformSharedMemoryRegionOptions;

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // MOJO_PUBLIC_C_SYSTEM_PLATFORM_HANDLE_H_
