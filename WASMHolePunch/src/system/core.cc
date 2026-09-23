#include "whp/c/system.h"

#include "whp/base/executor.h"
#include "whp/net/endpoint.h"

#include "src/sandbox/cage-allocator.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <sys/mman.h>
#include <unistd.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace {

// Lazily creates/activates the one process-wide Sandbox if nothing else
// already has (same pattern WASMCadidumBindings/src/lib/type_intern.cc's
// own Cage() uses for the same reason -- whichever of the two runs
// first wins and the other just reuses it via Sandbox::current(), since
// both ultimately read/write the same static slot). Not called from
// this repo's own CMake sibling on wcb (that would invert the
// wst/wck/wcb dependency direction) -- whp owns its own copy of this
// exact pattern instead.
v8::internal::Sandbox& Cage() {
  if (v8::internal::Sandbox* c = v8::internal::Sandbox::current()) {
    if (!c->is_initialized()) {
      c->Initialize();
    }
    return *c;
  }
  // Deliberately leaked (never `delete`d), not a function-local `static
  // Sandbox owned;` -- that destructs at process exit in an order the
  // standard leaves unspecified relative to Core::Get()'s own static
  // `Core c` in this same TU (whichever was *constructed* second
  // destructs *first*). Core's message queues can still hold
  // CageBytes-backed MessageObjs at exit; if this Sandbox tears down
  // first, CageAllocator::deallocate()'s Sandbox::current() check goes
  // null and it wild-frees a cage pointer via plain ::operator delete --
  // real heap corruption (0xc0000374), observed exactly this way. A
  // process-lifetime singleton that outlives every possible user is the
  // standard fix, same tradeoff every long-lived C++ singleton with this
  // hazard makes.
  static v8::internal::Sandbox& owned = *new v8::internal::Sandbox();
  if (!owned.is_initialized()) {
    owned.Initialize();
    v8::internal::Sandbox::set_current(&owned);
  }
  return owned;
}

enum class HandleKind {
  Free = 0,
  MessagePipe,
  Message,
  Trap,
  DataPipeProducer,
  DataPipeConsumer,
  SharedBuffer,
  Platform,
};

struct PlatformObj {
  WhpPlatformHandleType type = WHP_PLATFORM_HANDLE_TYPE_INVALID;
  uint64_t value = 0;
  bool owns = true;
};

void CloseNativePlatform(WhpPlatformHandleType type, uint64_t value) {
  if (type == WHP_PLATFORM_HANDLE_TYPE_WINDOWS_HANDLE) {
#ifdef _WIN32
    HANDLE h = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(value));
    if (h && h != INVALID_HANDLE_VALUE) {
      CloseHandle(h);
    }
#endif
  } else if (type == WHP_PLATFORM_HANDLE_TYPE_FILE_DESCRIPTOR) {
#ifndef _WIN32
    int fd = static_cast<int>(value);
    if (fd >= 0) {
      close(fd);
    }
#endif
  } else if (type == WHP_PLATFORM_HANDLE_TYPE_MACH_PORT) {
#if defined(__APPLE__)
    mach_port_deallocate(mach_task_self(), static_cast<mach_port_t>(value));
#else
    (void)value;
#endif
  }
}

bool PlatformTypeOkForHost(WhpPlatformHandleType type) {
  if (type == WHP_PLATFORM_HANDLE_TYPE_MACH_PORT) {
    return true;
  }
#ifdef _WIN32
  return type == WHP_PLATFORM_HANDLE_TYPE_WINDOWS_HANDLE;
#else
  return type == WHP_PLATFORM_HANDLE_TYPE_FILE_DESCRIPTOR;
#endif
}

// Cage-backed, not a plain std::vector: this is the actual per-message
// wire-byte buffer (header + payload) every WhpReadMessage/WhpWriteMessage
// round trip allocates and frees. See cage-allocator.h's file comment --
// this is exactly the high-churn buffer it exists for.
using MessageBytes = v8::internal::CageBytes;

struct MessageObj {
  MessageBytes bytes;
  std::vector<WhpHandle> handles;
  bool committed = false;
  uintptr_t context = 0;
  WhpMessageContextSerializer serializer = nullptr;
  WhpMessageContextDestructor destructor = nullptr;
};

struct PipeState;

struct PipeEnd {
  std::shared_ptr<PipeState> state;
  int end = 0;
};

struct PipeState {
  std::deque<std::shared_ptr<MessageObj>> queues[2];
  bool closed[2] = {false, false};
  uint64_t quota_count[2] = {~uint64_t{0}, ~uint64_t{0}};
  uint64_t quota_bytes[2] = {~uint64_t{0}, ~uint64_t{0}};
};

uint64_t QueueBytes(const std::deque<std::shared_ptr<MessageObj>>& q) {
  uint64_t n = 0;
  for (const auto& m : q) {
    n += m->bytes.size();
  }
  return n;
}

struct TrapTrigger {
  WhpHandle watched = WHP_HANDLE_INVALID;
  WhpHandleSignals signals = 0;
  uint32_t condition = WHP_TRIGGER_CONDITION_SIGNALS_SATISFIED;
  uintptr_t context = 0;
};

struct TrapObj {
  WhpTrapEventHandler handler = nullptr;
  std::vector<TrapTrigger> triggers;
  bool armed = false;
};

struct DataPipeState {
  uint32_t element_size = 1;
  uint32_t capacity = 65536;
  std::vector<uint8_t> buf;
  uint32_t r = 0;
  uint32_t w = 0;
  uint32_t available = 0;
  bool producer_closed = false;
  bool consumer_closed = false;
  void* two_phase_write = nullptr;
  uint32_t two_phase_write_max = 0;
  const void* two_phase_read = nullptr;
  uint32_t two_phase_read_max = 0;
};

struct DataPipeEnd {
  std::shared_ptr<DataPipeState> state;
  bool producer = true;
};

struct SharedBufferObj {
  std::vector<uint8_t> bytes;
  uint64_t size = 0;
  WhpPlatformHandleType plat_types[2]{};
  uint64_t plat_values[2]{};
  uint32_t plat_count = 0;
  bool plat_owns = false;
  uint64_t guid_high = 0;
  uint64_t guid_low = 0;
  uint32_t access_mode = 0;
  void* os_base = nullptr;
  uint64_t os_mapped_size = 0;

  void UnmapOs() {
    if (!os_base) {
      return;
    }
#ifdef _WIN32
    UnmapViewOfFile(os_base);
#else
    munmap(os_base, static_cast<size_t>(os_mapped_size));
#endif
    os_base = nullptr;
    os_mapped_size = 0;
  }

  ~SharedBufferObj() {
    UnmapOs();
    if (plat_owns) {
      for (uint32_t i = 0; i < plat_count; ++i) {
        CloseNativePlatform(plat_types[i], plat_values[i]);
      }
      plat_owns = false;
    }
  }
};

