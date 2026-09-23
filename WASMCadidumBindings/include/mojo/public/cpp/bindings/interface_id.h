#ifndef MOJO_PUBLIC_CPP_BINDINGS_INTERFACE_ID_H_
#define MOJO_PUBLIC_CPP_BINDINGS_INTERFACE_ID_H_

#include <cstdint>

namespace mojo {

using InterfaceId = uint32_t;

// The primary interface bound directly to a message pipe (as opposed to an
// associated interface multiplexed onto one) always uses id 0.
constexpr InterfaceId kPrimaryInterfaceId = 0;

// Never a valid interface id; used as a sentinel.
constexpr InterfaceId kInvalidInterfaceId = 0xFFFFFFFFu;

// MultiplexRouter mints associated-interface ids out of a 31-bit counter and
// tags the top bit with a per-router namespace bit, so the two routers on
// either end of one pipe (each minting ids independently, at any time) can
// never collide. This is a local id-allocation policy, not part of the wire
// format -- interface ids are opaque to whichever peer didn't mint them.
constexpr uint32_t kInterfaceIdNamespaceMask = 0x80000000u;

constexpr bool IsPrimaryInterfaceId(InterfaceId id) {
  return id == kPrimaryInterfaceId;
}

constexpr bool IsValidInterfaceId(InterfaceId id) {
  return id != kInvalidInterfaceId;
}

}  // namespace mojo

#endif  // MOJO_PUBLIC_CPP_BINDINGS_INTERFACE_ID_H_
