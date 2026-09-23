// Demo: a real mojo::SimpleWatcher notification driven end to end through
// WASMCadidumKernel -> WASMThunker -> WASMHolePunch.
#include "mojo/public/cpp/system/message_pipe.h"
#include "mojo/public/cpp/system/simple_watcher.h"
#include "whp/base/executor.h"

#include <cstdio>

int main() {
  MojoInitialize(nullptr);

  mojo::ScopedMessagePipeHandle a, b;
  mojo::CreateMessagePipe(nullptr, &a, &b);

  mojo::SimpleWatcher watcher(mojo::SimpleWatcher::ArmingPolicy::kAutomatic);
  watcher.Watch(b.get(), MOJO_HANDLE_SIGNAL_READABLE, [&](MojoResult result) {
    v8::internal::CageBytes payload;
    std::vector<MojoHandle> handles;
    mojo::ReadMessageRaw(b.get(), &payload, &handles,
                         MOJO_READ_MESSAGE_FLAG_NONE);
    std::printf("watcher fired (result=%u): %.*s\n",
                static_cast<unsigned>(result),
                static_cast<int>(payload.size()),
                reinterpret_cast<const char*>(payload.data()));
  });

  const char msg[] = "hello via SimpleWatcher";
  mojo::WriteMessageRaw(a.get(), msg, sizeof(msg) - 1, nullptr, 0,
                       MOJO_WRITE_MESSAGE_FLAG_NONE);

  whp::Executor::Current().RunUntilIdle();

  MojoShutdown(nullptr);
  return 0;
}
