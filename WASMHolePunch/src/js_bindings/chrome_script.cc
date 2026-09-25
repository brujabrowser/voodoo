// Line interpreter for the Chrome guest and ChromeErrStates deputies.
// One command per line, Lovelace-shell shape: the script is the test.
#include "whp/js_bindings/chrome_guest.h"

#include "ces/loader.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

extern "C" {
__attribute__((import_module("env"), import_name("browser")))
long long browser_host(const char* op, int op_len, const char* arg, int arg_len);

__attribute__((export_name("alloc")))
void* alloc(unsigned n) {
  return std::malloc(n ? n : 1);
}
}

namespace {

whp_js_bindings::ChromeFrame FrameOf(const std::string& name) {
  if (name == "page") return whp_js_bindings::ChromeFrame::kPage;
  if (name == "isolated") return whp_js_bindings::ChromeFrame::kIsolated;
  if (name == "extension") return whp_js_bindings::ChromeFrame::kExtension;
  if (name == "mojojs") return whp_js_bindings::ChromeFrame::kMojoJs;
  if (name == "internals") return whp_js_bindings::ChromeFrame::kInternals;
  if (name == "terraformed") return whp_js_bindings::ChromeFrame::kTerraformed;
  return whp_js_bindings::ChromeFrame::kPage;
}

struct Monitor {
  std::string name;
  std::string rows;
  bool on = false;
};

Monitor g_mon[8];
int g_mon_n = 0;

bool Digits(const std::string& s) {
  if (s.empty()) return false;
  for (char c : s) {
    if (c < '0' || c > '9') return false;
  }
  return true;
}

Monitor* FindMon(const std::string& name) {
  for (int i = 0; i < g_mon_n; ++i) {
    if (g_mon[i].name == name) return &g_mon[i];
  }
  return nullptr;
}

void Remember(const std::string& name, const std::string& ordinal, const std::string& extra = "") {
  Monitor* m = FindMon(name);
  if (!m || !m->on || !Digits(ordinal)) return;
  std::string row = ordinal + "\trun\ttrue";
  if (!extra.empty()) row += "\t" + extra;
  if (!m->rows.empty()) m->rows += "\n";
  m->rows += row;
  std::cout << "MON " << name << " " << row << "\n";
}

void WatchDevice(const std::string& value) {
  Remember("device", value);
}

std::string CallOrdinal(whp_js_bindings::ChromeGuest& guest,
                        whp_js_bindings::ChromeFrame frame,
                        const char* iface, const char* method) {
  std::string src = std::string("String(globalThis['") + iface + "']['" + method + "']())";
  return guest.RunString(frame, src.c_str());
}

std::string JsonField(const std::string& body, const char* key) {
  std::string pat = std::string("\"") + key + "\":\"";
  auto i = body.find(pat);
  if (i == std::string::npos) return "";
  i += pat.size();
  std::string out;
  for (; i < body.size(); ++i) {
    char c = body[i];
    if (c == '\\' && i + 1 < body.size()) {
      char n = body[++i];
      if (n == 'n') out.push_back('\n');
      else if (n == 't') out.push_back('\t');
      else out.push_back(n);
    } else if (c == '"') {
      break;
    } else {
      out.push_back(c);
    }
  }
  return out;
}

std::string BrowserHost(const std::string& op, const std::string& arg) {
  unsigned long long packed = static_cast<unsigned long long>(
      browser_host(op.data(), static_cast<int>(op.size()), arg.data(), static_cast<int>(arg.size())));
  if (packed == 0) return "no-host";
  auto len = static_cast<uint32_t>(packed);
  auto ptr = static_cast<uint32_t>(packed >> 32);
  std::string body(reinterpret_cast<const char*>(static_cast<uintptr_t>(ptr)), len);
  if (body.find("\"ok\":false") != std::string::npos) return JsonField(body, "error");
  return JsonField(body, "result");
}

std::string Trim(std::string s) {
  while (!s.empty() && (s.back() == '\r' || s.back() == ' ' || s.back() == '\t')) {
    s.pop_back();
  }
  size_t i = 0;
  while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) {
    ++i;
  }
  return s.substr(i);
}

