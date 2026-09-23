#ifndef WHP_BASE_REPEATING_CALLBACK_H_
#define WHP_BASE_REPEATING_CALLBACK_H_

#include "src/sandbox/cage-allocator.h"

#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

namespace whp {

template <typename Signature>
class RepeatingCallback;

// Copyable type-erased callback. Bindings re-exports this as
// base::RepeatingCallback. SimpleWatcher (and other re-arming traps)
// copy the callback into posted tasks.
template <typename R, typename... Args>
class RepeatingCallback<R(Args...)> {
 public:
  RepeatingCallback() = default;
  RepeatingCallback(std::nullptr_t) {}  // NOLINT

  template <typename F, typename = std::enable_if_t<
                            !std::is_same_v<std::decay_t<F>, RepeatingCallback> &&
                            std::is_invocable_r_v<R, std::decay_t<F>, Args...>>>
  RepeatingCallback(F&& fn)  // NOLINT
      // Cage-backed (see cage-allocator.h), not plain make_shared: a
      // SimpleWatcher's callback_ closure (and every copy Notify() takes
      // for a posted task) is exactly the kind of object that outlives
      // its own creating call and gets torn down from somewhere else
      // entirely (Cancel(), a later idle-pump) -- one more piece of the
      // same object graph CageAllocator exists for, not just the message
      // bytes it started with.
      : impl_(std::allocate_shared<Callable<std::decay_t<F>>>(
            v8::internal::CageAllocator<Callable<std::decay_t<F>>>(),
            std::forward<F>(fn))) {}

  RepeatingCallback(const RepeatingCallback&) = default;
  RepeatingCallback& operator=(const RepeatingCallback&) = default;
  RepeatingCallback(RepeatingCallback&&) noexcept = default;
  RepeatingCallback& operator=(RepeatingCallback&&) noexcept = default;

  explicit operator bool() const { return static_cast<bool>(impl_); }
  bool is_null() const { return !impl_; }

  R Run(Args... args) const {
    return impl_->Invoke(std::forward<Args>(args)...);
  }

  R operator()(Args... args) const {
    return Run(std::forward<Args>(args)...);
  }

 private:
  struct Invoker {
    virtual ~Invoker() = default;
    virtual R Invoke(Args... args) = 0;
  };

  template <typename F>
  struct Callable final : Invoker {
    explicit Callable(F fn) : fn_(std::move(fn)) {}
    R Invoke(Args... args) override {
      return std::invoke(fn_, std::forward<Args>(args)...);
    }
    F fn_;
  };

  std::shared_ptr<Invoker> impl_;
};

using RepeatingClosure = RepeatingCallback<void()>;

}  // namespace whp

#endif  // WHP_BASE_REPEATING_CALLBACK_H_
