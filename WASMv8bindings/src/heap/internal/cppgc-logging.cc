// Implements cppgc::internal::DCheckImpl/FatalImpl, declared in the ported
// include/cppgc/internal/logging.h (CPPGC_DCHECK/CPPGC_CHECK). No
// CPPGC_ENABLE_API_CHECKS in this build, so DCheckImpl is unused dead code
// kept only so the header's macro expansion still links if that build flag
// is ever turned on.
#include <cstdio>
#include <cstdlib>

#include "cppgc/internal/logging.h"

namespace cppgc {
namespace internal {

void DCheckImpl(const char* message, SourceLocation loc) {
  std::fprintf(stderr, "WASMv8bindings cppgc DCHECK failed: %s (%s:%zu)\n",
               message, loc.FileName() ? loc.FileName() : "?", loc.Line());
  std::abort();
}

[[noreturn]] void FatalImpl(const char* message, SourceLocation loc) {
  std::fprintf(stderr, "WASMv8bindings cppgc CHECK failed: %s (%s:%zu)\n",
               message, loc.FileName() ? loc.FileName() : "?", loc.Line());
  std::abort();
}

}  // namespace internal
}  // namespace cppgc
