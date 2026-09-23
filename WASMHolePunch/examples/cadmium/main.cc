// HolePunch2Cadmium — a separate class. Cadmium is unchanged.
//
// Maps Cadmium's existing `/api/rbi/ipc` invoke queue onto a WHP Invitation
// pipe (`header.name` = magic-key ordinal). EV SRPC magics are skipped.
//
//   whp_cadmium --bind 127.0.0.1:0 --offerer --cadmium http://127.0.0.1:8879 --sid <id>
//   whp_cadmium --bind 127.0.0.1:0 --peer 127.0.0.1:<port> --cadmium http://127.0.0.1:8879 --sid <id>

#include "whp/platform/invitation.h"
#include "whp/punch/punch.h"
#include "whp/remote.h"
#include "whp/net/tcp_socket.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace {

struct Opts {
  std::string bind = "127.0.0.1:0";
  std::string peer;
  std::string cadmium;
  std::string sid;
  bool offerer = false;
};

whp::net::Endpoint ParseHostPort(const std::string& s, uint16_t fallback = 0) {
  whp::net::Endpoint e = whp::net::Endpoint::Loopback(fallback);
  auto colon = s.rfind(':');
  if (colon == std::string::npos) {
    if (!s.empty()) {
      e.host = s;
    }
    return e;
  }
  e.host = s.substr(0, colon);
  e.port = static_cast<uint16_t>(std::atoi(s.c_str() + colon + 1));
  if (e.host.empty()) {
    e.host = "127.0.0.1";
  }
  return e;
}

bool ParseUrl(const std::string& url, whp::net::Endpoint* ep, std::string* path) {
  const char* p = url.c_str();
  if (std::strncmp(p, "http://", 7) == 0) {
    p += 7;
  }
  std::string rest = p;
  auto slash = rest.find('/');
  std::string hostport = slash == std::string::npos ? rest : rest.substr(0, slash);
  *path = slash == std::string::npos ? "/" : rest.substr(slash);
  *ep = ParseHostPort(hostport, 80);
  if (ep->port == 0) {
    ep->port = 80;
  }
  return !ep->host.empty();
}

std::string Http(const std::string& method,
                 const std::string& url,
                 const std::string& body) {
  whp::net::Endpoint ep;
  std::string path;
  if (!ParseUrl(url, &ep, &path)) {
    return {};
  }
  whp::net::TcpSocket sock;
  if (sock.Connect(ep) != WHP_RESULT_OK) {
    std::fprintf(stderr, "cadmium connect %s:%u failed\n", ep.host.c_str(),
                 ep.port);
    return {};
  }
  std::string req;
  req += method;
  req += " ";
  req += path;
  req += " HTTP/1.0\r\nHost: ";
  req += ep.host;
  req += "\r\nConnection: close\r\n";
  if (method == "POST") {
    req += "Content-Type: application/json\r\nContent-Length: ";
    req += std::to_string(body.size());
    req += "\r\n\r\n";
    req += body;
  } else {
    req += "\r\n";
  }
  if (sock.Send(req.data(), req.size()) < 0) {
    return {};
  }
  std::string resp;
  char buf[4096];
  for (;;) {
    int n = sock.Recv(buf, sizeof(buf));
    if (n <= 0) {
      break;
    }
    resp.append(buf, static_cast<size_t>(n));
  }
  auto hdr = resp.find("\r\n\r\n");
  if (hdr == std::string::npos) {
    return resp;
  }
  return resp.substr(hdr + 4);
}

uint64_t ParseU64(const std::string& s, size_t pos) {
  while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t' || s[pos] == '\"')) {
    ++pos;
  }
  return std::strtoull(s.c_str() + pos, nullptr, 10);
}

std::string JsonStringField(const std::string& obj, const char* key) {
  std::string pat = std::string("\"") + key + "\"";
  auto p = obj.find(pat);
  if (p == std::string::npos) {
    return {};
  }
  p = obj.find(':', p);
  if (p == std::string::npos) {
    return {};
  }
  p = obj.find('\"', p);
  if (p == std::string::npos) {
    return {};
  }
  auto e = obj.find('\"', p + 1);
  if (e == std::string::npos) {
    return {};
  }
  return obj.substr(p + 1, e - p - 1);
}

struct Cmd {
  uint64_t token = 0;
  std::string name;
  bool srpc = false;
};

