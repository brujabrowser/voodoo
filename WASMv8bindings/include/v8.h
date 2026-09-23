// Umbrella header, matching real V8's include/v8.h -- pulls in the whole
// facade surface. See each v8-*.h file's own comment for what's real vs.
// simplified about that piece, and the top-level README's "V8 embedder-API
// facade" section for the overall design (quickjs-ng underneath, cppgc for
// C++ wrapper object lifetime, CppHeapPointerTable for the JS<->C++
// handle).
#ifndef WASMV8_INCLUDE_V8_H_
#define WASMV8_INCLUDE_V8_H_

#include "v8-context.h"
#include "v8-exception.h"
#include "v8-function-callback.h"
#include "v8-function.h"
#include "v8-global.h"
#include "v8-isolate.h"
#include "v8-local-handle.h"
#include "v8-maybe.h"
#include "v8-object.h"
#include "v8-primitive.h"
#include "v8-property-callback.h"
#include "v8-sandbox.h"
#include "v8-script.h"
#include "v8-template.h"
#include "v8-value.h"

#endif  // WASMV8_INCLUDE_V8_H_
