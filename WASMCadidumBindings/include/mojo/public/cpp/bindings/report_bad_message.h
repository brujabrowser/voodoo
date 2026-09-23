// ReportBadMessage / GetBadMessageCallback -- matching Chromium's
// mojo/public/cpp/bindings/lib/message.h helpers. During incoming
// dispatch, Connector installs a dispatch-scoped "current connector"
// (plain globals in report_bad_message.cc, not thread_local -- this
// stack is cooperative single-logical-thread, and thread_local here hit
// a real MinGW emulated-TLS allocator bug) so a Stub_ can reject a
// malformed call: NotifyBadMessage + RaiseError (closes the pipe).
#ifndef MOJO_PUBLIC_CPP_BINDINGS_REPORT_BAD_MESSAGE_H_
#define MOJO_PUBLIC_CPP_BINDINGS_REPORT_BAD_MESSAGE_H_

#include "base/callback.h"

#include <memory>
#include <string>
#include <string_view>

namespace mojo {

class Connector;

using ReportBadMessageCallback =
    base::OnceCallback<void(std::string_view error)>;

void ReportBadMessage(std::string_view error);
ReportBadMessageCallback GetBadMessageCallback();

namespace internal {

class BadMessageDispatchScope {
 public:
  BadMessageDispatchScope(Connector* connector, std::shared_ptr<bool> alive);
  ~BadMessageDispatchScope();
  BadMessageDispatchScope(const BadMessageDispatchScope&) = delete;
  BadMessageDispatchScope& operator=(const BadMessageDispatchScope&) = delete;

 private:
  Connector* previous_connector_;
  std::shared_ptr<bool> previous_alive_;
};

}  // namespace internal

}  // namespace mojo

#endif  // MOJO_PUBLIC_CPP_BINDINGS_REPORT_BAD_MESSAGE_H_
