// Minimal stand-in for V8's include/v8-maybe.h -- just enough of
// Maybe<T>/the ToChecked()-style API shape that generated/hand-written
// embedder code can use the same fallible-operation idiom real V8 code
// does. Real v8-maybe.h has more helpers (ToChecked with a default, IsJust
// chaining, etc.) -- add as needed.
#ifndef WASMV8_INCLUDE_V8_MAYBE_H_
#define WASMV8_INCLUDE_V8_MAYBE_H_

#include <cassert>
#include <utility>

namespace v8 {

template <typename T>
class Maybe {
 public:
  bool IsJust() const { return has_value_; }
  bool IsNothing() const { return !has_value_; }

  T FromJust() const {
    assert(IsJust());
    return value_;
  }
  T FromMaybe(const T& default_value) const {
    return has_value_ ? value_ : default_value;
  }

  static Maybe<T> Just(const T& value) { return Maybe<T>(true, value); }
  static Maybe<T> Nothing() { return Maybe<T>(false, T()); }

 private:
  Maybe(bool has_value, T value) : has_value_(has_value), value_(std::move(value)) {}

  bool has_value_;
  T value_;
};

template <typename T>
Maybe<T> Just(const T& value) {
  return Maybe<T>::Just(value);
}

template <typename T>
Maybe<T> Nothing() {
  return Maybe<T>::Nothing();
}

}  // namespace v8

#endif  // WASMV8_INCLUDE_V8_MAYBE_H_
