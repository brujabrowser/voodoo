#ifndef WASMV8_SRC_BASE_BITS_H_
#define WASMV8_SRC_BASE_BITS_H_

#include <cstdint>
#include <type_traits>

namespace v8::base::bits {

template <typename T>
constexpr bool IsPowerOfTwo(T value) {
  static_assert(std::is_integral_v<T>);
  return value > 0 && (value & (value - 1)) == 0;
}

constexpr uint32_t RoundDownToPowerOfTwo32(uint32_t value) {
  if (value == 0) return 0;
  uint32_t result = value;
  result |= result >> 1;
  result |= result >> 2;
  result |= result >> 4;
  result |= result >> 8;
  result |= result >> 16;
  return result - (result >> 1);
}

// Returns the exponent: WhichPowerOfTwo(1) == 0, WhichPowerOfTwo(2) == 1, ...
// `value` must be a power of two.
inline uint32_t WhichPowerOfTwo(uint32_t value) {
#if defined(__GNUC__) || defined(__clang__)
  return 31 - static_cast<uint32_t>(__builtin_clz(value));
#else
  uint32_t exponent = 0;
  while (value >>= 1) ++exponent;
  return exponent;
#endif
}

}  // namespace v8::base::bits

#endif  // WASMV8_SRC_BASE_BITS_H_
