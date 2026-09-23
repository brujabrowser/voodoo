#ifndef NG_MODULE_H_
#define NG_MODULE_H_

// One module. mojovm names a method. Fire accepts any key, including
// the 48-bit SECRPC magic. HolePunch and the message proxy are not
// two buses here.
#include <cstdint>
#include <string>
#include <vector>

namespace ng {

inline constexpr uint64_t kSecrpc = 91556947316803ull;
inline constexpr uint64_t kMaxMojoOrdinal = 0x7fffffffull;

// HolePunch Fire returns when the write succeeds. Recv is a later read with
// no request id check, and a Run reply is swallowed before it reaches the
// sender. A row here is proved only when the listener answers that same
// request id and key.
inline constexpr uint64_t kRunMessageId = 0xffffffffull;

struct Fired {
  uint64_t request_id = 0;
  uint64_t key = 0;
  std::string name;
  std::string args;
  std::string reply;
  bool proved = false;
};

class Module {
 public:
  explicit Module(std::string mojom_root);
  ~Module();
  Module(const Module&) = delete;
  Module& operator=(const Module&) = delete;

  // mojovm.Call. Reply is ok, voodoo ordinal, chrome ordinal.
  std::string Call(const std::string& api, const std::string& arg);

  // Any key. SECRPC is valid. A key wider than kMaxMojoOrdinal is kept whole.
  std::string Fire(uint64_t key, const std::string& name, const std::string& args);

  // expected fires the chrome ordinal. The other fires the voodoo ordinal.
  std::string FireName(const std::string& api, const std::string& args, bool expected);

  const std::vector<Fired>& log() const { return log_; }

 private:
  struct State;
  std::string mojom_root_;
  std::vector<Fired> log_;
  uint64_t next_request_ = 1;
  State* state_;
};

}  // namespace ng

#endif  // NG_MODULE_H_
