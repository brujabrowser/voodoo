#ifndef MOJO_PUBLIC_C_SYSTEM_FUNCTIONS_H_
#define MOJO_PUBLIC_C_SYSTEM_FUNCTIONS_H_

#include "mojo/public/c/system/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t MojoInitializeFlags;
typedef struct MojoInitializeOptions {
  uint32_t struct_size;
  MojoInitializeFlags flags;
  const char* mojo_core_path;
  uint32_t mojo_core_path_length;
} MojoInitializeOptions;

typedef uint32_t MojoShutdownFlags;
typedef struct MojoShutdownOptions {
  uint32_t struct_size;
  MojoShutdownFlags flags;
} MojoShutdownOptions;

typedef uint32_t MojoQuotaType;
#define MOJO_QUOTA_TYPE_MAX_UNREAD_MESSAGE_COUNT ((MojoQuotaType)0)
#define MOJO_QUOTA_TYPE_MAX_UNREAD_MESSAGE_SIZE ((MojoQuotaType)1)

typedef uint32_t MojoSetQuotaFlags;
typedef struct MojoSetQuotaOptions {
  uint32_t struct_size;
  MojoSetQuotaFlags flags;
} MojoSetQuotaOptions;

typedef uint32_t MojoQueryQuotaFlags;
typedef struct MojoQueryQuotaOptions {
  uint32_t struct_size;
  MojoQueryQuotaFlags flags;
} MojoQueryQuotaOptions;

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // MOJO_PUBLIC_C_SYSTEM_FUNCTIONS_H_
