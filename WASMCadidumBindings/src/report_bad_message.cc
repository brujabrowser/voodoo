#include "mojo/public/cpp/bindings/report_bad_message.h"

#include "mojo/public/c/system/core.h"
#include "mojo/public/cpp/bindings/connector.h"

#include <utility>

namespace mojo {
namespace {

// Plain globals, not thread_local: this whole stack is cooperative
// single-logical-thread (see WASMHolePunch/src/system/core.cc's own
// GetCurrentThreadId comment -- "Cooperative wasigocvm is one logical
// thread until pthread ships"), so there's no real concurrent-dispatch
// case these need to isolate. thread_local here hit MinGW's emulated-TLS
// allocator (__emutls_get_address) on a BadMessageDispatchScope
// construction -- an observed calloc/realloc corruption in
// libwinpthread's TLS bookkeeping (the wcb_tests
// report_bad_message_closes_the_pipe crash this fixes), not a
// hypothetical concern.
Connector* t_connector = nullptr;
std::shared_ptr<bool> t_alive;

}  // namespace

void ReportBadMessage(std::string_view error) {
  MojoNotifyBadMessage(MOJO_MESSAGE_HANDLE_INVALID, error.data(),
                       static_cast<uint32_t>(error.size()), nullptr);
  if (t_connector && t_alive && *t_alive) {
    t_connector->RaiseError();
  }
}

ReportBadMessageCallback GetBadMessageCallback() {
  Connector* connector = t_connector;
  std::shared_ptr<bool> alive = t_alive;
  return [connector, alive](std::string_view error) {
    MojoNotifyBadMessage(MOJO_MESSAGE_HANDLE_INVALID, error.data(),
                         static_cast<uint32_t>(error.size()), nullptr);
    if (connector && alive && *alive) {
      connector->RaiseError();
    }
  };
}

namespace internal {

BadMessageDispatchScope::BadMessageDispatchScope(
    Connector* connector, std::shared_ptr<bool> alive)
    : previous_connector_(t_connector), previous_alive_(t_alive) {
  t_connector = connector;
  t_alive = std::move(alive);
}

BadMessageDispatchScope::~BadMessageDispatchScope() {
  t_connector = previous_connector_;
  t_alive = std::move(previous_alive_);
}

}  // namespace internal
}  // namespace mojo
