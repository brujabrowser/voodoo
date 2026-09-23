// Minimal end-to-end demo: drive a real message pipe through the public
// Mojo* C ABI, backed entirely by WASMThunker -> WASMHolePunch.
#include "mojo/public/c/system/core.h"

#include <cstdio>
#include <cstring>

int main() {
  MojoInitialize(nullptr);

  MojoHandle a = MOJO_HANDLE_INVALID;
  MojoHandle b = MOJO_HANDLE_INVALID;
  MojoCreateMessagePipe(nullptr, &a, &b);

  MojoMessageHandle msg = MOJO_MESSAGE_HANDLE_INVALID;
  MojoCreateMessage(nullptr, &msg);
  MojoAppendMessageDataOptions opts{};
  opts.struct_size = sizeof(opts);
  opts.flags = MOJO_APPEND_MESSAGE_DATA_FLAG_COMMIT_SIZE;
  void* buf = nullptr;
  uint32_t sz = 0;
  const char payload[] = "hello over the Mojo C ABI";
  MojoAppendMessageData(msg, sizeof(payload) - 1, nullptr, 0, &opts, &buf, &sz);
  std::memcpy(buf, payload, sizeof(payload) - 1);

  MojoWriteMessage(a, msg, nullptr);

  MojoMessageHandle got = MOJO_MESSAGE_HANDLE_INVALID;
  MojoReadMessage(b, nullptr, &got);
  void* rbuf = nullptr;
  uint32_t rn = 0;
  MojoGetMessageData(got, nullptr, &rbuf, &rn, nullptr, nullptr);
  std::printf("received %u bytes: %.*s\n", rn, static_cast<int>(rn),
              static_cast<const char*>(rbuf));

  MojoDestroyMessage(got);
  MojoClose(a);
  MojoClose(b);
  MojoShutdown(nullptr);
  return 0;
}
