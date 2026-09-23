// mojo::Message / mojo::MessageReceiver, matching the shape of Chromium's
// mojo/public/cpp/bindings/message.h closely enough that generated-style
// code (Proxy_/Stub_ classes, hand-written or emitted by a future .voodoom
// compiler) can be written the same way it would be against real Mojo:
// build a Message, fill its payload, hand it to a MessageReceiver::Accept().
//
// Produced messages use a v2 header (MessageHeaderV2) with the payload
// behind an explicit relative pointer immediately following the header.
// WrapWireBytes accepts v0/v1 (flat payload) and v2 (pointer payload) and
// normalizes them to v2 in memory. Versions newer than 2 are rejected.
#ifndef MOJO_PUBLIC_CPP_BINDINGS_MESSAGE_H_
#define MOJO_PUBLIC_CPP_BINDINGS_MESSAGE_H_

#include "mojo/public/cpp/bindings/interface_id.h"
#include "mojo/public/cpp/bindings/lib/message_internal.h"
#include "mojo/public/cpp/system/handle.h"
#include "src/sandbox/cage-allocator.h"

#include <cstdint>
#include <vector>

namespace mojo {

class Message {
 public:
  static constexpr uint32_t kFlagExpectsResponse = 1u << 0;
  static constexpr uint32_t kFlagIsResponse = 1u << 1;
  static constexpr uint32_t kFlagIsSync = 1u << 2;
  static constexpr uint32_t kFlagNoInterrupt = 1u << 3;
  static constexpr uint32_t kFlagIsUrgent = 1u << 4;

  // Null message (IsNull() == true); only valid targets of move-assignment.
  Message();

  // A fresh, empty (zero payload bytes) message ready for WritePayload().
  explicit Message(uint32_t name,
                    uint32_t flags = 0,
                    InterfaceId interface_id = kPrimaryInterfaceId);

  Message(Message&&) noexcept = default;
  Message& operator=(Message&&) noexcept = default;
  Message(const Message&) = delete;
  Message& operator=(const Message&) = delete;
  ~Message() = default;

  bool IsNull() const { return buffer_.empty(); }

  // Appends raw bytes to the payload (everything after the header).
  void WritePayload(const void* data, size_t num_bytes);
  const uint8_t* payload() const;
  uint8_t* mutable_payload();
  uint32_t payload_num_bytes() const;

  // Handles carried by (and owned by) this message.
  void AttachHandle(ScopedHandle handle);
  std::vector<ScopedHandle> TakeHandles();
  const std::vector<ScopedHandle>& handles() const { return handles_; }

  // Whole-message wire bytes (header + payload) -- what actually goes to
  // mojo::WriteMessageRaw, or comes back from mojo::ReadMessageRaw.
  const uint8_t* data() const { return buffer_.data(); }
  uint32_t data_num_bytes() const {
    return static_cast<uint32_t>(buffer_.size());
  }

  const internal::MessageHeader* header() const;
  const internal::MessageHeaderV1* header_v1() const;
  internal::MessageHeaderV1* mutable_header_v1();
  const internal::MessageHeaderV2* header_v2() const;
  internal::MessageHeaderV2* mutable_header_v2();

  uint32_t version() const { return header()->version; }
  InterfaceId interface_id() const { return header()->interface_id; }
  void set_interface_id(InterfaceId id) { mutable_header_v1()->interface_id = id; }
  uint32_t name() const { return header()->name; }
  uint32_t flags() const { return header()->flags; }
  bool has_flag(uint32_t flag) const { return (flags() & flag) != 0; }
  uint64_t request_id() const { return header_v1()->request_id; }
  void set_request_id(uint64_t id) { mutable_header_v1()->request_id = id; }

  // Reconstructs a Message from wire bytes (as produced by
  // mojo::ReadMessageRaw) and the handles read alongside them. Returns a
  // null message if `bytes` is too short, or its header version is newer
  // than this library understands (see class comment).
  static Message WrapWireBytes(v8::internal::CageBytes bytes,
                                std::vector<ScopedHandle> handles);

 private:
  // Cage-backed (see cage-allocator.h): this is the message's whole wire
  // representation, live for the entire dispatch path (Connector ->
  // MultiplexRouter -> Stub_ -> whatever ReadString/etc. reads out of
  // payload()) -- the same high-churn, crosses-multiple-owners object
  // that buffer's protection was added for at every other layer
  // (WASMHolePunch's MessageObj::bytes, ReadMessageRaw's own `payload`).
  v8::internal::CageBytes buffer_;  // MessageHeaderV2 followed by payload bytes.
  std::vector<ScopedHandle> handles_;
};

// Anything that can accept a fully-built Message: a Connector (send it out
// the pipe), a MultiplexRouter (route it to the right endpoint), or a
// generated Stub_ (deserialize it and call into an implementation).
class MessageReceiver {
 public:
  virtual ~MessageReceiver() = default;

  // False means the message was rejected (malformed, unknown ordinal, wrong
  // pipe state, ...); the caller treats that the same as a pipe error.
  [[nodiscard]] virtual bool Accept(Message* message) = 0;
};

}  // namespace mojo

#endif  // MOJO_PUBLIC_CPP_BINDINGS_MESSAGE_H_