bool EnsureMapped(SharedBufferObj* buf) {
  if (buf->os_base || !buf->bytes.empty()) {
    return true;
  }
  if (buf->plat_count == 0 || buf->size == 0) {
    return false;
  }
  if (buf->plat_types[0] == WHP_PLATFORM_HANDLE_TYPE_MACH_PORT) {
    buf->bytes.resize(static_cast<size_t>(buf->size));
    return true;
  }
#ifdef _WIN32
  if (buf->plat_types[0] != WHP_PLATFORM_HANDLE_TYPE_WINDOWS_HANDLE) {
    return false;
  }
  HANDLE h = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(buf->plat_values[0]));
  DWORD access = FILE_MAP_READ;
  if (buf->access_mode != WHP_PLATFORM_SHARED_MEMORY_REGION_ACCESS_MODE_READ_ONLY) {
    access = FILE_MAP_ALL_ACCESS;
  }
  void* view = MapViewOfFile(h, access, 0, 0, static_cast<size_t>(buf->size));
  if (!view) {
    return false;
  }
  buf->os_base = view;
  buf->os_mapped_size = buf->size;
  return true;
#else
  if (buf->plat_types[0] != WHP_PLATFORM_HANDLE_TYPE_FILE_DESCRIPTOR) {
    return false;
  }
  int fd = static_cast<int>(buf->plat_values[0]);
  int prot = PROT_READ;
  if (buf->access_mode != WHP_PLATFORM_SHARED_MEMORY_REGION_ACCESS_MODE_READ_ONLY) {
    prot |= PROT_WRITE;
  }
  void* view = mmap(nullptr, static_cast<size_t>(buf->size), prot, MAP_SHARED, fd, 0);
  if (view == MAP_FAILED) {
    return false;
  }
  buf->os_base = view;
  buf->os_mapped_size = buf->size;
  return true;
#endif
}

struct MappedRegion {
  std::shared_ptr<SharedBufferObj> buf;
  int refs = 0;
};

struct Slot {
  HandleKind kind = HandleKind::Free;
  std::shared_ptr<void> obj;
};

struct PendingEvent {
  WhpTrapEventHandler handler;
  WhpTrapEvent event;
};

class Core {
 public:
  static Core& Get() {
    static Core c;
    return c;
  }

  std::mutex mu;
  std::vector<Slot> slots;  // [0] unused
  std::vector<WhpHandle> free_list;
  std::vector<WhpHandle> traps;
  std::unordered_map<void*, MappedRegion> maps;
  bool inited = false;
  // Async bridge queue (see WhpPumpEvents' file comment): trap handlers
  // fire only from here, never synchronously nested inside whatever call
  // mutated state -- same completion-queue discipline
  // wasigocvm_net.hpp's WasigocvmNetBridge uses (Submit/PollOne/WaitOne),
  // just with the fixed WhpTrapEventHandler shape instead of a generic
  // topic/payload one.
  std::deque<PendingEvent> pending_events;

  Core() { slots.resize(1); }

  WhpHandle Alloc(HandleKind kind, std::shared_ptr<void> obj) {
    WhpHandle h;
    if (!free_list.empty()) {
      h = free_list.back();
      free_list.pop_back();
      slots[h].kind = kind;
      slots[h].obj = std::move(obj);
    } else {
      h = static_cast<WhpHandle>(slots.size());
      slots.push_back(Slot{kind, std::move(obj)});
    }
    return h;
  }

  Slot* Lookup(WhpHandle h) {
    if (h == 0 || h >= slots.size() || slots[h].kind == HandleKind::Free) {
      return nullptr;
    }
    return &slots[h];
  }

  void Free(WhpHandle h) {
    if (h == 0 || h >= slots.size()) {
      return;
    }
    slots[h].kind = HandleKind::Free;
    slots[h].obj.reset();
    free_list.push_back(h);
  }
};

WhpHandleSignalsState PipeSignals(const PipeEnd& end) {
  WhpHandleSignalsState st{};
  const int me = end.end;
  const int peer = 1 - me;
  const bool peer_closed = end.state->closed[peer];
  const bool have = !end.state->queues[me].empty();
  if (have) {
    st.satisfied_signals |= WHP_HANDLE_SIGNAL_READABLE;
  }
  if (!peer_closed) {
    st.satisfied_signals |= WHP_HANDLE_SIGNAL_WRITABLE;
    st.satisfiable_signals |= WHP_HANDLE_SIGNAL_WRITABLE;
    st.satisfiable_signals |= WHP_HANDLE_SIGNAL_READABLE;
    st.satisfiable_signals |= WHP_HANDLE_SIGNAL_PEER_CLOSED;
  } else {
    st.satisfied_signals |= WHP_HANDLE_SIGNAL_PEER_CLOSED;
    st.satisfiable_signals |= WHP_HANDLE_SIGNAL_PEER_CLOSED;
    if (have) {
      st.satisfiable_signals |= WHP_HANDLE_SIGNAL_READABLE;
    }
  }
  return st;
}

WhpHandleSignalsState DataPipeSignals(const DataPipeEnd& end) {
  WhpHandleSignalsState st{};
  auto& s = *end.state;
  if (end.producer) {
    if (!s.consumer_closed && s.available < s.capacity) {
      st.satisfied_signals |= WHP_HANDLE_SIGNAL_WRITABLE;
    }
    if (!s.consumer_closed) {
      st.satisfiable_signals |= WHP_HANDLE_SIGNAL_WRITABLE;
      st.satisfiable_signals |= WHP_HANDLE_SIGNAL_PEER_CLOSED;
    } else {
      st.satisfied_signals |= WHP_HANDLE_SIGNAL_PEER_CLOSED;
      st.satisfiable_signals |= WHP_HANDLE_SIGNAL_PEER_CLOSED;
    }
  } else {
    if (s.available > 0) {
      st.satisfied_signals |= WHP_HANDLE_SIGNAL_READABLE;
    }
    if (!s.producer_closed) {
      st.satisfiable_signals |= WHP_HANDLE_SIGNAL_READABLE;
      st.satisfiable_signals |= WHP_HANDLE_SIGNAL_PEER_CLOSED;
    } else {
      st.satisfied_signals |= WHP_HANDLE_SIGNAL_PEER_CLOSED;
      st.satisfiable_signals |= WHP_HANDLE_SIGNAL_PEER_CLOSED;
      if (s.available > 0) {
        st.satisfiable_signals |= WHP_HANDLE_SIGNAL_READABLE;
      }
    }
  }
  return st;
}

bool SignalsMatch(const WhpHandleSignalsState& st,
                  WhpHandleSignals want,
                  uint32_t condition) {
  if (condition == WHP_TRIGGER_CONDITION_SIGNALS_UNSATISFIABLE) {
    return (st.satisfiable_signals & want) == 0;
  }
  return (st.satisfied_signals & want) == want;
}

WhpHandleSignalsState QueryLocked(Core& c, WhpHandle h) {
  WhpHandleSignalsState st{};
  Slot* slot = c.Lookup(h);
  if (!slot) {
    return st;
  }
  if (slot->kind == HandleKind::MessagePipe) {
    auto* end = static_cast<PipeEnd*>(slot->obj.get());
    return PipeSignals(*end);
  }
  if (slot->kind == HandleKind::DataPipeProducer ||
      slot->kind == HandleKind::DataPipeConsumer) {
    auto* end = static_cast<DataPipeEnd*>(slot->obj.get());
    return DataPipeSignals(*end);
  }
  return st;
}

