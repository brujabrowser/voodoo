#ifndef WHP_CODEC_H_
#define WHP_CODEC_H_

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace whp {
namespace internal {

inline constexpr size_t Align(size_t size) { return (size + 7) & ~size_t{7}; }

inline bool IsAligned(const void* ptr) {
  return (reinterpret_cast<uintptr_t>(ptr) & 0x7) == 0;
}

inline void EncodePointer(const void* ptr, uint64_t* offset) {
  if (!ptr) {
    *offset = 0;
    return;
  }
  const char* obj = static_cast<const char*>(ptr);
  const char* slot = reinterpret_cast<const char*>(offset);
  *offset = static_cast<uint64_t>(obj - slot);
}

inline const void* DecodePointer(const uint64_t* offset) {
  if (!*offset) {
    return nullptr;
  }
  return reinterpret_cast<const char*>(offset) + *offset;
}

inline constexpr uint32_t kEncodedInvalidHandleValue = 0xFFFFFFFFu;
inline constexpr uint32_t kUnionDataSize = 16;

#pragma pack(push, 1)

struct StructHeader {
  uint32_t num_bytes;
  uint32_t version;
};
static_assert(sizeof(StructHeader) == 8, "Bad sizeof(StructHeader)");

struct ArrayHeader {
  uint32_t num_bytes;
  uint32_t num_elements;
};
static_assert(sizeof(ArrayHeader) == 8, "Bad sizeof(ArrayHeader)");

template <typename T>
struct Pointer {
  using BaseType = T;

  void Set(T* ptr) { EncodePointer(ptr, &offset); }
  const T* Get() const { return static_cast<const T*>(DecodePointer(&offset)); }
  T* Get() {
    return static_cast<T*>(const_cast<void*>(DecodePointer(&offset)));
  }
  bool is_null() const { return offset == 0; }

  uint64_t offset = 0;
};
static_assert(sizeof(Pointer<char>) == 8, "Bad sizeof(Pointer)");

using GenericPointer = Pointer<void>;

struct HandleData {
  uint32_t value = kEncodedInvalidHandleValue;
  bool is_valid() const { return value != kEncodedInvalidHandleValue; }
};
static_assert(sizeof(HandleData) == 4, "Bad sizeof(HandleData)");

struct MessageHeader : StructHeader {
  uint32_t interface_id = 0;
  uint32_t name = 0;
  uint32_t flags = 0;
  uint32_t trace_nonce = 0;
};
static_assert(sizeof(MessageHeader) == 24, "Bad sizeof(MessageHeader)");

struct MessageHeaderV1 : MessageHeader {
  uint64_t request_id = 0;
};
static_assert(sizeof(MessageHeaderV1) == 32, "Bad sizeof(MessageHeaderV1)");

struct MessageHeaderV2 : MessageHeaderV1 {
  GenericPointer payload;
  Pointer<ArrayHeader> payload_interface_ids;
};
static_assert(sizeof(MessageHeaderV2) == 48, "Bad sizeof(MessageHeaderV2)");

struct MessageHeaderV3 : MessageHeaderV2 {
  int64_t creation_timeticks_us = 0;
};
static_assert(sizeof(MessageHeaderV3) == 56, "Bad sizeof(MessageHeaderV3)");

#pragma pack(pop)

inline size_t StringDataSize(size_t num_chars) {
  // UTF-8 bytes + NUL terminator, 8-byte aligned, plus array header.
  return Align(sizeof(ArrayHeader) + num_chars + 1);
}

// Encode a string at `dest` (must have StringDataSize(s.size()) bytes).
inline void EncodeString(char* dest, std::string_view s) {
  auto* header = reinterpret_cast<ArrayHeader*>(dest);
  header->num_elements = static_cast<uint32_t>(s.size());
  header->num_bytes = static_cast<uint32_t>(StringDataSize(s.size()));
  char* chars = dest + sizeof(ArrayHeader);
  if (!s.empty()) {
    std::memcpy(chars, s.data(), s.size());
  }
  chars[s.size()] = '\0';
}

inline bool DecodeString(const ArrayHeader* header,
                         size_t max_bytes,
                         std::string* out) {
  if (!header || header->num_bytes < sizeof(ArrayHeader) ||
      header->num_bytes > max_bytes) {
    return false;
  }
  const char* chars = reinterpret_cast<const char*>(header + 1);
  if (sizeof(ArrayHeader) + header->num_elements + 1 > header->num_bytes) {
    return false;
  }
  out->assign(chars, header->num_elements);
  return true;
}

}  // namespace internal
}  // namespace whp

#endif  // WHP_CODEC_H_
