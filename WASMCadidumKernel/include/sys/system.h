// CadidumKernel is the sys:: rung (mojo/public/cpp/system).
#ifndef SYS_SYSTEM_H_
#define SYS_SYSTEM_H_

#include "mojo/public/cpp/system/buffer.h"
#include "mojo/public/cpp/system/data_pipe.h"
#include "mojo/public/cpp/system/handle.h"
#include "mojo/public/cpp/system/message_pipe.h"
#include "mojo/public/cpp/system/platform_handle.h"
#include "mojo/public/cpp/system/simple_watcher.h"

namespace sys {

using mojo::Handle;
using mojo::ScopedHandle;
using mojo::PlatformHandle;
using mojo::MessagePipeHandle;
using mojo::ScopedMessagePipeHandle;
using mojo::DataPipeProducerHandle;
using mojo::DataPipeConsumerHandle;
using mojo::ScopedDataPipeProducerHandle;
using mojo::ScopedDataPipeConsumerHandle;
using mojo::SharedBufferHandle;
using mojo::ScopedSharedBufferHandle;
using mojo::SimpleWatcher;

}  // namespace sys

#endif  // SYS_SYSTEM_H_