// Appends to c.pending_events -- never fires anything itself. Called from
// every state-mutating WhpXxx function (write/close/etc.), always while
// c.mu is held, same as before. What changed: it used to be paired with
// an immediate (if unlocked) FireTraps() call right after; now delivery
// is entirely WhpPumpEvents' job, so a mutation is never on the call
// stack when a handler runs. See WhpPumpEvents' own comment.
void CollectTrapEvents(Core& c) {
  for (WhpHandle th : c.traps) {
    Slot* slot = c.Lookup(th);
    if (!slot || slot->kind != HandleKind::Trap) {
      continue;
    }
    auto* trap = static_cast<TrapObj*>(slot->obj.get());
    if (!trap->armed || !trap->handler) {
      continue;
    }
    for (const auto& trig : trap->triggers) {
      WhpHandleSignalsState st = QueryLocked(c, trig.watched);
      if (!SignalsMatch(st, trig.signals, trig.condition)) {
        continue;
      }
      trap->armed = false;
      WhpTrapEvent ev{};
      ev.struct_size = sizeof(ev);
      ev.flags = 0;  // never WITHIN_API_CALL anymore -- see WhpPumpEvents.
      ev.trigger_context = trig.context;
      ev.result = WHP_RESULT_OK;
      if (trig.condition == WHP_TRIGGER_CONDITION_SIGNALS_UNSATISFIABLE ||
          (st.satisfiable_signals & trig.signals) == 0) {
        ev.result = WHP_RESULT_FAILED_PRECONDITION;
      }
      ev.signals_state = st;
      c.pending_events.push_back(PendingEvent{trap->handler, ev});
      break;
    }
  }
}

}  // namespace

