// Adapted from V8's include/cppgc/internal/persistent-node.h.
//
// Ported near-verbatim (PersistentNode, PersistentRegionBase, PersistentRegion):
// the freelist-of-256-node-slabs design has no OS/threading dependency at all.
// Dropped: CrossThreadPersistentRegion and PersistentRegionLock -- this port
// is single-heap, single-thread (matches go++'s own wasm32-wasip1 target;
// see the top-level README), so there is no cross-thread persistent handle
// to protect with a lock. TraceRootCallback also collapses to
// `void(*)(Visitor&, const void*)`: real cppgc uses a distinct RootVisitor
// here because conservative stack scanning treats roots differently from
// on-heap edges. This port is precise-only (no stack scanning at all -- see
// heap.h), so a Persistent<T> root and a Member<T> edge are traced exactly
// the same way, and one Visitor interface covers both.
#ifndef WASMV8_INCLUDE_CPPGC_INTERNAL_PERSISTENT_NODE_H_
#define WASMV8_INCLUDE_CPPGC_INTERNAL_PERSISTENT_NODE_H_

#include <array>
#include <memory>
#include <vector>

#include "cppgc/internal/logging.h"
#include "v8config.h"

namespace cppgc {

class Visitor;

namespace internal {

using TraceRootCallback = void (*)(Visitor&, const void* object);

class PersistentNode final {
 public:
  PersistentNode() = default;
  PersistentNode(const PersistentNode&) = delete;
  PersistentNode& operator=(const PersistentNode&) = delete;

  void InitializeAsUsedNode(void* owner, TraceRootCallback trace) {
    CPPGC_DCHECK(trace);
    owner_ = owner;
    trace_ = trace;
  }
  void InitializeAsFreeNode(PersistentNode* next) {
    next_ = next;
    trace_ = nullptr;
  }
  void UpdateOwner(void* owner) {
    CPPGC_DCHECK(IsUsed());
    owner_ = owner;
  }
  PersistentNode* FreeListNext() const {
    CPPGC_DCHECK(!IsUsed());
    return next_;
  }
  void Trace(Visitor& visitor) const {
    CPPGC_DCHECK(IsUsed());
    trace_(visitor, owner_);
  }
  bool IsUsed() const { return trace_; }
  void* owner() const {
    CPPGC_DCHECK(IsUsed());
    return owner_;
  }

 private:
  union {
    void* owner_ = nullptr;
    PersistentNode* next_;
  };
  TraceRootCallback trace_ = nullptr;
};

class V8_EXPORT PersistentRegion {
  using PersistentNodeSlots = std::array<PersistentNode, 256u>;

 public:
  PersistentRegion() = default;
  ~PersistentRegion() { ClearAllUsedNodes(); }
  PersistentRegion(const PersistentRegion&) = delete;
  PersistentRegion& operator=(const PersistentRegion&) = delete;

  V8_INLINE PersistentNode* AllocateNode(void* owner, TraceRootCallback trace) {
    auto* node = TryAllocateNodeFromFreeList(owner, trace);
    if (V8_LIKELY(node)) return node;
    return RefillFreeListAndAllocateNode(owner, trace);
  }

  V8_INLINE void FreeNode(PersistentNode* node) {
    CPPGC_DCHECK(node);
    CPPGC_DCHECK(node->IsUsed());
    node->InitializeAsFreeNode(free_list_head_);
    free_list_head_ = node;
    CPPGC_DCHECK(nodes_in_use_ > 0);
    nodes_in_use_--;
  }

  // Traces every used node's owner as a GC root, then rebuilds the free
  // list (dropping freed slabs) -- called once per atomic GC pause.
  void Iterate(Visitor& visitor) {
    free_list_head_ = nullptr;
    for (auto& slots : nodes_) {
      for (auto& node : *slots) {
        if (node.IsUsed()) {
          node.Trace(visitor);
        } else {
          node.InitializeAsFreeNode(free_list_head_);
          free_list_head_ = &node;
        }
      }
    }
  }

  size_t NodesInUse() const { return nodes_in_use_; }

  void ClearAllUsedNodes() {
    for (auto& slots : nodes_) {
      for (auto& node : *slots) {
        if (node.IsUsed()) {
          node.InitializeAsFreeNode(free_list_head_);
          free_list_head_ = &node;
          nodes_in_use_--;
        }
      }
    }
  }

 private:
  PersistentNode* TryAllocateNodeFromFreeList(void* owner,
                                              TraceRootCallback trace) {
    PersistentNode* node = nullptr;
    if (V8_LIKELY(free_list_head_)) {
      node = free_list_head_;
      free_list_head_ = free_list_head_->FreeListNext();
      node->InitializeAsUsedNode(owner, trace);
      nodes_in_use_++;
    }
    return node;
  }

  PersistentNode* RefillFreeListAndAllocateNode(void* owner,
                                                TraceRootCallback trace) {
    auto node_slots = std::make_unique<PersistentNodeSlots>();
    nodes_.push_back(std::move(node_slots));
    for (auto& node : *nodes_.back()) {
      node.InitializeAsFreeNode(free_list_head_);
      free_list_head_ = &node;
    }
    auto* node = TryAllocateNodeFromFreeList(owner, trace);
    CPPGC_DCHECK(node);
    return node;
  }

  std::vector<std::unique_ptr<PersistentNodeSlots>> nodes_;
  PersistentNode* free_list_head_ = nullptr;
  size_t nodes_in_use_ = 0;
};

}  // namespace internal
}  // namespace cppgc

#endif  // WASMV8_INCLUDE_CPPGC_INTERNAL_PERSISTENT_NODE_H_
