// Post-parse pass: rewrites every kUnresolvedRef TypeSpec (a bare
// identifier the parser couldn't classify on its own, since `interface`/
// `enum`/`dictionary`/`callback` declarations can appear in any order) to
// the correct kInterfaceRef/kEnumRef/kDictionaryRef/kCallbackRef kind, and
// validates interface base-name references. Runs between Parse() and
// GenerateCppHeader().
#ifndef BRUJA_RESOLVER_H_
#define BRUJA_RESOLVER_H_

#include <stdexcept>
#include <string>

#include "ast.h"

namespace bruja {

class ResolveError : public std::runtime_error {
 public:
  explicit ResolveError(const std::string& msg) : std::runtime_error(msg) {}
};

void Resolve(Module& module);

}  // namespace bruja

#endif  // BRUJA_RESOLVER_H_