extern "C" {

WhpResult WhpInit(void) {
  Core::Get().inited = true;
  // Ensures the cage MessageObj::bytes allocates through (see
  // cage-allocator.h) actually exists before the first message does,
  // rather than relying on some other consumer (e.g. wcb's type
  // interning) happening to have touched Sandbox::current() first.
  Cage();
  // whp::Executor::IdleSource is `void(*)()` (whp/base doesn't know
  // WhpResult -- see executor.h's file comment); this trivial wrapper is
  // the seam. Registered on every WhpInit() call, not just the first --
  // idempotent (SetIdleSource just overwrites the same function pointer)
  // and keeps this from depending on init-order versus whp::Executor's
  // own static singleton.
  whp::Executor::Current().SetIdleSource(+[] { WhpPumpEvents(); });
  return WHP_RESULT_OK;
}

void WhpShutdown(void) {}

// The async bridge's pump: drains c.pending_events and fires each handler,
// entirely outside c.mu and never nested inside whatever WhpXxx call
// mutated the state that made the trigger fire (compare
// wasigocvm_net.hpp's WasigocvmNetBridge::PollOne/WaitOne -- same
// "mutations queue, a separate pump delivers" split). A caller drives
// this the same way it already drives whp::Executor::RunUntilIdle()/Run()
// -- in fact those now call it automatically on every iteration (see
// executor.cc), so existing "just call RunUntilIdle() until things
// settle" call sites across wck/wcb/wst and every consumer need no
// changes to keep working.
WhpResult WhpPumpEvents(void) {
  Core& c = Core::Get();
  std::vector<PendingEvent> events;
  {
    std::lock_guard<std::mutex> lock(c.mu);
    if (c.pending_events.empty()) {
      return WHP_RESULT_OK;
    }
    events.assign(c.pending_events.begin(), c.pending_events.end());
    c.pending_events.clear();
  }
  for (auto& e : events) {
    e.handler(&e.event);
  }
  return WHP_RESULT_OK;
}

WhpResult WhpClose(WhpHandle handle) {
  Core& c = Core::Get();
  WhpMessageContextDestructor msg_dtor = nullptr;
  uintptr_t msg_ctx = 0;
  {
    std::lock_guard<std::mutex> lock(c.mu);
    Slot* slot = c.Lookup(handle);
    if (!slot) {
      return WHP_RESULT_INVALID_ARGUMENT;
    }
    if (slot->kind == HandleKind::MessagePipe) {
      auto* end = static_cast<PipeEnd*>(slot->obj.get());
      end->state->closed[end->end] = true;
    } else if (slot->kind == HandleKind::DataPipeProducer) {
      auto* end = static_cast<DataPipeEnd*>(slot->obj.get());
      end->state->producer_closed = true;
    } else if (slot->kind == HandleKind::DataPipeConsumer) {
      auto* end = static_cast<DataPipeEnd*>(slot->obj.get());
      end->state->consumer_closed = true;
    } else if (slot->kind == HandleKind::Trap) {
      // A trigger's `context` may already be sitting in c.pending_events
      // (CollectTrapEvents queued it, but WhpPumpEvents hasn't drained it
      // yet) at the moment this trap closes. If left there, that stale
      // PendingEvent survives this trap's teardown and fires later via
      // SimpleWatcher::OnTrapEvent -- and since `context` is a
      // CppHeapPointerTable handle (see simple_watcher.cc's
      // WatcherHandleTable/WatcherTag), a handle number is only as safe as
      // its *current* occupant: by the time the queued event finally
      // drains, FreeEntry+reuse may have handed that same handle number to
      // an unrelated, still-alive SimpleWatcher, and OnTrapEvent has no way
      // to tell "stale event for a torn-down watcher" apart from "fresh
      // event for whoever holds this handle now". It resolves through
      // Get() and fires Notify() on that unrelated watcher instead --
      // exactly the false PEER_CLOSED that made receiver.Pause()/Resume()
      // spuriously reset the pipe. Purging every pending event tied to
      // this trap's own triggers here, while contexts are still known and
      // before the handle can be reused, closes that window.
      auto* tobj = static_cast<TrapObj*>(slot->obj.get());
      if (!c.pending_events.empty() && !tobj->triggers.empty()) {
        std::vector<uintptr_t> contexts;
        contexts.reserve(tobj->triggers.size());
        for (const auto& trig : tobj->triggers) {
          contexts.push_back(trig.context);
        }
        c.pending_events.erase(
            std::remove_if(c.pending_events.begin(), c.pending_events.end(),
                           [&](const PendingEvent& e) {
                             return std::find(contexts.begin(), contexts.end(),
                                              e.event.trigger_context) !=
                                    contexts.end();
                           }),
            c.pending_events.end());
      }
      c.traps.erase(std::remove(c.traps.begin(), c.traps.end(), handle),
                    c.traps.end());
    } else if (slot->kind == HandleKind::Message) {
      auto* msg = static_cast<MessageObj*>(slot->obj.get());
      msg_dtor = msg->destructor;
      msg_ctx = msg->context;
      msg->destructor = nullptr;
      msg->serializer = nullptr;
    } else if (slot->kind == HandleKind::Platform) {
      auto* p = static_cast<PlatformObj*>(slot->obj.get());
      if (p->owns) {
        CloseNativePlatform(p->type, p->value);
        p->owns = false;
      }
    }
    c.Free(handle);
    CollectTrapEvents(c);
  }
  if (msg_dtor) {
    msg_dtor(msg_ctx);
  }
  return WHP_RESULT_OK;
}

WhpResult WhpQueryHandleSignalsState(WhpHandle handle,
                                     WhpHandleSignalsState* state) {
  if (!state) {
    return WHP_RESULT_INVALID_ARGUMENT;
  }
  Core& c = Core::Get();
  std::lock_guard<std::mutex> lock(c.mu);
  if (!c.Lookup(handle)) {
    return WHP_RESULT_INVALID_ARGUMENT;
  }
  *state = QueryLocked(c, handle);
  return WHP_RESULT_OK;
}

WhpResult WhpCreateMessagePipe(const WhpCreateMessagePipeOptions*,
                               WhpHandle* handle0,
                               WhpHandle* handle1) {
  if (!handle0 || !handle1) {
    return WHP_RESULT_INVALID_ARGUMENT;
  }
  Core& c = Core::Get();
  std::lock_guard<std::mutex> lock(c.mu);
  auto state = std::make_shared<PipeState>();
  auto e0 = std::make_shared<PipeEnd>(PipeEnd{state, 0});
  auto e1 = std::make_shared<PipeEnd>(PipeEnd{state, 1});
  *handle0 = c.Alloc(HandleKind::MessagePipe, e0);
  *handle1 = c.Alloc(HandleKind::MessagePipe, e1);
  return WHP_RESULT_OK;
}

WhpResult WhpCreateMessage(const WhpCreateMessageOptions*,
                           WhpMessageHandle* message) {
  if (!message) {
    return WHP_RESULT_INVALID_ARGUMENT;
  }
  Core& c = Core::Get();
  std::lock_guard<std::mutex> lock(c.mu);
  auto obj = std::make_shared<MessageObj>();
  *message = c.Alloc(HandleKind::Message, obj);
  return WHP_RESULT_OK;
}

WhpResult WhpDestroyMessage(WhpMessageHandle message) {
  return WhpClose(message);
}

WhpResult WhpAppendMessageData(WhpMessageHandle message,
                               uint32_t additional_num_bytes,
                               const WhpHandle* handles,
                               uint32_t num_handles,
                               const WhpAppendMessageDataOptions* options,
                               void** buffer,
                               uint32_t* buffer_size) {
  Core& c = Core::Get();
  std::lock_guard<std::mutex> lock(c.mu);
  Slot* slot = c.Lookup(message);
  if (!slot || slot->kind != HandleKind::Message) {
    return WHP_RESULT_INVALID_ARGUMENT;
  }
  auto* msg = static_cast<MessageObj*>(slot->obj.get());
  if (msg->committed && additional_num_bytes != 0) {
    return WHP_RESULT_FAILED_PRECONDITION;
  }
  if (additional_num_bytes) {
    msg->bytes.resize(msg->bytes.size() + additional_num_bytes);
  }
  if (handles && num_handles) {
    msg->handles.insert(msg->handles.end(), handles, handles + num_handles);
  }
  if (options && (options->flags & WHP_APPEND_MESSAGE_DATA_FLAG_COMMIT_SIZE)) {
    msg->committed = true;
  }
  if (buffer) {
    *buffer = msg->bytes.empty() ? nullptr : msg->bytes.data();
  }
  if (buffer_size) {
    *buffer_size = static_cast<uint32_t>(msg->bytes.size());
  }
  return WHP_RESULT_OK;
}

WhpResult WhpGetMessageData(WhpMessageHandle message,
                            const WhpGetMessageDataOptions*,
                            void** buffer,
                            uint32_t* num_bytes,
                            WhpHandle* handles,
                            uint32_t* num_handles) {
  Core& c = Core::Get();
  std::lock_guard<std::mutex> lock(c.mu);
  Slot* slot = c.Lookup(message);
  if (!slot || slot->kind != HandleKind::Message) {
    return WHP_RESULT_INVALID_ARGUMENT;
  }
  auto* msg = static_cast<MessageObj*>(slot->obj.get());
  const uint32_t have_handles = static_cast<uint32_t>(msg->handles.size());
  if (num_handles && handles == nullptr && have_handles > 0) {
    *num_handles = have_handles;
    if (num_bytes) {
      *num_bytes = static_cast<uint32_t>(msg->bytes.size());
    }
    return WHP_RESULT_RESOURCE_EXHAUSTED;
  }
  if (num_handles && handles && *num_handles < have_handles) {
    *num_handles = have_handles;
    return WHP_RESULT_RESOURCE_EXHAUSTED;
  }
  if (buffer) {
    *buffer = msg->bytes.empty() ? nullptr : msg->bytes.data();
  }
  if (num_bytes) {
    *num_bytes = static_cast<uint32_t>(msg->bytes.size());
  }
  if (handles && num_handles) {
    for (uint32_t i = 0; i < have_handles; ++i) {
      handles[i] = msg->handles[i];
    }
    *num_handles = have_handles;
    msg->handles.clear();
  } else if (num_handles) {
    *num_handles = 0;
  }
  return WHP_RESULT_OK;
}

WhpResult WhpSerializeMessage(WhpMessageHandle message) {
  WhpMessageContextSerializer serializer = nullptr;
  uintptr_t context = 0;
  {
    Core& c = Core::Get();
    std::lock_guard<std::mutex> lock(c.mu);
    Slot* slot = c.Lookup(message);
    if (!slot || slot->kind != HandleKind::Message) {
      return WHP_RESULT_INVALID_ARGUMENT;
    }
    auto* msg = static_cast<MessageObj*>(slot->obj.get());
    if (msg->committed && !msg->serializer) {
      return WHP_RESULT_OK;
    }
    serializer = msg->serializer;
    context = msg->context;
    msg->serializer = nullptr;
  }
  if (serializer) {
    serializer(context, message);
  }
  WhpMessageContextDestructor dtor = nullptr;
  uintptr_t ctx = 0;
  {
    Core& c = Core::Get();
    std::lock_guard<std::mutex> lock(c.mu);
    Slot* slot = c.Lookup(message);
    if (!slot || slot->kind != HandleKind::Message) {
      return WHP_RESULT_INVALID_ARGUMENT;
    }
    auto* msg = static_cast<MessageObj*>(slot->obj.get());
    msg->committed = true;
    dtor = msg->destructor;
    ctx = msg->context;
    msg->destructor = nullptr;
    msg->context = 0;
    msg->serializer = nullptr;
  }
  if (dtor) {
    dtor(ctx);
  }
  return WHP_RESULT_OK;
}

WhpResult WhpReserveMessageCapacity(WhpMessageHandle message,
                                    uint32_t payload_buffer_size,
                                    uint32_t* buffer_size) {
  Core& c = Core::Get();
  std::lock_guard<std::mutex> lock(c.mu);
  Slot* slot = c.Lookup(message);
  if (!slot || slot->kind != HandleKind::Message) {
    return WHP_RESULT_INVALID_ARGUMENT;
  }
  auto* msg = static_cast<MessageObj*>(slot->obj.get());
  if (msg->committed) {
    if (buffer_size) {
      *buffer_size = static_cast<uint32_t>(msg->bytes.size());
    }
    return WHP_RESULT_FAILED_PRECONDITION;
  }
  if (payload_buffer_size > msg->bytes.size()) {
    msg->bytes.resize(payload_buffer_size);
  }
  if (buffer_size) {
    *buffer_size = static_cast<uint32_t>(msg->bytes.size());
  }
  return WHP_RESULT_OK;
}

WhpResult WhpSetMessageContext(WhpMessageHandle message,
                               uintptr_t context,
                               WhpMessageContextSerializer serializer,
                               WhpMessageContextDestructor destructor) {
  WhpMessageContextDestructor old_dtor = nullptr;
  uintptr_t old_ctx = 0;
  {
    Core& c = Core::Get();
    std::lock_guard<std::mutex> lock(c.mu);
    Slot* slot = c.Lookup(message);
    if (!slot || slot->kind != HandleKind::Message) {
      return WHP_RESULT_INVALID_ARGUMENT;
    }
    auto* msg = static_cast<MessageObj*>(slot->obj.get());
    old_dtor = msg->destructor;
    old_ctx = msg->context;
    msg->context = context;
    msg->serializer = serializer;
    msg->destructor = destructor;
  }
  if (old_dtor) {
    old_dtor(old_ctx);
  }
  return WHP_RESULT_OK;
}

WhpResult WhpGetMessageContext(WhpMessageHandle message, uintptr_t* context) {
  if (!context) {
    return WHP_RESULT_INVALID_ARGUMENT;
  }
  Core& c = Core::Get();
  std::lock_guard<std::mutex> lock(c.mu);
  Slot* slot = c.Lookup(message);
  if (!slot || slot->kind != HandleKind::Message) {
    return WHP_RESULT_INVALID_ARGUMENT;
  }
  *context = static_cast<MessageObj*>(slot->obj.get())->context;
  return WHP_RESULT_OK;
}

WhpResult WhpGetBufferInfo(WhpHandle buffer, uint64_t* num_bytes) {
  if (!num_bytes) {
    return WHP_RESULT_INVALID_ARGUMENT;
  }
  Core& c = Core::Get();
  std::lock_guard<std::mutex> lock(c.mu);
  Slot* slot = c.Lookup(buffer);
  if (!slot || slot->kind != HandleKind::SharedBuffer) {
    return WHP_RESULT_INVALID_ARGUMENT;
  }
  *num_bytes = std::static_pointer_cast<SharedBufferObj>(slot->obj)->size;
  return WHP_RESULT_OK;
}

WhpResult WhpWriteMessage(WhpHandle pipe,
                          WhpMessageHandle message,
                          const WhpWriteMessageOptions*) {
  WhpResult ser = WhpSerializeMessage(message);
  if (ser != WHP_RESULT_OK) {
    return ser;
  }
  Core& c = Core::Get();
  {
    std::lock_guard<std::mutex> lock(c.mu);
    Slot* pipe_slot = c.Lookup(pipe);
    Slot* msg_slot = c.Lookup(message);
    if (!pipe_slot || pipe_slot->kind != HandleKind::MessagePipe || !msg_slot ||
        msg_slot->kind != HandleKind::Message) {
      return WHP_RESULT_INVALID_ARGUMENT;
    }
    auto* end = static_cast<PipeEnd*>(pipe_slot->obj.get());
    const int peer = 1 - end->end;
    if (end->state->closed[peer] || end->state->closed[end->end]) {
      c.Free(message);
      CollectTrapEvents(c);
      return WHP_RESULT_FAILED_PRECONDITION;
    }
    auto msg = std::static_pointer_cast<MessageObj>(msg_slot->obj);
    auto& inbound = end->state->queues[peer];
    if (inbound.size() >= end->state->quota_count[peer] ||
        QueueBytes(inbound) + msg->bytes.size() >
            end->state->quota_bytes[peer]) {
      return WHP_RESULT_RESOURCE_EXHAUSTED;
    }
    msg->committed = true;
    inbound.push_back(std::move(msg));
    c.Free(message);
    CollectTrapEvents(c);
  }
  return WHP_RESULT_OK;
}

WhpResult WhpReadMessage(WhpHandle pipe,
                         const WhpReadMessageOptions*,
                         WhpMessageHandle* message) {
  if (!message) {
    return WHP_RESULT_INVALID_ARGUMENT;
  }
  Core& c = Core::Get();
  std::lock_guard<std::mutex> lock(c.mu);
  Slot* slot = c.Lookup(pipe);
  if (!slot || slot->kind != HandleKind::MessagePipe) {
    return WHP_RESULT_INVALID_ARGUMENT;
  }
  auto* end = static_cast<PipeEnd*>(slot->obj.get());
  if (end->state->queues[end->end].empty()) {
    if (end->state->closed[1 - end->end]) {
      return WHP_RESULT_FAILED_PRECONDITION;
    }
    return WHP_RESULT_SHOULD_WAIT;
  }
  auto msg = std::move(end->state->queues[end->end].front());
  end->state->queues[end->end].pop_front();
  *message = c.Alloc(HandleKind::Message, std::move(msg));
  return WHP_RESULT_OK;
}

WhpResult WhpCreateTrap(WhpTrapEventHandler handler,
                        const WhpCreateTrapOptions*,
                        WhpHandle* trap) {
  if (!handler || !trap) {
    return WHP_RESULT_INVALID_ARGUMENT;
  }
  Core& c = Core::Get();
  std::lock_guard<std::mutex> lock(c.mu);
  auto obj = std::make_shared<TrapObj>();
  obj->handler = handler;
  *trap = c.Alloc(HandleKind::Trap, obj);
  c.traps.push_back(*trap);
  return WHP_RESULT_OK;
}

WhpResult WhpAddTrigger(WhpHandle trap,
                        WhpHandle handle,
                        WhpHandleSignals signals,
                        uint32_t condition,
                        uintptr_t context,
                        const WhpAddTriggerOptions*) {
  Core& c = Core::Get();
  std::lock_guard<std::mutex> lock(c.mu);
  Slot* tslot = c.Lookup(trap);
  Slot* hslot = c.Lookup(handle);
  if (!tslot || tslot->kind != HandleKind::Trap || !hslot) {
    return WHP_RESULT_INVALID_ARGUMENT;
  }
  auto* tobj = static_cast<TrapObj*>(tslot->obj.get());
  tobj->triggers.push_back(TrapTrigger{handle, signals, condition, context});
  return WHP_RESULT_OK;
}

WhpResult WhpRemoveTrigger(WhpHandle trap,
                           uintptr_t context,
                           const WhpRemoveTriggerOptions*) {
  Core& c = Core::Get();
  {
    std::lock_guard<std::mutex> lock(c.mu);
    Slot* tslot = c.Lookup(trap);
    if (!tslot || tslot->kind != HandleKind::Trap) {
      return WHP_RESULT_INVALID_ARGUMENT;
    }
    auto* tobj = static_cast<TrapObj*>(tslot->obj.get());
    auto it = std::remove_if(tobj->triggers.begin(), tobj->triggers.end(),
                             [context](const TrapTrigger& t) {
                               return t.context == context;
                             });
    if (it == tobj->triggers.end()) {
      return WHP_RESULT_NOT_FOUND;
    }
    tobj->triggers.erase(it, tobj->triggers.end());
    if (tobj->handler) {
      WhpTrapEvent ev{};
      ev.struct_size = sizeof(ev);
      ev.trigger_context = context;
      ev.result = WHP_RESULT_CANCELLED;
      c.pending_events.push_back(PendingEvent{tobj->handler, ev});
    }
  }
  return WHP_RESULT_OK;
}

WhpResult WhpArmTrap(WhpHandle trap,
                     const WhpArmTrapOptions*,
                     uint32_t* num_blocking_events,
                     WhpTrapEvent* blocking_events) {
  Core& c = Core::Get();
  std::lock_guard<std::mutex> lock(c.mu);
  Slot* tslot = c.Lookup(trap);
  if (!tslot || tslot->kind != HandleKind::Trap) {
    return WHP_RESULT_INVALID_ARGUMENT;
  }
  auto* tobj = static_cast<TrapObj*>(tslot->obj.get());
  uint32_t blocking = 0;
  for (const auto& trig : tobj->triggers) {
    WhpHandleSignalsState st = QueryLocked(c, trig.watched);
    if (!SignalsMatch(st, trig.signals, trig.condition)) {
      continue;
    }
    if (blocking_events && num_blocking_events &&
        blocking < *num_blocking_events) {
      WhpTrapEvent& ev = blocking_events[blocking];
      ev.struct_size = sizeof(WhpTrapEvent);
      ev.flags = 0;
      ev.trigger_context = trig.context;
      ev.result = WHP_RESULT_OK;
      ev.signals_state = st;
    }
    ++blocking;
  }
  if (blocking > 0) {
    if (num_blocking_events) {
      *num_blocking_events = blocking;
    }
    return WHP_RESULT_FAILED_PRECONDITION;
  }
  tobj->armed = true;
  return WHP_RESULT_OK;
}

WhpResult WhpCreateDataPipe(const WhpCreateDataPipeOptions* options,
                            WhpHandle* producer,
                            WhpHandle* consumer) {
  if (!producer || !consumer) {
    return WHP_RESULT_INVALID_ARGUMENT;
  }
  Core& c = Core::Get();
  std::lock_guard<std::mutex> lock(c.mu);
  auto state = std::make_shared<DataPipeState>();
  if (options) {
    if (options->element_num_bytes) {
      state->element_size = options->element_num_bytes;
    }
    if (options->capacity_num_bytes) {
      state->capacity = options->capacity_num_bytes;
    }
  }
  state->buf.resize(state->capacity);
  auto p = std::make_shared<DataPipeEnd>(DataPipeEnd{state, true});
  auto cons = std::make_shared<DataPipeEnd>(DataPipeEnd{state, false});
  *producer = c.Alloc(HandleKind::DataPipeProducer, p);
  *consumer = c.Alloc(HandleKind::DataPipeConsumer, cons);
  return WHP_RESULT_OK;
}

WhpResult WhpWriteData(WhpHandle producer,
                       const void* elements,
                       uint32_t* num_bytes,
                       uint32_t) {
  if (!num_bytes) {
    return WHP_RESULT_INVALID_ARGUMENT;
  }
  Core& c = Core::Get();
  {
    std::lock_guard<std::mutex> lock(c.mu);
    Slot* slot = c.Lookup(producer);
    if (!slot || slot->kind != HandleKind::DataPipeProducer) {
      return WHP_RESULT_INVALID_ARGUMENT;
    }
    auto* end = static_cast<DataPipeEnd*>(slot->obj.get());
    auto& s = *end->state;
    if (s.consumer_closed) {
      return WHP_RESULT_FAILED_PRECONDITION;
    }
    uint32_t space = s.capacity - s.available;
    uint32_t want = *num_bytes;
    if (want % s.element_size) {
      return WHP_RESULT_INVALID_ARGUMENT;
    }
    uint32_t n = std::min(want, space);
    n -= n % s.element_size;
    if (n == 0) {
      *num_bytes = 0;
      return WHP_RESULT_SHOULD_WAIT;
    }
    const uint8_t* src = static_cast<const uint8_t*>(elements);
    for (uint32_t i = 0; i < n; ++i) {
      s.buf[s.w] = src[i];
      s.w = (s.w + 1) % s.capacity;
    }
    s.available += n;
    *num_bytes = n;
    CollectTrapEvents(c);
  }
  return WHP_RESULT_OK;
}

WhpResult WhpReadData(WhpHandle consumer,
                      void* elements,
                      uint32_t* num_bytes,
                      uint32_t) {
  if (!num_bytes) {
    return WHP_RESULT_INVALID_ARGUMENT;
  }
  Core& c = Core::Get();
  {
    std::lock_guard<std::mutex> lock(c.mu);
    Slot* slot = c.Lookup(consumer);
    if (!slot || slot->kind != HandleKind::DataPipeConsumer) {
      return WHP_RESULT_INVALID_ARGUMENT;
    }
    auto* end = static_cast<DataPipeEnd*>(slot->obj.get());
    auto& s = *end->state;
    uint32_t want = *num_bytes;
    if (want % s.element_size) {
      return WHP_RESULT_INVALID_ARGUMENT;
    }
    uint32_t n = std::min(want, s.available);
    n -= n % s.element_size;
    if (n == 0) {
      *num_bytes = 0;
      if (s.producer_closed) {
        return WHP_RESULT_FAILED_PRECONDITION;
      }
      return WHP_RESULT_SHOULD_WAIT;
    }
    uint8_t* dst = static_cast<uint8_t*>(elements);
    for (uint32_t i = 0; i < n; ++i) {
      dst[i] = s.buf[s.r];
      s.r = (s.r + 1) % s.capacity;
    }
    s.available -= n;
    *num_bytes = n;
    CollectTrapEvents(c);
  }
  return WHP_RESULT_OK;
}

WhpResult WhpBeginWriteData(WhpHandle producer,
                            void** buffer,
                            uint32_t* num_bytes) {
  if (!buffer || !num_bytes) {
    return WHP_RESULT_INVALID_ARGUMENT;
  }
  Core& c = Core::Get();
  std::lock_guard<std::mutex> lock(c.mu);
  Slot* slot = c.Lookup(producer);
  if (!slot || slot->kind != HandleKind::DataPipeProducer) {
    return WHP_RESULT_INVALID_ARGUMENT;
  }
  auto* end = static_cast<DataPipeEnd*>(slot->obj.get());
  auto& s = *end->state;
  if (s.two_phase_write) {
    return WHP_RESULT_BUSY;
  }
  uint32_t space = s.capacity - s.available;
  uint32_t contig = s.capacity - s.w;
  uint32_t n = std::min(space, contig);
  n = std::min(n, *num_bytes);
  n -= n % s.element_size;
  if (n == 0) {
    return WHP_RESULT_SHOULD_WAIT;
  }
  s.two_phase_write = s.buf.data() + s.w;
  s.two_phase_write_max = n;
  *buffer = s.two_phase_write;
  *num_bytes = n;
  return WHP_RESULT_OK;
}

WhpResult WhpEndWriteData(WhpHandle producer, uint32_t num_bytes_written) {
  Core& c = Core::Get();
  {
    std::lock_guard<std::mutex> lock(c.mu);
    Slot* slot = c.Lookup(producer);
    if (!slot || slot->kind != HandleKind::DataPipeProducer) {
      return WHP_RESULT_INVALID_ARGUMENT;
    }
    auto* end = static_cast<DataPipeEnd*>(slot->obj.get());
    auto& s = *end->state;
    if (!s.two_phase_write) {
      return WHP_RESULT_FAILED_PRECONDITION;
    }
    if (num_bytes_written > s.two_phase_write_max ||
        num_bytes_written % s.element_size) {
      s.two_phase_write = nullptr;
      return WHP_RESULT_INVALID_ARGUMENT;
    }
    s.w = (s.w + num_bytes_written) % s.capacity;
    s.available += num_bytes_written;
    s.two_phase_write = nullptr;
    CollectTrapEvents(c);
  }
  return WHP_RESULT_OK;
}

WhpResult WhpBeginReadData(WhpHandle consumer,
                           const void** buffer,
                           uint32_t* num_bytes) {
  if (!buffer || !num_bytes) {
    return WHP_RESULT_INVALID_ARGUMENT;
  }
  Core& c = Core::Get();
  std::lock_guard<std::mutex> lock(c.mu);
  Slot* slot = c.Lookup(consumer);
  if (!slot || slot->kind != HandleKind::DataPipeConsumer) {
    return WHP_RESULT_INVALID_ARGUMENT;
  }
  auto* end = static_cast<DataPipeEnd*>(slot->obj.get());
  auto& s = *end->state;
  if (s.two_phase_read) {
    return WHP_RESULT_BUSY;
  }
  uint32_t contig = s.capacity - s.r;
  uint32_t n = std::min(s.available, contig);
  n = std::min(n, *num_bytes);
  n -= n % s.element_size;
  if (n == 0) {
    if (s.producer_closed) {
      return WHP_RESULT_FAILED_PRECONDITION;
    }
    return WHP_RESULT_SHOULD_WAIT;
  }
  s.two_phase_read = s.buf.data() + s.r;
  s.two_phase_read_max = n;
  *buffer = s.two_phase_read;
  *num_bytes = n;
  return WHP_RESULT_OK;
}

WhpResult WhpEndReadData(WhpHandle consumer, uint32_t num_bytes_read) {
  Core& c = Core::Get();
  {
    std::lock_guard<std::mutex> lock(c.mu);
    Slot* slot = c.Lookup(consumer);
    if (!slot || slot->kind != HandleKind::DataPipeConsumer) {
      return WHP_RESULT_INVALID_ARGUMENT;
    }
    auto* end = static_cast<DataPipeEnd*>(slot->obj.get());
    auto& s = *end->state;
    if (!s.two_phase_read) {
      return WHP_RESULT_FAILED_PRECONDITION;
    }
    if (num_bytes_read > s.two_phase_read_max ||
        num_bytes_read % s.element_size) {
      s.two_phase_read = nullptr;
      return WHP_RESULT_INVALID_ARGUMENT;
    }
    s.r = (s.r + num_bytes_read) % s.capacity;
    s.available -= num_bytes_read;
    s.two_phase_read = nullptr;
    CollectTrapEvents(c);
  }
  return WHP_RESULT_OK;
}

WhpResult WhpCreateSharedBuffer(uint64_t num_bytes,
                                const WhpCreateSharedBufferOptions*,
                                WhpHandle* buffer) {
  if (!buffer || num_bytes == 0) {
    return WHP_RESULT_INVALID_ARGUMENT;
  }
  Core& c = Core::Get();
  std::lock_guard<std::mutex> lock(c.mu);
  auto obj = std::make_shared<SharedBufferObj>();
  obj->bytes.resize(static_cast<size_t>(num_bytes));
  obj->size = num_bytes;
  *buffer = c.Alloc(HandleKind::SharedBuffer, obj);
  return WHP_RESULT_OK;
}

WhpResult WhpDuplicateBufferHandle(WhpHandle buffer, WhpHandle* new_handle) {
  if (!new_handle) {
    return WHP_RESULT_INVALID_ARGUMENT;
  }
  Core& c = Core::Get();
  std::lock_guard<std::mutex> lock(c.mu);
  Slot* slot = c.Lookup(buffer);
  if (!slot || slot->kind != HandleKind::SharedBuffer) {
    return WHP_RESULT_INVALID_ARGUMENT;
  }
  *new_handle = c.Alloc(HandleKind::SharedBuffer, slot->obj);
  return WHP_RESULT_OK;
}

WhpResult WhpMapBuffer(WhpHandle buffer,
                       uint64_t offset,
                       uint64_t num_bytes,
                       void** data) {
  if (!data) {
    return WHP_RESULT_INVALID_ARGUMENT;
  }
  Core& c = Core::Get();
  std::lock_guard<std::mutex> lock(c.mu);
  Slot* slot = c.Lookup(buffer);
  if (!slot || slot->kind != HandleKind::SharedBuffer) {
    return WHP_RESULT_INVALID_ARGUMENT;
  }
  auto buf = std::static_pointer_cast<SharedBufferObj>(slot->obj);
  if (!EnsureMapped(buf.get())) {
    return WHP_RESULT_FAILED_PRECONDITION;
  }
  const uint64_t span = buf->os_base ? buf->size : buf->bytes.size();
  if (offset + num_bytes > span) {
    return WHP_RESULT_OUT_OF_RANGE;
  }
  void* p = buf->os_base ? static_cast<uint8_t*>(buf->os_base) + offset
                         : buf->bytes.data() + offset;
  auto& region = c.maps[p];
  region.buf = buf;
  region.refs++;
  *data = p;
  return WHP_RESULT_OK;
}

WhpResult WhpUnmapBuffer(void* data) {
  Core& c = Core::Get();
  std::lock_guard<std::mutex> lock(c.mu);
  auto it = c.maps.find(data);
  if (it == c.maps.end() || it->second.refs <= 0) {
    return WHP_RESULT_INVALID_ARGUMENT;
  }
  if (--it->second.refs == 0) {
    c.maps.erase(it);
  }
  return WHP_RESULT_OK;
}

WhpResult WhpWrapPlatformHandle(WhpPlatformHandleType type,
                                uint64_t value,
                                WhpHandle* handle) {
  if (!handle) {
    return WHP_RESULT_INVALID_ARGUMENT;
  }
  if (!PlatformTypeOkForHost(type)) {
    return WHP_RESULT_INVALID_ARGUMENT;
  }
  if (type == WHP_PLATFORM_HANDLE_TYPE_MACH_PORT && value == 0) {
    return WHP_RESULT_INVALID_ARGUMENT;
  }
#ifdef _WIN32
  HANDLE h = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(value));
  if (!h || h == INVALID_HANDLE_VALUE) {
    return WHP_RESULT_INVALID_ARGUMENT;
  }
#endif
  Core& c = Core::Get();
  std::lock_guard<std::mutex> lock(c.mu);
  auto obj = std::make_shared<PlatformObj>();
  obj->type = type;
  obj->value = value;
  obj->owns = true;
  *handle = c.Alloc(HandleKind::Platform, std::move(obj));
  return WHP_RESULT_OK;
}

