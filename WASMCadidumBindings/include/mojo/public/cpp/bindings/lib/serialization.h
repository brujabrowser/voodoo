// Field-level serializer used by generated WriteX/ReadX. This is the
// generic (de)serializer Bindings ships: little-endian scalars, struct
// headers, and strings. Per-type StructTraits<T> is the Chromium hook for
// mapping a native C++ type onto those primitives; generated mojom structs
// call WriteX/ReadX directly.
#ifndef MOJO_PUBLIC_CPP_BINDINGS_LIB_SERIALIZATION_H_
#define MOJO_PUBLIC_CPP_BINDINGS_LIB_SERIALIZATION_H_

#include "mojo/public/cpp/bindings/lib/wire_primitives.h"
#include "mojo/public/cpp/bindings/message.h"

#include <string>

namespace mojo::internal {

template <typename T>
struct StructTraits {
  // Specialize: static void Write(Message*, const Native&);
  //             static bool Read(const Message&, size_t*, Native*);
};

class Serializer {
 public:
  explicit Serializer(Message* message) : message_(message) {}

  template <typename T>
  void Write(const T& value) {
    WriteScalar(message_, value);
  }

  void Write(const std::string& s) { WriteString(message_, s); }

  void WriteHeader(uint32_t num_bytes, uint32_t version) {
    WriteStructHeader(message_, num_bytes, version);
  }

  Message* message() { return message_; }

 private:
  Message* message_;
};

}  // namespace mojo::internal

#endif  // MOJO_PUBLIC_CPP_BINDINGS_LIB_SERIALIZATION_H_
