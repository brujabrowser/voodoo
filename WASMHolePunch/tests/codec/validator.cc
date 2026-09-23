#include "test.h"

#include "whp/message.h"
#include "whp/message_header_validator.h"

#include <cstring>
#include <vector>

static whp::Message MakeRaw(std::vector<uint8_t> bytes) {
  return whp::Message(bytes, {});
}

TEST(ValidatorAcceptsV3) {
  whp::Message msg(42, 0);
  whp::MessageHeaderValidator v;
  EXPECT(v.Accept(&msg));
  EXPECT_EQ(msg.version(), 3u);
  EXPECT_EQ(msg.name(), 42u);
  EXPECT_EQ(sizeof(whp::internal::MessageHeaderV3), msg.header()->num_bytes);
}

TEST(ValidatorRejectsBothResponseFlags) {
  whp::Message msg(1, whp::Message::kFlagExpectsResponse |
                          whp::Message::kFlagIsResponse);
  whp::MessageHeaderValidator v;
  EXPECT(!v.Accept(&msg));
}

TEST(ValidatorRejectsV0RequestIdFlags) {
  std::vector<uint8_t> bytes(sizeof(whp::internal::MessageHeader), 0);
  auto* h = reinterpret_cast<whp::internal::MessageHeader*>(bytes.data());
  h->num_bytes = sizeof(whp::internal::MessageHeader);
  h->version = 0;
  h->name = 1;
  h->flags = whp::Message::kFlagExpectsResponse;
  whp::Message msg = MakeRaw(bytes);
  whp::MessageHeaderValidator v;
  EXPECT(!v.Accept(&msg));
}

TEST(ValidatorRejectsUndersizedHeader) {
  std::vector<uint8_t> bytes(4, 0);
  whp::Message msg = MakeRaw(bytes);
  whp::MessageHeaderValidator v;
  EXPECT(!v.Accept(&msg));
}

TEST(ValidatorRejectsWrongV3Size) {
  std::vector<uint8_t> bytes(sizeof(whp::internal::MessageHeaderV3), 0);
  auto* h = reinterpret_cast<whp::internal::MessageHeaderV3*>(bytes.data());
  h->num_bytes = 40;  // not 56
  h->version = 3;
  h->payload.Set(h + 1);
  whp::Message msg = MakeRaw(bytes);
  whp::MessageHeaderValidator v;
  EXPECT(!v.Accept(&msg));
}
