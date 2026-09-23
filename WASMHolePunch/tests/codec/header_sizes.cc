#include "test.h"

#include "whp/codec.h"

TEST(HeaderSizesMatchChromium) {
  EXPECT_EQ(sizeof(whp::internal::StructHeader), 8u);
  EXPECT_EQ(sizeof(whp::internal::ArrayHeader), 8u);
  EXPECT_EQ(sizeof(whp::internal::Pointer<char>), 8u);
  EXPECT_EQ(sizeof(whp::internal::MessageHeader), 24u);
  EXPECT_EQ(sizeof(whp::internal::MessageHeaderV1), 32u);
  EXPECT_EQ(sizeof(whp::internal::MessageHeaderV2), 48u);
  EXPECT_EQ(sizeof(whp::internal::MessageHeaderV3), 56u);
  EXPECT_EQ(whp::internal::kUnionDataSize, 16u);
}

TEST(RelativePointerEncode) {
  alignas(8) unsigned char buf[32] = {};
  auto* slot = reinterpret_cast<uint64_t*>(buf);
  auto* obj = buf + 16;
  whp::internal::EncodePointer(obj, slot);
  EXPECT_EQ(*slot, 16u);
  EXPECT(whp::internal::DecodePointer(slot) == obj);
  uint64_t zero = 0;
  EXPECT(whp::internal::DecodePointer(&zero) == nullptr);
}

TEST(StringRoundTrip) {
  const char* s = "mojo";
  std::vector<char> buf(whp::internal::StringDataSize(4));
  whp::internal::EncodeString(buf.data(), s);
  auto* h = reinterpret_cast<whp::internal::ArrayHeader*>(buf.data());
  EXPECT_EQ(h->num_elements, 4u);
  std::string out;
  EXPECT(whp::internal::DecodeString(h, buf.size(), &out));
  EXPECT(out == "mojo");
}
