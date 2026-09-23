// Strip-ported from V8's src/heap/cppgc-internal/heap-object-header.h.
// Near-verbatim: the bit layout, encode/decode, and atomic mark/construct
// bits are exactly upstream's. The only real deletions are the
// CPPGC_CAGED_HEAP branches (next_unfinalized_ inline linked-list field) --
// dead code on this port since CPPGC_CAGED_HEAP is never defined (see
// member-storage.h's file comment on why: no OS-reservation cage on
// wasm32-wasip1). This port's sweeper (see object-allocator.cc) tracks
// to-be-finalized objects with a plain std::vector instead of that inline
// linked list, so nothing is lost by dropping it.
#ifndef WASMV8_SRC_HEAP_INTERNAL_HEAP_OBJECT_HEADER_H_
#define WASMV8_SRC_HEAP_INTERNAL_HEAP_OBJECT_HEADER_H_

#include <stdint.h>

#include <atomic>

#include "cppgc/internal/gc-info.h"
#include "cppgc/internal/name-trait.h"
#include "src/base/bit-field.h"
#include "src/base/logging.h"
#include "src/base/macros.h"
#include "src/heap/internal/gc-info-table.h"
#include "src/heap/internal/globals.h"

namespace cppgc {

class Visitor;

namespace internal {

// HeapObjectHeader contains meta data per object and is prepended to each
// object.
//
// +-----------------+------+------------------------------------------+
// | name            | bits |                                          |
// +-----------------+------+------------------------------------------+
// | GCInfoIndex     |   14 |                                          |
// | unused          |    1 |                                          |
// | in construction |    1 | In construction encoded as |false|.      |
// +-----------------+------+------------------------------------------+
// | size            |   15 |                                          |
// | mark bit        |    1 |                                          |
// +-----------------+------+------------------------------------------+
class HeapObjectHeader {
 public:
  static constexpr size_t kSizeLog2 = 17;
  static constexpr size_t kMaxSize = (size_t{1} << kSizeLog2) - 1;
  static constexpr uint16_t kLargeObjectSizeInHeader = 0;

  inline static HeapObjectHeader& FromObject(void* address);
  inline static const HeapObjectHeader& FromObject(const void* address);

  inline HeapObjectHeader(size_t size, GCInfoIndex gc_info_index);

  inline Address ObjectStart() const;
  inline Address ObjectEnd() const;

  inline GCInfoIndex GetGCInfoIndex() const;
  inline size_t AllocatedSize() const;
  inline void SetAllocatedSize(size_t size);
  inline size_t ObjectSize() const;
  inline bool IsLargeObject() const;

  bool IsInConstruction() const;
  void MarkAsFullyConstructed();

  bool IsMarked() const;
  void Unmark();
  inline bool TryMarkAtomic();
  inline void MarkNonAtomic();

  inline bool IsFree() const;
  inline bool IsFinalizable() const;
  void Finalize();

  HeapObjectName GetName() const;
  HeapObjectName GetName(HeapObjectNameForUnnamedObject) const;

  void TraceImpl(Visitor*) const;

 private:
  enum class EncodedHalf : uint8_t { kLow, kHigh };

  using GCInfoIndexField = v8::base::BitField16<GCInfoIndex, 0, 14>;
  using UnusedField1 = GCInfoIndexField::Next<bool, 1>;
  using FullyConstructedField = UnusedField1::Next<bool, 1>;
  using MarkBitField = v8::base::BitField16<bool, 0, 1>;
  using SizeField = MarkBitField::Next<size_t, 15>;

  static constexpr size_t DecodeSize(uint16_t encoded) {
    return SizeField::decode(encoded) * kAllocationGranularity;
  }
  static constexpr uint16_t EncodeSize(size_t size) {
    return SizeField::encode(size / kAllocationGranularity);
  }

