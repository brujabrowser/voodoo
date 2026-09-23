// Golden test tying the whole cage together, the same role
// v8_facade_console_test.cc plays for ../WASMv8bindings: a real in-cage
// "ArrayBuffer" backing store reached through a SandboxedPointer, a real
// "trusted metadata" object living genuinely outside the cage reached
// through a TrustedPointerTable handle, and a real callable "detach
// handler" reached through a CodePointerTable handle -- proving genuine
// end-to-end indirection, not just wiring nothing exercises. Then the
// actual point of a sandbox: prove that an attacker confined to corrupting
// sandboxed memory (the threat model every mechanism in this repo assumes,
// see sandbox.h's file comment) cannot use that corruption to read/write
// memory outside the cage or forge access to the wrong type of object.
#include <cassert>
#include <cstdint>
#include <cstdio>

#include "src/sandbox/code-pointer-table.h"
#include "src/sandbox/external-pointer-table.h"
#include "src/sandbox/sandbox.h"
#include "src/sandbox/sandboxed-pointer.h"
#include "src/sandbox/trusted-pointer-table.h"

using namespace v8;
using namespace v8::internal;

namespace {
// Analogous to real V8's BackingStore/ArrayBuffer split: the metadata
// object is deliberately kept outside the cage even though the bytes it
// describes live inside it -- exactly real V8's own design (see sandbox.h
// vendored at third_party/v8/src/sandbox/sandbox.h: "ArrayBuffer backing
// stores... the remainder of the sandbox is mostly used for memory
// buffers").
struct BufferMetadata {
  size_t length;
};

int HandlerFunction(int x) { return x * 10; }
using HandlerFn = int (*)(int);
}  // namespace

int main() {
  Sandbox sandbox;
  sandbox.Initialize(1 * 1024 * 1024);
  Sandbox::set_current(&sandbox);

  // 1. Real bytes, genuinely allocated inside the cage.
  const size_t kLen = 256;
  uint8_t* backing = static_cast<uint8_t*>(sandbox.Allocate(kLen, 8));
  for (size_t i = 0; i < kLen; ++i) backing[i] = static_cast<uint8_t>(i);
  assert(sandbox.Contains(backing));

  // The sandboxed-pointer field a real in-cage HeapObject would carry,
  // referencing the backing store by cage-relative offset.
  SandboxedPointer_t field =
      EncodeSandboxedPointer(&sandbox, reinterpret_cast<Address>(backing));

  // 2. Trusted metadata, genuinely outside the cage, reached through a
  // TrustedPointerTable handle instead of a raw pointer.
  BufferMetadata metadata{kLen};
  assert(!sandbox.Contains(&metadata));

  TrustedPointerTable tpt;
  TrustedPointerHandle metadata_handle = tpt.AllocateAndInitializeEntry(
      reinterpret_cast<Address>(&metadata), kGenericTrustedObjectTag);

  // 3. A real, callable handler reached through a CodePointerTable handle.
  CodePointerTable cpt;
  CodePointerHandle handler_handle = cpt.AllocateAndInitializeEntry(
      reinterpret_cast<Address>(&metadata),
      reinterpret_cast<Address>(&HandlerFunction));

  // --- Exercise the whole chain the way a legitimate caller would. ---

  Address recovered_backing = DecodeSandboxedPointer(&sandbox, field);
  assert(recovered_backing == reinterpret_cast<Address>(backing));
  assert(reinterpret_cast<uint8_t*>(recovered_backing)[10] == 10);

  Address recovered_metadata_addr =
      tpt.Get(metadata_handle, kGenericTrustedObjectTag);
  assert(recovered_metadata_addr == reinterpret_cast<Address>(&metadata));
  assert(reinterpret_cast<BufferMetadata*>(recovered_metadata_addr)->length ==
         kLen);

  HandlerFn fn = reinterpret_cast<HandlerFn>(cpt.GetEntrypoint(handler_handle));
  assert(fn(4) == 40);

  // --- The actual point: an attacker confined to corrupting sandboxed
  // memory cannot escape it. ---

  // A fully attacker-controlled sandboxed-pointer field still decodes to
  // some address *inside* the cage -- never to `metadata`, which lives
  // outside it.
  Address forged_decoded = DecodeSandboxedPointer(
      &sandbox, static_cast<SandboxedPointer_t>(0xFFFFFFFFu));
  assert(sandbox.Contains(forged_decoded));
  assert(forged_decoded != reinterpret_cast<Address>(&metadata));

  // A forged/wrong-tagged trusted-pointer access can't reinterpret the
  // metadata handle as if it held Code -- type confusion is rejected.
  assert(tpt.Get(metadata_handle, kCodeIndirectPointerTag) == kNullAddress);

  // A raw numeric handle value borrowed from one table is meaningless
  // against a different table: CodePointerTable has no tag to check
  // against, so its *only* defense against a foreign or corrupted handle
  // is bounds-checking -- and whatever it resolves to (kNullAddress if
  // out-of-range, or some already-legitimately-registered entrypoint if
  // the index happens to be in range) is never attacker-injected code, only
  // ever something this program itself already registered. That's CPT's
  // real, documented scope (see code-pointer-table.h's file comment): CFI,
  // not type safety.
  Address maybe_confused =
      cpt.GetEntrypoint(static_cast<CodePointerHandle>(metadata_handle));
  assert(maybe_confused == kNullAddress ||
         maybe_confused == reinterpret_cast<Address>(&HandlerFunction));

  sandbox.TearDown();
  std::printf("sandbox_integration_test: OK\n");
  return 0;
}
