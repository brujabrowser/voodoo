#include "mojo/public/cpp/bindings/lib/type_intern.h"

#include <unordered_map>
#include <vector>

namespace mojo::internal {
namespace {

struct InternState {
  cppgc::internal::CppHeapPointerTable heap;
  v8::internal::TrustedPointerTable types;
  std::unordered_map<const void*, uint32_t> by_key;
  std::vector<v8::CppHeapPointerTag> tags;
  std::vector<v8::internal::TrustedPointerHandle> tpt;
};

InternState& State() {
  static InternState s;
  return s;
}

v8::internal::Sandbox& Cage() {
  if (v8::internal::Sandbox* c = v8::internal::Sandbox::current()) {
    if (!c->is_initialized()) {
      c->Initialize();
    }
    return *c;
  }
  // Deliberately leaked, not a function-local `static Sandbox owned;` --
  // see WASMHolePunch/src/system/core.cc's own Cage() (same pattern,
  // same fix) for why: static destruction order across a TU's own
  // statics is unspecified relative to whichever container still holds
  // cage-allocated memory (here, InternState's tables) at process exit,
  // and a Sandbox that tears down first turns every later free into a
  // wild ::operator delete on a cage pointer.
  static v8::internal::Sandbox& owned = *new v8::internal::Sandbox();
  if (!owned.is_initialized()) {
    owned.Initialize();
    v8::internal::Sandbox::set_current(&owned);
  }
  return owned;
}

}  // namespace

v8::internal::Sandbox& TypeCage() { return Cage(); }

cppgc::internal::CppHeapPointerTable& TypeHeap() { return State().heap; }

v8::internal::TrustedPointerTable& TypeTable() { return State().types; }

uint32_t InternType(const void* type_key) {
  InternState& s = State();
  auto it = s.by_key.find(type_key);
  if (it != s.by_key.end()) {
    return it->second;
  }
  uint32_t id = static_cast<uint32_t>(s.tags.size() + 1);
  v8::CppHeapPointerTag tag = v8::AllocateCppHeapPointerTag();
  auto* rec = static_cast<InternedTypeRec*>(
      Cage().Allocate(sizeof(InternedTypeRec), alignof(InternedTypeRec)));
  rec->type_key = type_key;
  rec->intern_id = id;
  v8::internal::TrustedPointerHandle th = s.types.AllocateAndInitializeEntry(
      reinterpret_cast<v8::Address>(rec),
      v8::internal::kGenericTrustedObjectTag);
  s.by_key.emplace(type_key, id);
  s.tags.push_back(tag);
  s.tpt.push_back(th);
  return id;
}

v8::CppHeapPointerTag TypeTag(const void* type_key) {
  uint32_t id = InternType(type_key);
  return State().tags[id - 1];
}

const InternedTypeRec* InternedType(const void* type_key) {
  uint32_t id = InternType(type_key);
  InternState& s = State();
  v8::Address addr =
      s.types.Get(s.tpt[id - 1], v8::internal::kGenericTrustedObjectTag);
  return reinterpret_cast<const InternedTypeRec*>(addr);
}

}  // namespace mojo::internal
