// Proves Bindings type intern: type_key tokens intern to one id, intern
// records live in the WASMSafeSpace cage and are named on TPT, CHPT
// Name/Get is tag-checked (wrong type_key returns nullptr).
#include "test.h"

#include "mojo/public/cpp/bindings/lib/type_intern.h"

TEST(type_intern_same_key_is_same_id) {
  static char a;
  static char b;
  uint32_t id1 = mojo::internal::InternType(&a);
  uint32_t id2 = mojo::internal::InternType(&a);
  uint32_t id3 = mojo::internal::InternType(&b);
  EXPECT_EQ(id1, id2);
  EXPECT(id1 != id3);
}

TEST(type_intern_record_lives_in_the_cage_and_tpt) {
  static char k;
  const mojo::internal::InternedTypeRec* rec = mojo::internal::InternedType(&k);
  EXPECT(rec != nullptr);
  EXPECT(rec->type_key == &k);
  EXPECT(rec->intern_id == mojo::internal::InternType(&k));
  EXPECT(mojo::internal::TypeCage().Contains(rec));
}

TEST(type_intern_chpt_rejects_wrong_tag) {
  static char echo_key;
  static char listener_key;
  int echo_obj = 1;
  int listener_obj = 2;
  auto echo_h = mojo::internal::NameObject(&echo_obj, &echo_key);
  auto listener_h = mojo::internal::NameObject(&listener_obj, &listener_key);
  EXPECT(mojo::internal::GetObject(echo_h, &echo_key) == &echo_obj);
  EXPECT(mojo::internal::GetObject(listener_h, &listener_key) == &listener_obj);
  EXPECT(mojo::internal::GetObject(echo_h, &listener_key) == nullptr);
  EXPECT(mojo::internal::GetObject(listener_h, &echo_key) == nullptr);
  mojo::internal::FreeObject(echo_h);
  mojo::internal::FreeObject(listener_h);
}
