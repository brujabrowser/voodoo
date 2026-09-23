#include "mojo/public/cpp/bindings/connector.h"

#include "base/task/post_job.h"
#include "mojo/public/cpp/bindings/report_bad_message.h"
#include "src/sandbox/cage-allocator.h"
#include "whp/base/executor.h"
#include "whp/base/time.h"

#include <utility>

namespace mojo {

Connector::Connector(ScopedMessagePipeHandle pipe) : pipe_(std::move(pipe)) {}

Connector::~Connector() {
  *alive_ = false;
}

bool Connector::Accept(Message* message) {
  if (!pipe_ || error_) {
    return false;
  }
  std::vector<MojoHandle> raw_handles;
  for (auto& h : message->TakeHandles()) {
    raw_handles.push_back(h.release().value());
  }
  MojoResult result = WriteMessageRaw(
      pipe_.get(), message->data(), message->data_num_bytes(),
      raw_handles.empty() ? nullptr : raw_handles.data(),
      static_cast<uint32_t>(raw_handles.size()),
      MOJO_WRITE_MESSAGE_FLAG_NONE);
  if (result != MOJO_RESULT_OK) {
    RaiseError();
    return false;
  }
  return true;
}

void Connector::StartReceiving() {
  if (!pipe_ || read_watcher_) {
    return;
  }
  auto alive = alive_;

  read_watcher_ = std::make_unique<SimpleWatcher>(
      SimpleWatcher::ArmingPolicy::kAutomatic);
  read_watcher_->Watch(pipe_.get(), MOJO_HANDLE_SIGNAL_READABLE,
                        [this, alive](MojoResult result) {
                          if (*alive) {
                            OnReadable(result);
                          }
                        });

  closed_watcher_ = std::make_unique<SimpleWatcher>(
      SimpleWatcher::ArmingPolicy::kAutomatic);
  closed_watcher_->Watch(pipe_.get(), MOJO_HANDLE_SIGNAL_PEER_CLOSED,
                          [this, alive](MojoResult result) {
                            if (*alive) {
                              OnPeerClosed(result);
                            }
                          });
}

void Connector::PauseIncomingMethodCallProcessing() {
  paused_ = true;
  CancelWatchers();
}

void Connector::ResumeIncomingMethodCallProcessing() {
  if (!paused_) {
    return;
  }
  paused_ = false;
  StartReceiving();
}

void Connector::RaiseError() {
  if (error_) {
    return;
  }
  error_ = true;
  CancelWatchers();
  // Close the local end so the peer observes PEER_CLOSED -- this is what
  // makes RequireVersion (and other protocol errors) notify the other
  // side, matching real Mojo. HolePunch's trap on PEER_CLOSED then fires
  // the peer Connector's OnPeerClosed.
  pipe_.reset();
  if (connection_error_handler_) {
    std::move(connection_error_handler_).Run();
  }
}

void Connector::CloseMessagePipe() {
  CancelWatchers();
  pipe_.reset();
}

bool Connector::SyncWaitFor(base::RepeatingCallback<bool()> predicate,
                            base::TimeDelta timeout) {
  if (predicate.Run()) {
    return true;
  }
  class SyncWaitJob final : public base::JobTask {
   public:
    SyncWaitJob(Connector* c, base::RepeatingCallback<bool()> pred,
                base::TimeDelta timeout)
        : connector_(c),
          predicate_(std::move(pred)),
          timeout_(timeout),
          deadline_(timeout.is_max()
                        ? whp::TimeTicks::Now()
                        : whp::TimeTicks::Now() + timeout) {}

    void Run(base::JobDelegate* delegate) override {
      while (!predicate_.Run() && !connector_->error_) {
        if (delegate && delegate->ShouldYield()) {
          return;
        }
        whp::Executor::Current().RunUntilIdle();
        if (predicate_.Run() || connector_->error_) {
          return;
        }
        if (!timeout_.is_max()) {
          whp::TimeDelta left = deadline_ - whp::TimeTicks::Now();
          if (left.InMicroseconds() <= 0) {
            return;
          }
          if (!whp::Executor::Current().WaitForWork(left)) {
            return;
          }
        } else if (!whp::Executor::Current().WaitForWork(
                       whp::TimeDelta::FromMilliseconds(50))) {
          // Max wait: 50ms slices so ShouldYield/error can be observed.
          continue;
        }
      }
    }

    size_t GetMaxConcurrency(size_t worker_count) const override {
      return worker_count < 1 ? 1 : 0;
    }

   private:
    Connector* connector_;
    base::RepeatingCallback<bool()> predicate_;
    base::TimeDelta timeout_;
    whp::TimeTicks deadline_;
  };

  auto job = base::PostJob(
      base::TaskPriority::kUserBlocking,
      std::make_unique<SyncWaitJob>(this, predicate, timeout));
  if (job) {
    job->Join();
  }
  return predicate.Run() && !error_;
}

void Connector::CancelWatchers() {
  if (read_watcher_) {
    read_watcher_->Cancel();
    read_watcher_.reset();
  }
  if (closed_watcher_) {
    closed_watcher_->Cancel();
    closed_watcher_.reset();
  }
}

void Connector::OnReadable(MojoResult result) {
  if (paused_ || error_) {
    return;
  }
  if (result != MOJO_RESULT_OK) {
    RaiseError();
    return;
  }
  ReadAllAvailableMessages();
}

void Connector::OnPeerClosed(MojoResult) {
  // This watcher cancels itself here, every time -- MOJO_HANDLE_SIGNAL_
  // PEER_CLOSED, once satisfied, never becomes unsatisfied again, so an
  // automatic-policy re-arm on it would refire in a tight loop forever
  // (see StartReceiving()'s comment). Whatever's still buffered gets one
  // last drain before the error fires.
  if (closed_watcher_) {
    closed_watcher_->Cancel();
  }
  if (paused_ || error_) {
    return;
  }
  ReadAllAvailableMessages();
  RaiseError();
}

void Connector::ReadAllAvailableMessages() {
  for (;;) {
    v8::internal::CageBytes payload;
    std::vector<MojoHandle> raw_handles;
    MojoResult result = ReadMessageRaw(pipe_.get(), &payload, &raw_handles,
                                        MOJO_READ_MESSAGE_FLAG_NONE);
    if (result == MOJO_RESULT_SHOULD_WAIT) {
      return;
    }
    if (result != MOJO_RESULT_OK) {
      RaiseError();
      return;
    }

    std::vector<ScopedHandle> handles;
    handles.reserve(raw_handles.size());
    for (MojoHandle h : raw_handles) {
      handles.emplace_back(Handle(h));
    }

    Message message = Message::WrapWireBytes(std::move(payload),
                                              std::move(handles));
    if (message.IsNull()) {
      RaiseError();
      return;
    }
    if (incoming_receiver_) {
      internal::BadMessageDispatchScope dispatch(this, alive_);
      if (!incoming_receiver_->Accept(&message)) {
        RaiseError();
        return;
      }
    }
    if (error_ || paused_) {
      return;
    }
  }
}

}  // namespace mojo
