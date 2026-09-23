// Wire-format structs, byte-for-byte compatible with Chromium's
// mojo/public/cpp/bindings/lib/{bindings_internal.h,message_internal.h}.
// This is what makes a WASMCadidumBindings Message interoperable, at the
// byte level, with anything that understands a real Mojo Message header --
// even though the transport underneath (wck/wst/whp) is not real Mojo Core.
#ifndef MOJO_PUBLIC_CPP_BINDINGS_LIB_MESSAGE_INTERNAL_H_
#define MOJO_PUBLIC_CPP_BINDINGS_LIB_MESSAGE_INTERNAL_H_

#include <cstdint>

namespace mojo::internal {

#pragma pack(push, 1)

// Common 8-byte header prefix shared by every serialized mojom struct,
// including the message header itself.
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

// A relative-offset pointer, as it appears on the wire inside a serialized
// mojom struct. We only need the storage shape (8 bytes) to keep
// MessageHeaderV2 byte-compatible -- WASMCadidumBindings does not itself
// walk pointer-encoded payloads (see message.h).
template <typename T>
struct Pointer {
  using BaseType = T;
  bool is_null() const { return offset == 0; }
  uint64_t offset = 0;
};

using GenericPointer = Pointer<void>;

template <typename T>
struct Array_Data;  // Not instantiated; exists only so Pointer<Array_Data<T>>
                     // below names a type, matching the real header shape.

// v0: the header every message has. `interface_id` selects which endpoint
// on the (possibly multiplexed) pipe this message targets --
// kPrimaryInterfaceId (0) for the primary interface, or an associated
// interface id minted by MultiplexRouter. `name` is the method ordinal.
struct MessageHeader : StructHeader {
  uint32_t interface_id;
  uint32_t name;
  uint32_t flags;
  uint32_t trace_nonce;
};
static_assert(sizeof(MessageHeader) == 24, "Bad sizeof(MessageHeader)");

// v1: adds a request id, used to pair a [Sync] or callback-bearing call
// with its response when kFlagExpectsResponse/kFlagIsResponse is set.
// WASMCadidumBindings always emits at least a v1 header.
struct MessageHeaderV1 : MessageHeader {
  uint64_t request_id;
};
static_assert(sizeof(MessageHeaderV1) == 32, "Bad sizeof(MessageHeaderV1)");

// v2: payload moves behind an explicit relative pointer. Message produces
// v2 (payload immediately follows the 48-byte header; payload.offset = 16).
struct MessageHeaderV2 : MessageHeaderV1 {
  GenericPointer payload;
  Pointer<Array_Data<uint32_t>> payload_interface_ids;
};
static_assert(sizeof(MessageHeaderV2) == 48, "Bad sizeof(MessageHeaderV2)");

#pragma pack(pop)

}  // namespace mojo::internal

#endif  // MOJO_PUBLIC_CPP_BINDINGS_LIB_MESSAGE_INTERNAL_H_
