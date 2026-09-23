# Chromium reference sources

These files are **spec only**. They are copies of Chromium
`mojo/public/cpp/bindings` sources and are **not compiled** into WASMHolePunch.

They document the wire format and control-message behavior we reimplement:

| File | Chromium path |
|---|---|
| `message.cc` | `mojo/public/cpp/bindings/message.cc` |
| `message_header.cc` | `mojo/public/cpp/bindings/message_header_validator.cc` |
| `controlproxy.cc` | `mojo/public/cpp/bindings/lib/control_message_proxy.cc` |
| `send_message.cc` | `mojo/public/cpp/bindings/lib/send_message_helper.cc` |
| `raw_ptr.cc` | `mojo/public/cpp/bindings/raw_ptr_impl_ref_traits.h` |

Copyright The Chromium Authors. BSD-style license as in the Chromium tree.
