#include "whp/message_header_validator.h"

#include "whp/interface_id.h"

#include <algorithm>
#include <cstring>

namespace whp {
namespace {

class ValidationContext {
 public:
  ValidationContext(const void* data, size_t num_bytes)
      : data_(static_cast<const uint8_t*>(data)), num_bytes_(num_bytes) {}

  bool ClaimMemory(const void* ptr, size_t size) {
    const auto* p = static_cast<const uint8_t*>(ptr);
    if (p < data_ || p + size > data_ + num_bytes_) {
      return false;
    }
    return true;
  }

  size_t num_bytes() const { return num_bytes_; }
  const uint8_t* data() const { return data_; }

 private:
  const uint8_t* data_;
  size_t num_bytes_;
};

bool ValidateStructHeaderAndClaimMemory(const void* data,
                                        ValidationContext* ctx) {
  if (!ctx->ClaimMemory(data, sizeof(internal::StructHeader))) {
    return false;
  }
  const auto* header = static_cast<const internal::StructHeader*>(data);
  if (header->num_bytes < sizeof(internal::StructHeader)) {
    return false;
  }
  return ctx->ClaimMemory(data, header->num_bytes);
}

bool IsValidMessageHeader(const internal::MessageHeader* header,
                          ValidationContext* ctx) {
  do {
    if (header->version == 0) {
      if (header->num_bytes == sizeof(internal::MessageHeader)) {
        break;
      }
    } else if (header->version == 1) {
      if (header->num_bytes == sizeof(internal::MessageHeaderV1)) {
        break;
      }
    } else if (header->version == 2) {
      if (header->num_bytes == sizeof(internal::MessageHeaderV2)) {
        break;
      }
    } else if (header->version == 3) {
      if (header->num_bytes == sizeof(internal::MessageHeaderV3)) {
        break;
      }
    } else if (header->version > 3) {
      if (header->num_bytes >= sizeof(internal::MessageHeaderV3)) {
        break;
      }
    }
    return false;
  } while (false);

  constexpr uint32_t kRequestIdFlags =
      Message::kFlagExpectsResponse | Message::kFlagIsResponse;
  if (header->version == 0 && (header->flags & kRequestIdFlags)) {
    return false;
  }
  if ((header->flags & kRequestIdFlags) == kRequestIdFlags) {
    return false;
  }
  if (header->version < 2) {
    return true;
  }

  const auto* header_v2 = static_cast<const internal::MessageHeaderV2*>(header);
  if (header_v2->payload.is_null()) {
    return false;
  }
  const auto* payload =
      static_cast<const uint8_t*>(header_v2->payload.Get());
  const uint8_t* begin = ctx->data();
  const uint8_t* end = begin + ctx->num_bytes();
  if (payload < begin || payload > end) {
    return false;
  }
  // Empty payload is legal: pointer may sit at the exclusive end of the buffer.
  if (payload < end && !ctx->ClaimMemory(payload, 1)) {
    return false;
  }

  if (!header_v2->payload_interface_ids.is_null()) {
    const auto* arr = header_v2->payload_interface_ids.Get();
    if (!ctx->ClaimMemory(arr, sizeof(internal::ArrayHeader))) {
      return false;
    }
    const size_t bytes = arr->num_bytes;
    if (bytes < sizeof(internal::ArrayHeader) ||
        !ctx->ClaimMemory(arr, bytes)) {
      return false;
    }
    const uint32_t* ids = reinterpret_cast<const uint32_t*>(
        reinterpret_cast<const uint8_t*>(arr) + sizeof(internal::ArrayHeader));
    for (uint32_t i = 0; i < arr->num_elements; ++i) {
      if (!IsValidInterfaceId(ids[i]) || IsPrimaryInterfaceId(ids[i])) {
        return false;
      }
    }
  }
  return true;
}

}  // namespace

MessageHeaderValidator::MessageHeaderValidator()
    : MessageHeaderValidator("MessageHeaderValidator") {}

MessageHeaderValidator::MessageHeaderValidator(std::string description)
    : description_(std::move(description)) {}

void MessageHeaderValidator::SetDescription(std::string description) {
  description_ = std::move(description);
}

bool MessageHeaderValidator::Accept(Message* message) {
  if (!message || message->IsNull()) {
    return false;
  }
  ValidationContext ctx(message->data(), message->data_num_bytes());
  if (!ValidateStructHeaderAndClaimMemory(message->data(), &ctx)) {
    return false;
  }
  return IsValidMessageHeader(message->header(), &ctx);
}

}  // namespace whp
