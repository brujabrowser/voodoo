#ifndef MOJO_PUBLIC_C_SYSTEM_INVITATION_H_
#define MOJO_PUBLIC_C_SYSTEM_INVITATION_H_

#include "mojo/public/c/system/platform_handle.h"
#include "mojo/public/c/system/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t MojoCreateInvitationFlags;
typedef struct MojoCreateInvitationOptions {
  uint32_t struct_size;
  MojoCreateInvitationFlags flags;
} MojoCreateInvitationOptions;

typedef uint32_t MojoAttachMessagePipeToInvitationFlags;
typedef struct MojoAttachMessagePipeToInvitationOptions {
  uint32_t struct_size;
  MojoAttachMessagePipeToInvitationFlags flags;
} MojoAttachMessagePipeToInvitationOptions;

typedef uint32_t MojoExtractMessagePipeFromInvitationFlags;
typedef struct MojoExtractMessagePipeFromInvitationOptions {
  uint32_t struct_size;
  MojoExtractMessagePipeFromInvitationFlags flags;
} MojoExtractMessagePipeFromInvitationOptions;

typedef uint32_t MojoInvitationTransportType;
#define MOJO_INVITATION_TRANSPORT_TYPE_CHANNEL \
  ((MojoInvitationTransportType)0)
#define MOJO_INVITATION_TRANSPORT_TYPE_CHANNEL_SERVER \
  ((MojoInvitationTransportType)1)

typedef struct MojoInvitationTransportEndpoint {
  uint32_t struct_size;
  MojoInvitationTransportType type;
  uint32_t num_platform_handles;
  const struct MojoPlatformHandle* platform_handles;
} MojoInvitationTransportEndpoint;

typedef uint32_t MojoProcessErrorFlags;
typedef struct MojoProcessErrorDetails {
  uint32_t struct_size;
  MojoProcessErrorFlags flags;
  const char* error_message;
  uint32_t error_message_length;
  MojoHandle mojo_handle;
} MojoProcessErrorDetails;

typedef void (*MojoProcessErrorHandler)(
    uintptr_t context,
    const struct MojoProcessErrorDetails* details);

typedef uint32_t MojoSendInvitationFlags;
typedef struct MojoSendInvitationOptions {
  uint32_t struct_size;
  MojoSendInvitationFlags flags;
} MojoSendInvitationOptions;

typedef uint32_t MojoAcceptInvitationFlags;
typedef struct MojoAcceptInvitationOptions {
  uint32_t struct_size;
  MojoAcceptInvitationFlags flags;
} MojoAcceptInvitationOptions;

typedef void (*MojoDefaultProcessErrorHandler)(
    const char* error_message,
    uint32_t error_message_length);

typedef uint32_t MojoSetDefaultProcessErrorHandlerFlags;
typedef struct MojoSetDefaultProcessErrorHandlerOptions {
  uint32_t struct_size;
  MojoSetDefaultProcessErrorHandlerFlags flags;
} MojoSetDefaultProcessErrorHandlerOptions;

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // MOJO_PUBLIC_C_SYSTEM_INVITATION_H_
