#include "test.h"

#include "mojo/public/cpp/bindings/lib/message_internal.h"
#include "mojo/public/cpp/bindings/lib/serialization.h"
#include "mojo/public/cpp/bindings/lib/wire_primitives.h"
#include "mojo/public/cpp/bindings/message.h"

#include <cstring>

using mojo::Message;
using mojo::ScopedHandle;

TEST(struct_header_is_little_endian_on_the_wire) {
  Message m(/*name=*/1);
  mojo::internal::WriteStructHeader(&m, 0x01020304u, 7u);
  EXPECT_EQ(m.payload_num_bytes(), 8u);
  const uint8_t* p = m.payload();
  EXPECT_EQ(p[0], 0x04u);
  EXPECT_EQ(p[1], 0x03u);
  EXPECT_EQ(p[2], 0x02u);
  EXPECT_EQ(p[3], 0x01u);
  EXPECT_EQ(p[4], 7u);
  size_t off = 0;
  uint32_t num_bytes = 0;
  uint32_t version = 0;
  EXPECT(mojo::internal::ReadStructHeader(m, &off, &num_bytes, &version));
  EXPECT_EQ(num_bytes, 0x01020304u);
  EXPECT_EQ(version, 7u);
}

TEST(message_header_sizes) {
  // Compile-time static_asserts already pin these; re-check at runtime too
  // so a mismatch shows up in test output, not just a build failure.
  EXPECT_EQ(sizeof(mojo::internal::StructHeader), 8u);
  EXPECT_EQ(sizeof(mojo::internal::ArrayHeader), 8u);
  EXPECT_EQ(sizeof(mojo::internal::MessageHeader), 24u);
  EXPECT_EQ(sizeof(mojo::internal::MessageHeaderV1), 32u);
  EXPECT_EQ(sizeof(mojo::internal::MessageHeaderV2), 48u);
}

TEST(message_build_and_read_fields) {
  Message m(/*name=*/7, Message::kFlagExpectsResponse, /*interface_id=*/3);
  m.set_request_id(42);
  const char payload[] = "hello";
  m.WritePayload(payload, sizeof(payload) - 1);

  EXPECT_EQ(m.name(), 7u);
  EXPECT_EQ(m.interface_id(), 3u);
  EXPECT(m.has_flag(Message::kFlagExpectsResponse));
  EXPECT(!m.has_flag(Message::kFlagIsResponse));
  EXPECT_EQ(m.request_id(), 42u);
  EXPECT_EQ(m.payload_num_bytes(), 5u);
  EXPECT(std::memcmp(m.payload(), payload, 5) == 0);
  EXPECT_EQ(m.version(), 2u);
  EXPECT_EQ(m.data_num_bytes(),
            static_cast<uint32_t>(sizeof(mojo::internal::MessageHeaderV2)) + 5u);
  EXPECT(!m.header_v2()->payload.is_null());
}

TEST(message_wrap_wire_bytes_v1_roundtrip) {
  Message m(/*name=*/1, /*flags=*/0, /*interface_id=*/0);
  m.set_request_id(9);
  m.WritePayload("abc", 3);

  v8::internal::CageBytes bytes(m.data(), m.data() + m.data_num_bytes());
  std::vector<ScopedHandle> handles;
  Message wrapped = Message::WrapWireBytes(std::move(bytes), std::move(handles));

  EXPECT(!wrapped.IsNull());
  EXPECT_EQ(wrapped.name(), 1u);
  EXPECT_EQ(wrapped.request_id(), 9u);
  EXPECT_EQ(wrapped.payload_num_bytes(), 3u);
  EXPECT(std::memcmp(wrapped.payload(), "abc", 3) == 0);
}

TEST(message_wrap_wire_bytes_v0_upgrades_to_v1) {
  // Hand-build a v0 header (24 bytes, no request_id) + payload, as a peer
  // that only speaks v0 might send.
  mojo::internal::MessageHeader v0{};
  v0.num_bytes = sizeof(v0) + 4;
  v0.version = 0;
  v0.interface_id = 0;
  v0.name = 5;
  v0.flags = 0;
  v0.trace_nonce = 0;

  v8::internal::CageBytes bytes(sizeof(v0) + 4);
  std::memcpy(bytes.data(), &v0, sizeof(v0));
  std::memcpy(bytes.data() + sizeof(v0), "WXYZ", 4);

  Message wrapped = Message::WrapWireBytes(std::move(bytes), {});
  EXPECT(!wrapped.IsNull());
  EXPECT_EQ(wrapped.version(), 2u);  // normalized to v2 in memory
  EXPECT_EQ(wrapped.name(), 5u);
  EXPECT_EQ(wrapped.request_id(), 0u);
  EXPECT_EQ(wrapped.payload_num_bytes(), 4u);
  EXPECT(std::memcmp(wrapped.payload(), "WXYZ", 4) == 0);
}

TEST(message_wrap_wire_bytes_v2_roundtrip) {
  Message m(/*name=*/4, /*flags=*/0, /*interface_id=*/0);
  m.WritePayload("v2ok", 4);
  EXPECT_EQ(m.version(), 2u);
  v8::internal::CageBytes bytes(m.data(), m.data() + m.data_num_bytes());
  Message wrapped = Message::WrapWireBytes(std::move(bytes), {});
  EXPECT(!wrapped.IsNull());
  EXPECT_EQ(wrapped.version(), 2u);
  EXPECT_EQ(wrapped.payload_num_bytes(), 4u);
  EXPECT(std::memcmp(wrapped.payload(), "v2ok", 4) == 0);
}

TEST(message_wrap_wire_bytes_too_short_is_null) {
  v8::internal::CageBytes bytes(4, 0);
  Message wrapped = Message::WrapWireBytes(std::move(bytes), {});
  EXPECT(wrapped.IsNull());
}
