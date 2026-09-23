// JS bindings for mojo/public/cpp/system/invitation.h -- outgoing/incoming
// loopback invitation round-trip on the WASMv8bindings facade.
#ifndef WCK_BINDINGS_INVITATION_BINDINGS_H_
#define WCK_BINDINGS_INVITATION_BINDINGS_H_

#include "v8.h"

namespace wck_bindings {

void InstallInvitationBindings(v8::Isolate* isolate,
                               v8::Local<v8::Context> context);

}  // namespace wck_bindings

#endif  // WCK_BINDINGS_INVITATION_BINDINGS_H_
