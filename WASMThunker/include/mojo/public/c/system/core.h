// Umbrella header + the extern "C" Mojo System entry points that WASMThunker
// implements directly against WASMHolePunch's whp::c layer. See ../../../../
// (repo root) README.md for what is wired vs. MOJO_RESULT_UNIMPLEMENTED.
#ifndef MOJO_PUBLIC_C_SYSTEM_CORE_H_
#define MOJO_PUBLIC_C_SYSTEM_CORE_H_

#include "mojo/public/c/system/buffer.h"
#include "mojo/public/c/system/data_pipe.h"
#include "mojo/public/c/system/functions.h"
#include "mojo/public/c/system/invitation.h"
#include "mojo/public/c/system/message_pipe.h"
#include "mojo/public/c/system/platform_handle.h"
#include "mojo/public/c/system/trap.h"
#include "mojo/public/c/system/types.h"

#ifdef __cplusplus
extern "C" {
#endif

MojoResult MojoInitialize(const struct MojoInitializeOptions* options);
MojoResult MojoShutdown(const struct MojoShutdownOptions* options);
MojoTimeTicks MojoGetTimeTicksNow(void);

MojoResult MojoClose(MojoHandle handle);
MojoResult MojoQueryHandleSignalsState(
    MojoHandle handle,
    struct MojoHandleSignalsState* signals_state);

MojoResult MojoCreateMessagePipe(const struct MojoCreateMessagePipeOptions* options,
                                 MojoHandle* message_pipe_handle0,
                                 MojoHandle* message_pipe_handle1);
MojoResult MojoWriteMessage(MojoHandle message_pipe_handle,
                            MojoMessageHandle message_handle,
                            const struct MojoWriteMessageOptions* options);
MojoResult MojoReadMessage(MojoHandle message_pipe_handle,
                           const struct MojoReadMessageOptions* options,
                           MojoMessageHandle* message_handle);
MojoResult MojoFuseMessagePipes(MojoHandle handle0,
                                MojoHandle handle1,
                                const struct MojoFuseMessagePipesOptions* options);

MojoResult MojoCreateMessage(const struct MojoCreateMessageOptions* options,
                             MojoMessageHandle* message);
MojoResult MojoDestroyMessage(MojoMessageHandle message);
MojoResult MojoSerializeMessage(MojoMessageHandle message,
                                const struct MojoSerializeMessageOptions* options);
MojoResult MojoReserveMessageCapacity(MojoMessageHandle message,
                                      uint32_t payload_buffer_size,
                                      uint32_t* buffer_size);
MojoResult MojoAppendMessageData(MojoMessageHandle message,
                                 uint32_t payload_size,
                                 const MojoHandle* handles,
                                 uint32_t num_handles,
                                 const struct MojoAppendMessageDataOptions* options,
                                 void** buffer,
                                 uint32_t* buffer_size);
MojoResult MojoGetMessageData(MojoMessageHandle message,
                              const struct MojoGetMessageDataOptions* options,
                              void** buffer,
                              uint32_t* num_bytes,
                              MojoHandle* handles,
                              uint32_t* num_handles);
MojoResult MojoSetMessageContext(MojoMessageHandle message,
                                 uintptr_t context,
                                 MojoMessageContextSerializer serializer,
                                 MojoMessageContextDestructor destructor,
                                 const struct MojoSetMessageContextOptions* options);
MojoResult MojoGetMessageContext(MojoMessageHandle message,
                                 const struct MojoGetMessageContextOptions* options,
                                 uintptr_t* context);
MojoResult MojoNotifyBadMessage(MojoMessageHandle message,
                                const char* error,
                                uint32_t error_num_bytes,
                                const struct MojoNotifyBadMessageOptions* options);

MojoResult MojoCreateDataPipe(const struct MojoCreateDataPipeOptions* options,
                              MojoHandle* data_pipe_producer_handle,
                              MojoHandle* data_pipe_consumer_handle);
MojoResult MojoWriteData(MojoHandle data_pipe_producer_handle,
                         const void* elements,
                         uint32_t* num_elements,
                         const struct MojoWriteDataOptions* options);
MojoResult MojoBeginWriteData(MojoHandle data_pipe_producer_handle,
                              const struct MojoBeginWriteDataOptions* options,
                              void** buffer,
                              uint32_t* buffer_num_elements);
MojoResult MojoEndWriteData(MojoHandle data_pipe_producer_handle,
                            uint32_t num_elements_written,
                            const struct MojoEndWriteDataOptions* options);
MojoResult MojoReadData(MojoHandle data_pipe_consumer_handle,
                        const struct MojoReadDataOptions* options,
                        void* elements,
                        uint32_t* num_elements);
MojoResult MojoBeginReadData(MojoHandle data_pipe_consumer_handle,
                             const struct MojoBeginReadDataOptions* options,
                             const void** buffer,
                             uint32_t* buffer_num_elements);
MojoResult MojoEndReadData(MojoHandle data_pipe_consumer_handle,
                           uint32_t num_elements_read,
                           const struct MojoEndReadDataOptions* options);

MojoResult MojoCreateSharedBuffer(uint64_t num_bytes,
                                  const struct MojoCreateSharedBufferOptions* options,
                                  MojoHandle* shared_buffer_handle);
MojoResult MojoDuplicateBufferHandle(
    MojoHandle buffer_handle,
    const struct MojoDuplicateBufferHandleOptions* options,
    MojoHandle* new_buffer_handle);
MojoResult MojoMapBuffer(MojoHandle buffer_handle,
                         uint64_t offset,
                         uint64_t num_bytes,
                         const struct MojoMapBufferOptions* options,
                         void** buffer);
MojoResult MojoUnmapBuffer(void* buffer);
MojoResult MojoGetBufferInfo(MojoHandle buffer_handle,
                             const struct MojoGetBufferInfoOptions* options,
                             struct MojoSharedBufferInfo* info);

MojoResult MojoCreateTrap(MojoTrapEventHandler handler,
                          const struct MojoCreateTrapOptions* options,
                          MojoHandle* trap_handle);
MojoResult MojoAddTrigger(MojoHandle trap_handle,
                          MojoHandle handle,
                          MojoHandleSignals signals,
                          MojoTriggerCondition condition,
                          uintptr_t context,
                          const struct MojoAddTriggerOptions* options);
MojoResult MojoRemoveTrigger(MojoHandle trap_handle,
                             uintptr_t context,
                             const struct MojoRemoveTriggerOptions* options);
MojoResult MojoArmTrap(MojoHandle trap_handle,
                       const struct MojoArmTrapOptions* options,
                       uint32_t* num_blocking_events,
                       struct MojoTrapEvent* blocking_events);

MojoResult MojoWrapPlatformHandle(
    const struct MojoPlatformHandle* platform_handle,
    const struct MojoWrapPlatformHandleOptions* options,
    MojoHandle* mojo_handle);
MojoResult MojoUnwrapPlatformHandle(
    MojoHandle mojo_handle,
    const struct MojoUnwrapPlatformHandleOptions* options,
    struct MojoPlatformHandle* platform_handle);
MojoResult MojoWrapPlatformSharedMemoryRegion(
    const struct MojoPlatformHandle* platform_handles,
    uint32_t num_platform_handles,
    uint64_t num_bytes,
    const struct MojoSharedBufferGuid* guid,
    MojoPlatformSharedMemoryRegionAccessMode access_mode,
    const struct MojoWrapPlatformSharedMemoryRegionOptions* options,
    MojoHandle* mojo_handle);
MojoResult MojoUnwrapPlatformSharedMemoryRegion(
    MojoHandle mojo_handle,
    const struct MojoUnwrapPlatformSharedMemoryRegionOptions* options,
    struct MojoPlatformHandle* platform_handles,
    uint32_t* num_platform_handles,
    uint64_t* num_bytes,
    struct MojoSharedBufferGuid* guid,
    MojoPlatformSharedMemoryRegionAccessMode* access_mode);

MojoResult MojoCreateInvitation(const struct MojoCreateInvitationOptions* options,
                                MojoHandle* invitation_handle);
MojoResult MojoAttachMessagePipeToInvitation(
    MojoHandle invitation_handle,
    const void* name,
    uint32_t name_num_bytes,
    const struct MojoAttachMessagePipeToInvitationOptions* options,
    MojoHandle* message_pipe_handle);
MojoResult MojoExtractMessagePipeFromInvitation(
    MojoHandle invitation_handle,
    const void* name,
    uint32_t name_num_bytes,
    const struct MojoExtractMessagePipeFromInvitationOptions* options,
    MojoHandle* message_pipe_handle);
MojoResult MojoSendInvitation(
    MojoHandle invitation_handle,
    const struct MojoPlatformProcessHandle* process_handle,
    const struct MojoInvitationTransportEndpoint* transport_endpoint,
    MojoProcessErrorHandler error_handler,
    uintptr_t error_handler_context,
    const struct MojoSendInvitationOptions* options);
MojoResult MojoAcceptInvitation(
    const struct MojoInvitationTransportEndpoint* transport_endpoint,
    const struct MojoAcceptInvitationOptions* options,
    MojoHandle* invitation_handle);
MojoResult MojoSetDefaultProcessErrorHandler(
    MojoDefaultProcessErrorHandler handler,
    const struct MojoSetDefaultProcessErrorHandlerOptions* options);

MojoResult MojoSetQuota(MojoHandle handle,
                        MojoQuotaType type,
                        uint64_t limit,
                        const struct MojoSetQuotaOptions* options);
MojoResult MojoQueryQuota(MojoHandle handle,
                          MojoQuotaType type,
                          const struct MojoQueryQuotaOptions* options,
                          uint64_t* limit,
                          uint64_t* usage);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // MOJO_PUBLIC_C_SYSTEM_CORE_H_
