#include "test.h"

#include "whp/control.h"
#include "whp/platform/invitation.h"
#include "whp/punch/punch.h"
#include "whp/remote.h"

#include <cstring>
#include <string>
#include <thread>

namespace {

bool PunchPair(whp::punch::ConnectedPath* a, whp::punch::ConnectedPath* b) {
  whp::net::UdpSocket sa;
  whp::net::UdpSocket sb;
  if (sa.Bind(whp::net::Endpoint::Loopback(0)) != WHP_RESULT_OK) {
    return false;
  }
  if (sb.Bind(whp::net::Endpoint::Loopback(0)) != WHP_RESULT_OK) {
    return false;
  }
  auto ca = whp::punch::Gather(sa);
  auto cb = whp::punch::Gather(sb);
  WhpResult ra = WHP_RESULT_UNKNOWN;
  WhpResult rb = WHP_RESULT_UNKNOWN;
  std::thread t([&] { ra = whp::punch::Punch(sa, cb, a); });
  rb = whp::punch::Punch(sb, ca, b);
  t.join();
  return ra == WHP_RESULT_OK && rb == WHP_RESULT_OK && a->is_valid() &&
         b->is_valid();
}

bool PumpUntilRecv(whp::platform::Invitation* a,
                   whp::platform::Invitation* b,
                   whp::Message* out) {
  for (int i = 0; i < 80; ++i) {
    a->Pump(5);
    b->Pump(5);
    if (b->Recv(out) == WHP_RESULT_OK && !out->IsNull()) {
      return true;
    }
  }
  return false;
}

}  // namespace

TEST(InviteOverLoopback) {
  whp::punch::ConnectedPath pa;
  whp::punch::ConnectedPath pb;
  EXPECT(PunchPair(&pa, &pb));

  whp::platform::Invitation offerer;
  whp::platform::Invitation answerer;
  WhpResult ra = WHP_RESULT_UNKNOWN;
  WhpResult rb = WHP_RESULT_UNKNOWN;
  std::thread t([&] {
    ra = whp::platform::InviteOver(std::move(pa), &offerer, true);
  });
  rb = whp::platform::InviteOver(std::move(pb), &answerer, false);
  t.join();
  EXPECT_EQ(ra, WHP_RESULT_OK);
  EXPECT_EQ(rb, WHP_RESULT_OK);
  EXPECT(offerer.is_attached());
  EXPECT(answerer.is_attached());

  const char* body = "iwa";
  EXPECT_EQ(offerer.Fire(894355474u, body, 3), WHP_RESULT_OK);
  whp::Message got;
  EXPECT(PumpUntilRecv(&offerer, &answerer, &got));
  EXPECT_EQ(got.name(), 894355474u);
  EXPECT_EQ(got.payload_num_bytes(), 3u);
  EXPECT(std::string(reinterpret_cast<const char*>(got.payload()), 3) == "iwa");
}

TEST(RemoteFiresSRPC) {
  EXPECT(whp::IsSRPCToken(whp::kSECRPC));
  EXPECT(!whp::IsMojoOrdinal(whp::kSECRPC));
  EXPECT(whp::IsMojoOrdinal(894355474u));
  whp::punch::ConnectedPath pa;
  whp::punch::ConnectedPath pb;
  EXPECT(PunchPair(&pa, &pb));
  whp::platform::Invitation offerer;
  whp::platform::Invitation answerer;
  WhpResult ra = WHP_RESULT_UNKNOWN;
  WhpResult rb = WHP_RESULT_UNKNOWN;
  std::thread t([&] {
    ra = whp::platform::InviteOver(std::move(pa), &offerer, true);
  });
  rb = whp::platform::InviteOver(std::move(pb), &answerer, false);
  t.join();
  EXPECT_EQ(ra, WHP_RESULT_OK);
  EXPECT_EQ(rb, WHP_RESULT_OK);
  whp::Remote remote(&offerer);
  EXPECT_EQ(remote.FireToken(whp::kSECRPC), WHP_RESULT_OK);
}

TEST(ControlRunAutoReply) {
  whp::punch::ConnectedPath pa;
  whp::punch::ConnectedPath pb;
  EXPECT(PunchPair(&pa, &pb));

  whp::platform::Invitation offerer;
  whp::platform::Invitation answerer;
  std::thread t([&] {
    (void)whp::platform::InviteOver(std::move(pa), &offerer, true);
  });
  (void)whp::platform::InviteOver(std::move(pb), &answerer, false);
  t.join();
  EXPECT(offerer.is_attached());
  EXPECT(answerer.is_attached());

  EXPECT_EQ(offerer.Fire(whp::control::kRunMessageId, nullptr, 0,
                         whp::Message::kFlagExpectsResponse),
            WHP_RESULT_OK);
  // Run is swallowed on the answerer; the offerer should see the response.
  bool saw = false;
  for (int i = 0; i < 80; ++i) {
    answerer.Pump(5);
    offerer.Pump(5);
    whp::Message m;
    if (offerer.Recv(&m) == WHP_RESULT_OK && !m.IsNull()) {
      EXPECT_EQ(m.name(), whp::control::kRunMessageId);
      EXPECT(m.has_flag(whp::Message::kFlagIsResponse));
      saw = true;
      break;
    }
  }
  EXPECT(saw);
}
