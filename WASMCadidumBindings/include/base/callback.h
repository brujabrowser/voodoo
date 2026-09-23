// Bindings is the base:: rung. Source of truth is HolePunch's
// whp::OnceCallback (whp/base/once_callback.h).
#ifndef BASE_CALLBACK_H_
#define BASE_CALLBACK_H_

#include "whp/base/once_callback.h"
#include "whp/base/repeating_callback.h"

namespace base {

using whp::OnceCallback;
using whp::OnceClosure;
using whp::RepeatingCallback;
using whp::RepeatingClosure;
using whp::BindOnce;
using whp::Unretained;
using whp::UnretainedWrapper;

}  // namespace base

#endif  // BASE_CALLBACK_H_