std::vector<Cmd> ExtractCommands(const std::string& json) {
  std::vector<Cmd> out;
  size_t pos = 0;
  while (pos < json.size()) {
    auto ord = json.find("\"ordinal\"", pos);
    auto mag = json.find("\"magic\"", pos);
    size_t hit = std::string::npos;
    bool magic = false;
    if (ord == std::string::npos && mag == std::string::npos) {
      break;
    }
    if (ord != std::string::npos && (mag == std::string::npos || ord < mag)) {
      hit = ord;
    } else {
      hit = mag;
      magic = true;
    }
    auto colon = json.find(':', hit);
    if (colon == std::string::npos) {
      break;
    }
    Cmd c;
    c.token = ParseU64(json, colon + 1);
    c.srpc = magic || whp::IsSRPCToken(c.token);
    size_t start = json.rfind('{', hit);
    size_t end = json.find('}', hit);
    if (start != std::string::npos && end != std::string::npos && end > start) {
      c.name = JsonStringField(json.substr(start, end - start + 1), "name");
    }
    out.push_back(c);
    pos = colon + 1;
  }
  return out;
}

// AnnounceCandidate and DiscoverPeerCandidate fix a real deadlock in the
// "launch both peers with no human in the loop" flow (project_lovelace's
// nettest.WHP.Start): whp::punch::Punch requires a non-empty remote
// candidate on both sides before either can punch, so this binary used
// to just print its own candidate and exit(2) whenever launched without
// --peer -- fine for a human copying that candidate into a second
// terminal by hand, fatal for an orchestrator that launches an offerer
// specifically to learn its ephemeral port before the answerer (its
// future peer) even exists yet. Cadmium is already the rendezvous server
// both sides are given via --cadmium; this uses it as real signaling
// instead, mirroring bridge_ipc.go's own ordinal-queue polling pattern
// one HTTP round trip at a time rather than requiring a human.
void AnnounceCandidate(const Opts& opt, const std::string& role,
                       const whp::net::Endpoint& local) {
  std::string body = "{\"sid\":\"" + opt.sid + "\",\"role\":\"" + role +
                     "\",\"host\":\"" + local.host + "\",\"port\":" +
                     std::to_string(local.port) + "}";
  (void)Http("POST", opt.cadmium + "/api/rbi/whp/candidate", body);
}