WhpResult WhpUnwrapPlatformHandle(WhpHandle handle,
                                  WhpPlatformHandleType* type,
                                  uint64_t* value) {
  if (!type || !value) {
    return WHP_RESULT_INVALID_ARGUMENT;
  }
  Core& c = Core::Get();
  std::lock_guard<std::mutex> lock(c.mu);
  Slot* slot = c.Lookup(handle);
  if (!slot || slot->kind != HandleKind::Platform) {
    return WHP_RESULT_INVALID_ARGUMENT;
  }
  auto* p = static_cast<PlatformObj*>(slot->obj.get());
  *type = p->type;
  *value = p->value;
  p->owns = false;
  c.Free(handle);
  return WHP_RESULT_OK;
}

WhpResult WhpWrapPlatformSharedMemoryRegion(
    const WhpPlatformHandleType* types,
    const uint64_t* values,
    uint32_t num_handles,
    uint64_t num_bytes,
    uint64_t guid_high,
    uint64_t guid_low,
    WhpPlatformSharedMemoryRegionAccessMode access_mode,
    WhpHandle* handle) {
  if (!types || !values || !handle || num_bytes == 0 || num_handles == 0 ||
      num_handles > 2) {
    return WHP_RESULT_INVALID_ARGUMENT;
  }
  for (uint32_t i = 0; i < num_handles; ++i) {
    if (!PlatformTypeOkForHost(types[i])) {
      return WHP_RESULT_INVALID_ARGUMENT;
    }
    if (types[i] == WHP_PLATFORM_HANDLE_TYPE_MACH_PORT && values[i] == 0) {
      return WHP_RESULT_INVALID_ARGUMENT;
    }
  }
  Core& c = Core::Get();
  std::lock_guard<std::mutex> lock(c.mu);
  auto obj = std::make_shared<SharedBufferObj>();
  obj->size = num_bytes;
  obj->plat_count = num_handles;
  obj->plat_owns = true;
  obj->guid_high = guid_high;
  obj->guid_low = guid_low;
  obj->access_mode = access_mode;
  for (uint32_t i = 0; i < num_handles; ++i) {
    obj->plat_types[i] = types[i];
    obj->plat_values[i] = values[i];
  }
  if (types[0] == WHP_PLATFORM_HANDLE_TYPE_MACH_PORT) {
    obj->bytes.resize(static_cast<size_t>(num_bytes));
  }
  *handle = c.Alloc(HandleKind::SharedBuffer, obj);
  return WHP_RESULT_OK;
}

