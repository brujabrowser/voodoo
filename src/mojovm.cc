#include "mojovm.h"

#include "module_loader.h"
#include "type_intern.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace mojovm {
namespace {

namespace fs = std::filesystem;

std::optional<std::string> ReadFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in.is_open()) return std::nullopt;
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

std::string Err(const std::string& msg) { return "error: " + msg; }

std::string Sep() { return std::string(1, static_cast<char>(0x1f)); }

// FNV-1a 32-bit (go++ hash/fnv New32a), masked to 0x7fffffff.
// That is the uint32 width project_lovelace ExtractMojo stores
// (1 .. kMaxMojoOrdinal). The hashed bytes are the full FQN.
// This is the voodoo ordinal. Chrome does not use it.
uint32_t FnvOrdinal(const std::string& s) {
  uint32_t h = 2166136261u;
  for (unsigned char c : s) {
    h ^= c;
    h *= 16777619u;
  }
  h &= 0x7fffffffu;
  if (h == 0) h = 1;
  return h;
}

// Official desktop Chrome scrambles method ids:
//   sha256(chrome/VERSION ‖ interface.mojom_name ‖ decimal_index)[:4]
//   little-endian, then & 0x7fffffff.
// index is 1-based among methods that have no explicit @N.
// Salt is //chrome/VERSION on main (156.0.8070.0).
const char kChromeVersionSalt[] =
    "MAJOR=156\nMINOR=0\nBUILD=8070\nPATCH=0\n";

uint32_t Rotr(uint32_t x, uint32_t n) { return (x >> n) | (x << (32 - n)); }

