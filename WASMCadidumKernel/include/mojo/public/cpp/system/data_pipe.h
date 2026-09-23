#ifndef MOJO_PUBLIC_CPP_SYSTEM_DATA_PIPE_H_
#define MOJO_PUBLIC_CPP_SYSTEM_DATA_PIPE_H_

#include "mojo/public/c/system/core.h"
#include "mojo/public/cpp/system/handle.h"

namespace mojo {

class DataPipeProducerHandle : public Handle {
 public:
  DataPipeProducerHandle() = default;
  explicit DataPipeProducerHandle(MojoHandle value) : Handle(value) {}
};
using ScopedDataPipeProducerHandle = ScopedHandleBase<DataPipeProducerHandle>;

class DataPipeConsumerHandle : public Handle {
 public:
  DataPipeConsumerHandle() = default;
  explicit DataPipeConsumerHandle(MojoHandle value) : Handle(value) {}
};
using ScopedDataPipeConsumerHandle = ScopedHandleBase<DataPipeConsumerHandle>;

inline MojoResult CreateDataPipe(const MojoCreateDataPipeOptions* options,
                                 ScopedDataPipeProducerHandle* producer,
                                 ScopedDataPipeConsumerHandle* consumer) {
  MojoHandle p = MOJO_HANDLE_INVALID;
  MojoHandle c = MOJO_HANDLE_INVALID;
  MojoResult result = MojoCreateDataPipe(options, &p, &c);
  if (result == MOJO_RESULT_OK) {
    producer->reset(DataPipeProducerHandle(p));
    consumer->reset(DataPipeConsumerHandle(c));
  }
  return result;
}

inline MojoResult WriteDataRaw(DataPipeProducerHandle producer,
                               const void* elements,
                               uint32_t* num_bytes,
                               const MojoWriteDataOptions* options) {
  return MojoWriteData(producer.value(), elements, num_bytes, options);
}

inline MojoResult ReadDataRaw(DataPipeConsumerHandle consumer,
                              void* elements,
                              uint32_t* num_bytes,
                              const MojoReadDataOptions* options) {
  return MojoReadData(consumer.value(), options, elements, num_bytes);
}

// NOTE: unlike some Mojo variants, whp::WhpBeginWriteData treats
// `*buffer_num_bytes` as in/out: set it to the max bytes you're willing to
// claim before calling, and it comes back with the number actually granted
// (which may be smaller, or MOJO_RESULT_SHOULD_WAIT if you passed 0 or the
// pipe is full).
inline MojoResult BeginWriteDataRaw(DataPipeProducerHandle producer,
                                    const MojoBeginWriteDataOptions* options,
                                    void** buffer,
                                    uint32_t* buffer_num_bytes) {
  return MojoBeginWriteData(producer.value(), options, buffer,
                            buffer_num_bytes);
}

inline MojoResult EndWriteDataRaw(DataPipeProducerHandle producer,
                                  uint32_t num_bytes_written,
                                  const MojoEndWriteDataOptions* options) {
  return MojoEndWriteData(producer.value(), num_bytes_written, options);
}

// Same in/out contract as BeginWriteDataRaw above: set *buffer_num_bytes to
// the max bytes you're willing to consume before calling.
inline MojoResult BeginReadDataRaw(DataPipeConsumerHandle consumer,
                                   const MojoBeginReadDataOptions* options,
                                   const void** buffer,
                                   uint32_t* buffer_num_bytes) {
  return MojoBeginReadData(consumer.value(), options, buffer,
                           buffer_num_bytes);
}

inline MojoResult EndReadDataRaw(DataPipeConsumerHandle consumer,
                                 uint32_t num_bytes_read,
                                 const MojoEndReadDataOptions* options) {
  return MojoEndReadData(consumer.value(), num_bytes_read, options);
}

}  // namespace mojo

#endif  // MOJO_PUBLIC_CPP_SYSTEM_DATA_PIPE_H_
