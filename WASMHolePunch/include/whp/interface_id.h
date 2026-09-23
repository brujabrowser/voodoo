#ifndef WHP_INTERFACE_ID_H_
#define WHP_INTERFACE_ID_H_

#include <cstdint>

namespace whp {

inline constexpr uint32_t kInvalidInterfaceId = 0xFFFFFFFFu;
inline constexpr uint32_t kPrimaryInterfaceId = 0u;

inline bool IsValidInterfaceId(uint32_t id) {
  return id != kInvalidInterfaceId;
}

inline bool IsPrimaryInterfaceId(uint32_t id) {
  return id == kPrimaryInterfaceId;
}

}  // namespace whp

#endif  // WHP_INTERFACE_ID_H_