WhpResult WhpUnwrapPlatformSharedMemoryRegion(
    WhpHandle handle,
    WhpPlatformHandleType* types,
    uint64_t* values,
    uint32_t* num_handles,
    uint64_t* num_bytes,
    uint64_t* guid_high,
    uint64_t* guid_low,
    WhpPlatformSharedMemoryRegionAccessMode* access_mode) {
  if (!num_handles) {
    return WHP_RESULT_INVALID_ARGUMENT;
  }
  Core& c = Core::Get();
  std::lock_guard<std::mutex> lock(c.mu);
  Slot* slot = c.Lookup(handle);
  if (!slot || slot->kind != HandleKind::SharedBuffer) {
    return WHP_RESULT_INVALID_ARGUMENT;
  }
  auto buf = std::static_pointer_cast<SharedBufferObj>(slot->obj);
  const uint32_t have = buf->plat_count;
  if (have == 0) {
    return WHP_RESULT_FAILED_PRECONDITION;
  }
  if (!types || !values || *num_handles < have) {
    *num_handles = have;
    return WHP_RESULT_RESOURCE_EXHAUSTED;
  }
  for (uint32_t i = 0; i < have; ++i) {
    types[i] = buf->plat_types[i];
    values[i] = buf->plat_values[i];
  }
  *num_handles = have;
  if (num_bytes) *num_bytes = buf->size;
  if (guid_high) *guid_high = buf->guid_high;
  if (guid_low) *guid_low = buf->guid_low;
  if (access_mode) *access_mode = buf->access_mode;
  buf->plat_owns = false;
  c.Free(handle);
  return WHP_RESULT_OK;
}

