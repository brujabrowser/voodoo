// Object Type Identifier intern for generated .voodoom types.
//
// Same fashion as Go++:
//   type_key_of<T>()  -- C++ identity is a static-char address
//   go/types intern   -- one canonical *Type per shape
//   CHPT tag          -- interned type id; Get fails unless the tag matches
//
// Runtime pieces:
//   WASMSafeSpace Sandbox  -- interned Type records live in the cage
//   WASMSafeSpace TPT      -- names those records (kGenericTrustedObjectTag)
//   WASMv8Bindings CHPT    -- names C++ objects (Proxy_/impl/struct) with
//                             the interned CppHeapPointerTag
#ifndef MOJO_PUBLIC_CPP_BINDINGS_LIB_TYPE_INTERN_H_
#define MOJO_PUBLIC_CPP_BINDINGS_LIB_TYPE_INTERN_H_

#include "src/sandbox/cppheap-pointer-table.h"
#include "src/sandbox/sandbox.h"
#include "src/sandbox/trusted-pointer-table.h"
#include "v8-sandbox.h"

#include <cstdint>
#include <type_traits>

namespace mojo::internal {

// Cage-resident intern record, named on TPT.
struct InternedTypeRec {
  const void* type_key;
  uint32_t intern_id;
};

// Intern `type_key` (typically Interface::type_key()) to a process-wide
// 1-based id. Same key always returns the same id.
uint32_t InternType(const void* type_key);

// CHPT tag for this C++ type. Allocated once, lazily, via
// v8::AllocateCppHeapPointerTag (cross-header uniqueness, same as
// brujac's V8 backend).
v8::CppHeapPointerTag TypeTag(const void* type_key);

cppgc::internal::CppHeapPointerTable& TypeHeap();
v8::internal::TrustedPointerTable& TypeTable();
v8::internal::Sandbox& TypeCage();

// The cage intern record for `type_key`, recovered through TPT.
const InternedTypeRec* InternedType(const void* type_key);

inline v8::CppHeapPointerHandle NameObject(void* p, const void* type_key) {
  return TypeHeap().AllocateAndInitializeEntry(p, TypeTag(type_key));
}

inline void* GetObject(v8::CppHeapPointerHandle h, const void* type_key) {
  return TypeHeap().Get(h, TypeTag(type_key));
}

inline void FreeObject(v8::CppHeapPointerHandle h) {
  TypeHeap().FreeEntry(h);
}

template <typename T, typename = void>
struct has_type_key : std::false_type {};
template <typename T>
struct has_type_key<T, std::void_t<decltype(T::type_key())>> : std::true_type {
};

// Generated interfaces use T::type_key(); hand-written ones get a per-T
// static-char token (same idea as Go++ type_key_of<T>()).
template <typename T>
const void* InterfaceTypeKey() {
  if constexpr (has_type_key<T>::value) {
    return T::type_key();
  } else {
    static char k;
    return &k;
  }
}

}  // namespace mojo::internal

#endif  // MOJO_PUBLIC_CPP_BINDINGS_LIB_TYPE_INTERN_H_
