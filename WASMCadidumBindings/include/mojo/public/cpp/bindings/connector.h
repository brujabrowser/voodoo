// mojo::Connector: owns one end of a message pipe, writes outgoing
// mojo::Message objects to it and dispatches incoming ones to a
// MessageReceiver, matching the role of Chromium's
// mojo/public/cpp/bindings/connector.h. Built directly on wck's
// mojo::WriteMessageRaw/ReadMessageRaw and mojo::SimpleWatcher -- this is
// the one place in the bindings stack that touches the raw message pipe.
#ifndef MOJO_PUBLIC_CPP_BINDINGS_CONNECTOR_H_
#define MOJO_PUBLIC_CPP_BINDINGS_CONNECTOR_H_

#include "base/callback.h"
#include "base/time.h"
#include "mojo/public/cpp/bindings/message.h"
#include "mojo/public/cpp/system/message_pipe.h"
#include "mojo/public/cpp/system/simple_watcher.h"

#include <memory>

namespace mojo {

class Connector : public MessageReceiver {
 public:
  explicit Connector(ScopedMessagePipeHandle pipe);
  ~Connector() override;

  Connector(const Connector&) = delete;
  Connector& operator=(const Connector&) = delete;

  // MessageReceiver: writes `message` out the pipe.
  [[nodiscard]] bool Accept(Message* message) override;

  // Where incoming messages go once StartReceiving() is watching the pipe.
  // Must outlive the Connector, or be cleared first.
  void set_incoming_receiver(MessageReceiver* receiver) {
    incoming_receiver_ = receiver;
  }

  // Arms two SimpleWatchers: one on MOJO_HANDLE_SIGNAL_READABLE (drains
  // every queued message to incoming_receiver_ on each fire -- automatic
  // re-arm, see wck's SimpleWatcher, covers more than one queued message),
  // one on MOJO_HANDLE_SIGNAL_PEER_CLOSED. They have to be separate:
  // whp/wst's trigger semantics fire SIGNALS_SATISFIED only once *every*
  // watched bit is satisfied at once, not on any one of them -- READABLE
  // and PEER_CLOSED are essentially never satisfied simultaneously (no
  // pending message right when the peer closes), so a single watcher for
  // both would just never fire. The peer-closed watcher cancels itself the
  // moment it fires (see connector.cc) since that signal, once satisfied,
  // stays satisfied forever -- automatic re-arm on a permanent condition
  // would spin the executor's pump in a tight forever-loop.
  void StartReceiving();

  void PauseIncomingMethodCallProcessing();
  void ResumeIncomingMethodCallProcessing();

  void set_connection_error_handler(base::OnceClosure handler) {
    connection_error_handler_ = std::move(handler);
  }

  bool encountered_error() const { return error_; }
  void RaiseError();

  bool is_valid() const { return static_cast<bool>(pipe_); }
  MessagePipeHandle handle() const { return pipe_.get(); }
  ScopedMessagePipeHandle PassMessagePipe() {
    CancelWatchers();
    return std::move(pipe_);
  }
  void CloseMessagePipe();

  // Cooperatively pumps whp::Executor::Current() -- there's no real
  // OS-level blocking wait anywhere in this stack; incoming-message
  // dispatch only ever happens when that queue is pumped (see
  // simple_watcher.h's header comment: the trap handler that notices a
  // pipe went readable only *posts*, it never calls back directly) -- until
  // `predicate()` returns true or this connector raises an error (peer
  // closed, or a read/write failure). This is what backs a `[Sync]`
  // .voodoom method's blocking Proxy_ overload: register the pending
  // response as usual, send the request, then SyncWaitFor the response
  // handler to have flipped a local flag.
  //
  // Returns true if predicate() became true, false if an error ended the
  // wait first (the caller should treat that the same as any other pipe
  // error -- the sync call didn't get its response). If predicate() is
  // already true, returns true immediately without pumping anything.
  //
  // Blocks via base::PostJob (WASMv8Bindings cppgc::Platform::PostJob /
  // JobHandle::Join — the C++ job rung; not WASMJobHandler playbooks),
  // waiting on HolePunch Executor::WaitForWork instead of spinning.
  // `timeout` of TimeDelta::Max() waits until the predicate or a pipe
  // error; a finite timeout returns false on expiry.
  bool SyncWaitFor(base::RepeatingCallback<bool()> predicate,
                   base::TimeDelta timeout = base::TimeDelta::Max());

 private:
  void OnReadable(MojoResult result);
  void OnPeerClosed(MojoResult result);
  void ReadAllAvailableMessages();
  void CancelWatchers();

  ScopedMessagePipeHandle pipe_;
  std::unique_ptr<SimpleWatcher> read_watcher_;
  std::unique_ptr<SimpleWatcher> closed_watcher_;
  MessageReceiver* incoming_receiver_ = nullptr;
  base::OnceClosure connection_error_handler_;
  bool error_ = false;
  bool paused_ = false;
  std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
};

}  // namespace mojo

#endif  // MOJO_PUBLIC_CPP_BINDINGS_CONNECTOR_H_
