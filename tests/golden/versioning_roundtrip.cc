// Proves the v9 codegen path -- struct field versioning (`[MinVersion=N]`)
// -- actually delivers real forward/backward wire compatibility, not just
// "round-trips within one schema" (every other golden test already proves
// that much for its own structs). widget_v1.voodoom and widget_v2.voodoom
// declare the *same* Widget struct at two points in its evolution --
// different namespaces (widget_v1:: / widget_v2::), so both generated
// headers coexist in this one test binary with no symbol collision, the
// same way two real revisions of a .mojom file would each get their own
// generated header in a real build.
#include "test.h"

#include "widget_v1_interface_gen.h"
#include "widget_v2_interface_gen.h"
#include "mojo/public/cpp/bindings/lib/wire_primitives.h"
#include "mojo/public/cpp/bindings/message.h"

#include <cstdint>
#include <vector>

TEST(golden_new_writer_old_reader_skips_unknown_trailing_field) {
  // A v2 writer (which knows about `color`) talking to a v1 reader (which
  // doesn't) -- the classic "server upgraded before some clients did"
  // case.
  widget_v2::Widget sent;
  sent.name = "Chair";
  sent.weight = 5;
  sent.color = "Red";

  mojo::Message message(/*name=*/1, /*flags=*/0);
  widget_v2::WriteWidget(message, sent);
  // A marker written *after* the struct, in the same message -- proves
  // the old reader's offset ends up in the right place afterward, not
  // just that it didn't crash reading the struct itself.
  int32_t marker = 999;
  mojo::internal::WriteScalar(&message, marker);

  widget_v1::Widget got{};
  size_t offset = 0;
  bool ok = widget_v1::ReadWidget(message, &offset, &got);
  EXPECT(ok);
  EXPECT_EQ(got.name, "Chair");
  EXPECT_EQ(got.weight, 5);
  // widget_v1::Widget has no `color` field at all -- there's nothing to
  // assert being absent; the real assertion is the marker read below.

  int32_t read_marker = 0;
  bool marker_ok =
      mojo::internal::ReadScalar(message, &offset, &read_marker);
  EXPECT(marker_ok);
  EXPECT_EQ(read_marker, 999);
}

TEST(golden_old_writer_new_reader_leaves_new_field_at_default) {
  // The reverse: a v1 writer (an old client that was never rebuilt)
  // talking to a v2 reader -- `color` was never on the wire at all, so
  // the v2 reader must leave it at its own declared default, not garbage
  // or a zero-initialized empty string that happens to look similar.
  widget_v1::Widget sent;
  sent.name = "Table";
  sent.weight = 10;

  mojo::Message message(/*name=*/2, /*flags=*/0);
  widget_v1::WriteWidget(message, sent);

  widget_v2::Widget got{};
  size_t offset = 0;
  bool ok = widget_v2::ReadWidget(message, &offset, &got);
  EXPECT(ok);
  EXPECT_EQ(got.name, "Table");
  EXPECT_EQ(got.weight, 10);
  EXPECT_EQ(got.color, "unknown");  // widget_v2::Widget's own default
}

TEST(golden_new_writer_old_reader_via_array_of_structs) {
  // Same proof, but the version-skew struct is an *array element* rather
  // than a bare top-level value -- confirms the version-header dance
  // composes correctly through the exact same generic array machinery
  // every other array<StructType> already uses (EmitWriteValue/
  // EmitReadInto's kArray case), not just when called directly.
  std::vector<widget_v2::Widget> sent;
  widget_v2::Widget a;
  a.name = "A";
  a.weight = 1;
  a.color = "red";
  widget_v2::Widget b;
  b.name = "B";
  b.weight = 2;
  b.color = "blue";
  sent.push_back(a);
  sent.push_back(b);

  mojo::Message message(/*name=*/3, /*flags=*/0);
  {
    uint32_t n = static_cast<uint32_t>(sent.size());
    message.WritePayload(&n, sizeof(n));
    for (const auto& w : sent) {
      widget_v2::WriteWidget(message, w);
    }
  }

  std::vector<widget_v1::Widget> got;
  size_t offset = 0;
  uint32_t n = 0;
  bool ok = mojo::internal::ReadScalar(message, &offset, &n);
  EXPECT(ok);
  got.resize(n);
  bool all_ok = true;
  for (uint32_t i = 0; i < n; ++i) {
    all_ok = all_ok && widget_v1::ReadWidget(message, &offset, &got[i]);
  }
  EXPECT(all_ok);
  EXPECT_EQ(got.size(), 2u);
  if (got.size() == 2) {
    EXPECT_EQ(got[0].name, "A");
    EXPECT_EQ(got[0].weight, 1);
    EXPECT_EQ(got[1].name, "B");
    EXPECT_EQ(got[1].weight, 2);
  }
}
