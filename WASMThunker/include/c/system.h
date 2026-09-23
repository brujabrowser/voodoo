// Thunker is the c:: rung (mojo/public/c/system).
#ifndef C_SYSTEM_H_
#define C_SYSTEM_H_

#include "mojo/public/c/system/core.h"
#include "mojo/public/c/system/platform_handle.h"
#include "mojo/public/c/system/types.h"

namespace c {

using Handle = MojoHandle;
using Result = MojoResult;
using PlatformHandle = MojoPlatformHandle;
using PlatformHandleType = MojoPlatformHandleType;

}  // namespace c

#endif  // C_SYSTEM_H_
