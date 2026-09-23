#include "test.h"

#include "../src/lexer.h"
#include "../src/parser.h"
#include "../src/type_intern.h"

using namespace voodoom;

TEST(type_intern_same_shape_is_same_id) {
  TypeIntern t;
  TypeSpec a;
  a.kind = TypeKind::kInt32;
  TypeSpec b;
  b.kind = TypeKind::kInt32;
  EXPECT(t.Identical(t.Intern(a, "echo"), t.Intern(b, "echo")));
  EXPECT_EQ(t.size(), 1u);
}

TEST(type_intern_named_types_intern_by_name_not_shape) {
  TypeIntern t;
  uint32_t duration = t.InternNamed("time", "Duration", "struct");
  uint32_t also = t.InternNamed("time", "Duration", "struct");
  uint32_t other = t.InternNamed("time", "Instant", "struct");
  EXPECT(t.Identical(duration, also));
  EXPECT(!t.Identical(duration, other));
  EXPECT_EQ(t.Key(duration), std::string("struct:time.Duration"));
}

TEST(type_intern_pending_kinds_are_distinct) {
  TypeIntern t;
  TypeSpec remote;
  remote.kind = TypeKind::kPendingRemote;
  remote.name = "Echo";
  TypeSpec recv;
  recv.kind = TypeKind::kPendingReceiver;
  recv.name = "Echo";
  EXPECT(!t.Identical(t.Intern(remote, "echo"), t.Intern(recv, "echo")));
}

TEST(type_intern_array_fixed_vs_dynamic) {
  TypeIntern t;
  TypeSpec elem;
  elem.kind = TypeKind::kInt32;
  TypeSpec dyn;
  dyn.kind = TypeKind::kArray;
  dyn.element = std::make_shared<TypeSpec>(elem);
  TypeSpec fixed = dyn;
  fixed.fixed_array_size = 4;
  EXPECT(!t.Identical(t.Intern(dyn, ""), t.Intern(fixed, "")));
  EXPECT_EQ(t.Key(t.Intern(fixed, "")), std::string("array<int32,4>"));
}

TEST(type_intern_method_signature) {
  Module m = Parse(Tokenize(
      "module echo;\n"
      "interface Echo {\n"
      "  EchoString(string in) => (string out);\n"
      "};\n"));
  TypeIntern t;
  uint32_t a = t.InternMethod("echo", "Echo", m.interfaces[0].methods[0]);
  uint32_t b = t.InternMethod("echo", "Echo", m.interfaces[0].methods[0]);
  EXPECT(t.Identical(a, b));
  EXPECT_EQ(t.Key(a),
            std::string("func echo.Echo.EchoString(string)->(string)"));
}

TEST(type_intern_nullable_is_a_distinct_shape) {
  TypeIntern t;
  TypeSpec s;
  s.kind = TypeKind::kString;
  TypeSpec n = s;
  n.nullable = true;
  EXPECT(!t.Identical(t.Intern(s, ""), t.Intern(n, "")));
  EXPECT_EQ(t.Key(t.Intern(n, "")), std::string("string?"));
}
