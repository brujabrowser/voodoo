// Chromium-shaped expected<T, E>. Bindings re-exports this as base::expected
// (Bindings is the base:: rung; this file is the HolePunch source of truth).
#ifndef WHP_BASE_EXPECTED_H_
#define WHP_BASE_EXPECTED_H_

#include <optional>
#include <utility>

namespace whp {

template <typename E>
class unexpected {
 public:
  explicit unexpected(E error) : error_(std::move(error)) {}
  E& error() & { return error_; }
  const E& error() const& { return error_; }
  E&& error() && { return std::move(error_); }

 private:
  E error_;
};

template <typename T, typename E>
class expected {
 public:
  expected() = default;
  expected(T value) : value_(std::move(value)) {}  // NOLINT
  expected(unexpected<E> unex) : error_(std::move(unex.error())) {}  // NOLINT

  bool has_value() const { return value_.has_value(); }
  explicit operator bool() const { return has_value(); }

  T& value() & { return *value_; }
  const T& value() const& { return *value_; }
  T&& value() && { return std::move(*value_); }

  E& error() & { return *error_; }
  const E& error() const& { return *error_; }
  E&& error() && { return std::move(*error_); }

 private:
  std::optional<T> value_;
  std::optional<E> error_;
};

}  // namespace whp

#endif  // WHP_BASE_EXPECTED_H_
