// Trap-based watcher, matching mojo::SimpleWatcher's shape. whp's trap
// handler fires synchronously, possibly re-entrantly, from inside whatever
// API call changed the watched handle's signals (flagged
// MOJO_TRAP_EVENT_FLAG_WITHIN_API_CALL) -- exactly like real Mojo Core. So
// the handler here never calls the user's ReadyCallback directly; it posts
// through whp::Executor and only runs when that queue is pumped
// (RunUntilIdle()/Run()).
//
// MojoAddTrigger's `context` is *this*, indirected through WASMv8bindings'
// CppHeapPointerTable (src/sandbox/cppheap-pointer-table.h, built on
// WASMSafeSpace's ExternalEntityTable/Sandbox) rather than a bare
// `reinterpret_cast<uintptr_t>(this)` -- see simple_watcher.cc's
// WatcherHandleTable/WatcherTag. A trap event that fires (whp's trap
// dispatch is synchronous, but can still be mid-flight when a nested call
// -- e.g. ReportBadMessage's reentrant Connector::RaiseError ->
// CancelWatchers -- tears this object down) after Cancel()/~SimpleWatcher
// already freed the handle resolves through Get() to a clean nullptr
// instead of dereferencing freed memory.
#ifndef MOJO_PUBLIC_CPP_SYSTEM_SIMPLE_WATCHER_H_
#define MOJO_PUBLIC_CPP_SYSTEM_SIMPLE_WATCHER_H_

#include "mojo/public/c/system/core.h"
#include "mojo/public/cpp/system/handle.h"
#include "whp/base/repeating_callback.h"
#include "v8-sandbox.h"

#include <memory>

namespace mojo {

class SimpleWatcher {
 public:
  enum class ArmingPolicy { kAutomatic, kManual };
  using ReadyCallback = whp::RepeatingCallback<void(MojoResult)>;

  explicit SimpleWatcher(ArmingPolicy policy = ArmingPolicy::kAutomatic);
  ~SimpleWatcher();

  SimpleWatcher(const SimpleWatcher&) = delete;
  SimpleWatcher& operator=(const SimpleWatcher&) = delete;

  bool IsWatching() const { return trap_ != MOJO_HANDLE_INVALID; }

  MojoResult Watch(Handle handle,
                   MojoHandleSignals signals,
                   ReadyCallback callback);
  void Cancel();

  // Manual-policy re-arm. If a watched signal is already satisfied, returns
  // MOJO_RESULT_FAILED_PRECONDITION, stores the ready result in
  // `*ready_result` (if non-null), and schedules the callback -- it does
  // not invoke it inline.
  MojoResult Arm(MojoResult* ready_result = nullptr);

 private:
  static void OnTrapEvent(const MojoTrapEvent* event);
  void Notify(MojoResult result);

  ArmingPolicy policy_;
  MojoHandle trap_ = MOJO_HANDLE_INVALID;
  MojoHandle handle_ = MOJO_HANDLE_INVALID;
  ReadyCallback callback_;
  std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
  // Safe handle for `this`, valid only while trap_ is (see file comment).
  v8::CppHeapPointerHandle watcher_handle_ = v8::kNullCppHeapPointerHandle;
};

}  // namespace mojo

#endif  // MOJO_PUBLIC_CPP_SYSTEM_SIMPLE_WATCHER_H_
