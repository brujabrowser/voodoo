#include "mojo/public/cpp/system/simple_watcher.h"

#include "src/sandbox/cppheap-pointer-table.h"
#include "whp/base/executor.h"

namespace mojo {

namespace {

// One table/tag for every SimpleWatcher in this process -- not tied to any
// v8::Isolate (SimpleWatcher is usable with no JS engine at all), just
// reused as the generic safe-pointer-indirection utility it already is.
// See simple_watcher.h's file comment.
cppgc::internal::CppHeapPointerTable& WatcherHandleTable() {
  static cppgc::internal::CppHeapPointerTable table;
  return table;
}

v8::CppHeapPointerTag WatcherTag() {
  static v8::CppHeapPointerTag tag = v8::AllocateCppHeapPointerTag();
  return tag;
}

}  // namespace

SimpleWatcher::SimpleWatcher(ArmingPolicy policy) : policy_(policy) {}

SimpleWatcher::~SimpleWatcher() {
  *alive_ = false;
  Cancel();
}

MojoResult SimpleWatcher::Watch(Handle handle,
                                MojoHandleSignals signals,
                                ReadyCallback callback) {
  Cancel();
  callback_ = std::move(callback);
  handle_ = handle.value();

  MojoResult result = MojoCreateTrap(&SimpleWatcher::OnTrapEvent, nullptr, &trap_);
  if (result != MOJO_RESULT_OK) {
    handle_ = MOJO_HANDLE_INVALID;
    return result;
  }

  watcher_handle_ = WatcherHandleTable().AllocateAndInitializeEntry(this, WatcherTag());
  result = MojoAddTrigger(trap_, handle_, signals,
                          MOJO_TRIGGER_CONDITION_SIGNALS_SATISFIED,
                          static_cast<uintptr_t>(watcher_handle_), nullptr);
  if (result != MOJO_RESULT_OK) {
    WatcherHandleTable().FreeEntry(watcher_handle_);
    watcher_handle_ = v8::kNullCppHeapPointerHandle;
    MojoClose(trap_);
    trap_ = MOJO_HANDLE_INVALID;
    handle_ = MOJO_HANDLE_INVALID;
    return result;
  }

  if (policy_ == ArmingPolicy::kAutomatic) {
    Arm(nullptr);
  }
  return MOJO_RESULT_OK;
}

void SimpleWatcher::Cancel() {
  if (trap_ != MOJO_HANDLE_INVALID) {
    // Free the handle *before* closing the trap: MojoClose can fire a
    // final synchronous trap event (see OnTrapEvent) for triggers it's
    // tearing down, and that must already see this handle as gone, not
    // resolve back to a `this` that Cancel()'s caller (often
    // ~SimpleWatcher) is in the middle of unwinding.
    if (watcher_handle_ != v8::kNullCppHeapPointerHandle) {
      WatcherHandleTable().FreeEntry(watcher_handle_);
      watcher_handle_ = v8::kNullCppHeapPointerHandle;
    }
    MojoClose(trap_);
    trap_ = MOJO_HANDLE_INVALID;
  }
  handle_ = MOJO_HANDLE_INVALID;
  callback_ = nullptr;
}

MojoResult SimpleWatcher::Arm(MojoResult* ready_result) {
  if (trap_ == MOJO_HANDLE_INVALID) {
    return MOJO_RESULT_FAILED_PRECONDITION;
  }
  MojoTrapEvent blocking{};
  blocking.struct_size = sizeof(blocking);
  uint32_t num_blocking = 1;
  MojoResult result = MojoArmTrap(trap_, nullptr, &num_blocking, &blocking);
  if (result == MOJO_RESULT_FAILED_PRECONDITION && num_blocking > 0) {
    if (ready_result) {
      *ready_result = blocking.result;
    }
    Notify(blocking.result);
  }
  return result;
}

void SimpleWatcher::OnTrapEvent(const MojoTrapEvent* event) {
  void* raw = WatcherHandleTable().Get(
      static_cast<v8::CppHeapPointerHandle>(event->trigger_context), WatcherTag());
  if (!raw) {
    // Handle already freed (Cancel()/~SimpleWatcher ran) -- this trap event
    // was in flight when its watcher was torn down. Safe no-op instead of
    // the use-after-free a raw trigger_context pointer would have been.
    return;
  }
  static_cast<SimpleWatcher*>(raw)->Notify(event->result);
}

void SimpleWatcher::Notify(MojoResult result) {
  // whp's trap handler fires synchronously (possibly nested inside the
  // call that changed the watched signal), so never invoke callback_ here
  // directly -- post through the executor instead. Snapshot the callback
  // and the trap identity now: if Cancel()/Watch() runs before this task is
  // pumped, trap_ will differ (or be invalid) and this stale event is
  // dropped instead of firing a since-replaced callback.
  std::shared_ptr<bool> alive = alive_;
  MojoHandle trap_at_post = trap_;
  bool automatic_rearm = policy_ == ArmingPolicy::kAutomatic;
  SimpleWatcher* self = this;
  whp::Executor::Current().PostTask(whp::BindOnce(
      [self, alive, trap_at_post, callback = callback_,
       automatic_rearm](MojoResult result) {
        if (!*alive || self->trap_ != trap_at_post) {
          return;
        }
        if (callback) {
          callback(result);
        }
        if (*alive && automatic_rearm && self->trap_ == trap_at_post) {
          self->Arm(nullptr);
        }
      },
      result));
}

}  // namespace mojo
