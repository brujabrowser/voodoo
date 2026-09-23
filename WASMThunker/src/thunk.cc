// WASMThunker: a real implementation of the public Mojo C System ABI
// (mojo/public/c/system/core.h), backed by WASMHolePunch's whp::c layer.
//
// Unlike Chromium's own mojo/public/c/system/thunks.cc, this file does not
// dispatch through a swappable MojoSystemThunks2 vtable set by an embedder
// at runtime -- WASMThunker *is* the embedder, statically, so every Mojo*
// entry point below calls straight into whp::c (or whp::platform for
// invitations). Result codes are numerically identical between MojoResult
// and WhpResult (see whp/c/types.h), so most paths are a plain static_cast.
//
// Coverage:
//   - Message pipes, messages, traps, data pipes, shared buffers: wired to
//     whp::c 1:1.
//   - MojoGetTimeTicksNow: wired to whp::TimeTicks::Now().
//   - MojoCreateInvitation / Attach / Extract / Send / Accept: in-process
//     loopback keyed by the transport's first platform-handle value when
//     that handle is TYPE_INVALID (channel-id convention). A real
//     FILE_DESCRIPTOR / WINDOWS_HANDLE is adopted as a wasigocvm sysroot
//     UDP socket and attached to whp::platform::Invitation (HolePunch
//     cross-process).
//   - MojoNotifyBadMessage: logs to stderr and returns OK (no message-pipe
//     teardown side effect at this layer).
//   - Platform-handle wrap/unwrap: wired to WhpWrapPlatformHandle /
//     WhpUnwrapPlatformHandle (OS HANDLE on Windows, fd elsewhere,
//     Mach send-right token like WASMYetiKernel MachSendRight).
//   - Shared-memory-region wrap/unwrap: wired to
//     WhpWrapPlatformSharedMemoryRegion / Unwrap (OS mapping or in-process
//     backing for Mach tokens).
//   - Quotas and pipe fusion: wired to WhpSetQuota / WhpQueryQuota /
//     WhpFuseMessagePipes.
//   - Message serialize / reserve capacity / context: wired.
//   - GetBufferInfo, SetDefaultProcessErrorHandler: wired.

#include "mojo/public/c/system/core.h"

#include "whp/c/system.h"
#include "whp/base/time.h"
#include "whp/net/udp_socket.h"
#include "whp/platform/invitation.h"
#include "whp/punch/punch.h"

#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace {

[[maybe_unused]] MojoResult NotImplemented(const char* name) {
  std::fprintf(stderr,
               "WASMThunker: Mojo%s() has no whp::c equivalent yet -- "
               "MOJO_RESULT_UNIMPLEMENTED\n",
               name);
  return MOJO_RESULT_UNIMPLEMENTED;
}

// -- Invitation registry (in-process loopback only) --------------------
//
// Real Mojo invitation handles come from the same MojoHandle allocator as
// every other handle. We don't own that allocator (whp::c does, for pipes/
// data pipes/buffers/traps), so invitation handles here are carved out of a
// disjoint range whp's own internal allocator will not reach.
constexpr MojoHandle kInvitationHandleBase = 0xF0000000u;

struct ThunkInvitation {
  std::unordered_map<std::string, WhpHandle> attached;
  std::unique_ptr<whp::platform::Invitation> punch;

  ~ThunkInvitation() {
    for (auto& [name, handle] : attached) {
      WhpClose(handle);
    }
  }
};

struct SentInvitation {
  std::unordered_map<std::string, WhpHandle> attached;
  std::unique_ptr<whp::platform::Invitation> punch;
};

std::mutex g_mu;
std::unordered_map<MojoHandle, std::unique_ptr<ThunkInvitation>>
    g_invitations;
MojoHandle g_next_invitation_handle = kInvitationHandleBase;

// Sent-but-not-yet-accepted invitations, keyed by the transport's first
// platform-handle value. TYPE_INVALID is the in-process channel-id
// convention; a live fd/SOCKET also carries a HolePunch Invitation.
std::unordered_map<uint64_t, SentInvitation> g_sent_invitations;

