#ifndef MOJO_PUBLIC_CPP_SYSTEM_MESSAGE_PIPE_H_
#define MOJO_PUBLIC_CPP_SYSTEM_MESSAGE_PIPE_H_

#include "mojo/public/c/system/core.h"
#include "mojo/public/cpp/system/handle.h"
#include "src/sandbox/cage-allocator.h"

#include <vector>

namespace mojo {

class MessagePipeHandle : public Handle {
 public:
  MessagePipeHandle() = default;
  explicit MessagePipeHandle(MojoHandle value) : Handle(value) {}
};

using ScopedMessagePipeHandle = ScopedHandleBase<MessagePipeHandle>;

inline MojoResult CreateMessagePipe(
    const MojoCreateMessagePipeOptions* options,
    ScopedMessagePipeHandle* handle0,
    ScopedMessagePipeHandle* handle1) {
  MojoHandle h0 = MOJO_HANDLE_INVALID;
  MojoHandle h1 = MOJO_HANDLE_INVALID;
  MojoResult result = MojoCreateMessagePipe(options, &h0, &h1);
  if (result == MOJO_RESULT_OK) {
    handle0->reset(MessagePipeHandle(h0));
    handle1->reset(MessagePipeHandle(h1));
  }
  return result;
}

inline MojoResult FuseMessagePipes(ScopedMessagePipeHandle handle0,
                                   ScopedMessagePipeHandle handle1) {
  return MojoFuseMessagePipes(handle0.release().value(),
                              handle1.release().value(), nullptr);
}

// Convenience wrapper matching mojo::WriteMessageRaw: builds a
// MojoMessageHandle, appends `bytes`, attaches `handles` (which are
// consumed -- their MojoHandle values are cleared), and writes it.
MojoResult WriteMessageRaw(MessagePipeHandle pipe,
                           const void* bytes,
                           uint32_t num_bytes,
                           const MojoHandle* handles,
                           uint32_t num_handles,
                           MojoWriteMessageFlags flags);

// Convenience wrapper matching mojo::ReadMessageRaw: reads one message,
// copies its payload into `*payload` and its handles into `*handles`
// (clearing prior contents), then destroys the message. `payload` is
// cage-backed (v8::internal::CageBytes, see cage-allocator.h) -- the same
// wire-byte buffer WASMHolePunch's own MessageObj::bytes now uses, so it
// stays cage-protected the whole way from WhpGetMessageData's memcpy
// through to whatever Connector::ReadAllAvailableMessages (wcb) does with
// it, instead of landing back in plain heap the moment it crosses this
// C++ API boundary.
MojoResult ReadMessageRaw(MessagePipeHandle pipe,
                          v8::internal::CageBytes* payload,
                          std::vector<MojoHandle>* handles,
                          MojoReadMessageFlags flags);

}  // namespace mojo

#endif  // MOJO_PUBLIC_CPP_SYSTEM_MESSAGE_PIPE_H_
