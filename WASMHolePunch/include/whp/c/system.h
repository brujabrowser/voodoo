#ifndef WHP_C_SYSTEM_H_
#define WHP_C_SYSTEM_H_

#include "whp/c/types.h"

#ifdef __cplusplus
extern "C" {
#endif

// Library lifetime. Native builds call WSAStartup from here as well.
WhpResult WhpInit(void);
void WhpShutdown(void);

WhpResult WhpClose(WhpHandle handle);
WhpResult WhpQueryHandleSignalsState(WhpHandle handle,
                                     WhpHandleSignalsState* state);

// Message pipes
WhpResult WhpCreateMessagePipe(const WhpCreateMessagePipeOptions* options,
                               WhpHandle* handle0,
                               WhpHandle* handle1);
WhpResult WhpFuseMessagePipes(WhpHandle handle0, WhpHandle handle1);
WhpResult WhpSetQuota(WhpHandle handle, WhpQuotaType type, uint64_t limit);
WhpResult WhpQueryQuota(WhpHandle handle,
                        WhpQuotaType type,
                        uint64_t* limit,
                        uint64_t* usage);
WhpResult WhpWriteMessage(WhpHandle pipe,
                          WhpMessageHandle message,
                          const WhpWriteMessageOptions* options);
WhpResult WhpReadMessage(WhpHandle pipe,
                         const WhpReadMessageOptions* options,
                         WhpMessageHandle* message);

// Messages
WhpResult WhpCreateMessage(const WhpCreateMessageOptions* options,
                           WhpMessageHandle* message);
WhpResult WhpDestroyMessage(WhpMessageHandle message);
WhpResult WhpAppendMessageData(WhpMessageHandle message,
                               uint32_t additional_num_bytes,
                               const WhpHandle* handles,
                               uint32_t num_handles,
                               const WhpAppendMessageDataOptions* options,
                               void** buffer,
                               uint32_t* buffer_size);
WhpResult WhpGetMessageData(WhpMessageHandle message,
                            const WhpGetMessageDataOptions* options,
                            void** buffer,
                            uint32_t* num_bytes,
                            WhpHandle* handles,
                            uint32_t* num_handles);
WhpResult WhpSerializeMessage(WhpMessageHandle message);
WhpResult WhpReserveMessageCapacity(WhpMessageHandle message,
                                    uint32_t payload_buffer_size,
                                    uint32_t* buffer_size);

typedef void (*WhpMessageContextSerializer)(uintptr_t context,
                                            WhpMessageHandle message);
typedef void (*WhpMessageContextDestructor)(uintptr_t context);

WhpResult WhpSetMessageContext(WhpMessageHandle message,
                               uintptr_t context,
                               WhpMessageContextSerializer serializer,
                               WhpMessageContextDestructor destructor);
WhpResult WhpGetMessageContext(WhpMessageHandle message, uintptr_t* context);

WhpResult WhpGetBufferInfo(WhpHandle buffer, uint64_t* num_bytes);

// Traps
WhpResult WhpCreateTrap(WhpTrapEventHandler handler,
                        const WhpCreateTrapOptions* options,
                        WhpHandle* trap);
WhpResult WhpAddTrigger(WhpHandle trap,
                        WhpHandle handle,
                        WhpHandleSignals signals,
                        uint32_t condition,
                        uintptr_t context,
                        const WhpAddTriggerOptions* options);
WhpResult WhpRemoveTrigger(WhpHandle trap,
                           uintptr_t context,
                           const WhpRemoveTriggerOptions* options);
WhpResult WhpArmTrap(WhpHandle trap,
                     const WhpArmTrapOptions* options,
                     uint32_t* num_blocking_events,
                     WhpTrapEvent* blocking_events);

// Async bridge pump: fires every WhpTrapEventHandler whose trigger has
// become satisfied since the last pump, entirely outside any WhpXxx call's
// own state mutation -- same completion-queue discipline
// wasigocvm_net.hpp's WasigocvmNetBridge uses (Submit mutates/queues,
// PollOne/WaitOne deliver). Handlers used to fire synchronously, nested
// inside whatever call (WhpWriteMessage, WhpClose, ...) changed the
// trigger's watched signal -- they no longer do; that call now only
// queues, and only WhpPumpEvents (or whp::Executor::RunUntilIdle()/Run(),
// which call it automatically every iteration) delivers. Safe to call
// with nothing pending (returns WHP_RESULT_OK immediately).
WhpResult WhpPumpEvents(void);

// Data pipes (in-process in v1)
WhpResult WhpCreateDataPipe(const WhpCreateDataPipeOptions* options,
                            WhpHandle* producer,
                            WhpHandle* consumer);
WhpResult WhpWriteData(WhpHandle producer,
                       const void* elements,
                       uint32_t* num_bytes,
                       uint32_t flags);
WhpResult WhpReadData(WhpHandle consumer,
                      void* elements,
                      uint32_t* num_bytes,
                      uint32_t flags);
WhpResult WhpBeginWriteData(WhpHandle producer,
                            void** buffer,
                            uint32_t* num_bytes);
WhpResult WhpEndWriteData(WhpHandle producer, uint32_t num_bytes_written);
WhpResult WhpBeginReadData(WhpHandle consumer,
                           const void** buffer,
                           uint32_t* num_bytes);
WhpResult WhpEndReadData(WhpHandle consumer, uint32_t num_bytes_read);

// Shared buffers (in-process shared; copy-on-transfer over a path)
WhpResult WhpCreateSharedBuffer(uint64_t num_bytes,
                                const WhpCreateSharedBufferOptions* options,
                                WhpHandle* buffer);
WhpResult WhpDuplicateBufferHandle(WhpHandle buffer, WhpHandle* new_handle);
WhpResult WhpMapBuffer(WhpHandle buffer,
                       uint64_t offset,
                       uint64_t num_bytes,
                       void** data);
WhpResult WhpUnmapBuffer(void* data);

// Platform-handle wrap: takes ownership of the OS primitive (`value` is a
// HANDLE on Windows, an fd elsewhere) and returns a WhpHandle that can
// transit a message pipe. Unwrap consumes the WhpHandle without closing
// the OS primitive. Close on a still-wrapped handle closes the OS
// primitive.
WhpResult WhpWrapPlatformHandle(WhpPlatformHandleType type,
                                uint64_t value,
                                WhpHandle* handle);
WhpResult WhpUnwrapPlatformHandle(WhpHandle handle,
                                  WhpPlatformHandleType* type,
                                  uint64_t* value);

// Wrap an OS shared-memory region (HANDLE/fd/Mach send-right token) as a
// Whp shared-buffer handle. Unwrap consumes that handle and returns the
// platform primitives without closing them. Mach send rights follow
// WASMYetiKernel's MachSendRight analog (opaque loopback id).
WhpResult WhpWrapPlatformSharedMemoryRegion(
    const WhpPlatformHandleType* types,
    const uint64_t* values,
    uint32_t num_handles,
    uint64_t num_bytes,
    uint64_t guid_high,
    uint64_t guid_low,
    WhpPlatformSharedMemoryRegionAccessMode access_mode,
    WhpHandle* handle);
WhpResult WhpUnwrapPlatformSharedMemoryRegion(
    WhpHandle handle,
    WhpPlatformHandleType* types,
    uint64_t* values,
    uint32_t* num_handles,
    uint64_t* num_bytes,
    uint64_t* guid_high,
    uint64_t* guid_low,
    WhpPlatformSharedMemoryRegionAccessMode* access_mode);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // WHP_C_SYSTEM_H_