WhpResult WhpSetQuota(WhpHandle handle, WhpQuotaType type, uint64_t limit) {
  Core& c = Core::Get();
  std::lock_guard<std::mutex> lock(c.mu);
  Slot* slot = c.Lookup(handle);
  if (!slot || slot->kind != HandleKind::MessagePipe) {
    return WHP_RESULT_INVALID_ARGUMENT;
  }
  auto* end = static_cast<PipeEnd*>(slot->obj.get());
  if (type == WHP_QUOTA_TYPE_MAX_UNREAD_MESSAGE_COUNT) {
    end->state->quota_count[end->end] = limit;
    return WHP_RESULT_OK;
  }
  if (type == WHP_QUOTA_TYPE_MAX_UNREAD_MESSAGE_SIZE) {
    end->state->quota_bytes[end->end] = limit;
    return WHP_RESULT_OK;
  }
  return WHP_RESULT_INVALID_ARGUMENT;
}

WhpResult WhpQueryQuota(WhpHandle handle,
                        WhpQuotaType type,
                        uint64_t* limit,
                        uint64_t* usage) {
  if (!limit || !usage) {
    return WHP_RESULT_INVALID_ARGUMENT;
  }
  Core& c = Core::Get();
  std::lock_guard<std::mutex> lock(c.mu);
  Slot* slot = c.Lookup(handle);
  if (!slot || slot->kind != HandleKind::MessagePipe) {
    return WHP_RESULT_INVALID_ARGUMENT;
  }
  auto* end = static_cast<PipeEnd*>(slot->obj.get());
  auto& q = end->state->queues[end->end];
  if (type == WHP_QUOTA_TYPE_MAX_UNREAD_MESSAGE_COUNT) {
    *limit = end->state->quota_count[end->end];
    *usage = q.size();
    return WHP_RESULT_OK;
  }
  if (type == WHP_QUOTA_TYPE_MAX_UNREAD_MESSAGE_SIZE) {
    *limit = end->state->quota_bytes[end->end];
    *usage = QueueBytes(q);
    return WHP_RESULT_OK;
  }
  return WHP_RESULT_INVALID_ARGUMENT;
}

