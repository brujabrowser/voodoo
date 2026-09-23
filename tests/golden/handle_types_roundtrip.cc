// Proves real-mojom-parity phase 4's codegen path -- the `handle` type
// (bare, `handle<message_pipe>`, and `handle<shared_buffer>`, both
// non-nullable and nullable) -- by compiling the generated
// handle_types_gen.h (from examples/handle_types/handle_types.voodoom)
// against real WASMCadidumBindings/WASMCadidumKernel and actually
// transiting live resources through it: a bare `handle`/
// `handle<message_pipe>` is rewrapped on the receiving end into a real
// mojo::PendingReceiver<Pingback> and completes a genuine Ping() round
// trip (proving it's the exact live pipe endpoint, not a dup or
// placeholder); a `handle<shared_buffer>` is mapped on both ends and a
// value written on the sender side is read back on the receiver side
// (proving it's the exact live shared buffer).
#include "test.h"

#include "handle_types_gen.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "mojo/public/cpp/system/buffer.h"
#include "mojo/public/cpp/system/message_pipe.h"
#include "mojo/public/cpp/system/platform_handle.h"
#include "whp/base/executor.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace {

class PingbackImpl : public handle_types::Pingback {
 public:
  void Ping() override { ++ping_count; }
  int ping_count = 0;
};

class HandleSinkImpl : public handle_types::HandleSink {
 public:
  void SendAny(mojo::ScopedHandle h) override {
    ++send_any_calls;
    last_any = std::move(h);
  }
  void SendAnyOptional(mojo::ScopedHandle h) override {
    ++send_any_optional_calls;
    last_any_optional_present = h.is_valid();
    if (h.is_valid()) last_any_optional = std::move(h);
  }
  void SendPipe(mojo::ScopedMessagePipeHandle p) override {
    ++send_pipe_calls;
    last_pipe = std::move(p);
  }
  void SendPipeOptional(mojo::ScopedMessagePipeHandle p) override {
    ++send_pipe_optional_calls;
    last_pipe_optional_present = p.is_valid();
    if (p.is_valid()) last_pipe_optional = std::move(p);
  }
  void SendBuffer(mojo::ScopedSharedBufferHandle b) override {
    ++send_buffer_calls;
    last_buffer = std::move(b);
  }
  void SendBufferOptional(mojo::ScopedSharedBufferHandle b) override {
    ++send_buffer_optional_calls;
    last_buffer_optional_present = b.is_valid();
    if (b.is_valid()) last_buffer_optional = std::move(b);
  }
  void SendPlatform(mojo::PlatformHandle p) override {
    ++send_platform_calls;
    last_platform = std::move(p);
  }

  int send_any_calls = 0;
  mojo::ScopedHandle last_any;
  int send_any_optional_calls = 0;
  bool last_any_optional_present = false;
  mojo::ScopedHandle last_any_optional;
  int send_pipe_calls = 0;
  mojo::ScopedMessagePipeHandle last_pipe;
  int send_pipe_optional_calls = 0;
  bool last_pipe_optional_present = false;
  mojo::ScopedMessagePipeHandle last_pipe_optional;
  int send_buffer_calls = 0;
  mojo::ScopedSharedBufferHandle last_buffer;
  int send_buffer_optional_calls = 0;
  bool last_buffer_optional_present = false;
  mojo::ScopedSharedBufferHandle last_buffer_optional;
  int send_platform_calls = 0;
  mojo::PlatformHandle last_platform;
};

void Pump(int iterations = 6) {
  for (int i = 0; i < iterations; ++i) {
    whp::Executor::Current().RunUntilIdle();
  }
}

// Rewraps a raw message-pipe-shaped handle (received as a bare `handle`
// or `handle<message_pipe>`) into a mojo::PendingReceiver<Pingback>,
// binds it, and pumps a single Ping() sent over `remote_pipe` (the other
// endpoint) -- a real Pingback delivery is only possible if `received`
// really is the exact live pipe endpoint the sender attached.
void ProvePipeIsLive(mojo::ScopedHandle received,
                      mojo::ScopedMessagePipeHandle remote_pipe) {
  mojo::ScopedMessagePipeHandle receiver_pipe(
      mojo::MessagePipeHandle(received.release().value()));
  PingbackImpl impl;
  mojo::PendingReceiver<handle_types::Pingback> pending_receiver(
      std::move(receiver_pipe));
  mojo::Receiver<handle_types::Pingback> receiver(&impl);
  receiver.Bind(std::move(pending_receiver));

  mojo::PendingRemote<handle_types::Pingback> pending_remote(
      std::move(remote_pipe), 0);
  mojo::Remote<handle_types::Pingback> remote;
  remote.Bind(std::move(pending_remote));

  remote->Ping();
  Pump();

  EXPECT_EQ(impl.ping_count, 1);
}

}  // namespace

