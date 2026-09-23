// Golden test for CageAllocator<T>: a std::vector<T, CageAllocator<T>>
// actually lives inside the active Sandbox (not just carries a pointer
// that happens to be valid), reallocation (grow/shrink/clear) round-trips
// through Allocate()/Free() correctly, and the allocator falls back to
// the plain heap gracefully when no Sandbox is active -- both before
// Initialize() and after TearDown().
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "src/sandbox/cage-allocator.h"
#include "src/sandbox/sandbox.h"

using v8::internal::CageAllocator;
using v8::internal::Sandbox;

namespace {
using CageBytes = std::vector<uint8_t, CageAllocator<uint8_t>>;
}  // namespace

int main() {
  // No active Sandbox yet: falls back to the plain heap instead of
  // crashing or silently doing nothing.
  {
    CageBytes v(16, 0x42);
    assert(v.size() == 16);
    assert(v[0] == 0x42 && v[15] == 0x42);
  }

  Sandbox sandbox;
  sandbox.Initialize(1 * 1024 * 1024);
  Sandbox::set_current(&sandbox);

  // A freshly-constructed cage-backed vector's storage is genuinely
  // inside the cage, not just a valid pointer from somewhere else.
  {
    CageBytes v(256, 0);
    assert(sandbox.Contains(v.data()));
    for (size_t i = 0; i < v.size(); ++i) v[i] = static_cast<uint8_t>(i);
    for (size_t i = 0; i < v.size(); ++i) assert(v[i] == static_cast<uint8_t>(i));
  }

  // Growth (push_back past capacity) reallocates -- old storage is freed
  // (reclaimed via Sandbox::Free(), not leaked in the cage) and new
  // storage is still in-cage. Exercise real reallocation, not just a
  // single up-front resize.
  {
    CageBytes v;
    for (int i = 0; i < 5000; ++i) {
      v.push_back(static_cast<uint8_t>(i & 0xff));
      assert(sandbox.Contains(v.data()));
    }
    assert(v.size() == 5000);
    assert(v[0] == 0 && v[4999] == static_cast<uint8_t>(4999 & 0xff));
  }

  // Alloc/free churn actually reclaims cage space -- allocate and destroy
  // many vectors, each larger than any single reused free block would be
  // if reclamation weren't happening; a 1MB cage would exhaust (CHECK-fail
  // abort) well before 2000 iterations of a 4KB buffer if Free() weren't
  // real.
  {
    for (int i = 0; i < 2000; ++i) {
      CageBytes v(4096, 0xAB);
      assert(sandbox.Contains(v.data()));
    }
  }

  // Two same-sized cage vectors' storage don't overlap.
  {
    CageBytes a(128, 1);
    CageBytes b(128, 2);
    assert(a.data() != b.data());
    assert(a[0] == 1 && b[0] == 2);
  }

  // CageVector / CageMap -- the occupancy spelling generated .voodoom
  // array/map fields use. Incomplete mapped/element types (values.mojom
  // DictionaryValue holding Value) compile and round-trip.
  {
    v8::internal::CageVector<int> v;
    for (int i = 0; i < 64; ++i) v.push_back(i);
    assert(v.size() == 64);
    assert(sandbox.Contains(v.data()));
    assert(v[63] == 63);
  }
  {
    v8::internal::CageMap<int, int> m;
    m.emplace(1, 2);
    assert(m.at(1) == 2);
  }
  {
    struct Rec;
    struct Box {
      v8::internal::CageMap<std::string, Rec> kids;
    };
    struct Rec {
      Box box;
      int x = 0;
    };
    Rec child;
    child.x = 7;
    Rec root;
    root.box.kids.emplace("c", child);
    assert(root.box.kids.at("c").x == 7);
  }

  sandbox.TearDown();
  Sandbox::set_current(nullptr);

  // After TearDown(), falls back to the plain heap again rather than
  // touching the (now-invalid) cage.
  {
    CageBytes v(32, 7);
    assert(v.size() == 32);
    assert(v[0] == 7);
  }

  std::printf("cage_allocator_test: OK\n");
  return 0;
}
