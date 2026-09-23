#ifndef MOJO_PUBLIC_C_SYSTEM_MESSAGE_PIPE_H_
#define MOJO_PUBLIC_C_SYSTEM_MESSAGE_PIPE_H_

#include "mojo/public/c/system/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t MojoCreateMessagePipeFlags;
#define MOJO_CREATE_MESSAGE_PIPE_FLAG_NONE ((MojoCreateMessagePipeFlags)0)
typedef struct MojoCreateMessagePipeOptions {
  uint32_t struct_size;
  MojoCreateMessagePipeFlags flags;
} MojoCreateMessagePipeOptions;

typedef uint32_t MojoWriteMessageFlags;
#define MOJO_WRITE_MESSAGE_FLAG_NONE ((MojoWriteMessageFlags)0)
typedef struct MojoWriteMessageOptions {
  uint32_t struct_size;
  MojoWriteMessageFlags flags;
} MojoWriteMessageOptions;

typedef uint32_t MojoReadMessageFlags;
#define MOJO_READ_MESSAGE_FLAG_NONE ((MojoReadMessageFlags)0)
typedef struct MojoReadMessageOptions {
  uint32_t struct_size;
  MojoReadMessageFlags flags;
} MojoReadMessageOptions;

typedef uint32_t MojoFuseMessagePipesFlags;
#define MOJO_FUSE_MESSAGE_PIPES_FLAG_NONE ((MojoFuseMessagePipesFlags)0)
typedef struct MojoFuseMessagePipesOptions {
  uint32_t struct_size;
  MojoFuseMessagePipesFlags flags;
} MojoFuseMessagePipesOptions;

typedef uint32_t MojoCreateMessageFlags;
#define MOJO_CREATE_MESSAGE_FLAG_NONE ((MojoCreateMessageFlags)0)
typedef struct MojoCreateMessageOptions {
  uint32_t struct_size;
  MojoCreateMessageFlags flags;
} MojoCreateMessageOptions;

typedef uint32_t MojoAppendMessageDataFlags;
#define MOJO_APPEND_MESSAGE_DATA_FLAG_NONE ((MojoAppendMessageDataFlags)0)
#define MOJO_APPEND_MESSAGE_DATA_FLAG_COMMIT_SIZE \
  ((MojoAppendMessageDataFlags)1 << 0)
typedef struct MojoAppendMessageDataOptions {
  uint32_t struct_size;
  MojoAppendMessageDataFlags flags;
} MojoAppendMessageDataOptions;

typedef uint32_t MojoSerializeMessageFlags;
typedef struct MojoSerializeMessageOptions {
  uint32_t struct_size;
  MojoSerializeMessageFlags flags;
} MojoSerializeMessageOptions;

typedef uint32_t MojoGetMessageDataFlags;
#define MOJO_GET_MESSAGE_DATA_FLAG_NONE ((MojoGetMessageDataFlags)0)
typedef struct MojoGetMessageDataOptions {
  uint32_t struct_size;
  MojoGetMessageDataFlags flags;
} MojoGetMessageDataOptions;

typedef uint32_t MojoSetMessageContextFlags;
typedef struct MojoSetMessageContextOptions {
  uint32_t struct_size;
  MojoSetMessageContextFlags flags;
} MojoSetMessageContextOptions;
typedef void (*MojoMessageContextSerializer)(uintptr_t context,
                                             MojoMessageHandle message);
typedef void (*MojoMessageContextDestructor)(uintptr_t context);

typedef uint32_t MojoGetMessageContextFlags;
typedef struct MojoGetMessageContextOptions {
  uint32_t struct_size;
  MojoGetMessageContextFlags flags;
} MojoGetMessageContextOptions;

typedef uint32_t MojoNotifyBadMessageFlags;
typedef struct MojoNotifyBadMessageOptions {
  uint32_t struct_size;
  MojoNotifyBadMessageFlags flags;
} MojoNotifyBadMessageOptions;

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // MOJO_PUBLIC_C_SYSTEM_MESSAGE_PIPE_H_
