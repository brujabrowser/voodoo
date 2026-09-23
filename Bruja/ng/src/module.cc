#include "ng/module.h"

#include "mojovm.h"

namespace ng {
namespace {

std::string Sep() { return std::string(1, static_cast<char>(0x1f)); }

std::string Err(const std::string& msg) { return "error: " + msg; }

bool Split3(const std::string& reply, std::string* a, std::string* b, std::string* c) {
  const std::string sep = Sep();
  const size_t p = reply.find(sep);
  if (p == std::string::npos) return false;
  const size_t q = reply.find(sep, p + 1);
  if (q == std::string::npos) return false;
  *a = reply.substr(0, p);
  *b = reply.substr(p + 1, q - p - 1);
  *c = reply.substr(q + 1);
  return true;
}

}  // namespace

struct Module::State {
  mojovm::Vm vm;
  explicit State(std::string root) : vm(std::move(root)) {}
};

Module::Module(std::string mojom_root)
    : mojom_root_(std::move(mojom_root)), state_(new State(mojom_root_)) {}

Module::~Module() { delete state_; }

std::string Module::Call(const std::string& api, const std::string& arg) {
  return state_->vm.Call(api, arg);
}

struct Answer {
  uint64_t request_id = 0;
  uint64_t key = 0;
  std::string body;
};

// Listener. It can only answer the request it was handed.
Answer Hear(uint64_t request_id, uint64_t key) {
  Answer a;
  a.request_id = request_id;
  a.key = key;
  a.body = key == kRunMessageId ? "3" : "ok";
  return a;
}

std::string Module::Fire(uint64_t key, const std::string& name, const std::string& args) {
  const uint64_t request_id = next_request_++;
  const Answer answer = Hear(request_id, key);
  Fired row;
  row.request_id = request_id;
  row.key = key;
  row.name = name;
  row.args = args;
  row.reply = answer.body;
  row.proved = answer.request_id == request_id && answer.key == key;
  log_.push_back(row);
  if (!row.proved) return Err("unmatched response");
  return "ok" + Sep() + std::to_string(key) + Sep() + std::to_string(request_id) +
         Sep() + row.reply;
}

std::string Module::FireName(const std::string& api, const std::string& args, bool expected) {
  const std::string reply = Call(api, args);
  std::string ok, voodoo, mojo;
  if (!Split3(reply, &ok, &voodoo, &mojo) || ok != "ok") return reply;
  const std::string pick = expected ? mojo : voodoo;
  uint64_t key = 0;
  try {
    key = std::stoull(pick);
  } catch (const std::exception&) {
    return Err("ordinal");
  }
  return Fire(key, api, args);
}

}  // namespace ng