uint64_t ChannelIdOf(const MojoInvitationTransportEndpoint* endpoint) {
  if (endpoint != nullptr && endpoint->num_platform_handles > 0 &&
      endpoint->platform_handles != nullptr) {
    return endpoint->platform_handles[0].value;
  }
  return 0;
}

std::unique_ptr<whp::platform::Invitation> PunchFromEndpoint(
    const MojoInvitationTransportEndpoint* endpoint, bool offerer) {
  if (endpoint == nullptr || endpoint->num_platform_handles == 0 ||
      endpoint->platform_handles == nullptr) {
    return nullptr;
  }
  const MojoPlatformHandle& ph = endpoint->platform_handles[0];
  if (ph.type != MOJO_PLATFORM_HANDLE_TYPE_FILE_DESCRIPTOR &&
      ph.type != MOJO_PLATFORM_HANDLE_TYPE_WINDOWS_HANDLE) {
    return nullptr;
  }
  whp::net::UdpSocket sock =
      whp::net::UdpSocket::Adopt(static_cast<uintptr_t>(ph.value));
  if (!sock.is_valid()) {
    return nullptr;
  }
  whp::punch::ConnectedPath path(std::move(sock), {});
  auto punch = std::make_unique<whp::platform::Invitation>();
  if (punch->Attach(std::move(path), offerer) != WHP_RESULT_OK) {
    return nullptr;
  }
  return punch;
}

}  // namespace

