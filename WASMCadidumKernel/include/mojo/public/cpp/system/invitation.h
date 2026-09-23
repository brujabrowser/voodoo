// C++ wrappers over WASMThunker's invitation C ABI. Note the same caveat as
// wst/README.md: Send/Accept here is an in-process loopback registry, not a
// real cross-process transport. Use whp::platform::Invitation directly (one
// layer below, in WASMHolePunch) for the punched-UDP transport.
#ifndef MOJO_PUBLIC_CPP_SYSTEM_INVITATION_H_
#define MOJO_PUBLIC_CPP_SYSTEM_INVITATION_H_

#include "mojo/public/c/system/core.h"
#include "mojo/public/cpp/system/handle.h"
#include "mojo/public/cpp/system/message_pipe.h"

#include <cstdint>
#include <string>
#include <unordered_map>

namespace mojo {

class InvitationHandle : public Handle {
 public:
  InvitationHandle() = default;
  explicit InvitationHandle(MojoHandle value) : Handle(value) {}
};
using ScopedInvitationHandle = ScopedHandleBase<InvitationHandle>;

// Identifies one loopback channel for MojoSendInvitation/MojoAcceptInvitation
// (see wst's README for the "first platform handle value" convention). Two
// invitations that use the same LoopbackChannel id rendezvous in-process.
class LoopbackChannel {
 public:
  explicit LoopbackChannel(uint64_t id) : id_(id) {}

  MojoInvitationTransportEndpoint AsEndpoint() {
    platform_handle_ = MojoPlatformHandle{};
    platform_handle_.struct_size = sizeof(platform_handle_);
    platform_handle_.type = MOJO_PLATFORM_HANDLE_TYPE_INVALID;
    platform_handle_.value = id_;

    MojoInvitationTransportEndpoint endpoint{};
    endpoint.struct_size = sizeof(endpoint);
    endpoint.type = MOJO_INVITATION_TRANSPORT_TYPE_CHANNEL;
    endpoint.num_platform_handles = 1;
    endpoint.platform_handles = &platform_handle_;
    return endpoint;
  }

 private:
  uint64_t id_;
  MojoPlatformHandle platform_handle_{};
};

// Mirrors mojo::OutgoingInvitation: attach named message pipes, then Send()
// hands the still-unaccepted peer ends to the loopback registry.
class OutgoingInvitation {
 public:
  OutgoingInvitation() { MojoCreateInvitation(nullptr, &handle_); }
  ~OutgoingInvitation() {
    if (handle_ != MOJO_HANDLE_INVALID) {
      MojoClose(handle_);
    }
  }
  OutgoingInvitation(OutgoingInvitation&& other) noexcept
      : handle_(other.handle_) {
    other.handle_ = MOJO_HANDLE_INVALID;
  }
  OutgoingInvitation(const OutgoingInvitation&) = delete;
  OutgoingInvitation& operator=(const OutgoingInvitation&) = delete;

  ScopedMessagePipeHandle AttachMessagePipe(const std::string& name) {
    MojoHandle pipe = MOJO_HANDLE_INVALID;
    MojoAttachMessagePipeToInvitation(handle_, name.data(),
                                      static_cast<uint32_t>(name.size()),
                                      nullptr, &pipe);
    return ScopedMessagePipeHandle(MessagePipeHandle(pipe));
  }

  static MojoResult Send(OutgoingInvitation invitation,
                         LoopbackChannel& channel) {
    MojoInvitationTransportEndpoint endpoint = channel.AsEndpoint();
    MojoResult result = MojoSendInvitation(invitation.handle_, nullptr,
                                           &endpoint, nullptr, 0, nullptr);
    invitation.handle_ = MOJO_HANDLE_INVALID;  // consumed by MojoSendInvitation
    return result;
  }

 private:
  MojoHandle handle_ = MOJO_HANDLE_INVALID;
};

// Mirrors mojo::IncomingInvitation.
class IncomingInvitation {
 public:
  static IncomingInvitation Accept(LoopbackChannel& channel) {
    MojoInvitationTransportEndpoint endpoint = channel.AsEndpoint();
    MojoHandle handle = MOJO_HANDLE_INVALID;
    MojoAcceptInvitation(&endpoint, nullptr, &handle);
    return IncomingInvitation(handle);
  }

  IncomingInvitation() = default;
  ~IncomingInvitation() {
    if (handle_ != MOJO_HANDLE_INVALID) {
      MojoClose(handle_);
    }
  }
  IncomingInvitation(IncomingInvitation&& other) noexcept
      : handle_(other.handle_) {
    other.handle_ = MOJO_HANDLE_INVALID;
  }
  IncomingInvitation(const IncomingInvitation&) = delete;
  IncomingInvitation& operator=(const IncomingInvitation&) = delete;

  bool is_valid() const { return handle_ != MOJO_HANDLE_INVALID; }

  ScopedMessagePipeHandle ExtractMessagePipe(const std::string& name) {
    MojoHandle pipe = MOJO_HANDLE_INVALID;
    MojoExtractMessagePipeFromInvitation(
        handle_, name.data(), static_cast<uint32_t>(name.size()), nullptr,
        &pipe);
    return ScopedMessagePipeHandle(MessagePipeHandle(pipe));
  }

 private:
  explicit IncomingInvitation(MojoHandle handle) : handle_(handle) {}

  MojoHandle handle_ = MOJO_HANDLE_INVALID;
};

}  // namespace mojo

#endif  // MOJO_PUBLIC_CPP_SYSTEM_INVITATION_H_
