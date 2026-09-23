#ifndef WVC_SRC_MOJOVM_H_
#define WVC_SRC_MOJOVM_H_

// mojovm.Call(api, arg) is the same shape as gocvm.Call(api, arg):
// api is module.Interface.Method, arg is 0x1F-separated fields.
// The method is parsed from the .mojom the first time that module is
// called. Nothing here is linked into gocvm.
#include <memory>
#include <string>

namespace mojovm {

class Vm {
 public:
  explicit Vm(std::string mojom_root);
  ~Vm();
  Vm(const Vm&) = delete;
  Vm& operator=(const Vm&) = delete;

  // Reply is "ok", the voodoo ordinal, then the Chrome ordinal.
  // Voodoo is FNV-1a of the full FQN, masked to 0x7fffffff.
  // Chrome is sha256(//chrome/VERSION + interface name + 1-based index),
  // first 4 bytes little-endian, masked to 0x7fffffff.
  std::string Call(const std::string& api, const std::string& arg);

  // Parse every .mojom under the root and write one row per method:
  // module.Interface.Method, voodoo ordinal, chrome ordinal, FQN, path.
  // Returns "ok" plus counts, or "error: ...".
  std::string Portfolio(const std::string& out_path);

 private:
  struct State;
  void EnsureIndex();
  std::string mojom_root_;
  std::unique_ptr<State> state_;
};

}  // namespace mojovm

#endif  // WVC_SRC_MOJOVM_H_