void Sha256Block(uint32_t state[8], const uint8_t block[64]) {
  static const uint32_t k[64] = {
      0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
      0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
      0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
      0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
      0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
      0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
      0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
      0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
      0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
      0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
      0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
  uint32_t w[64];
  for (int i = 0; i < 16; ++i) {
    w[i] = (uint32_t)block[i * 4] << 24 | (uint32_t)block[i * 4 + 1] << 16 |
           (uint32_t)block[i * 4 + 2] << 8 | (uint32_t)block[i * 4 + 3];
  }
  for (int i = 16; i < 64; ++i) {
    const uint32_t s0 = Rotr(w[i - 15], 7) ^ Rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
    const uint32_t s1 = Rotr(w[i - 2], 17) ^ Rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }
  uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
  uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
  for (int i = 0; i < 64; ++i) {
    const uint32_t S1 = Rotr(e, 6) ^ Rotr(e, 11) ^ Rotr(e, 25);
    const uint32_t ch = (e & f) ^ ((~e) & g);
    const uint32_t t1 = h + S1 + ch + k[i] + w[i];
    const uint32_t S0 = Rotr(a, 2) ^ Rotr(a, 13) ^ Rotr(a, 22);
    const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
    const uint32_t t2 = S0 + maj;
    h = g;
    g = f;
    f = e;
    e = d + t1;
    d = c;
    c = b;
    b = a;
    a = t1 + t2;
  }
  state[0] += a;
  state[1] += b;
  state[2] += c;
  state[3] += d;
  state[4] += e;
  state[5] += f;
  state[6] += g;
  state[7] += h;
}

void Sha256(const uint8_t* data, size_t len, uint8_t out[32]) {
  uint32_t state[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                       0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  uint8_t block[64];
  size_t off = 0;
  while (len - off >= 64) {
    Sha256Block(state, data + off);
    off += 64;
  }
  const size_t rem = len - off;
  std::memcpy(block, data + off, rem);
  block[rem] = 0x80;
  if (rem + 1 <= 56) {
    std::memset(block + rem + 1, 0, 56 - (rem + 1));
  } else {
    std::memset(block + rem + 1, 0, 64 - (rem + 1));
    Sha256Block(state, block);
    std::memset(block, 0, 56);
  }
  const uint64_t bits = (uint64_t)len * 8;
  for (int i = 0; i < 8; ++i) {
    block[63 - i] = (uint8_t)(bits >> (8 * i));
  }
  Sha256Block(state, block);
  for (int i = 0; i < 8; ++i) {
    out[i * 4] = (uint8_t)(state[i] >> 24);
    out[i * 4 + 1] = (uint8_t)(state[i] >> 16);
    out[i * 4 + 2] = (uint8_t)(state[i] >> 8);
    out[i * 4 + 3] = (uint8_t)state[i];
  }
}

// Chrome's wire ordinal for an official desktop build.
uint32_t ChromeOrdinal(const voodoom::Interface& def, const voodoom::Method& target) {
  if (target.has_explicit_ordinal) return target.ordinal & 0x7fffffffu;
  int index = 0;
  for (const voodoom::Method& m : def.methods) {
    if (m.has_explicit_ordinal) continue;
    ++index;
    if (m.name != target.name) continue;
    const std::string tail = def.name + std::to_string(index);
    std::string msg(kChromeVersionSalt);
    msg += tail;
    uint8_t dig[32];
    Sha256(reinterpret_cast<const uint8_t*>(msg.data()), msg.size(), dig);
    const uint32_t le = (uint32_t)dig[0] | (uint32_t)dig[1] << 8 |
                        (uint32_t)dig[2] << 16 | (uint32_t)dig[3] << 24;
    uint32_t ord = le & 0x7fffffffu;
    if (ord == 0) ord = 1;
    return ord;
  }
  return 0;
}

// api is "<module>.<Interface>.<Method>". Module names contain dots
// ("extensions.mojom"), so the split is from the right.
bool SplitApi(const std::string& api, std::string* module, std::string* iface,
              std::string* method) {
  const size_t method_dot = api.rfind('.');
  if (method_dot == std::string::npos || method_dot == 0) return false;
  const size_t iface_dot = api.rfind('.', method_dot - 1);
  if (iface_dot == std::string::npos || iface_dot == 0) return false;
  *module = api.substr(0, iface_dot);
  *iface = api.substr(iface_dot + 1, method_dot - iface_dot - 1);
  *method = api.substr(method_dot + 1);
  return !module->empty() && !iface->empty() && !method->empty();
}

std::string ModuleOf(const std::string& text) {
  const std::string key = "module ";
  size_t at = 0;
  while ((at = text.find(key, at)) != std::string::npos) {
    if (at > 0 && text[at - 1] != '\n' && text[at - 1] != '\r') {
      at += key.size();
      continue;
    }
    size_t i = at + key.size();
    while (i < text.size() && (text[i] == ' ' || text[i] == '\t')) ++i;
    size_t end = i;
    while (end < text.size() && text[end] != ';' && text[end] != '\n' &&
           text[end] != ' ' && text[end] != '\t') {
      ++end;
    }
    if (end > i && end < text.size() &&
        (text[end] == ';' || text[end] == ' ' || text[end] == '\t')) {
      return text.substr(i, end - i);
    }
    at += key.size();
  }
  return {};
}

}  // namespace

struct Vm::State {
  std::vector<std::string> paths;
  std::unordered_map<std::string, std::vector<std::string>> by_module;
  std::unordered_map<std::string, voodoom::LoadedGraph> graphs;
  bool indexed = false;
};

Vm::Vm(std::string mojom_root)
    : mojom_root_(std::move(mojom_root)), state_(std::make_unique<State>()) {}

Vm::~Vm() = default;

void Vm::EnsureIndex() {
  if (state_->indexed) return;
  std::error_code ec;
  for (fs::recursive_directory_iterator it(mojom_root_, ec), end;
       !ec && it != end; it.increment(ec)) {
    if (!it->is_regular_file(ec)) continue;
    if (it->path().extension() != ".mojom") continue;
    const std::string path = it->path().string();
    state_->paths.push_back(path);
    const std::optional<std::string> text = ReadFile(path);
    if (!text) continue;
    const std::string name = ModuleOf(*text);
    if (!name.empty()) state_->by_module[name].push_back(path);
  }
  state_->indexed = true;
}

std::string Vm::Call(const std::string& api, const std::string& /*arg*/) {
  std::string module, iface, method;
  if (!SplitApi(api, &module, &iface, &method)) {
    return Err("api is module.Interface.Method");
  }
  EnsureIndex();
  const auto files = state_->by_module.find(module);
  if (files == state_->by_module.end()) return Err("unknown module " + module);

  for (const std::string& path : files->second) {
    if (!state_->graphs.count(path)) {
      try {
        std::vector<std::string> skipped;
        bool entry_loaded = false;
        state_->graphs.emplace(
            path, voodoom::LoadModuleGraph(
                      path, {mojom_root_}, ReadFile,
                      std::unordered_set<std::string>{}, &skipped,
                      &entry_loaded));
      } catch (const std::exception& ex) {
        return Err(ex.what());
      }
    }
    const voodoom::LoadedGraph& graph = state_->graphs[path];
    for (const voodoom::LoadedFile& file : graph) {
      if (file.module.name != module) continue;
      for (const voodoom::Interface& def : file.module.interfaces) {
        if (def.name != iface) continue;
        for (const voodoom::Method& m : def.methods) {
          if (m.name != method) continue;
          const uint32_t voodoo = FnvOrdinal(voodoom::MethodKey(module, iface, m));
          const uint32_t mojo = ChromeOrdinal(def, m);
          return "ok" + Sep() + std::to_string(voodoo) + Sep() +
                 std::to_string(mojo);
        }
        return Err("unknown method " + method);
      }
    }
  }
  return Err("unknown interface " + iface);
}

std::string Vm::Portfolio(const std::string& out_path) {
  EnsureIndex();
  struct Row {
    std::string api;
    uint32_t voodoo = 0;
    uint32_t mojo = 0;
    std::string fqn;
    std::string rel;
  };
  std::vector<Row> rows;
  std::vector<std::string> failed;
  const fs::path root(mojom_root_);
  int n = 0;
  for (const std::string& path : state_->paths) {
    ++n;
    if (n % 200 == 0) {
      std::cerr << "mojovm portfolio " << n << "/" << state_->paths.size()
                << "\n";
    }
    std::string rel = path;
    std::error_code rec;
    const fs::path relative = fs::relative(path, root, rec);
    if (!rec) rel = relative.generic_string();
    try {
      std::vector<std::string> skipped;
      bool entry_loaded = false;
      const voodoom::LoadedGraph graph = voodoom::LoadModuleGraph(
          path, {mojom_root_}, ReadFile, std::unordered_set<std::string>{},
          &skipped, &entry_loaded);
      if (!entry_loaded || graph.empty()) {
        std::string why = "entry not loaded";
        if (!skipped.empty()) why = skipped.back();
        failed.push_back(rel + "\t" + why);
        continue;
      }
      const voodoom::Module& mod = graph.back().module;
      if (mod.name.empty()) {
        failed.push_back(rel + "\tno module name");
        continue;
      }
      for (const voodoom::Interface& def : mod.interfaces) {
        for (const voodoom::Method& m : def.methods) {
          const std::string api = mod.name + "." + def.name + "." + m.name;
          const std::string fqn = voodoom::MethodKey(mod.name, def.name, m);
          rows.push_back(
              Row{api, FnvOrdinal(fqn), ChromeOrdinal(def, m), fqn, rel});
        }
      }
    } catch (const std::exception& ex) {
      failed.push_back(rel + "\t" + ex.what());
    }
  }
  std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
    if (a.api != b.api) return a.api < b.api;
    return a.rel < b.rel;
  });
  std::sort(failed.begin(), failed.end());

  std::error_code dec;
  fs::create_directories(fs::path(out_path).parent_path(), dec);
  std::ofstream out(out_path, std::ios::binary);
  if (!out) return Err("cannot write " + out_path);
  out << "# mojovm portfolio\tmethods=" << rows.size()
      << "\tfiles=" << state_->paths.size() << "\tfailed=" << failed.size()
      << "\n";
  for (const Row& row : rows) {
    out << row.api << "\t" << row.voodoo << "\t" << row.mojo << "\t" << row.fqn
        << "\t" << row.rel << "\n";
  }
  if (!failed.empty()) {
    out << "# failed\n";
    for (const std::string& line : failed) out << line << "\n";
  }
  return "ok" + Sep() + std::to_string(rows.size()) + Sep() +
         std::to_string(state_->paths.size()) + Sep() +
         std::to_string(failed.size());
}

}  // namespace mojovm