int RunLines(std::istream& in, const std::string& path) {
  whp_js_bindings::ChromeGuest guest;
  whp_js_bindings::ChromeFrame frame = whp_js_bindings::ChromeFrame::kPage;
  std::string last;
  int failures = 0;
  int checks = 0;
  int line_no = 0;
  std::string line;
  while (std::getline(in, line)) {
    ++line_no;
    line = Trim(line);
    if (line.empty() || line[0] == '#') continue;
    std::string cmd;
    std::string arg;
    auto sp = line.find(' ');
    if (sp == std::string::npos) {
      cmd = line;
    } else {
      cmd = line.substr(0, sp);
      arg = Trim(line.substr(sp + 1));
    }
    if (cmd == "world") {
      frame = FrameOf(arg);
      last = arg;
    } else if (cmd == "eval") {
      last = guest.RunString(frame, arg.c_str());
    } else if (cmd == "num") {
      double n = guest.RunNumber(frame, arg.c_str());
      if (n == std::floor(n) && n < 1e15 && n > -1e15) {
        last = std::to_string(static_cast<long long>(n));
      } else {
        last = std::to_string(n);
      }
    } else if (cmd == "run") {
      guest.Run(frame, arg.c_str());
    } else if (cmd == "checkpoint") {
      guest.Checkpoint();
    } else if (cmd == "deputy") {
      ces::Shot shot = ces::Loader::Resolve(arg);
      std::ostringstream out;
      out << (shot.ok ? "ok" : "fail") << " deputy=" << shot.deputy
          << " access=" << ces::AccessName(shot.access)
          << " webdata=" << (shot.chromewebdata ? "1" : "0")
          << " committed=" << shot.committed;
      last = out.str();
    } else if (cmd == "print") {
      if (arg.empty()) {
        std::cout << last << "\n";
      } else {
        std::cout << arg << "\n";
      }
    } else if (cmd == "loadtimes") {
      last = guest.RunString(frame, "chrome.loadTimes()");
    } else if (cmd == "csi") {
      last = guest.RunString(frame, "chrome.csi()");
    } else if (cmd == "app") {
      last = guest.RunString(frame, "chrome.app.getDetails()");
    } else if (cmd == "id") {
      last = guest.RunString(frame, "chrome.runtime.id");
    } else if (cmd == "send") {
      last = guest.RunString(frame, ("chrome.runtime.sendMessage('" + arg + "')").c_str());
    } else if (cmd == "platform") {
      last = guest.RunString(frame, "chrome.runtime.getPlatformInfo()");
    } else if (cmd == "bind") {
      last = guest.RunString(frame, ("Mojo.bindInterface('" + arg + "', pipe.handle0)").c_str());
    } else if (cmd == "execute") {
      last = guest.RunString(frame, ("JavaScriptExecuteRequest('" + arg + "')").c_str());
    } else if (cmd == "isolated") {
      last = guest.RunString(frame, ("JavaScriptExecuteRequestInIsolatedWorld('" + arg + "')").c_str());
    } else if (cmd == "tests") {
      last = guest.RunString(frame, ("JavaScriptExecuteRequestForTests('" + arg + "')").c_str());
    } else if (cmd == "count") {
      last = guest.RunString(frame, "String(MojoVM.count)");
    } else if (cmd == "lookup") {
      last = guest.RunString(frame, ("JSON.stringify(MojoVM.lookup('" + arg + "'))").c_str());
    } else if (cmd == "call") {
      last = guest.RunString(frame, ("MojoVM.call('" + arg + "')").c_str());
    } else if (cmd == "targets") {
      last = guest.RunString(frame, "DevTools.listTargets()");
    } else if (cmd == "attach") {
      last = guest.RunString(frame, ("DevTools.attach('" + arg + "')").c_str());
    } else if (cmd == "dispatch") {
      last = guest.RunString(frame, ("DevTools.dispatch('" + arg + "')").c_str());
    } else if (cmd == "debug") {
      last = guest.RunString(frame, ("DevTools.evaluate('" + arg + "')").c_str());
    } else if (cmd == "trust") {
      last = guest.RunString(frame, "DevTools.trusted()");
    } else if (cmd == "paused") {
      last = guest.RunString(frame, "DevTools.paused()");
    } else if (cmd == "resume") {
      last = guest.RunString(frame, "DevTools.resume()");
    } else if (cmd == "monitor") {
      Monitor* m = FindMon(arg);
      if (!m && g_mon_n < 8) {
        g_mon[g_mon_n].name = arg;
        m = &g_mon[g_mon_n++];
      }
      if (!m) {
        last = "missing";
      } else {
        m->on = true;
        std::string ordinal;
        if (arg == "network") {
          guest.RunString(frame, "String(globalThis['network.mojom.NetworkContext']['createNetLogExporter']())");
          ordinal = guest.RunString(frame, "String(globalThis['network.mojom.NetLogExporter']['start'](null, null, 2, 0, 0))");
          if (Digits(ordinal)) Remember(arg, ordinal, "kEverything");
          ordinal.clear();
        } else if (arg == "tracing") ordinal = CallOrdinal(guest, frame, "tracing.mojom.ConsumerHost", "enableTracing");
        else if (arg == "webrtc") ordinal = CallOrdinal(guest, frame, "chrome.mojom.WebRtcLoggingAgent", "start");
        else if (arg == "audio") ordinal = CallOrdinal(guest, frame, "media.mojom.AudioLog", "onStarted");
        else ordinal = arg;
        if (Digits(ordinal)) Remember(arg, ordinal);
        last = m->on ? arg : "missing";
      }
    } else if (cmd == "entry") {
      std::string name = arg;
      std::string rest;
      auto sp2 = arg.find(' ');
      if (sp2 != std::string::npos) {
        name = arg.substr(0, sp2);
        rest = Trim(arg.substr(sp2 + 1));
      }
      std::string ordinal;
      if (name == "network") ordinal = CallOrdinal(guest, frame, "network.mojom.NetLogProxySink", "addEntry");
      else if (name == "tracing") ordinal = CallOrdinal(guest, frame, "tracing.mojom.TracingSessionHost", "readBuffers");
      else if (name == "webrtc") ordinal = CallOrdinal(guest, frame, "chrome.mojom.WebRtcLoggingClient", "onAddMessages");
      else if (name == "audio") ordinal = CallOrdinal(guest, frame, "media.mojom.AudioLog", "onLogMessage");
      else ordinal = rest.empty() ? last : rest;
      Remember(name, ordinal);
      last = ordinal;
    } else if (cmd == "unmonitor") {
      Monitor* m = FindMon(arg);
      std::string ordinal;
      if (arg == "network") ordinal = CallOrdinal(guest, frame, "network.mojom.NetLogExporter", "stop");
      else if (arg == "tracing") ordinal = CallOrdinal(guest, frame, "tracing.mojom.TracingSessionHost", "disableTracing");
      else if (arg == "webrtc") ordinal = CallOrdinal(guest, frame, "chrome.mojom.WebRtcLoggingAgent", "stop");
      else if (arg == "audio") ordinal = CallOrdinal(guest, frame, "media.mojom.AudioLog", "onStopped");
      if (m && Digits(ordinal)) Remember(arg, ordinal);
      if (m) m->on = false;
      last = arg;
    } else if (cmd == "log") {
      Monitor* m = FindMon(arg);
      last = m ? m->rows : "";
      std::cout << "address\tstate\texecuting\n";
      if (!last.empty()) std::cout << last << "\n";
    } else if (cmd == "browser") {
      auto sp2 = arg.find(' ');
      std::string op = sp2 == std::string::npos ? arg : arg.substr(0, sp2);
      std::string rest = sp2 == std::string::npos ? "" : Trim(arg.substr(sp2 + 1));
      last = BrowserHost(op, rest);
    } else if (cmd == "expect") {
      ++checks;
      if (last != arg) {
        std::cerr << path << ":" << line_no << ": expected [" << arg << "] got [" << last << "]\n";
        ++failures;
      }
    } else if (cmd == "contains") {
      ++checks;
      if (last.find(arg) == std::string::npos) {
        std::cerr << path << ":" << line_no << ": missing [" << arg << "] in [" << last << "]\n";
        ++failures;
      }
    } else {
      std::cerr << path << ":" << line_no << ": unknown command " << cmd << "\n";
      ++failures;
    }
    WatchDevice(last);
  }
  std::cout << "checks " << checks << "\n";
  if (failures != 0) {
    std::cerr << "harness: " << failures << " failure(s)\n";
    return 1;
  }
  std::cout << "harness: ok\n";
  return 0;
}

int RunFile(const std::string& path) {
  std::ifstream in(path);
  if (!in) {
    std::istringstream text(path);
    return RunLines(text, "<script>");
  }
  return RunLines(in, path);
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "usage: chrome-script <file-or-script>\n";
    return 2;
  }
  return RunFile(argv[1]);
}
