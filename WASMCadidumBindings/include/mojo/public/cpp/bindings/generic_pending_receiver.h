// GenericPendingReceiver: a PendingReceiver whose interface name is a
// string ( intern_key() for generated types). Matching Chromium's
// mojo/public/cpp/bindings/generic_pending_receiver.h.
#ifndef MOJO_PUBLIC_CPP_BINDINGS_GENERIC_PENDING_RECEIVER_H_
#define MOJO_PUBLIC_CPP_BINDINGS_GENERIC_PENDING_RECEIVER_H_

#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/system/message_pipe.h"

#include <optional>
#include <string>
#include <type_traits>
#include <utility>

namespace mojo {

class GenericPendingReceiver {
 public:
  GenericPendingReceiver() = default;
  GenericPendingReceiver(std::string interface_name,
                         ScopedMessagePipeHandle pipe)
      : interface_name_(std::move(interface_name)), pipe_(std::move(pipe)) {}

  template <typename Interface>
  explicit GenericPendingReceiver(PendingReceiver<Interface> pending)
      : interface_name_(NameOf<Interface>()),
        pipe_(pending.PassPipe()) {}

  GenericPendingReceiver(GenericPendingReceiver&&) noexcept = default;
  GenericPendingReceiver& operator=(GenericPendingReceiver&&) noexcept =
      default;
  GenericPendingReceiver(const GenericPendingReceiver&) = delete;
  GenericPendingReceiver& operator=(const GenericPendingReceiver&) = delete;

  bool is_valid() const { return pipe_.is_valid(); }
  explicit operator bool() const { return is_valid(); }

  const std::string& interface_name() const { return interface_name_; }

  template <typename Interface>
  std::optional<PendingReceiver<Interface>> As() {
    const char* want = NameOf<Interface>();
    if (!pipe_.is_valid()) {
      return std::nullopt;
    }
    if (want[0] != '\0' && interface_name_ != want) {
      return std::nullopt;
    }
    return PendingReceiver<Interface>(std::move(pipe_));
  }

  void reset() {
    interface_name_.clear();
    pipe_.reset();
  }

 private:
  template <typename T, typename = void>
  struct has_intern_key : std::false_type {};
  template <typename T>
  struct has_intern_key<T, std::void_t<decltype(T::intern_key())>>
      : std::true_type {};

  template <typename Interface>
  static const char* NameOf() {
    if constexpr (has_intern_key<Interface>::value) {
      return Interface::intern_key();
    }
    return "";
  }

  std::string interface_name_;
  ScopedMessagePipeHandle pipe_;
};

}  // namespace mojo

#endif  // MOJO_PUBLIC_CPP_BINDINGS_GENERIC_PENDING_RECEIVER_H_
