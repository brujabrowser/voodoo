// Interned type representation for .voodoom types. Every distinct shape
// ("int32", "string?", "array<int32,4>", "pending_remote<echo.Echo>",
// "interface:echo.Echo", "func echo.Echo.EchoString(string)->(string)")
// gets exactly one canonical id. Identical is id equality, not a
// structural walk -- the same Object Type Identifier shape Go++ uses
// (type_key_of<T>() / go/types intern) and Blink WrapperTypeInfo uses
// for DOM wrappers.
#ifndef WVC_SRC_TYPE_INTERN_H_
#define WVC_SRC_TYPE_INTERN_H_

#include "ast.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace voodoom {

// Shape-string for a TypeSpec. `current_ns` is the file's module name
// (used when TypeSpec::owner_namespace is empty).
std::string TypeKey(const TypeSpec& t, const std::string& current_ns);

std::string NamedKey(const std::string& ns, const std::string& name,
                     const char* kind);

std::string MethodKey(const std::string& ns, const std::string& iface,
                      const Method& m);

class TypeIntern {
 public:
  uint32_t InternKey(const std::string& key);
  uint32_t Intern(const TypeSpec& t, const std::string& current_ns);
  uint32_t InternNamed(const std::string& ns, const std::string& name,
                       const char* kind);
  uint32_t InternMethod(const std::string& ns, const std::string& iface,
                        const Method& m);

  bool Identical(uint32_t a, uint32_t b) const { return a == b; }
  const std::string& Key(uint32_t id) const;
  uint32_t size() const { return static_cast<uint32_t>(keys_.size()); }

 private:
  std::unordered_map<std::string, uint32_t> by_key_;
  std::vector<std::string> keys_;  // index = id - 1
};

}  // namespace voodoom

#endif  // WVC_SRC_TYPE_INTERN_H_