  uint16_t LoadEncoded(EncodedHalf part) const {
    return part == EncodedHalf::kLow ? encoded_low_ : encoded_high_;
  }
  void StoreEncoded(EncodedHalf part, uint16_t bits, uint16_t mask) {
    uint16_t& half = part == EncodedHalf::kLow ? encoded_low_ : encoded_high_;
    half = (half & ~mask) | bits;
  }

#if defined(V8_HOST_ARCH_64_BIT)
  uint32_t padding_ = 0;
#endif
  uint16_t encoded_high_ = 0;
  uint16_t encoded_low_ = 0;
};

static_assert(kAllocationGranularity == sizeof(HeapObjectHeader),
              "sizeof(HeapObjectHeader) must match allocation granularity to "
              "guarantee alignment");

// static
HeapObjectHeader& HeapObjectHeader::FromObject(void* object) {
  return *reinterpret_cast<HeapObjectHeader*>(static_cast<Address>(object) -
                                              sizeof(HeapObjectHeader));
}
// static
const HeapObjectHeader& HeapObjectHeader::FromObject(const void* object) {
  return *reinterpret_cast<const HeapObjectHeader*>(
      static_cast<ConstAddress>(object) - sizeof(HeapObjectHeader));
}

HeapObjectHeader::HeapObjectHeader(size_t size, GCInfoIndex gc_info_index) {
#if defined(V8_HOST_ARCH_64_BIT)
  USE(padding_);
#endif
  DCHECK_LT(gc_info_index, kMaxGCInfoIndex);
  DCHECK_EQ(0u, size & (sizeof(HeapObjectHeader) - 1));
  DCHECK_GE(kMaxSize, size);
  encoded_low_ = EncodeSize(size);
  encoded_high_ = GCInfoIndexField::encode(gc_info_index);
  DCHECK(IsInConstruction());
}

Address HeapObjectHeader::ObjectStart() const {
  return reinterpret_cast<Address>(const_cast<HeapObjectHeader*>(this)) +
         sizeof(HeapObjectHeader);
}
Address HeapObjectHeader::ObjectEnd() const {
  DCHECK(!IsLargeObject());
  return reinterpret_cast<Address>(const_cast<HeapObjectHeader*>(this)) +
         AllocatedSize();
}

GCInfoIndex HeapObjectHeader::GetGCInfoIndex() const {
  return GCInfoIndexField::decode(LoadEncoded(EncodedHalf::kHigh));
}
size_t HeapObjectHeader::AllocatedSize() const {
  return DecodeSize(LoadEncoded(EncodedHalf::kLow));
}
void HeapObjectHeader::SetAllocatedSize(size_t size) {
  DCHECK(!IsMarked());
  encoded_low_ &= ~SizeField::encode(SizeField::kMax);
  encoded_low_ |= EncodeSize(size);
}
size_t HeapObjectHeader::ObjectSize() const {
  DCHECK_GT(AllocatedSize(), sizeof(HeapObjectHeader));
  return AllocatedSize() - sizeof(HeapObjectHeader);
}
bool HeapObjectHeader::IsLargeObject() const {
  return AllocatedSize() == kLargeObjectSizeInHeader;
}

inline bool HeapObjectHeader::IsMarked() const {
  return MarkBitField::decode(LoadEncoded(EncodedHalf::kLow));
}
inline void HeapObjectHeader::Unmark() {
  DCHECK(IsMarked());
  StoreEncoded(EncodedHalf::kLow, MarkBitField::encode(false),
               MarkBitField::kMask);
}
bool HeapObjectHeader::TryMarkAtomic() {
  if (IsMarked()) return false;
  StoreEncoded(EncodedHalf::kLow, MarkBitField::encode(true),
               MarkBitField::kMask);
  return true;
}
void HeapObjectHeader::MarkNonAtomic() {
  DCHECK(!IsMarked());
  encoded_low_ |= MarkBitField::encode(true);
}

bool HeapObjectHeader::IsFree() const {
  return GetGCInfoIndex() == kFreeListGCInfoIndex;
}
bool HeapObjectHeader::IsFinalizable() const {
  const GCInfo& gc_info = GlobalGCInfoTable::GCInfoFromIndex(GetGCInfoIndex());
  return gc_info.finalize;
}

inline bool HeapObjectHeader::IsInConstruction() const {
  return !FullyConstructedField::decode(LoadEncoded(EncodedHalf::kHigh));
}
inline void HeapObjectHeader::MarkAsFullyConstructed() {
  StoreEncoded(EncodedHalf::kHigh, FullyConstructedField::encode(true),
               FullyConstructedField::kMask);
}
inline void HeapObjectHeader::Finalize() {
  const GCInfo& gc_info = GlobalGCInfoTable::GCInfoFromIndex(GetGCInfoIndex());
  if (gc_info.finalize) {
    gc_info.finalize(ObjectStart());
  }
}
inline HeapObjectName HeapObjectHeader::GetName(
    HeapObjectNameForUnnamedObject mode) const {
  const GCInfo& gc_info = GlobalGCInfoTable::GCInfoFromIndex(GetGCInfoIndex());
  return gc_info.name(ObjectStart(), mode);
}
inline HeapObjectName HeapObjectHeader::GetName() const {
  return GetName(HeapObjectNameForUnnamedObject::kUseClassNameIfSupported);
}

inline void HeapObjectHeader::TraceImpl(Visitor* visitor) const {
  const GCInfo& gc_info = GlobalGCInfoTable::GCInfoFromIndex(GetGCInfoIndex());
  return gc_info.trace(visitor, ObjectStart());
}

}  // namespace internal
}  // namespace cppgc

#endif  // WASMV8_SRC_HEAP_INTERNAL_HEAP_OBJECT_HEADER_H_
