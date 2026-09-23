#include "test.h"

#include "whp/punch/punch.h"

#include <cstring>
#include <string>
#include <thread>

TEST(PunchLoopback) {
  whp::net::UdpSocket a;
  whp::net::UdpSocket b;
  EXPECT_EQ(a.Bind(whp::net::Endpoint::Loopback(0)), WHP_RESULT_OK);
  EXPECT_EQ(b.Bind(whp::net::Endpoint::Loopback(0)), WHP_RESULT_OK);
  auto ca = whp::punch::Gather(a);
  auto cb = whp::punch::Gather(b);
  EXPECT(!ca.empty());
  EXPECT(!cb.empty());

  whp::punch::ConnectedPath pa;
  whp::punch::ConnectedPath pb;
  WhpResult ra = WHP_RESULT_UNKNOWN;
  WhpResult rb = WHP_RESULT_UNKNOWN;
  std::thread t([&] { ra = whp::punch::Punch(a, cb, &pa); });
  rb = whp::punch::Punch(b, ca, &pb);
  t.join();
  EXPECT_EQ(ra, WHP_RESULT_OK);
  EXPECT_EQ(rb, WHP_RESULT_OK);
  EXPECT(pa.is_valid());
  EXPECT(pb.is_valid());

  const char* msg = "punched";
  EXPECT(pa.Send(msg, std::strlen(msg)) == static_cast<int>(std::strlen(msg)));
  char buf[32] = {};
  int n = pb.Recv(buf, sizeof(buf));
  EXPECT(n == static_cast<int>(std::strlen(msg)));
  if (n > 0) {
    EXPECT(std::string(buf, static_cast<size_t>(n)) == msg);
  }
}
