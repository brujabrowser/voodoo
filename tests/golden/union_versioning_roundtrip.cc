// Proves the v11 codegen path -- union `[MinVersion=N]`/`[Extensible]` --
// by compiling three generated headers (from status_v1_closed.voodoom,
// status_v1_extensible.voodoom, and status_v2.voodoom) against real
// WASMCadidumBindings. Same "two schemas of the same type, different
// namespaces, one test binary" approach as
// tests/golden/versioning_roundtrip.cc uses for structs.
#include "test.h"

#include "status_v1_closed_interface_gen.h"
#include "status_v1_extensible_interface_gen.h"
#include "status_v2_interface_gen.h"
#include "mojo/public/cpp/bindings/lib/wire_primitives.h"
#include "mojo/public/cpp/bindings/message.h"

TEST(golden_extensible_union_reads_unknown_tag_as_kunknown) {
  status_v2::Status sent;
  sent.set_retry(true);  // the variant neither v1 schema has ever heard of

  mojo::Message message(/*name=*/1, /*flags=*/0);
  status_v2::WriteStatus(message, sent);
  // A marker written after the union, in the same message -- proves the
  // extensible reader's offset skip is exact, the same thing
  // versioning_roundtrip.cc checks for struct forward-compat.
  int32_t marker = 777;
  mojo::internal::WriteScalar(&message, marker);

  status_v1_extensible::Status got{};
  size_t offset = 0;
  bool ok = status_v1_extensible::ReadStatus(message, &offset, &got);
  EXPECT(ok);
  EXPECT(got.which() == status_v1_extensible::Status::Tag::kUnknown);

  int32_t read_marker = 0;
  bool marker_ok =
      mojo::internal::ReadScalar(message, &offset, &read_marker);
  EXPECT(marker_ok);
  EXPECT_EQ(read_marker, 777);
}

TEST(golden_non_extensible_union_read_fails_on_unknown_tag) {
  status_v2::Status sent;
  sent.set_retry(false);

  mojo::Message message(/*name=*/2, /*flags=*/0);
  status_v2::WriteStatus(message, sent);

  status_v1_closed::Status got{};
  size_t offset = 0;
  bool ok = status_v1_closed::ReadStatus(message, &offset, &got);
  EXPECT(!ok);
}

TEST(golden_known_tag_still_roundtrips_through_extensible_union) {
  // [Extensible] must not break the ordinary case -- a tag both schemas
  // know about still round-trips normally.
  status_v2::Status sent;
  sent.set_code(42);

  mojo::Message message(/*name=*/3, /*flags=*/0);
  status_v2::WriteStatus(message, sent);

  status_v1_extensible::Status got{};
  size_t offset = 0;
  bool ok = status_v1_extensible::ReadStatus(message, &offset, &got);
  EXPECT(ok);
  EXPECT(got.which() == status_v1_extensible::Status::Tag::code);
  EXPECT_EQ(got.code(), 42);
}

TEST(golden_union_kversion_constant) {
  EXPECT_EQ(status_v1_closed::Status::kVersion, 0u);
  EXPECT_EQ(status_v1_extensible::Status::kVersion, 0u);
  EXPECT_EQ(status_v2::Status::kVersion, 1u);
}
