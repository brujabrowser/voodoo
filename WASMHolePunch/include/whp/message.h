#ifndef WHP_MESSAGE_H_
#define WHP_MESSAGE_H_

#include "whp/c/system.h"
#include "whp/codec.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace whp {

class Message {
 public:
  static constexpr uint32_t kFlagExpectsResponse = 1u << 0;
  static constexpr uint32_t kFlagIsResponse = 1u << 1;
  static constexpr uint32_t kFlagIsSync = 1u << 2;
  static constexpr uint32_t kFlagNoInterrupt = 1u << 3;
  static constexpr uint32_t kFlagIsUrgent = 1u << 4;

  Message();
  Message(uint32_t name, uint32_t flags, size_t estimated_payload_size = 0);
  Message(std::span<const uint8_t> payload, std::span<WhpHandle> handles);
  Message(Message&& other) noexcept;
  Message& operator=(Message&& other) noexcept;
  Message(const Message&) = delete;
  Message& operator=(const Message&) = delete;
  ~Message();

  static Message CreateFromMessageHandle(WhpMessageHandle* handle);

  void Reset();
  bool IsNull() const { return handle_ == WHP_MESSAGE_HANDLE_INVALID; }

  const uint8_t* data() const;
  uint8_t* mutable_data();
  size_t data_num_bytes() const;

  const internal::MessageHeader* header() const;
  internal::MessageHeader* header();
  const internal::MessageHeaderV1* header_v1() const;
  internal::MessageHeaderV1* header_v1();
  const internal::MessageHeaderV2* header_v2() const;
  internal::MessageHeaderV2* header_v2();
  const internal::MessageHeaderV3* header_v3() const;
  internal::MessageHeaderV3* header_v3();

  uint32_t version() const;
  uint32_t interface_id() const;
  void set_interface_id(uint32_t id);
  uint32_t name() const;
  bool has_flag(uint32_t flag) const;
  uint64_t request_id() const;
  void set_request_id(uint64_t id);

  const uint8_t* payload() const;
  uint8_t* mutable_payload();
  uint32_t payload_num_bytes() const;
  uint32_t payload_num_interface_ids() const;
  const uint32_t* payload_interface_ids() const;

  // Grow the payload (after the V3 header). Returns pointer to the new bytes.
  void* AllocatePayload(size_t n);

  WhpMessageHandle TakeMojoMessage();
  WhpMessageHandle handle() const { return handle_; }

 private:
  void EnsureHeaderV3(uint32_t name, uint32_t flags);

  WhpMessageHandle handle_ = WHP_MESSAGE_HANDLE_INVALID;
};

class MessageReceiver {
 public:
  virtual ~MessageReceiver() = default;
  virtual bool PrefersSerializedMessages() { return false; }
  [[nodiscard]] virtual bool Accept(Message* message) = 0;
};

class MessageReceiverWithResponder : public MessageReceiver {
 public:
  ~MessageReceiverWithResponder() override = default;
  [[nodiscard]] virtual bool AcceptWithResponder(
      Message* message,
      std::unique_ptr<MessageReceiver> responder) = 0;
};

void SendMojoMessage(MessageReceiver& receiver, Message& message);
void SendMojoMessage(MessageReceiverWithResponder& receiver,
                     Message& message,
                     std::unique_ptr<MessageReceiver> responder);

}  // namespace whp

#endif  // WHP_MESSAGE_H_