TEST(golden_bare_handle_transmits_live_pipe_endpoint) {
  HandleSinkImpl impl;
  mojo::Receiver<handle_types::HandleSink> receiver(&impl);
  mojo::Remote<handle_types::HandleSink> remote;
  receiver.Bind(remote.BindNewPipeAndPassReceiver());

  mojo::ScopedMessagePipeHandle a;
  mojo::ScopedMessagePipeHandle b;
  mojo::CreateMessagePipe(nullptr, &a, &b);

  mojo::ScopedHandle a_as_handle(a.release());
  remote->SendAny(std::move(a_as_handle));
  Pump();

  EXPECT_EQ(impl.send_any_calls, 1);
  EXPECT(impl.last_any.is_valid());

  ProvePipeIsLive(std::move(impl.last_any), std::move(b));
}

TEST(golden_nullable_bare_handle_absent_transmits_cleanly) {
  HandleSinkImpl impl;
  mojo::Receiver<handle_types::HandleSink> receiver(&impl);
  mojo::Remote<handle_types::HandleSink> remote;
  receiver.Bind(remote.BindNewPipeAndPassReceiver());

  remote->SendAnyOptional(mojo::ScopedHandle());
  Pump();
  EXPECT_EQ(impl.send_any_optional_calls, 1);
  EXPECT(!impl.last_any_optional_present);

  // The connection must still work normally afterward -- an absent
  // handle mustn't have desynced the message stream.
  remote->SendAnyOptional(mojo::ScopedHandle());
  Pump();
  EXPECT_EQ(impl.send_any_optional_calls, 2);
}

TEST(golden_nullable_bare_handle_present_transmits_live_pipe_endpoint) {
  HandleSinkImpl impl;
  mojo::Receiver<handle_types::HandleSink> receiver(&impl);
  mojo::Remote<handle_types::HandleSink> remote;
  receiver.Bind(remote.BindNewPipeAndPassReceiver());

  mojo::ScopedMessagePipeHandle a;
  mojo::ScopedMessagePipeHandle b;
  mojo::CreateMessagePipe(nullptr, &a, &b);

  mojo::ScopedHandle a_as_handle(a.release());
  remote->SendAnyOptional(std::move(a_as_handle));
  Pump();

  EXPECT_EQ(impl.send_any_optional_calls, 1);
  EXPECT(impl.last_any_optional_present);

  ProvePipeIsLive(std::move(impl.last_any_optional), std::move(b));
}

TEST(golden_handle_message_pipe_transmits_live_pipe_endpoint) {
  HandleSinkImpl impl;
  mojo::Receiver<handle_types::HandleSink> receiver(&impl);
  mojo::Remote<handle_types::HandleSink> remote;
  receiver.Bind(remote.BindNewPipeAndPassReceiver());

  mojo::ScopedMessagePipeHandle a;
  mojo::ScopedMessagePipeHandle b;
  mojo::CreateMessagePipe(nullptr, &a, &b);

  remote->SendPipe(std::move(a));
  Pump();

  EXPECT_EQ(impl.send_pipe_calls, 1);
  EXPECT(impl.last_pipe.is_valid());

  mojo::ScopedHandle as_handle(impl.last_pipe.release());
  ProvePipeIsLive(std::move(as_handle), std::move(b));
}

