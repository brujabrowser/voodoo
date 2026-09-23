#ifndef MOJO_PUBLIC_C_SYSTEM_DATA_PIPE_H_
#define MOJO_PUBLIC_C_SYSTEM_DATA_PIPE_H_

#include "mojo/public/c/system/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t MojoCreateDataPipeFlags;
#define MOJO_CREATE_DATA_PIPE_FLAG_NONE ((MojoCreateDataPipeFlags)0)
typedef struct MojoCreateDataPipeOptions {
  uint32_t struct_size;
  MojoCreateDataPipeFlags flags;
  uint32_t element_num_bytes;
  uint32_t capacity_num_bytes;
} MojoCreateDataPipeOptions;

typedef uint32_t MojoWriteDataFlags;
#define MOJO_WRITE_DATA_FLAG_NONE ((MojoWriteDataFlags)0)
#define MOJO_WRITE_DATA_FLAG_ALL_OR_NONE ((MojoWriteDataFlags)1 << 0)
typedef struct MojoWriteDataOptions {
  uint32_t struct_size;
  MojoWriteDataFlags flags;
} MojoWriteDataOptions;

typedef uint32_t MojoBeginWriteDataFlags;
typedef struct MojoBeginWriteDataOptions {
  uint32_t struct_size;
  MojoBeginWriteDataFlags flags;
} MojoBeginWriteDataOptions;

typedef uint32_t MojoEndWriteDataFlags;
typedef struct MojoEndWriteDataOptions {
  uint32_t struct_size;
  MojoEndWriteDataFlags flags;
} MojoEndWriteDataOptions;

typedef uint32_t MojoReadDataFlags;
#define MOJO_READ_DATA_FLAG_NONE ((MojoReadDataFlags)0)
#define MOJO_READ_DATA_FLAG_ALL_OR_NONE ((MojoReadDataFlags)1 << 0)
#define MOJO_READ_DATA_FLAG_DISCARD ((MojoReadDataFlags)1 << 1)
#define MOJO_READ_DATA_FLAG_QUERY ((MojoReadDataFlags)1 << 2)
#define MOJO_READ_DATA_FLAG_PEEK ((MojoReadDataFlags)1 << 3)
typedef struct MojoReadDataOptions {
  uint32_t struct_size;
  MojoReadDataFlags flags;
} MojoReadDataOptions;

typedef uint32_t MojoBeginReadDataFlags;
typedef struct MojoBeginReadDataOptions {
  uint32_t struct_size;
  MojoBeginReadDataFlags flags;
} MojoBeginReadDataOptions;

typedef uint32_t MojoEndReadDataFlags;
typedef struct MojoEndReadDataOptions {
  uint32_t struct_size;
  MojoEndReadDataFlags flags;
} MojoEndReadDataOptions;

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // MOJO_PUBLIC_C_SYSTEM_DATA_PIPE_H_
