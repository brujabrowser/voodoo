// Minimal stand-in for V8's src/base/logging.h -- just DCHECK/CHECK family,
// implemented as fatal aborts (no logging infra, no stack traces). DCHECK_*
// compiles away entirely outside DEBUG, matching real V8's release behavior;
// CHECK_* is always live (used by cppgc's own OOM/invariant guards, which
// must hold in release too).
#ifndef WASMV8_SRC_BASE_LOGGING_H_
#define WASMV8_SRC_BASE_LOGGING_H_

#include <cstdio>
#include <cstdlib>

namespace v8::base::detail {
[[noreturn]] inline void Abort(const char* cond, const char* file, int line) {
  std::fprintf(stderr, "WASMv8bindings CHECK failed: %s (%s:%d)\n", cond,
               file, line);
  std::abort();
}
}  // namespace v8::base::detail

#define CHECK(cond)                                             \
  do {                                                          \
    if (!(cond)) ::v8::base::detail::Abort(#cond, __FILE__, __LINE__); \
  } while (false)

#define CHECK_OP(op, lhs, rhs) CHECK((lhs)op(rhs))
#define CHECK_EQ(lhs, rhs) CHECK_OP(==, lhs, rhs)
#define CHECK_NE(lhs, rhs) CHECK_OP(!=, lhs, rhs)
#define CHECK_LT(lhs, rhs) CHECK_OP(<, lhs, rhs)
#define CHECK_LE(lhs, rhs) CHECK_OP(<=, lhs, rhs)
#define CHECK_GT(lhs, rhs) CHECK_OP(>, lhs, rhs)
#define CHECK_GE(lhs, rhs) CHECK_OP(>=, lhs, rhs)
#define CHECK_IMPLIES(a, b) CHECK(!(a) || (b))

#if defined(DEBUG)
#define DCHECK(cond) CHECK(cond)
#define DCHECK_EQ(lhs, rhs) CHECK_EQ(lhs, rhs)
#define DCHECK_NE(lhs, rhs) CHECK_NE(lhs, rhs)
#define DCHECK_LT(lhs, rhs) CHECK_LT(lhs, rhs)
#define DCHECK_LE(lhs, rhs) CHECK_LE(lhs, rhs)
#define DCHECK_GT(lhs, rhs) CHECK_GT(lhs, rhs)
#define DCHECK_GE(lhs, rhs) CHECK_GE(lhs, rhs)
#define DCHECK_IMPLIES(a, b) CHECK_IMPLIES(a, b)
#else
#define DCHECK(cond) ((void)0)
#define DCHECK_EQ(lhs, rhs) ((void)0)
#define DCHECK_NE(lhs, rhs) ((void)0)
#define DCHECK_LT(lhs, rhs) ((void)0)
#define DCHECK_LE(lhs, rhs) ((void)0)
#define DCHECK_GT(lhs, rhs) ((void)0)
#define DCHECK_GE(lhs, rhs) ((void)0)
#define DCHECK_IMPLIES(a, b) ((void)0)
#endif

#define USE(x) ((void)(x))

#endif  // WASMV8_SRC_BASE_LOGGING_H_