TEST(golden_nullable_handle_message_pipe_absent_transmits_cleanly) {
  HandleSinkImpl impl;
  mojo::Receiver<handle_types::HandleSink> receiver(&impl);
  mojo::Remote<handle_types::HandleSink> remote;
  receiver.Bind(remote.BindNewPipeAndPassReceiver());

  remote->SendPipeOptional(mojo::ScopedMessagePipeHandle());
  Pump();
  EXPECT_EQ(impl.send_pipe_optional_calls, 1);
  EXPECT(!impl.last_pipe_optional_present);

  remote->SendPipeOptional(mojo::ScopedMessagePipeHandle());
  Pump();
  EXPECT_EQ(impl.send_pipe_optional_calls, 2);
}

TEST(golden_handle_shared_buffer_shares_live_memory) {
  HandleSinkImpl impl;
  mojo::Receiver<handle_types::HandleSink> receiver(&impl);
  mojo::Remote<handle_types::HandleSink> remote;
  receiver.Bind(remote.BindNewPipeAndPassReceiver());

  mojo::ScopedSharedBufferHandle buf;
  MojoResult create_result = mojo::CreateSharedBuffer(4096, nullptr, &buf);
  EXPECT_EQ(create_result, MOJO_RESULT_OK);
  if (create_result != MOJO_RESULT_OK) return;

  mojo::ScopedSharedBufferMapping write_mapping;
  EXPECT_EQ(mojo::MapBuffer(buf.get(), 0, 4096, &write_mapping),
            MOJO_RESULT_OK);
  if (write_mapping.is_valid()) {
    static_cast<uint8_t*>(write_mapping.get())[0] = 0xAB;
    write_mapping.reset();
  }

  remote->SendBuffer(std::move(buf));
  Pump();

  EXPECT_EQ(impl.send_buffer_calls, 1);
  EXPECT(impl.last_buffer.is_valid());
  if (!impl.last_buffer.is_valid()) return;

  mojo::ScopedSharedBufferMapping read_mapping;
  EXPECT_EQ(mojo::MapBuffer(impl.last_buffer.get(), 0, 4096, &read_mapping),
            MOJO_RESULT_OK);
  if (read_mapping.is_valid()) {
    EXPECT_EQ(static_cast<uint8_t*>(read_mapping.get())[0], 0xAB);
  }
}

TEST(golden_nullable_handle_shared_buffer_absent_transmits_cleanly) {
  HandleSinkImpl impl;
  mojo::Receiver<handle_types::HandleSink> receiver(&impl);
  mojo::Remote<handle_types::HandleSink> remote;
  receiver.Bind(remote.BindNewPipeAndPassReceiver());

  remote->SendBufferOptional(mojo::ScopedSharedBufferHandle());
  Pump();
  EXPECT_EQ(impl.send_buffer_optional_calls, 1);
  EXPECT(!impl.last_buffer_optional_present);

  remote->SendBufferOptional(mojo::ScopedSharedBufferHandle());
  Pump();
  EXPECT_EQ(impl.send_buffer_optional_calls, 2);
}

TEST(golden_handle_platform_wrap_transits_live_os_handle) {
  HandleSinkImpl impl;
  mojo::Receiver<handle_types::HandleSink> receiver(&impl);
  mojo::Remote<handle_types::HandleSink> remote;
  receiver.Bind(remote.BindNewPipeAndPassReceiver());

  MojoPlatformHandle native{};
  native.struct_size = sizeof(native);
#ifdef _WIN32
  HANDLE ev = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  EXPECT(ev != nullptr);
  native.type = MOJO_PLATFORM_HANDLE_TYPE_WINDOWS_HANDLE;
  native.value = reinterpret_cast<uint64_t>(ev);
#else
  int fds[2];
  EXPECT_EQ(pipe(fds), 0);
  close(fds[1]);
  native.type = MOJO_PLATFORM_HANDLE_TYPE_FILE_DESCRIPTOR;
  native.value = static_cast<uint64_t>(fds[0]);
#endif

  remote->SendPlatform(mojo::PlatformHandle::Wrap(native));
  Pump();

  EXPECT_EQ(impl.send_platform_calls, 1);
  EXPECT(impl.last_platform.is_valid());
  MojoPlatformHandle out{};
  EXPECT(std::move(impl.last_platform).Unwrap(&out));
  EXPECT_EQ(out.value, native.value);
#ifdef _WIN32
  CloseHandle(reinterpret_cast<HANDLE>(static_cast<uintptr_t>(out.value)));
#else
  close(static_cast<int>(out.value));
#endif
}
