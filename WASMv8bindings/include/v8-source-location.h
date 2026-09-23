// Adapted from V8's include/v8-source-location.h -- genuinely portable
// as-is, it only wraps std::source_location. Trimmed to what cppgc's
// logging.h actually calls (Current(), ToString() is dropped since nothing
// here needs to print one).
#ifndef WASMV8_INCLUDE_V8_SOURCE_LOCATION_H_
#define WASMV8_INCLUDE_V8_SOURCE_LOCATION_H_

#include <cstddef>
#include <source_location>

#include "v8config.h"

namespace v8 {

class V8_EXPORT SourceLocation final {
 public:
  static constexpr SourceLocation Current(
      const std::source_location& loc = std::source_location::current()) {
    return SourceLocation(loc);
  }

  constexpr SourceLocation() = default;

  constexpr const char* Function() const { return loc_.function_name(); }
  constexpr const char* FileName() const { return loc_.file_name(); }
  constexpr size_t Line() const { return loc_.line(); }

  operator bool() const { return loc_.line() != 0; }

 private:
  constexpr explicit SourceLocation(const std::source_location& loc)
      : loc_(loc) {}

  std::source_location loc_;
};

}  // namespace v8

#endif  // WASMV8_INCLUDE_V8_SOURCE_LOCATION_H_
