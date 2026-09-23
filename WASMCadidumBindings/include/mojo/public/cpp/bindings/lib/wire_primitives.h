// Field-level (de)serialization primitives shared by generated-style
// Proxy_/Stub_ code -- hand-written (examples/echo) or emitted by
// WASMVoodooCompile. This is *not* the generic struct (de)serializer
// message.h's header comment explains this repo doesn't ship: each of these
// functions handles exactly one scalar or one length-prefixed string, the
// same way examples/echo/echo_interface.h's WriteString/ReadString did
// before this header existed -- callers still hand-roll each field's
// position in the payload, just without re-typing the memcpy boilerplate
// per interface.
//
// Wire format: scalars and string lengths are little-endian (Mojo's
// on-the-wire endianness). Hosts that are already LE write the same
// bytes as memcpy; BE hosts swap.
#ifndef MOJO_PUBLIC_CPP_BINDINGS_LIB_WIRE_PRIMITIVES_H_
#define MOJO_PUBLIC_CPP_BINDINGS_LIB_WIRE_PRIMITIVES_H_

#include "mojo/public/cpp/bindings/message.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <type_traits>

namespace mojo::internal {

template <typename T>
inline void StoreLittleEndian(uint8_t* dest, T value) {
  static_assert(std::is_trivially_copyable_v<T>);
  uint8_t tmp[sizeof(T)];
  std::memcpy(tmp, &value, sizeof(T));
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
  for (size_t i = 0; i < sizeof(T) / 2; ++i) {
    uint8_t s = tmp[i];
    tmp[i] = tmp[sizeof(T) - 1 - i];
    tmp[sizeof(T) - 1 - i] = s;
  }
#endif
  std::memcpy(dest, tmp, sizeof(T));
}

template <typename T>
inline T LoadLittleEndian(const uint8_t* src) {
  static_assert(std::is_trivially_copyable_v<T>);
  uint8_t tmp[sizeof(T)];
  std::memcpy(tmp, src, sizeof(T));
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
  for (size_t i = 0; i < sizeof(T) / 2; ++i) {
    uint8_t s = tmp[i];
    tmp[i] = tmp[sizeof(T) - 1 - i];
    tmp[sizeof(T) - 1 - i] = s;
  }
#endif
  T value{};
  std::memcpy(&value, tmp, sizeof(T));
  return value;
}

template <typename T>
inline void WriteScalar(Message* message, const T& value) {
  static_assert(std::is_trivially_copyable_v<T>);
  uint8_t buf[sizeof(T)];
  StoreLittleEndian(buf, value);
  message->WritePayload(buf, sizeof(T));
}

// Reads a T starting at byte `*offset` of `message`'s payload, advancing
// *offset past it. False (and *offset left where it was) if truncated.
template <typename T>
inline bool ReadScalar(const Message& message, size_t* offset, T* out) {
  static_assert(std::is_trivially_copyable_v<T>);
  const uint8_t* payload = message.payload();
  const uint32_t avail = message.payload_num_bytes();
  if (*offset + sizeof(T) > avail) {
    return false;
  }
  *out = LoadLittleEndian<T>(payload + *offset);
  *offset += sizeof(T);
  return true;
}

inline void WriteStructHeader(Message* message, uint32_t num_bytes,
                              uint32_t version) {
  WriteScalar(message, num_bytes);
  WriteScalar(message, version);
}

inline void PatchStructHeader(uint8_t* dest, uint32_t num_bytes,
                              uint32_t version) {
  StoreLittleEndian(dest, num_bytes);
  StoreLittleEndian(dest + sizeof(uint32_t), version);
}

inline void PatchUint32(uint8_t* dest, uint32_t value) {
  StoreLittleEndian(dest, value);
}

inline bool ReadStructHeader(const Message& message, size_t* offset,
                             uint32_t* num_bytes, uint32_t* version) {
  return ReadScalar(message, offset, num_bytes) &&
         ReadScalar(message, offset, version);
}

inline void WriteString(Message* message, const std::string& s) {
  WriteScalar(message, static_cast<uint32_t>(s.size()));
  if (!s.empty()) {
    message->WritePayload(s.data(), s.size());
  }
}

inline bool ReadString(const Message& message, size_t* offset,
                        std::string* out) {
  uint32_t len = 0;
  if (!ReadScalar(message, offset, &len)) {
    return false;
  }
  const uint32_t avail = message.payload_num_bytes();
  if (*offset + len > avail) {
    return false;
  }
  out->assign(reinterpret_cast<const char*>(message.payload()) + *offset,
              len);
  *offset += len;
  return true;
}

}  // namespace mojo::internal

#endif  // MOJO_PUBLIC_CPP_BINDINGS_LIB_WIRE_PRIMITIVES_H_
