#ifndef MOJO_PUBLIC_C_SYSTEM_TRAP_H_
#define MOJO_PUBLIC_C_SYSTEM_TRAP_H_

#include "mojo/public/c/system/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t MojoTrapEventFlags;
#define MOJO_TRAP_EVENT_FLAG_NONE ((MojoTrapEventFlags)0)
#define MOJO_TRAP_EVENT_FLAG_WITHIN_API_CALL ((MojoTrapEventFlags)1 << 0)

// Layout-identical to whp::WhpTrapEvent.
typedef struct MojoTrapEvent {
  uint32_t struct_size;
  MojoTrapEventFlags flags;
  uintptr_t trigger_context;
  MojoResult result;
  MojoHandleSignalsState signals_state;
} MojoTrapEvent;

typedef void (*MojoTrapEventHandler)(const struct MojoTrapEvent* event);

typedef uint32_t MojoCreateTrapFlags;
typedef struct MojoCreateTrapOptions {
  uint32_t struct_size;
  MojoCreateTrapFlags flags;
} MojoCreateTrapOptions;

typedef uint32_t MojoAddTriggerFlags;
typedef struct MojoAddTriggerOptions {
  uint32_t struct_size;
  MojoAddTriggerFlags flags;
} MojoAddTriggerOptions;

typedef uint32_t MojoRemoveTriggerFlags;
typedef struct MojoRemoveTriggerOptions {
  uint32_t struct_size;
  MojoRemoveTriggerFlags flags;
} MojoRemoveTriggerOptions;

typedef uint32_t MojoArmTrapFlags;
typedef struct MojoArmTrapOptions {
  uint32_t struct_size;
  MojoArmTrapFlags flags;
} MojoArmTrapOptions;

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // MOJO_PUBLIC_C_SYSTEM_TRAP_H_
