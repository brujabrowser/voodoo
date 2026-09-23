#ifndef WHP_RAW_PTR_IMPL_REF_TRAITS_H_
#define WHP_RAW_PTR_IMPL_REF_TRAITS_H_

namespace whp {

template <typename Interface>
struct RawPtrImplRefTraits {
  using PointerType = Interface*;

  static bool IsNull(PointerType ptr) { return !ptr; }
  static Interface* GetRawPointer(PointerType* ptr) { return *ptr; }
};

}  // namespace whp

#endif  // WHP_RAW_PTR_IMPL_REF_TRAITS_H_