WhpResult WhpFuseMessagePipes(WhpHandle handle0, WhpHandle handle1) {
  Core& c = Core::Get();
  {
    std::lock_guard<std::mutex> lock(c.mu);
    Slot* s0 = c.Lookup(handle0);
    Slot* s1 = c.Lookup(handle1);
    if (!s0 || !s1 || s0->kind != HandleKind::MessagePipe ||
        s1->kind != HandleKind::MessagePipe || handle0 == handle1) {
      return WHP_RESULT_INVALID_ARGUMENT;
    }
    auto* e0 = static_cast<PipeEnd*>(s0->obj.get());
    auto* e1 = static_cast<PipeEnd*>(s1->obj.get());
    if (e0->state == e1->state) {
      return WHP_RESULT_INVALID_ARGUMENT;
    }
    PipeEnd* peer0 = nullptr;
    PipeEnd* peer1 = nullptr;
    for (WhpHandle i = 1; i < c.slots.size(); ++i) {
      if (i == handle0 || i == handle1) {
        continue;
      }
      Slot* s = c.Lookup(i);
      if (!s || s->kind != HandleKind::MessagePipe) {
        continue;
      }
      auto* e = static_cast<PipeEnd*>(s->obj.get());
      if (e->state == e0->state && e->end != e0->end) {
        peer0 = e;
      }
      if (e->state == e1->state && e->end != e1->end) {
        peer1 = e;
      }
    }
    if (!peer0 || !peer1) {
      return WHP_RESULT_FAILED_PRECONDITION;
    }
    auto fused = std::make_shared<PipeState>();
    fused->queues[0] = std::move(peer0->state->queues[peer0->end]);
    for (auto& m : e1->state->queues[e1->end]) {
      fused->queues[0].push_back(std::move(m));
    }
    fused->queues[1] = std::move(peer1->state->queues[peer1->end]);
    for (auto& m : e0->state->queues[e0->end]) {
      fused->queues[1].push_back(std::move(m));
    }
    fused->quota_count[0] = peer0->state->quota_count[peer0->end];
    fused->quota_bytes[0] = peer0->state->quota_bytes[peer0->end];
    fused->quota_count[1] = peer1->state->quota_count[peer1->end];
    fused->quota_bytes[1] = peer1->state->quota_bytes[peer1->end];
    peer0->state = fused;
    peer0->end = 0;
    peer1->state = fused;
    peer1->end = 1;
    c.Free(handle0);
    c.Free(handle1);
    CollectTrapEvents(c);
  }
  return WHP_RESULT_OK;
}

}  // extern "C"