// Polls Cadmium for other_role's announced candidate under opt.sid, up to
// timeout_ms. Returns "host:port", or "" if it never appears in time.
std::string DiscoverPeerCandidate(const Opts& opt, const std::string& other_role,
                                  int timeout_ms) {
  std::string url = opt.cadmium + "/api/rbi/whp/candidate?sid=" + opt.sid +
                    "&role=" + other_role;
  auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  while (std::chrono::steady_clock::now() < deadline) {
    std::string body = Http("GET", url, {});
    if (body.find("\"ok\":true") != std::string::npos) {
      std::string host = JsonStringField(body, "host");
      auto p = body.find("\"port\"");
      if (!host.empty() && p != std::string::npos) {
        uint64_t port = ParseU64(body, body.find(':', p) + 1);
        if (port != 0) {
          return host + ":" + std::to_string(port);
        }
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
  }
  return "";
}

void Report(const Opts& opt, const char* kind, uint32_t ordinal, const std::string& name,
            const char* note) {
  if (opt.cadmium.empty()) {
    return;
  }
  std::string body = "{";
  body += "\"kind\":\"";
  body += kind;
  body += "\",\"via\":\"holepunch\",\"ordinal\":";
  body += std::to_string(ordinal);
  body += ",\"name\":\"";
  body += name;
  body += "\",\"sid\":\"";
  body += opt.sid;
  body += "\",\"note\":\"";
  body += note;
  body += "\"}";
  (void)Http("POST", opt.cadmium + "/api/rbi/ipc", body);
}

void BridgeLoop(whp::platform::Invitation* inv, const Opts& opt) {
  whp::Remote remote(inv);
  std::string hook = opt.cadmium + "/api/rbi/ipc?hook=1";
  if (!opt.sid.empty()) {
    hook += "&sid=" + opt.sid;
  }
  for (;;) {
    inv->Pump(50);
    whp::Message inbound;
    if (inv->Recv(&inbound) == WHP_RESULT_OK && !inbound.IsNull()) {
      Report(opt, "mojo_recv", inbound.name(), "", "whp primordial");
      std::printf("recv ordinal=%u bytes=%u\n", inbound.name(),
                  inbound.payload_num_bytes());
    }
    if (opt.cadmium.empty()) {
      continue;
    }
    std::string body = Http("GET", hook, {});
    if (body.empty()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
      continue;
    }
    for (const auto& c : ExtractCommands(body)) {
      if (c.token == 0) {
        continue;
      }
      WhpResult r = remote.FireToken(c.token);
      std::printf("fire token=%llu name=%s srpc=%d -> %u\n",
                  static_cast<unsigned long long>(c.token), c.name.c_str(),
                  c.srpc ? 1 : 0, r);
      Report(opt, "mojo_send", static_cast<uint32_t>(c.token), c.name,
             r == WHP_RESULT_OK ? "whp fire" : "whp fire fail");
    }
  }
}

void Usage() {
  std::fprintf(
      stderr,
      "whp_cadmium — HolePunch2Cadmium (Cadmium is unchanged)\n"
      "  --bind host:port     local UDP (default 127.0.0.1:0)\n"
      "  --peer host:port     remote candidate (required unless printing bind)\n"
      "  --offerer            this side sends WHP2 invite\n"
      "  --cadmium URL        Cadmium origin, e.g. http://127.0.0.1:8879\n"
      "  --sid ID             occupancy sid for /api/rbi/ipc?hook=1\n");
}

}  // namespace

int main(int argc, char** argv) {
  Opts opt;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto take = [&](std::string* dst) {
      if (i + 1 < argc) {
        *dst = argv[++i];
      }
    };
    if (a == "--bind") {
      take(&opt.bind);
    } else if (a == "--peer") {
      take(&opt.peer);
    } else if (a == "--cadmium") {
      take(&opt.cadmium);
    } else if (a == "--sid") {
      take(&opt.sid);
    } else if (a == "--offerer") {
      opt.offerer = true;
    } else if (a == "-h" || a == "--help") {
      Usage();
      return 0;
    }
  }

  whp::net::UdpSocket sock;
  auto bind_ep = ParseHostPort(opt.bind, 0);
  if (sock.Bind(bind_ep) != WHP_RESULT_OK) {
    std::fprintf(stderr, "bind failed\n");
    return 1;
  }
  auto local = sock.LocalEndpoint();
  std::printf("whp candidate %s:%u offerer=%d\n", local.host.c_str(), local.port,
              opt.offerer ? 1 : 0);
  std::fflush(stdout);

  if (opt.peer.empty()) {
    if (!opt.cadmium.empty() && !opt.sid.empty()) {
      std::string role = opt.offerer ? "offerer" : "answerer";
      std::string other_role = opt.offerer ? "answerer" : "offerer";
      AnnounceCandidate(opt, role, local);
      std::fprintf(stderr, "announced candidate to cadmium, waiting for %s...\n",
                   other_role.c_str());
      opt.peer = DiscoverPeerCandidate(opt, other_role, 10000);
      if (opt.peer.empty()) {
        std::fprintf(stderr, "no %s candidate appeared via cadmium within 10s\n",
                     other_role.c_str());
        return 2;
      }
      std::printf("discovered peer via cadmium: %s\n", opt.peer.c_str());
    } else {
      std::fprintf(stderr, "waiting --peer host:port (printed candidate above)\n");
      Usage();
      return 2;
    }
  }

  auto remote_ep = ParseHostPort(opt.peer, 0);
  std::vector<whp::punch::Candidate> remotes;
  remotes.push_back({remote_ep, whp::punch::CandidateType::kHost});
  whp::punch::ConnectedPath path;
  if (whp::punch::Punch(sock, remotes, &path) != WHP_RESULT_OK) {
    std::fprintf(stderr, "punch failed\n");
    return 3;
  }
  std::printf("punched %s:%u\n", path.peer().host.c_str(), path.peer().port);

  whp::platform::Invitation inv;
  if (whp::platform::InviteOver(std::move(path), &inv, opt.offerer) !=
      WHP_RESULT_OK) {
    std::fprintf(stderr, "invite failed\n");
    return 4;
  }
  std::printf("mojo pipe open (WHP2 invitation)\n");
  if (!opt.cadmium.empty()) {
    Report(opt, "mojo_pipe", 0, "whp.invitation", "HolePunch2Cadmium attached");
  }
  BridgeLoop(&inv, opt);
  return 0;
}
