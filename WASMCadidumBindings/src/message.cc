#include "mojo/public/cpp/bindings/message.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <utility>

namespace mojo {
namespace {

// payload field sits at the end of MessageHeaderV1 (32); payload bytes
// follow the 48-byte v2 header, so the relative offset is 16.
constexpr uint64_t kV2PayloadOffset =
    sizeof(internal::MessageHeaderV2) - sizeof(internal::MessageHeaderV1);

size_t HeaderSizeForVersion(uint32_t version) {
  if (version == 0) {
    return sizeof(internal::MessageHeader);
  }
  if (version == 1) {
    return sizeof(internal::MessageHeaderV1);
  }
  return sizeof(internal::MessageHeaderV2);
}

}  // namespace

Message::Message() = default;

Message::Message(uint32_t name, uint32_t flags, InterfaceId interface_id) {
  buffer_.resize(sizeof(internal::MessageHeaderV2), 0);
  auto* h = mutable_header_v2();
  h->num_bytes = static_cast<uint32_t>(buffer_.size());
  h->version = 2;
  h->interface_id = interface_id;
  h->name = name;
  h->flags = flags;
  h->trace_nonce = 0;
  h->request_id = 0;
  h->payload.offset = kV2PayloadOffset;
  h->payload_interface_ids.offset = 0;
}

internal::MessageHeaderV2* Message::mutable_header_v2() {
  return reinterpret_cast<internal::MessageHeaderV2*>(buffer_.data());
}

const internal::MessageHeaderV2* Message::header_v2() const {
  return reinterpret_cast<const internal::MessageHeaderV2*>(buffer_.data());
}

void Message::WritePayload(const void* data, size_t num_bytes) {
  const auto* bytes = static_cast<const uint8_t*>(data);
  buffer_.insert(buffer_.end(), bytes, bytes + num_bytes);
  reinterpret_cast<internal::MessageHeader*>(buffer_.data())->num_bytes =
      static_cast<uint32_t>(buffer_.size());
}

const uint8_t* Message::payload() const {
  if (buffer_.empty()) {
    return nullptr;
  }
  if (version() < 2) {
    return buffer_.data() + sizeof(internal::MessageHeaderV1);
  }
  const auto* v2 = header_v2();
  if (v2->payload.is_null()) {
    return buffer_.data() + sizeof(internal::MessageHeaderV2);
  }
  return reinterpret_cast<const uint8_t*>(&v2->payload) + v2->payload.offset;
}

uint8_t* Message::mutable_payload() {
  return const_cast<uint8_t*>(payload());
}

uint32_t Message::payload_num_bytes() const {
  if (buffer_.empty()) {
    return 0;
  }
  const uint8_t* p = payload();
  return static_cast<uint32_t>(buffer_.data() + buffer_.size() - p);
}

void Message::AttachHandle(ScopedHandle handle) {
  handles_.push_back(std::move(handle));
}

std::vector<ScopedHandle> Message::TakeHandles() {
  return std::move(handles_);
}

const internal::MessageHeader* Message::header() const {
  return reinterpret_cast<const internal::MessageHeader*>(buffer_.data());
}

const internal::MessageHeaderV1* Message::header_v1() const {
  return reinterpret_cast<const internal::MessageHeaderV1*>(buffer_.data());
}

internal::MessageHeaderV1* Message::mutable_header_v1() {
  return reinterpret_cast<internal::MessageHeaderV1*>(buffer_.data());
}

// static
Message Message::WrapWireBytes(v8::internal::CageBytes bytes,
                                std::vector<ScopedHandle> handles) {
  if (bytes.size() < sizeof(internal::MessageHeader)) {
    return Message();
  }
  const auto* h =
      reinterpret_cast<const internal::MessageHeader*>(bytes.data());
  if (h->version > 2) {
    return Message();
  }
  const size_t incoming_header = HeaderSizeForVersion(h->version);
  if (bytes.size() < incoming_header) {
    return Message();
  }

  const uint8_t* incoming_payload = bytes.data() + incoming_header;
  uint32_t incoming_payload_size =
      static_cast<uint32_t>(bytes.size() - incoming_header);
  if (h->version >= 2) {
    const auto* v2 =
        reinterpret_cast<const internal::MessageHeaderV2*>(bytes.data());
    if (!v2->payload.is_null()) {
      incoming_payload =
          reinterpret_cast<const uint8_t*>(&v2->payload) + v2->payload.offset;
      if (incoming_payload < bytes.data() ||
          incoming_payload > bytes.data() + bytes.size()) {
        return Message();
      }
      incoming_payload_size = static_cast<uint32_t>(
          bytes.data() + bytes.size() - incoming_payload);
    }
  }

  Message m;
  m.buffer_.resize(sizeof(internal::MessageHeaderV2), 0);
  auto* out = m.mutable_header_v2();
  static_cast<internal::MessageHeader&>(*out) = *h;
  out->version = 2;
  out->request_id =
      h->version >= 1
          ? reinterpret_cast<const internal::MessageHeaderV1*>(bytes.data())
                ->request_id
          : 0;
  out->payload.offset = kV2PayloadOffset;
  out->payload_interface_ids.offset = 0;
  m.buffer_.insert(m.buffer_.end(), incoming_payload,
                    incoming_payload + incoming_payload_size);
  out->num_bytes = static_cast<uint32_t>(m.buffer_.size());
  m.handles_ = std::move(handles);
  return m;
}

}  // namespace mojo
