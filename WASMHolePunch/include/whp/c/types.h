#ifndef WHP_C_TYPES_H_
#define WHP_C_TYPES_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t WhpHandle;
typedef uint32_t WhpMessageHandle;
typedef uint32_t WhpResult;
typedef uint32_t WhpHandleSignals;

#define WHP_HANDLE_INVALID ((WhpHandle)0)
#define WHP_MESSAGE_HANDLE_INVALID ((WhpMessageHandle)0)

// Numeric values match Chromium mojo/public/c/system/types.h.
#define WHP_RESULT_OK 0u
#define WHP_RESULT_CANCELLED 1u
#define WHP_RESULT_UNKNOWN 2u
#define WHP_RESULT_INVALID_ARGUMENT 3u
#define WHP_RESULT_DEADLINE_EXCEEDED 4u
#define WHP_RESULT_NOT_FOUND 5u
#define WHP_RESULT_ALREADY_EXISTS 6u
#define WHP_RESULT_PERMISSION_DENIED 7u
#define WHP_RESULT_RESOURCE_EXHAUSTED 8u
#define WHP_RESULT_FAILED_PRECONDITION 9u
#define WHP_RESULT_ABORTED 10u
#define WHP_RESULT_OUT_OF_RANGE 11u
#define WHP_RESULT_UNIMPLEMENTED 12u
#define WHP_RESULT_INTERNAL 13u
#define WHP_RESULT_UNAVAILABLE 14u
#define WHP_RESULT_DATA_LOSS 15u
#define WHP_RESULT_BUSY 16u
#define WHP_RESULT_SHOULD_WAIT 17u

#define WHP_HANDLE_SIGNAL_NONE 0u
#define WHP_HANDLE_SIGNAL_READABLE (1u << 0)
#define WHP_HANDLE_SIGNAL_WRITABLE (1u << 1)
#define WHP_HANDLE_SIGNAL_PEER_CLOSED (1u << 2)
#define WHP_HANDLE_SIGNAL_PEER_REMOTE (1u << 3)

// Numeric values match mojo/public/c/system/platform_handle.h.
typedef uint32_t WhpPlatformHandleType;
#define WHP_PLATFORM_HANDLE_TYPE_INVALID ((WhpPlatformHandleType)0)
#define WHP_PLATFORM_HANDLE_TYPE_FILE_DESCRIPTOR ((WhpPlatformHandleType)1)
#define WHP_PLATFORM_HANDLE_TYPE_WINDOWS_HANDLE ((WhpPlatformHandleType)2)
#define WHP_PLATFORM_HANDLE_TYPE_MACH_PORT ((WhpPlatformHandleType)3)

typedef uint32_t WhpPlatformSharedMemoryRegionAccessMode;
#define WHP_PLATFORM_SHARED_MEMORY_REGION_ACCESS_MODE_UNSAFE \
  ((WhpPlatformSharedMemoryRegionAccessMode)0)
#define WHP_PLATFORM_SHARED_MEMORY_REGION_ACCESS_MODE_READ_ONLY \
  ((WhpPlatformSharedMemoryRegionAccessMode)1)
#define WHP_PLATFORM_SHARED_MEMORY_REGION_ACCESS_MODE_WRITABLE \
  ((WhpPlatformSharedMemoryRegionAccessMode)2)

typedef uint32_t WhpQuotaType;
#define WHP_QUOTA_TYPE_MAX_UNREAD_MESSAGE_COUNT ((WhpQuotaType)0)
#define WHP_QUOTA_TYPE_MAX_UNREAD_MESSAGE_SIZE ((WhpQuotaType)1)

typedef struct WhpHandleSignalsState {
  WhpHandleSignals satisfied_signals;
  WhpHandleSignals satisfiable_signals;
} WhpHandleSignalsState;

#define WHP_TRIGGER_CONDITION_SIGNALS_SATISFIED 0u
#define WHP_TRIGGER_CONDITION_SIGNALS_UNSATISFIABLE 1u

#define WHP_APPEND_MESSAGE_DATA_FLAG_NONE 0u
#define WHP_APPEND_MESSAGE_DATA_FLAG_COMMIT_SIZE (1u << 0)

#define WHP_TRAP_EVENT_FLAG_NONE 0u
#define WHP_TRAP_EVENT_FLAG_WITHIN_API_CALL (1u << 0)

typedef struct WhpCreateMessagePipeOptions {
  uint32_t struct_size;
} WhpCreateMessagePipeOptions;

typedef struct WhpCreateMessageOptions {
  uint32_t struct_size;
} WhpCreateMessageOptions;

typedef struct WhpAppendMessageDataOptions {
  uint32_t struct_size;
  uint32_t flags;
} WhpAppendMessageDataOptions;

typedef struct WhpGetMessageDataOptions {
  uint32_t struct_size;
} WhpGetMessageDataOptions;

typedef struct WhpWriteMessageOptions {
  uint32_t struct_size;
} WhpWriteMessageOptions;

typedef struct WhpReadMessageOptions {
  uint32_t struct_size;
} WhpReadMessageOptions;

typedef struct WhpCreateTrapOptions {
  uint32_t struct_size;
} WhpCreateTrapOptions;

typedef struct WhpAddTriggerOptions {
  uint32_t struct_size;
} WhpAddTriggerOptions;

typedef struct WhpRemoveTriggerOptions {
  uint32_t struct_size;
} WhpRemoveTriggerOptions;

typedef struct WhpArmTrapOptions {
  uint32_t struct_size;
} WhpArmTrapOptions;

typedef struct WhpCreateDataPipeOptions {
  uint32_t struct_size;
  uint32_t flags;
  uint32_t element_num_bytes;
  uint32_t capacity_num_bytes;
} WhpCreateDataPipeOptions;

typedef struct WhpCreateSharedBufferOptions {
  uint32_t struct_size;
} WhpCreateSharedBufferOptions;

typedef struct WhpTrapEvent {
  uint32_t struct_size;
  uint32_t flags;
  uintptr_t trigger_context;
  WhpResult result;
  WhpHandleSignalsState signals_state;
} WhpTrapEvent;

typedef void (*WhpTrapEventHandler)(const struct WhpTrapEvent* event);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // WHP_C_TYPES_H_
