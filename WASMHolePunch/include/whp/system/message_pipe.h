#ifndef WHP_SYSTEM_MESSAGE_PIPE_H_
#define WHP_SYSTEM_MESSAGE_PIPE_H_

#include "whp/system/handle.h"

namespace whp {

struct MessagePipe {
  ScopedMessagePipeHandle handle0;
  ScopedMessagePipeHandle handle1;

  MessagePipe() {
    WhpHandle a = WHP_HANDLE_INVALID;
    WhpHandle b = WHP_HANDLE_INVALID;
    WhpInit();
    if (WhpCreateMessagePipe(nullptr, &a, &b) == WHP_RESULT_OK) {
      handle0 = ScopedMessagePipeHandle(a);
      handle1 = ScopedMessagePipeHandle(b);
    }
  }
};

}  // namespace whp

#endif  // WHP_SYSTEM_MESSAGE_PIPE_H_
