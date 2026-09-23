// Bindings is the base:: rung. Source of truth is HolePunch's
// whp::TimeTicks / TimeDelta (whp/base/time.h).
#ifndef BASE_TIME_H_
#define BASE_TIME_H_

#include "whp/base/time.h"

namespace base {

using TimeDelta = whp::TimeDelta;
using TimeTicks = whp::TimeTicks;

}  // namespace base

#endif  // BASE_TIME_H_
