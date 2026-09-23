#ifndef WHP_BASE_ONCE_CALLBACK_H_
#define WHP_BASE_ONCE_CALLBACK_H_

#include "src/sandbox/cage-allocator.h"
#include "src/sandbox/sandbox.h"

#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

namespace whp {

template <typename Signature>
class OnceCallback;

// Move-only type-erased callback. Bindings re-exports this as
// base::OnceCallback. Storage is a unique_ptr invoker (not std::function)
// so a generated Proxy_ can capture a move-only user callback.
template <typename R, typename... Args>
class OnceCallback<R(Args...)> {
 public:
  OnceCallback() = default;
  OnceCallback(std::nullptr_t) {}  // NOLINT

  template <typename F, typename = std::enable_if_t<
                            !std::is_same_v<std::decay_t<F>, OnceCallback> &&
                            std::is_invocable_r_v<R, std::decay_t<F>, Args...>>>
  OnceCallback(F&& fn)  // NOLINT
      : impl_(MakeCallable(std::forward<F>(fn)), CageDelete{}) {}

  OnceCallback(OnceCallback&&) noexcept = default;
  OnceCallback& operator=(OnceCallback&&) noexcept = default;
  OnceCallback(const OnceCallback&) = delete;
  OnceCallback& operator=(const OnceCallback&) = delete;

  explicit operator bool() const { return static_cast<bool>(impl_); }
  bool is_null() const { return !impl_; }

  R Run(Args... args) && {
    auto impl = std::move(impl_);
    return impl->Invoke(std::forward<Args>(args)...);
  }

  // Consume-and-invoke from an lvalue (generated Proxy_/impls can write
  // `callback(x)` the same way Chromium writes `std::move(callback).Run(x)`).
  R operator()(Args... args) {
    return std::move(*this).Run(std::forward<Args>(args)...);
  }

 private:
  struct Invoker {
    virtual ~Invoker() = default;
    virtual R Invoke(Args... args) = 0;
    // Own heap footprint (sizeof the *concrete* Callable<F>, not just
    // Invoker) -- CageDelete needs this to call Sandbox::Free() with the
    // same size Allocate() was given; there's no other way to recover it
    // once only the base Invoker* is left. See cage-allocator.h's file
    // comment for why this closure is worth cage-backing at all: it's
    // exactly the kind of object (created in one call, torn down from
    // somewhere else entirely -- a Stub_ dispatch's response callback,
    // destroyed unfired when ReportBadMessage short-circuits it) this
    // whole family's crashes kept moving into next.
    virtual size_t FootprintBytes() const = 0;
  };

  template <typename F>
  struct Callable final : Invoker {
    explicit Callable(F fn) : fn_(std::move(fn)) {}
    R Invoke(Args... args) override {
      return std::invoke(std::move(fn_), std::forward<Args>(args)...);
    }
    size_t FootprintBytes() const override { return sizeof(Callable<F>); }
    F fn_;
  };

  struct CageDelete {
    void operator()(Invoker* p) const {
      if (!p) return;
      size_t bytes = p->FootprintBytes();
      p->~Invoker();
      if (v8::internal::Sandbox* s = v8::internal::Sandbox::current();
          s && s->Contains(p)) {
        s->Free(p, bytes);
      } else {
        ::operator delete(p);
      }
    }
  };

  template <typename F>
  static Invoker* MakeCallable(F&& fn) {
    using CallableT = Callable<std::decay_t<F>>;
    void* mem;
    if (v8::internal::Sandbox* s = v8::internal::Sandbox::current()) {
      mem = s->Allocate(sizeof(CallableT), alignof(CallableT));
    } else {
      mem = ::operator new(sizeof(CallableT));
    }
    return new (mem) CallableT(std::forward<F>(fn));
  }

  std::unique_ptr<Invoker, CageDelete> impl_;
};

using OnceClosure = OnceCallback<void()>;

template <typename T>
class UnretainedWrapper {
 public:
  explicit UnretainedWrapper(T* p) : p_(p) {}
  T* get() const { return p_; }

 private:
  T* p_;
};

template <typename T>
UnretainedWrapper<T> Unretained(T* p) {
  return UnretainedWrapper<T>(p);
}

template <typename R, typename... Args>
OnceCallback<R(Args...)> BindOnce(R (*fn)(Args...)) {
  return OnceCallback<R(Args...)>(fn);
}

template <typename R, typename C, typename... Args>
OnceCallback<R(Args...)> BindOnce(R (C::*method)(Args...), C* obj) {
  return OnceCallback<R(Args...)>(
      [method, obj](Args... args) { return (obj->*method)(std::forward<Args>(args)...); });
}

template <typename R, typename C, typename... Args>
OnceCallback<R(Args...)> BindOnce(R (C::*method)(Args...),
                                  UnretainedWrapper<C> obj) {
  return BindOnce(method, obj.get());
}

template <typename F, typename... Bound>
auto BindOnce(F&& fn, Bound&&... bound) {
  return OnceCallback<void()>(
      [fn = std::forward<F>(fn),
       ... bound = std::forward<Bound>(bound)]() mutable {
        std::invoke(std::move(fn), std::move(bound)...);
      });
}

}  // namespace whp

#endif  // WHP_BASE_ONCE_CALLBACK_H_
