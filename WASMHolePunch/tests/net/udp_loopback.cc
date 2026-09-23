#include "test.h"

#include "whp/net/tcp_socket.h"
#include "whp/net/udp_socket.h"

#include <cstring>
#include <string>

TEST(UdpLoopback) {
  whp::net::UdpSocket a;
  whp::net::UdpSocket b;
  EXPECT(a.is_valid());
  EXPECT(b.is_valid());
  EXPECT_EQ(a.Bind(whp::net::Endpoint::Loopback(0)), WHP_RESULT_OK);
  EXPECT_EQ(b.Bind(whp::net::Endpoint::Loopback(0)), WHP_RESULT_OK);

  auto ea = a.LocalEndpoint();
  auto eb = b.LocalEndpoint();
  EXPECT(ea.port != 0);
  EXPECT(eb.port != 0);

  const char* msg = "holepunch";
  EXPECT(a.SendTo(msg, std::strlen(msg), eb) ==
         static_cast<int>(std::strlen(msg)));

  char buf[32] = {};
  whp::net::Endpoint src;
  int n = b.RecvFrom(buf, sizeof(buf), &src);
  EXPECT(n == static_cast<int>(std::strlen(msg)));
  EXPECT(std::string(buf, n) == msg);
  EXPECT(src.port == ea.port);
}

TEST(TcpLoopback) {
  whp::net::TcpSocket listener;
  EXPECT(listener.is_valid());
  EXPECT_EQ(listener.SetReuseAddr(true), WHP_RESULT_OK);
  EXPECT_EQ(listener.Bind(whp::net::Endpoint::Loopback(0)), WHP_RESULT_OK);
  EXPECT_EQ(listener.Listen(), WHP_RESULT_OK);
  auto lep = listener.LocalEndpoint();
  EXPECT(lep.port != 0);

  whp::net::TcpSocket client;
  EXPECT_EQ(client.Connect(lep), WHP_RESULT_OK);

  whp::net::TcpSocket server;
  whp::net::Endpoint peer;
  EXPECT_EQ(listener.Accept(&server, &peer), WHP_RESULT_OK);
  EXPECT(server.is_valid());

  const char* msg = "tcp-ok";
  EXPECT(client.Send(msg, std::strlen(msg)) ==
         static_cast<int>(std::strlen(msg)));
  char buf[32] = {};
  int n = server.Recv(buf, sizeof(buf));
  EXPECT(n == static_cast<int>(std::strlen(msg)));
  EXPECT(std::string(buf, n) == msg);
}
