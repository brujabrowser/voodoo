// Proves the v5 codegen path -- nullable ('?') types -- by compiling the
// generated profile_interface_gen.h (from examples/profile/profile.voodoom)
// against real WASMCadidumBindings and actually running it. Covers both
// states (present and absent) of a nullable scalar-bearing struct field
// (Address.zip), a nullable struct field (Profile.address), a nullable
// top-level string (Profile.nickname), a nullable array
// (Profile.tags), and a nullable map (Profile.scores).
#include "test.h"

#include "profile_interface_gen.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "whp/base/executor.h"

namespace {

class ProfileStoreImpl : public profile::ProfileStore {
 public:
  void Put(const profile::Profile& p,
           base::OnceCallback<void(bool)> callback) override {
    stored_ = p;
    callback(true);
  }
  void Get(base::OnceCallback<void(profile::Profile)> callback) override {
    callback(stored_);
  }

 private:
  profile::Profile stored_;
};

void Pump(int iterations = 6) {
  for (int i = 0; i < iterations; ++i) {
    whp::Executor::Current().RunUntilIdle();
  }
}

}  // namespace

TEST(golden_nullable_fields_all_present_roundtrip) {
  ProfileStoreImpl impl;
  mojo::Receiver<profile::ProfileStore> receiver(&impl);
  mojo::Remote<profile::ProfileStore> remote;
  receiver.Bind(remote.BindNewPipeAndPassReceiver());

  profile::Profile p;
  p.name = "Ada";
  p.nickname = "The Countess";
  profile::Address addr;
  addr.city = "London";
  addr.zip = "SW1A";
  p.address = addr;
  p.tags = v8::internal::CageVector<std::string>{"pioneer", "mathematician"};
  p.scores = v8::internal::CageMap<std::string, int32_t>{{"analytical_engine", 100}};

  bool put_done = false;
  remote->Put(p, [&](bool) { put_done = true; });
  Pump();
  EXPECT(put_done);

  profile::Profile got;
  bool get_done = false;
  remote->Get([&](profile::Profile out) {
    got = out;
    get_done = true;
  });
  Pump();

  EXPECT(get_done);
  EXPECT_EQ(got.name, "Ada");
  EXPECT(got.nickname.has_value());
  if (got.nickname) EXPECT_EQ(*got.nickname, "The Countess");
  EXPECT(got.address.has_value());
  if (got.address) {
    EXPECT_EQ(got.address->city, "London");
    EXPECT(got.address->zip.has_value());
    if (got.address->zip) EXPECT_EQ(*got.address->zip, "SW1A");
  }
  EXPECT(got.tags.has_value());
  if (got.tags) EXPECT_EQ(got.tags->size(), 2u);
  EXPECT(got.scores.has_value());
  if (got.scores) EXPECT_EQ(got.scores->at("analytical_engine"), 100);
}

TEST(golden_nullable_fields_all_absent_roundtrip) {
  ProfileStoreImpl impl;
  mojo::Receiver<profile::ProfileStore> receiver(&impl);
  mojo::Remote<profile::ProfileStore> remote;
  receiver.Bind(remote.BindNewPipeAndPassReceiver());

  profile::Profile p;
  p.name = "Anonymous";
  // nickname, address, tags, scores all left as default (nullopt).

  bool put_done = false;
  remote->Put(p, [&](bool) { put_done = true; });
  Pump();
  EXPECT(put_done);

  profile::Profile got;
  got.nickname = "leftover";  // must be overwritten with nullopt, not left
  bool get_done = false;
  remote->Get([&](profile::Profile out) {
    got = out;
    get_done = true;
  });
  Pump();

  EXPECT(get_done);
  EXPECT_EQ(got.name, "Anonymous");
  EXPECT(!got.nickname.has_value());
  EXPECT(!got.address.has_value());
  EXPECT(!got.tags.has_value());
  EXPECT(!got.scores.has_value());
}

TEST(golden_nullable_struct_field_present_but_inner_nullable_absent) {
  // Address itself is present, but its own nullable field (zip) is absent
  // -- proves nullability nests correctly (Profile.address? -> present,
  // Address.zip? -> absent) rather than one flag controlling both.
  ProfileStoreImpl impl;
  mojo::Receiver<profile::ProfileStore> receiver(&impl);
  mojo::Remote<profile::ProfileStore> remote;
  receiver.Bind(remote.BindNewPipeAndPassReceiver());

  profile::Profile p;
  p.name = "NoZip";
  profile::Address addr;
  addr.city = "Nowhere";
  // addr.zip left as nullopt.
  p.address = addr;

  bool put_done = false;
  remote->Put(p, [&](bool) { put_done = true; });
  Pump();
  EXPECT(put_done);

  profile::Profile got;
  bool get_done = false;
  remote->Get([&](profile::Profile out) {
    got = out;
    get_done = true;
  });
  Pump();

  EXPECT(get_done);
  EXPECT(got.address.has_value());
  if (got.address) {
    EXPECT_EQ(got.address->city, "Nowhere");
    EXPECT(!got.address->zip.has_value());
  }
}
