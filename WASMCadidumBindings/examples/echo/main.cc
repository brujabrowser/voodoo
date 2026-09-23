// Demo: a real Echo interface call (request/response over the primary
// interface) plus a real associated interface (EchoListener, handed to the
// server via SetListener and used to push a notification back) driven end
// to end through WASMCadidumBindings -> WASMCadidumKernel -> WASMThunker ->
// WASMHolePunch.
#include "echo_interface.h"
#include "mojo/public/c/system/core.h"
#include "mojo/public/cpp/bindings/associated_receiver.h"
#include "mojo/public/cpp/bindings/associated_remote.h"
#include "mojo/public/cpp/bindings/pending_associated_receiver.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "whp/base/executor.h"

#include <cstdio>
#include <string>

namespace {

class EchoListenerImpl : public echo::EchoListener {
 public:
  void OnEcho(const std::string& value) override {
    std::printf("listener notified: %s\n", value.c_str());
    ++notifications;
  }
  int notifications = 0;
};

class EchoImpl : public echo::Echo {
 public:
  void EchoString(const std::string& in,
                   base::OnceCallback<void(std::string)> callback) override {
    callback(in);
    if (listener_.is_bound()) {
      listener_->OnEcho("server saw: " + in);
    }
  }

  void SetListener(
      mojo::PendingAssociatedRemote<echo::EchoListener> listener) override {
    listener_.Bind(std::move(listener));
  }

 private:
  mojo::AssociatedRemote<echo::EchoListener> listener_;
};

}  // namespace

int main() {
  MojoInitialize(nullptr);

  EchoImpl echo_impl;
  mojo::Receiver<echo::Echo> echo_receiver(&echo_impl);
  mojo::Remote<echo::Echo> echo_remote;
  echo_receiver.Bind(echo_remote.BindNewPipeAndPassReceiver());

  EchoListenerImpl listener_impl;
  mojo::AssociatedReceiver<echo::EchoListener> listener_receiver_obj(
      &listener_impl);
  {
    mojo::AssociatedGroup group = echo_remote.associated_group();
    mojo::PendingAssociatedRemote<echo::EchoListener> listener_remote;
    mojo::PendingAssociatedReceiver<echo::EchoListener> listener_receiver =
        listener_remote.InitWithNewEndpointAndPassReceiver(group);
    echo_remote->SetListener(std::move(listener_remote));
    listener_receiver_obj.Bind(std::move(listener_receiver));
  }

  bool got_response = false;
  echo_remote->EchoString(
      "hello via WASMCadidumBindings", [&got_response](std::string out) {
        std::printf("echo response: %s\n", out.c_str());
        got_response = true;
      });

  for (int i = 0; i < 8 && !(got_response && listener_impl.notifications > 0);
       ++i) {
    whp::Executor::Current().RunUntilIdle();
  }

  MojoShutdown(nullptr);
  return (got_response && listener_impl.notifications == 1) ? 0 : 1;
}
