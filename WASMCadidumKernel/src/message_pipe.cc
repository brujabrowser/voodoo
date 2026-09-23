#include "mojo/public/cpp/system/message_pipe.h"

#include <cstring>

namespace mojo {

MojoResult WriteMessageRaw(MessagePipeHandle pipe,
                           const void* bytes,
                           uint32_t num_bytes,
                           const MojoHandle* handles,
                           uint32_t num_handles,
                           MojoWriteMessageFlags flags) {
  MojoMessageHandle message = MOJO_MESSAGE_HANDLE_INVALID;
  MojoResult result = MojoCreateMessage(nullptr, &message);
  if (result != MOJO_RESULT_OK) {
    return result;
  }

  MojoAppendMessageDataOptions options{};
  options.struct_size = sizeof(options);
  options.flags = MOJO_APPEND_MESSAGE_DATA_FLAG_COMMIT_SIZE;
  void* buffer = nullptr;
  uint32_t buffer_size = 0;
  result = MojoAppendMessageData(message, num_bytes, handles, num_handles,
                                 &options, &buffer, &buffer_size);
  if (result != MOJO_RESULT_OK) {
    MojoDestroyMessage(message);
    return result;
  }
  if (num_bytes > 0) {
    std::memcpy(buffer, bytes, num_bytes);
  }

  MojoWriteMessageOptions write_options{};
  write_options.struct_size = sizeof(write_options);
  write_options.flags = flags;
  return MojoWriteMessage(pipe.value(), message, &write_options);
}

MojoResult ReadMessageRaw(MessagePipeHandle pipe,
                          v8::internal::CageBytes* payload,
                          std::vector<MojoHandle>* handles,
                          MojoReadMessageFlags flags) {
  MojoReadMessageOptions options{};
  options.struct_size = sizeof(options);
  options.flags = flags;

  MojoMessageHandle message = MOJO_MESSAGE_HANDLE_INVALID;
  MojoResult result = MojoReadMessage(pipe.value(), &options, &message);
  if (result != MOJO_RESULT_OK) {
    return result;
  }

  void* buffer = nullptr;
  uint32_t num_bytes = 0;
  uint32_t num_handles = 0;
  MojoGetMessageData(message, nullptr, &buffer, &num_bytes, nullptr,
                     &num_handles);

  payload->clear();
  if (num_bytes > 0) {
    payload->resize(num_bytes);
  }
  handles->clear();
  if (num_handles > 0) {
    handles->resize(num_handles);
  }

  result = MojoGetMessageData(
      message, nullptr, &buffer, &num_bytes,
      num_handles > 0 ? handles->data() : nullptr, &num_handles);
  if (result == MOJO_RESULT_OK && num_bytes > 0) {
    std::memcpy(payload->data(), buffer, num_bytes);
  }

  MojoDestroyMessage(message);
  return result;
}

}  // namespace mojo