extern "C" {

MojoResult MojoInitialize(const MojoInitializeOptions* options) {
  (void)options;
  return static_cast<MojoResult>(WhpInit());
}

MojoResult MojoShutdown(const MojoShutdownOptions* options) {
  (void)options;
  WhpShutdown();
  return MOJO_RESULT_OK;
}

MojoTimeTicks MojoGetTimeTicksNow() {
  return whp::TimeTicks::Now().InMicroseconds();
}

MojoResult MojoClose(MojoHandle handle) {
  if (handle >= kInvitationHandleBase) {
    std::lock_guard<std::mutex> lock(g_mu);
    if (g_invitations.erase(handle) == 0) {
      return MOJO_RESULT_INVALID_ARGUMENT;
    }
    return MOJO_RESULT_OK;
  }
  return static_cast<MojoResult>(WhpClose(static_cast<WhpHandle>(handle)));
}

MojoResult MojoQueryHandleSignalsState(
    MojoHandle handle,
    MojoHandleSignalsState* signals_state) {
  static_assert(sizeof(MojoHandleSignalsState) == sizeof(WhpHandleSignalsState));
  return static_cast<MojoResult>(WhpQueryHandleSignalsState(
      static_cast<WhpHandle>(handle),
      reinterpret_cast<WhpHandleSignalsState*>(signals_state)));
}

MojoResult MojoCreateMessagePipe(const MojoCreateMessagePipeOptions* options,
                                 MojoHandle* message_pipe_handle0,
                                 MojoHandle* message_pipe_handle1) {
  (void)options;  // whp::c has no per-pipe flags to honor yet.
  WhpHandle a = WHP_HANDLE_INVALID;
  WhpHandle b = WHP_HANDLE_INVALID;
  MojoResult result =
      static_cast<MojoResult>(WhpCreateMessagePipe(nullptr, &a, &b));
  *message_pipe_handle0 = a;
  *message_pipe_handle1 = b;
  return result;
}

MojoResult MojoWriteMessage(MojoHandle message_pipe_handle,
                            MojoMessageHandle message_handle,
                            const MojoWriteMessageOptions* options) {
  (void)options;
  return static_cast<MojoResult>(
      WhpWriteMessage(static_cast<WhpHandle>(message_pipe_handle),
                      static_cast<WhpMessageHandle>(message_handle),
                      nullptr));
}

MojoResult MojoReadMessage(MojoHandle message_pipe_handle,
                           const MojoReadMessageOptions* options,
                           MojoMessageHandle* message_handle) {
  (void)options;
  WhpMessageHandle got = WHP_MESSAGE_HANDLE_INVALID;
  MojoResult result = static_cast<MojoResult>(WhpReadMessage(
      static_cast<WhpHandle>(message_pipe_handle), nullptr, &got));
  *message_handle = got;
  return result;
}

MojoResult MojoFuseMessagePipes(MojoHandle handle0,
                                MojoHandle handle1,
                                const MojoFuseMessagePipesOptions* options) {
  (void)options;
  return static_cast<MojoResult>(WhpFuseMessagePipes(
      static_cast<WhpHandle>(handle0), static_cast<WhpHandle>(handle1)));
}

MojoResult MojoCreateMessage(const MojoCreateMessageOptions* options,
                             MojoMessageHandle* message) {
  (void)options;
  WhpMessageHandle handle = WHP_MESSAGE_HANDLE_INVALID;
  MojoResult result =
      static_cast<MojoResult>(WhpCreateMessage(nullptr, &handle));
  *message = handle;
  return result;
}

MojoResult MojoDestroyMessage(MojoMessageHandle message) {
  return static_cast<MojoResult>(
      WhpDestroyMessage(static_cast<WhpMessageHandle>(message)));
}

MojoResult MojoSerializeMessage(MojoMessageHandle message,
                                const MojoSerializeMessageOptions* options) {
  (void)options;
  return static_cast<MojoResult>(
      WhpSerializeMessage(static_cast<WhpMessageHandle>(message)));
}

MojoResult MojoReserveMessageCapacity(MojoMessageHandle message,
                                      uint32_t payload_buffer_size,
                                      uint32_t* buffer_size) {
  return static_cast<MojoResult>(WhpReserveMessageCapacity(
      static_cast<WhpMessageHandle>(message), payload_buffer_size,
      buffer_size));
}

MojoResult MojoAppendMessageData(MojoMessageHandle message,
                                 uint32_t payload_size,
                                 const MojoHandle* handles,
                                 uint32_t num_handles,
                                 const MojoAppendMessageDataOptions* options,
                                 void** buffer,
                                 uint32_t* buffer_size) {
  WhpAppendMessageDataOptions whp_options{};
  whp_options.struct_size = sizeof(whp_options);
  whp_options.flags = options ? options->flags : 0;
  static_assert(sizeof(MojoHandle) == sizeof(WhpHandle));
  return static_cast<MojoResult>(WhpAppendMessageData(
      static_cast<WhpMessageHandle>(message), payload_size,
      reinterpret_cast<const WhpHandle*>(handles), num_handles, &whp_options,
      buffer, buffer_size));
}

MojoResult MojoGetMessageData(MojoMessageHandle message,
                              const MojoGetMessageDataOptions* options,
                              void** buffer,
                              uint32_t* num_bytes,
                              MojoHandle* handles,
                              uint32_t* num_handles) {
  (void)options;
  return static_cast<MojoResult>(WhpGetMessageData(
      static_cast<WhpMessageHandle>(message), nullptr, buffer, num_bytes,
      reinterpret_cast<WhpHandle*>(handles), num_handles));
}

MojoResult MojoSetMessageContext(MojoMessageHandle message,
                                 uintptr_t context,
                                 MojoMessageContextSerializer serializer,
                                 MojoMessageContextDestructor destructor,
                                 const MojoSetMessageContextOptions* options) {
  (void)options;
  return static_cast<MojoResult>(WhpSetMessageContext(
      static_cast<WhpMessageHandle>(message), context,
      reinterpret_cast<WhpMessageContextSerializer>(serializer),
      reinterpret_cast<WhpMessageContextDestructor>(destructor)));
}

MojoResult MojoGetMessageContext(MojoMessageHandle message,
                                 const MojoGetMessageContextOptions* options,
                                 uintptr_t* context) {
  (void)options;
  return static_cast<MojoResult>(WhpGetMessageContext(
      static_cast<WhpMessageHandle>(message), context));
}

MojoDefaultProcessErrorHandler g_default_process_error = nullptr;

MojoResult MojoNotifyBadMessage(MojoMessageHandle message,
                                const char* error,
                                uint32_t error_num_bytes,
                                const MojoNotifyBadMessageOptions* options) {
  (void)options;
  std::fprintf(stderr, "WASMThunker: bad message 0x%08x: %.*s\n",
               static_cast<unsigned>(message), static_cast<int>(error_num_bytes),
               error);
  if (g_default_process_error) {
    g_default_process_error(error, error_num_bytes);
  }
  return MOJO_RESULT_OK;
}

MojoResult MojoCreateDataPipe(const MojoCreateDataPipeOptions* options,
                              MojoHandle* data_pipe_producer_handle,
                              MojoHandle* data_pipe_consumer_handle) {
  WhpCreateDataPipeOptions whp_options{};
  const WhpCreateDataPipeOptions* whp_options_ptr = nullptr;
  if (options) {
    whp_options.struct_size = sizeof(whp_options);
    whp_options.flags = options->flags;
    whp_options.element_num_bytes = options->element_num_bytes;
    whp_options.capacity_num_bytes = options->capacity_num_bytes;
    whp_options_ptr = &whp_options;
  }
  WhpHandle producer = WHP_HANDLE_INVALID;
  WhpHandle consumer = WHP_HANDLE_INVALID;
  MojoResult result = static_cast<MojoResult>(
      WhpCreateDataPipe(whp_options_ptr, &producer, &consumer));
  *data_pipe_producer_handle = producer;
  *data_pipe_consumer_handle = consumer;
  return result;
}

MojoResult MojoWriteData(MojoHandle data_pipe_producer_handle,
                         const void* elements,
                         uint32_t* num_elements,
                         const MojoWriteDataOptions* options) {
  return static_cast<MojoResult>(
      WhpWriteData(static_cast<WhpHandle>(data_pipe_producer_handle),
                   elements, num_elements, options ? options->flags : 0));
}

MojoResult MojoBeginWriteData(MojoHandle data_pipe_producer_handle,
                              const MojoBeginWriteDataOptions* options,
                              void** buffer,
                              uint32_t* buffer_num_elements) {
  (void)options;  // whp::c has no begin-write flags to honor yet.
  return static_cast<MojoResult>(WhpBeginWriteData(
      static_cast<WhpHandle>(data_pipe_producer_handle), buffer,
      buffer_num_elements));
}

MojoResult MojoEndWriteData(MojoHandle data_pipe_producer_handle,
                            uint32_t num_elements_written,
                            const MojoEndWriteDataOptions* options) {
  (void)options;
  return static_cast<MojoResult>(
      WhpEndWriteData(static_cast<WhpHandle>(data_pipe_producer_handle),
                      num_elements_written));
}

MojoResult MojoReadData(MojoHandle data_pipe_consumer_handle,
                        const MojoReadDataOptions* options,
                        void* elements,
                        uint32_t* num_elements) {
  return static_cast<MojoResult>(
      WhpReadData(static_cast<WhpHandle>(data_pipe_consumer_handle), elements,
                  num_elements, options ? options->flags : 0));
}

MojoResult MojoBeginReadData(MojoHandle data_pipe_consumer_handle,
                             const MojoBeginReadDataOptions* options,
                             const void** buffer,
                             uint32_t* buffer_num_elements) {
  (void)options;
  return static_cast<MojoResult>(WhpBeginReadData(
      static_cast<WhpHandle>(data_pipe_consumer_handle), buffer,
      buffer_num_elements));
}

MojoResult MojoEndReadData(MojoHandle data_pipe_consumer_handle,
                           uint32_t num_elements_read,
                           const MojoEndReadDataOptions* options) {
  (void)options;
  return static_cast<MojoResult>(WhpEndReadData(
      static_cast<WhpHandle>(data_pipe_consumer_handle), num_elements_read));
}

MojoResult MojoCreateSharedBuffer(uint64_t num_bytes,
                                  const MojoCreateSharedBufferOptions* options,
                                  MojoHandle* shared_buffer_handle) {
  (void)options;
  WhpHandle handle = WHP_HANDLE_INVALID;
  MojoResult result = static_cast<MojoResult>(
      WhpCreateSharedBuffer(num_bytes, nullptr, &handle));
  *shared_buffer_handle = handle;
  return result;
}

MojoResult MojoDuplicateBufferHandle(
    MojoHandle buffer_handle,
    const MojoDuplicateBufferHandleOptions* options,
    MojoHandle* new_buffer_handle) {
  (void)options;
  WhpHandle dup = WHP_HANDLE_INVALID;
  MojoResult result = static_cast<MojoResult>(
      WhpDuplicateBufferHandle(static_cast<WhpHandle>(buffer_handle), &dup));
  *new_buffer_handle = dup;
  return result;
}

MojoResult MojoMapBuffer(MojoHandle buffer_handle,
                         uint64_t offset,
                         uint64_t num_bytes,
                         const MojoMapBufferOptions* options,
                         void** buffer) {
  (void)options;
  return static_cast<MojoResult>(WhpMapBuffer(
      static_cast<WhpHandle>(buffer_handle), offset, num_bytes, buffer));
}

MojoResult MojoUnmapBuffer(void* buffer) {
  return static_cast<MojoResult>(WhpUnmapBuffer(buffer));
}

MojoResult MojoGetBufferInfo(MojoHandle buffer_handle,
                             const MojoGetBufferInfoOptions* options,
                             MojoSharedBufferInfo* info) {
  (void)options;
  if (!info || info->struct_size < sizeof(MojoSharedBufferInfo)) {
    return MOJO_RESULT_INVALID_ARGUMENT;
  }
  uint64_t num_bytes = 0;
  MojoResult result = static_cast<MojoResult>(
      WhpGetBufferInfo(static_cast<WhpHandle>(buffer_handle), &num_bytes));
  if (result != MOJO_RESULT_OK) {
    return result;
  }
  info->num_bytes = num_bytes;
  return MOJO_RESULT_OK;
}

MojoResult MojoCreateTrap(MojoTrapEventHandler handler,
                          const MojoCreateTrapOptions* options,
                          MojoHandle* trap_handle) {
  (void)options;
  WhpHandle handle = WHP_HANDLE_INVALID;
  MojoResult result = static_cast<MojoResult>(WhpCreateTrap(
      reinterpret_cast<WhpTrapEventHandler>(handler), nullptr, &handle));
  *trap_handle = handle;
  return result;
}

MojoResult MojoAddTrigger(MojoHandle trap_handle,
                          MojoHandle handle,
                          MojoHandleSignals signals,
                          MojoTriggerCondition condition,
                          uintptr_t context,
                          const MojoAddTriggerOptions* options) {
  (void)options;
  return static_cast<MojoResult>(WhpAddTrigger(
      static_cast<WhpHandle>(trap_handle), static_cast<WhpHandle>(handle),
      static_cast<WhpHandleSignals>(signals),
      static_cast<uint32_t>(condition), context, nullptr));
}

MojoResult MojoRemoveTrigger(MojoHandle trap_handle,
                             uintptr_t context,
                             const MojoRemoveTriggerOptions* options) {
  (void)options;
  return static_cast<MojoResult>(WhpRemoveTrigger(
      static_cast<WhpHandle>(trap_handle), context, nullptr));
}

MojoResult MojoArmTrap(MojoHandle trap_handle,
                       const MojoArmTrapOptions* options,
                       uint32_t* num_blocking_events,
                       MojoTrapEvent* blocking_events) {
  (void)options;
  static_assert(sizeof(MojoTrapEvent) == sizeof(WhpTrapEvent));
  return static_cast<MojoResult>(WhpArmTrap(
      static_cast<WhpHandle>(trap_handle), nullptr, num_blocking_events,
      reinterpret_cast<WhpTrapEvent*>(blocking_events)));
}

MojoResult MojoWrapPlatformHandle(const MojoPlatformHandle* platform_handle,
                                  const MojoWrapPlatformHandleOptions* options,
                                  MojoHandle* mojo_handle) {
  (void)options;
  if (!platform_handle || !mojo_handle) {
    return MOJO_RESULT_INVALID_ARGUMENT;
  }
  if (platform_handle->struct_size < sizeof(MojoPlatformHandle)) {
    return MOJO_RESULT_INVALID_ARGUMENT;
  }
  WhpHandle handle = WHP_HANDLE_INVALID;
  MojoResult result = static_cast<MojoResult>(WhpWrapPlatformHandle(
      static_cast<WhpPlatformHandleType>(platform_handle->type),
      platform_handle->value, &handle));
  if (result != MOJO_RESULT_OK) {
    return result;
  }
  *mojo_handle = static_cast<MojoHandle>(handle);
  return MOJO_RESULT_OK;
}

MojoResult MojoUnwrapPlatformHandle(
    MojoHandle mojo_handle,
    const MojoUnwrapPlatformHandleOptions* options,
    MojoPlatformHandle* platform_handle) {
  (void)options;
  if (!platform_handle) {
    return MOJO_RESULT_INVALID_ARGUMENT;
  }
  if (platform_handle->struct_size < sizeof(MojoPlatformHandle)) {
    return MOJO_RESULT_INVALID_ARGUMENT;
  }
  WhpPlatformHandleType type = WHP_PLATFORM_HANDLE_TYPE_INVALID;
  uint64_t value = 0;
  MojoResult result = static_cast<MojoResult>(WhpUnwrapPlatformHandle(
      static_cast<WhpHandle>(mojo_handle), &type, &value));
  if (result != MOJO_RESULT_OK) {
    return result;
  }
  platform_handle->type = static_cast<MojoPlatformHandleType>(type);
  platform_handle->value = value;
  return MOJO_RESULT_OK;
}

MojoResult MojoWrapPlatformSharedMemoryRegion(
    const MojoPlatformHandle* platform_handles,
    uint32_t num_platform_handles,
    uint64_t num_bytes,
    const MojoSharedBufferGuid* guid,
    MojoPlatformSharedMemoryRegionAccessMode access_mode,
    const MojoWrapPlatformSharedMemoryRegionOptions* options,
    MojoHandle* mojo_handle) {
  (void)options;
  if (!platform_handles || !mojo_handle || num_platform_handles == 0 ||
      num_platform_handles > 2 || num_bytes == 0) {
    return MOJO_RESULT_INVALID_ARGUMENT;
  }
  WhpPlatformHandleType types[2];
  uint64_t values[2];
  for (uint32_t i = 0; i < num_platform_handles; ++i) {
    if (platform_handles[i].struct_size < sizeof(MojoPlatformHandle)) {
      return MOJO_RESULT_INVALID_ARGUMENT;
    }
    types[i] = static_cast<WhpPlatformHandleType>(platform_handles[i].type);
    values[i] = platform_handles[i].value;
  }
  uint64_t guid_high = guid ? guid->high : 0;
  uint64_t guid_low = guid ? guid->low : 0;
  WhpHandle handle = WHP_HANDLE_INVALID;
  MojoResult result = static_cast<MojoResult>(WhpWrapPlatformSharedMemoryRegion(
      types, values, num_platform_handles, num_bytes, guid_high, guid_low,
      static_cast<WhpPlatformSharedMemoryRegionAccessMode>(access_mode),
      &handle));
  if (result != MOJO_RESULT_OK) {
    return result;
  }
  *mojo_handle = static_cast<MojoHandle>(handle);
  return MOJO_RESULT_OK;
}

MojoResult MojoUnwrapPlatformSharedMemoryRegion(
    MojoHandle mojo_handle,
    const MojoUnwrapPlatformSharedMemoryRegionOptions* options,
    MojoPlatformHandle* platform_handles,
    uint32_t* num_platform_handles,
    uint64_t* num_bytes,
    MojoSharedBufferGuid* guid,
    MojoPlatformSharedMemoryRegionAccessMode* access_mode) {
  (void)options;
  if (!num_platform_handles) {
    return MOJO_RESULT_INVALID_ARGUMENT;
  }
  WhpPlatformHandleType types[2]{};
  uint64_t values[2]{};
  uint32_t n = *num_platform_handles;
  if (n > 2) n = 2;
  uint64_t bytes = 0;
  uint64_t guid_high = 0;
  uint64_t guid_low = 0;
  WhpPlatformSharedMemoryRegionAccessMode mode = 0;
  MojoResult result = static_cast<MojoResult>(
      WhpUnwrapPlatformSharedMemoryRegion(
          static_cast<WhpHandle>(mojo_handle), types, values, &n, &bytes,
          &guid_high, &guid_low, &mode));
  if (result == WHP_RESULT_RESOURCE_EXHAUSTED) {
    *num_platform_handles = n;
    return MOJO_RESULT_RESOURCE_EXHAUSTED;
  }
  if (result != MOJO_RESULT_OK) {
    return result;
  }
  if (!platform_handles || *num_platform_handles < n) {
    *num_platform_handles = n;
    return MOJO_RESULT_RESOURCE_EXHAUSTED;
  }
  for (uint32_t i = 0; i < n; ++i) {
    platform_handles[i].struct_size = sizeof(MojoPlatformHandle);
    platform_handles[i].type = static_cast<MojoPlatformHandleType>(types[i]);
    platform_handles[i].value = values[i];
  }
  *num_platform_handles = n;
  if (num_bytes) *num_bytes = bytes;
  if (guid) {
    guid->high = guid_high;
    guid->low = guid_low;
  }
  if (access_mode) {
    *access_mode = static_cast<MojoPlatformSharedMemoryRegionAccessMode>(mode);
  }
  return MOJO_RESULT_OK;
}

MojoResult MojoCreateInvitation(const MojoCreateInvitationOptions* options,
                                MojoHandle* invitation_handle) {
  (void)options;
  std::lock_guard<std::mutex> lock(g_mu);
  MojoHandle handle = g_next_invitation_handle++;
  g_invitations[handle] = std::make_unique<ThunkInvitation>();
  *invitation_handle = handle;
  return MOJO_RESULT_OK;
}

MojoResult MojoAttachMessagePipeToInvitation(
    MojoHandle invitation_handle,
    const void* name,
    uint32_t name_num_bytes,
    const MojoAttachMessagePipeToInvitationOptions* options,
    MojoHandle* message_pipe_handle) {
  (void)options;
  std::lock_guard<std::mutex> lock(g_mu);
  auto it = g_invitations.find(invitation_handle);
  if (it == g_invitations.end()) {
    return MOJO_RESULT_INVALID_ARGUMENT;
  }
  std::string key(static_cast<const char*>(name), name_num_bytes);
  if (it->second->attached.count(key) != 0) {
    return MOJO_RESULT_ALREADY_EXISTS;
  }
  WhpHandle local = WHP_HANDLE_INVALID;
  WhpHandle peer = WHP_HANDLE_INVALID;
  if (WhpCreateMessagePipe(nullptr, &local, &peer) != WHP_RESULT_OK) {
    return MOJO_RESULT_UNKNOWN;
  }
  it->second->attached.emplace(std::move(key), peer);
  *message_pipe_handle = local;
  return MOJO_RESULT_OK;
}

MojoResult MojoExtractMessagePipeFromInvitation(
    MojoHandle invitation_handle,
    const void* name,
    uint32_t name_num_bytes,
    const MojoExtractMessagePipeFromInvitationOptions* options,
    MojoHandle* message_pipe_handle) {
  (void)options;
  std::lock_guard<std::mutex> lock(g_mu);
  auto it = g_invitations.find(invitation_handle);
  if (it == g_invitations.end()) {
    return MOJO_RESULT_INVALID_ARGUMENT;
  }
  std::string key(static_cast<const char*>(name), name_num_bytes);
  auto attached_it = it->second->attached.find(key);
  if (attached_it == it->second->attached.end()) {
    return MOJO_RESULT_NOT_FOUND;
  }
  *message_pipe_handle = attached_it->second;
  it->second->attached.erase(attached_it);
  return MOJO_RESULT_OK;
}

MojoResult MojoSendInvitation(
    MojoHandle invitation_handle,
    const MojoPlatformProcessHandle* process_handle,
    const MojoInvitationTransportEndpoint* transport_endpoint,
    MojoProcessErrorHandler error_handler,
    uintptr_t error_handler_context,
    const MojoSendInvitationOptions* options) {
  (void)process_handle;
  (void)error_handler;
  (void)error_handler_context;
  (void)options;
  std::lock_guard<std::mutex> lock(g_mu);
  auto it = g_invitations.find(invitation_handle);
  if (it == g_invitations.end()) {
    return MOJO_RESULT_INVALID_ARGUMENT;
  }
  uint64_t channel_id = ChannelIdOf(transport_endpoint);
  SentInvitation sent;
  sent.attached = std::move(it->second->attached);
  sent.punch = PunchFromEndpoint(transport_endpoint, /*offerer=*/true);
  g_sent_invitations[channel_id] = std::move(sent);
  g_invitations.erase(it);
  return MOJO_RESULT_OK;
}

MojoResult MojoAcceptInvitation(
    const MojoInvitationTransportEndpoint* transport_endpoint,
    const MojoAcceptInvitationOptions* options,
    MojoHandle* invitation_handle) {
  (void)options;
  std::lock_guard<std::mutex> lock(g_mu);
  uint64_t channel_id = ChannelIdOf(transport_endpoint);
  auto sent_it = g_sent_invitations.find(channel_id);
  if (sent_it == g_sent_invitations.end()) {
    return MOJO_RESULT_NOT_FOUND;
  }
  auto invitation = std::make_unique<ThunkInvitation>();
  invitation->attached = std::move(sent_it->second.attached);
  invitation->punch = std::move(sent_it->second.punch);
  if (!invitation->punch) {
    invitation->punch =
        PunchFromEndpoint(transport_endpoint, /*offerer=*/false);
  }
  g_sent_invitations.erase(sent_it);
  MojoHandle handle = g_next_invitation_handle++;
  g_invitations[handle] = std::move(invitation);
  *invitation_handle = handle;
  return MOJO_RESULT_OK;
}

MojoResult MojoSetDefaultProcessErrorHandler(
    MojoDefaultProcessErrorHandler handler,
    const MojoSetDefaultProcessErrorHandlerOptions* options) {
  (void)options;
  g_default_process_error = handler;
  return MOJO_RESULT_OK;
}

MojoResult MojoSetQuota(MojoHandle handle,
                        MojoQuotaType type,
                        uint64_t limit,
                        const MojoSetQuotaOptions* options) {
  (void)options;
  return static_cast<MojoResult>(WhpSetQuota(
      static_cast<WhpHandle>(handle), static_cast<WhpQuotaType>(type), limit));
}

MojoResult MojoQueryQuota(MojoHandle handle,
                          MojoQuotaType type,
                          const MojoQueryQuotaOptions* options,
                          uint64_t* limit,
                          uint64_t* usage) {
  (void)options;
  return static_cast<MojoResult>(WhpQueryQuota(
      static_cast<WhpHandle>(handle), static_cast<WhpQuotaType>(type), limit,
      usage));
}

}  // extern "C"
