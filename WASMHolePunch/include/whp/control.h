#ifndef WHP_CONTROL_H_
#define WHP_CONTROL_H_

#include <cstdint>

namespace whp {
namespace control {

// Chromium mojo/public/interfaces/bindings/interface_control_messages.mojom
// names. These are Mojo header `name` values, not EV SRPC magics.
inline constexpr uint32_t kRunMessageId = 0xFFFFFFFFu;
inline constexpr uint32_t kRunOrClosePipeMessageId = 0xFFFFFFFEu;

inline constexpr uint32_t kQueryVersion = 0u;
inline constexpr uint32_t kBindingsVersion = 3u;

}  // namespace control
}  // namespace whp

#endif  // WHP_CONTROL_H_
