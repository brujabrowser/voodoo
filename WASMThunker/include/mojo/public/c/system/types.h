// WASMThunker's own reimplementation of the public Mojo C System types.
// Not copied from the Chromium tree: authored to the well-known, stable
// public Mojo C ABI shape so unmodified upstream mojo/public/cpp code can
// link against this shim. Numeric values are kept consistent with
// WASMHolePunch's whp/c/types.h (which documents the same mapping).
#ifndef MOJO_PUBLIC_C_SYSTEM_TYPES_H_
#define MOJO_PUBLIC_C_SYSTEM_TYPES_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t MojoHandle;
typedef uint32_t MojoHandle32;
typedef uint32_t MojoMessageHandle;
typedef uint32_t MojoResult;
typedef int64_t MojoTimeTicks;
typedef uint32_t MojoHandleSignals;
typedef uint32_t MojoTriggerCondition;

#define MOJO_HANDLE_INVALID ((MojoHandle)0)
#define MOJO_MESSAGE_HANDLE_INVALID ((MojoMessageHandle)0)

#define MOJO_RESULT_OK ((MojoResult)0)
#define MOJO_RESULT_CANCELLED ((MojoResult)1)
#define MOJO_RESULT_UNKNOWN ((MojoResult)2)
#define MOJO_RESULT_INVALID_ARGUMENT ((MojoResult)3)
#define MOJO_RESULT_DEADLINE_EXCEEDED ((MojoResult)4)
#define MOJO_RESULT_NOT_FOUND ((MojoResult)5)
#define MOJO_RESULT_ALREADY_EXISTS ((MojoResult)6)
#define MOJO_RESULT_PERMISSION_DENIED ((MojoResult)7)
#define MOJO_RESULT_RESOURCE_EXHAUSTED ((MojoResult)8)
#define MOJO_RESULT_FAILED_PRECONDITION ((MojoResult)9)
#define MOJO_RESULT_ABORTED ((MojoResult)10)
#define MOJO_RESULT_OUT_OF_RANGE ((MojoResult)11)
#define MOJO_RESULT_UNIMPLEMENTED ((MojoResult)12)
#define MOJO_RESULT_INTERNAL ((MojoResult)13)
#define MOJO_RESULT_UNAVAILABLE ((MojoResult)14)
#define MOJO_RESULT_DATA_LOSS ((MojoResult)15)
#define MOJO_RESULT_BUSY ((MojoResult)16)
#define MOJO_RESULT_SHOULD_WAIT ((MojoResult)17)

#define MOJO_HANDLE_SIGNAL_NONE ((MojoHandleSignals)0)
#define MOJO_HANDLE_SIGNAL_READABLE ((MojoHandleSignals)1 << 0)
#define MOJO_HANDLE_SIGNAL_WRITABLE ((MojoHandleSignals)1 << 1)
#define MOJO_HANDLE_SIGNAL_PEER_CLOSED ((MojoHandleSignals)1 << 2)
#define MOJO_HANDLE_SIGNAL_PEER_REMOTE ((MojoHandleSignals)1 << 3)
#define MOJO_HANDLE_SIGNAL_QUOTA_EXCEEDED ((MojoHandleSignals)1 << 4)

#define MOJO_TRIGGER_CONDITION_SIGNALS_SATISFIED \
  ((MojoTriggerCondition)0)
#define MOJO_TRIGGER_CONDITION_SIGNALS_UNSATISFIABLE \
  ((MojoTriggerCondition)1)

// Deliberately no struct_size prefix: matches whp::WhpHandleSignalsState so
// the two are layout-identical and can be passed by reinterpret_cast.
typedef struct MojoHandleSignalsState {
  MojoHandleSignals satisfied_signals;
  MojoHandleSignals satisfiable_signals;
} MojoHandleSignalsState;

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // MOJO_PUBLIC_C_SYSTEM_TYPES_H_
