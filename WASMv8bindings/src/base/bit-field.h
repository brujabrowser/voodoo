// Minimal stand-in for V8's src/base/bit-field.h -- just BitField16, the one
// instantiation src/heap/cppgc-internal/heap-object-header.h actually needs
// to pack {GCInfoIndex, unused, in-construction} and {mark bit, size} into
// its two 16-bit halves. Same encode/decode/Next<> shape as the real one.
#ifndef WASMV8_SRC_BASE_BIT_FIELD_H_
#define WASMV8_SRC_BASE_BIT_FIELD_H_

#include <cstdint>
#include <type_traits>

namespace v8::base {

template <typename T, int kShiftArg, int kSizeArg, typename StorageType>
class BitFieldBase {
 public:
  using FieldType = T;
  static constexpr int kShift = kShiftArg;
  static constexpr int kSize = kSizeArg;
  static constexpr StorageType kMax = (StorageType{1} << kSizeArg) - 1;
  static constexpr StorageType kMask = kMax << kShiftArg;

  template <typename U, int kNextSize>
  using Next = BitFieldBase<U, kShiftArg + kSizeArg, kNextSize, StorageType>;

  static constexpr StorageType encode(T value) {
    return static_cast<StorageType>(static_cast<StorageType>(value) & kMax)
           << kShiftArg;
  }

  static constexpr T decode(StorageType storage) {
    return static_cast<T>((storage >> kShiftArg) & kMax);
  }
};

template <typename T, int kShift, int kSize>
using BitField16 = BitFieldBase<T, kShift, kSize, uint16_t>;

}  // namespace v8::base

#endif  // WASMV8_SRC_BASE_BIT_FIELD_H_
