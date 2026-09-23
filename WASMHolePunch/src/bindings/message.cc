#include "whp/message.h"

#include "whp/base/check.h"

#include <cstring>
#include <tuple>

namespace whp {

namespace {

void* GetBuffer(WhpMessageHandle h, uint32_t* size) {
  void* buf = nullptr;
  uint32_t n = 0;
  WhpGetMessageData(h, nullptr, &buf, &n, nullptr, nullptr);
  if (size) {
    *size = n;
  }
  return buf;
}

}  // namespace

Message::Message() { WhpInit(); }

Message::Message(uint32_t name, uint32_t flags, size_t estimated_payload_size) {
  WhpInit();
  EnsureHeaderV3(name, flags);
  if (estimated_payload_size) {
    AllocatePayload(estimated_payload_size);
  }
}

Message::Message(std::span<const uint8_t> payload, std::span<WhpHandle> handles) {
  WhpInit();
  WHP_CHECK(WhpCreateMessage(nullptr, &handle_) == WHP_RESULT_OK);
  void* buf = nullptr;
  uint32_t sz = 0;
  WhpAppendMessageDataOptions opts{};
  opts.struct_size = sizeof(opts);
  opts.flags = WHP_APPEND_MESSAGE_DATA_FLAG_COMMIT_SIZE;
  WHP_CHECK(WhpAppendMessageData(
                handle_, static_cast<uint32_t>(payload.size()),
                handles.empty() ? nullptr : handles.data(),
                static_cast<uint32_t>(handles.size()), &opts, &buf, &sz) ==
            WHP_RESULT_OK);
  if (!payload.empty() && buf) {
    std::memcpy(buf, payload.data(), payload.size());
  }
}

Message::Message(Message&& other) noexcept : handle_(other.handle_) {
  other.handle_ = WHP_MESSAGE_HANDLE_INVALID;
}

Message& Message::operator=(Message&& other) noexcept {
  if (this != &other) {
    Reset();
    handle_ = other.handle_;
    other.handle_ = WHP_MESSAGE_HANDLE_INVALID;
  }
  return *this;
}

Message::~Message() { Reset(); }

Message Message::CreateFromMessageHandle(WhpMessageHandle* handle) {
  Message m;
  if (handle && *handle != WHP_MESSAGE_HANDLE_INVALID) {
    m.handle_ = *handle;
    *handle = WHP_MESSAGE_HANDLE_INVALID;
  }
  return m;
}

void Message::Reset() {
  if (handle_ != WHP_MESSAGE_HANDLE_INVALID) {
    WhpDestroyMessage(handle_);
    handle_ = WHP_MESSAGE_HANDLE_INVALID;
  }
}

void Message::EnsureHeaderV3(uint32_t name, uint32_t flags) {
  WHP_CHECK(WhpCreateMessage(nullptr, &handle_) == WHP_RESULT_OK);
  void* buf = nullptr;
  uint32_t sz = 0;
  WHP_CHECK(WhpAppendMessageData(handle_, sizeof(internal::MessageHeaderV3),
                                 nullptr, 0, nullptr, &buf, &sz) ==
            WHP_RESULT_OK);
  std::memset(buf, 0, sizeof(internal::MessageHeaderV3));
  auto* h = static_cast<internal::MessageHeaderV3*>(buf);
  h->num_bytes = sizeof(internal::MessageHeaderV3);
  h->version = 3;
  h->name = name;
  h->flags = flags;
  h->payload.Set(h + 1);
}

const uint8_t* Message::data() const {
  return static_cast<const uint8_t*>(GetBuffer(handle_, nullptr));
}

uint8_t* Message::mutable_data() { return const_cast<uint8_t*>(data()); }

size_t Message::data_num_bytes() const {
  uint32_t n = 0;
  GetBuffer(handle_, &n);
  return n;
}

const internal::MessageHeader* Message::header() const {
  return reinterpret_cast<const internal::MessageHeader*>(data());
}

internal::MessageHeader* Message::header() {
  return reinterpret_cast<internal::MessageHeader*>(mutable_data());
}

const internal::MessageHeaderV1* Message::header_v1() const {
  return reinterpret_cast<const internal::MessageHeaderV1*>(data());
}

internal::MessageHeaderV1* Message::header_v1() {
  return reinterpret_cast<internal::MessageHeaderV1*>(mutable_data());
}

const internal::MessageHeaderV2* Message::header_v2() const {
  return reinterpret_cast<const internal::MessageHeaderV2*>(data());
}

internal::MessageHeaderV2* Message::header_v2() {
  return reinterpret_cast<internal::MessageHeaderV2*>(mutable_data());
}

const internal::MessageHeaderV3* Message::header_v3() const {
  return reinterpret_cast<const internal::MessageHeaderV3*>(data());
}

internal::MessageHeaderV3* Message::header_v3() {
  return reinterpret_cast<internal::MessageHeaderV3*>(mutable_data());
}

uint32_t Message::version() const { return header()->version; }

uint32_t Message::interface_id() const { return header()->interface_id; }

void Message::set_interface_id(uint32_t id) { header()->interface_id = id; }

uint32_t Message::name() const { return header()->name; }

bool Message::has_flag(uint32_t flag) const {
  return (header()->flags & flag) != 0;
}

uint64_t Message::request_id() const { return header_v1()->request_id; }

void Message::set_request_id(uint64_t id) { header_v1()->request_id = id; }

const uint8_t* Message::payload() const {
  if (version() < 2) {
    return data() + header()->num_bytes;
  }
  return static_cast<const uint8_t*>(header_v2()->payload.Get());
}

uint8_t* Message::mutable_payload() { return const_cast<uint8_t*>(payload()); }

uint32_t Message::payload_num_bytes() const {
  if (version() < 2) {
    return static_cast<uint32_t>(data_num_bytes() - header()->num_bytes);
  }
  auto begin = reinterpret_cast<uintptr_t>(header_v2()->payload.Get());
  auto end = reinterpret_cast<uintptr_t>(header_v2()->payload_interface_ids.Get());
  if (!end) {
    end = reinterpret_cast<uintptr_t>(data() + data_num_bytes());
  }
  return static_cast<uint32_t>(end - begin);
}

uint32_t Message::payload_num_interface_ids() const {
  if (version() < 2 || header_v2()->payload_interface_ids.is_null()) {
    return 0;
  }
  return header_v2()->payload_interface_ids.Get()->num_elements;
}

const uint32_t* Message::payload_interface_ids() const {
  if (version() < 2 || header_v2()->payload_interface_ids.is_null()) {
    return nullptr;
  }
  const auto* header = header_v2()->payload_interface_ids.Get();
  return reinterpret_cast<const uint32_t*>(
      reinterpret_cast<const uint8_t*>(header) + sizeof(internal::ArrayHeader));
}

void* Message::AllocatePayload(size_t n) {
  void* buf = nullptr;
  uint32_t sz = 0;
  WHP_CHECK(WhpAppendMessageData(handle_, static_cast<uint32_t>(n), nullptr, 0,
                                 nullptr, &buf, &sz) == WHP_RESULT_OK);
  return mutable_payload() + (payload_num_bytes() - static_cast<uint32_t>(n));
}

WhpMessageHandle Message::TakeMojoMessage() {
  WhpAppendMessageDataOptions opts{};
  opts.struct_size = sizeof(opts);
  opts.flags = WHP_APPEND_MESSAGE_DATA_FLAG_COMMIT_SIZE;
  void* buf = nullptr;
  uint32_t sz = 0;
  WhpAppendMessageData(handle_, 0, nullptr, 0, &opts, &buf, &sz);
  WhpMessageHandle h = handle_;
  handle_ = WHP_MESSAGE_HANDLE_INVALID;
  return h;
}

void SendMojoMessage(MessageReceiver& receiver, Message& message) {
  std::ignore = receiver.Accept(&message);
}

void SendMojoMessage(MessageReceiverWithResponder& receiver,
                     Message& message,
                     std::unique_ptr<MessageReceiver> responder) {
  std::ignore = receiver.AcceptWithResponder(&message, std::move(responder));
}

}  // namespace whp
