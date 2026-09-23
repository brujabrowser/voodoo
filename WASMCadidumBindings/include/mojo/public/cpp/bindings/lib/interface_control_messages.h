// Reserved Message::name() ordinals and the wire handling behind them for
// per-interface version control -- QueryVersion (does the peer support at
// least version N?) and RequireVersion (close the pipe if it doesn't) --
// matching the role of real Mojo's
// mojo/public/interfaces/bindings/interface_control_messages.mojom
// (there, the two operations are named Run_/RunOrClosePipe_; kept here to
// avoid a Windows platform macro clash the same reason real Mojo's own
// comment gives).
//
// This is a genuinely different mechanism from MultiplexRouter's own
// router-level pipe-control-message protocol (see that header's comment
// and NotifyPeerEndpointClosed/HandlePipeControlMessage) -- that one
// operates on associated-endpoint lifecycle (peer-closed propagation
// across a ScopedInterfaceEndpointHandle) and is distinguished by
// targeting kInvalidInterfaceId, a separate wire concept from these
// *interface-level*, ordinal-based control messages. This file implements
// the interface-level half; multiplex_router.h implements the router-level
// half.
//
// No .voodoom-level attribute is needed to opt an interface in --
// QueryVersion/RequireVersion are protocol-level and available on every
// generated interface unconditionally (see WASMVoodooCompile's
// cpp_generator.cc: every Proxy_ gets QueryVersion()/RequireVersion(N),
// every Stub_::Accept() checks these two ordinals before its own ordinary
// method dispatch).
//
// Wire payloads are the tagged RunInput / RunOutput / RunOrClosePipeInput
// unions (tag then payload), so a later subcommand can be added without
// colliding with QueryVersion/RequireVersion:
//   kRunMessageId request:  {uint32_t tag=kRunQueryVersion}
//   kRunMessageId response: {uint32_t tag=kRunQueryVersionResult,
//                            uint32_t version}
//   kRunOrClosePipeMessageId request: {uint32_t tag=kRunRequireVersion,
//                            uint32_t min_version} -- no response; the
//                            receiver either accepts silently (its own
//                            version >= min_version) or Accept() returns
//                            false, which Connector::RaiseError() treats
//                            as a protocol error and closes the local
//                            pipe so the peer observes PEER_CLOSED.
#ifndef MOJO_PUBLIC_CPP_BINDINGS_LIB_INTERFACE_CONTROL_MESSAGES_H_
#define MOJO_PUBLIC_CPP_BINDINGS_LIB_INTERFACE_CONTROL_MESSAGES_H_

#include "mojo/public/cpp/bindings/interface_id.h"
#include "mojo/public/cpp/bindings/lib/multiplex_router.h"
#include "mojo/public/cpp/bindings/lib/wire_primitives.h"
#include "mojo/public/cpp/bindings/message.h"

#include <cstddef>
#include <cstdint>

namespace mojo::internal {

inline constexpr uint32_t kRunMessageId = 0xFFFFFFFFu;
inline constexpr uint32_t kRunOrClosePipeMessageId = 0xFFFFFFFEu;
inline constexpr uint32_t kRunQueryVersion = 0u;
inline constexpr uint32_t kRunQueryVersionResult = 0u;
inline constexpr uint32_t kRunRequireVersion = 0u;

// Builds and sends the {uint32_t version} response to an incoming
// kRunMessageId (QueryVersion) request. Returns whether sending the
// response succeeded, matching Stub_::Accept's own [[nodiscard]] bool
// contract (a generated Stub_::Accept just returns this directly).
inline bool HandleQueryVersionMessage(Message* message,
                                       MultiplexRouter* router,
                                       InterfaceId id,
                                       uint32_t version) {
  const uint64_t request_id = message->request_id();
  Message response(kRunMessageId, Message::kFlagIsResponse, id);
  response.set_request_id(request_id);
  WriteScalar(&response, kRunQueryVersionResult);
  WriteScalar(&response, version);
  return router->SendMessage(&response);
}

// Reads the requested minimum version out of an incoming
// kRunOrClosePipeMessageId (RequireVersion) request and reports whether
// `version` (the responder's own interface version) satisfies it. See
// this header's comment for what a `false` return actually does
// (propagates out through Stub_::Accept into Connector::RaiseError()).
inline bool HandleRequireVersionMessage(const Message& message,
                                         uint32_t version) {
  size_t offset = 0;
  uint32_t tag = 0;
  uint32_t required_version = 0;
  if (!ReadScalar(message, &offset, &tag)) return false;
  if (tag != kRunRequireVersion) return false;
  if (!ReadScalar(message, &offset, &required_version)) return false;
  return required_version <= version;
}

}  // namespace mojo::internal

#endif  // MOJO_PUBLIC_CPP_BINDINGS_LIB_INTERFACE_CONTROL_MESSAGES_H_
