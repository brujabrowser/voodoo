#include "mojo/public/cpp/bindings/associated_group.h"

#include "mojo/public/cpp/bindings/lib/multiplex_router.h"

#include <utility>

namespace mojo {

ScopedInterfaceEndpointHandle::ScopedInterfaceEndpointHandle() = default;

ScopedInterfaceEndpointHandle::ScopedInterfaceEndpointHandle(
    std::shared_ptr<MultiplexRouter> router,
    InterfaceId id)
    : router_(std::move(router)), id_(id) {
  if (router_) {
    router_->RetainEndpoint(id_);
  }
}

ScopedInterfaceEndpointHandle::~ScopedInterfaceEndpointHandle() {
  CloseIfNecessary();
}

ScopedInterfaceEndpointHandle::ScopedInterfaceEndpointHandle(
    ScopedInterfaceEndpointHandle&& other) noexcept
    : router_(std::move(other.router_)), id_(other.id_) {
  other.router_.reset();
  other.id_ = kInvalidInterfaceId;
}

ScopedInterfaceEndpointHandle& ScopedInterfaceEndpointHandle::operator=(
    ScopedInterfaceEndpointHandle&& other) noexcept {
  if (this != &other) {
    CloseIfNecessary();
    router_ = std::move(other.router_);
    id_ = other.id_;
    other.router_.reset();
    other.id_ = kInvalidInterfaceId;
  }
  return *this;
}

void ScopedInterfaceEndpointHandle::reset() {
  CloseIfNecessary();
  router_.reset();
  id_ = kInvalidInterfaceId;
}

InterfaceId ScopedInterfaceEndpointHandle::ReleaseWithoutClosing() {
  InterfaceId id = id_;
  if (router_) {
    router_->DropEndpointHandle(id_);
  }
  router_.reset();
  id_ = kInvalidInterfaceId;
  return id;
}

void ScopedInterfaceEndpointHandle::CloseIfNecessary() {
  if (router_) {
    router_->CloseEndpoint(id_);
  }
}

}  // namespace mojo
