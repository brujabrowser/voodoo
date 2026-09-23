#include "test.h"

#include "../src/lexer.h"
#include "../src/parser.h"

#include <unordered_set>

using namespace voodoom;

namespace {
Module ParseSrc(const std::string& src) { return Parse(Tokenize(src)); }

// (v15) Parses `src` seeded with `prelude`, the way module_loader.cc seeds
// an importing file with everything loaded before it -- mirrors
// module_loader.cc's LoadFileInto (ParseHeader, then ParseBody(&prelude)),
// minus any actual import resolution (the tests using this build `prelude`
// by hand instead).
Module ParseWithPrelude(const std::string& src, const Prelude& prelude) {
  std::vector<Token> tokens = Tokenize(src);
  Parser p(tokens);
  FileHeader header = p.ParseHeader();
  Module mod = p.ParseBody(&prelude);
  mod.imports = std::move(header.imports);
  return mod;
}

// A Prelude standing in for a fully-loaded "common" file: a struct, an
// enum, a const, and an interface, each attributed to owner_namespace
// "common" -- exactly what module_loader.cc would build after loading a
// `module common;` file that declares them.
Prelude MakeCommonPrelude() {
  Prelude prelude;
  StructDecl s;
  s.name = "Shared";
  s.decl_index = 0;
  prelude.module.structs.push_back(s);

  EnumDecl e;
  e.name = "Status";
  e.values = {{"OK", 0}, {"ERROR", 1}};
  prelude.module.enums.push_back(e);

  ConstDecl c;
  c.type = TypeSpec{TypeKind::kInt32, "", nullptr, nullptr, false};
  c.name = "kMax";
  c.value.has_value = true;
  c.value.int_value = 5;
  prelude.module.consts.push_back(c);

  Interface iface;
  iface.name = "CommonIface";
  // (v16) A nested enum, to test the cross-file *and* nested case
  // (owner_namespace="common", owner_container="CommonIface" both set at
  // once) -- see parser_prelude_nested_enum_gets_both_qualifiers below.
  EnumDecl nested;
  nested.name = "NestedStatus";
  nested.values = {{"A", 0}, {"B", 1}};
  iface.enums.push_back(nested);
  prelude.module.interfaces.push_back(iface);

  for (const std::string& name :
       {"Shared", "Status", "kMax", "CommonIface", "NestedStatus"}) {
    prelude.owner_by_name[name] = "common";
  }
  return prelude;
}
}  // namespace

TEST(parser_module_and_empty_interface) {
  Module m = ParseSrc("module echo;\ninterface Foo {};");
  EXPECT_EQ(m.name, "echo");
  EXPECT_EQ(m.interfaces.size(), 1u);
  EXPECT_EQ(m.interfaces[0].name, "Foo");
  EXPECT_EQ(m.interfaces[0].methods.size(), 0u);
}

TEST(parser_module_is_optional) {
  Module m = ParseSrc("interface Foo {};");
  EXPECT(m.name.empty());
  EXPECT_EQ(m.interfaces.size(), 1u);
}

TEST(parser_implicit_ordinals_sequential) {
  Module m = ParseSrc("interface Foo { A(); B(); C(); };");
  auto& methods = m.interfaces[0].methods;
  EXPECT_EQ(methods.size(), 3u);
  EXPECT_EQ(methods[0].ordinal, 0u);
  EXPECT_EQ(methods[1].ordinal, 1u);
  EXPECT_EQ(methods[2].ordinal, 2u);
  EXPECT(!methods[0].has_explicit_ordinal);
}

TEST(parser_explicit_ordinals) {
  Module m = ParseSrc("interface Foo { A() @5; B() @2; };");
  auto& methods = m.interfaces[0].methods;
  EXPECT_EQ(methods[0].ordinal, 5u);
  EXPECT_EQ(methods[1].ordinal, 2u);
  EXPECT(methods[0].has_explicit_ordinal);
}

TEST(parser_rejects_mixed_ordinals) {
  bool threw = false;
  try {
    ParseSrc("interface Foo { A() @0; B(); };");
  } catch (const ParseError&) {
    threw = true;
  }
  EXPECT(threw);
}

TEST(parser_params_and_response) {
  Module m = ParseSrc(
      "interface Echo {\n"
      "  EchoString(string in) => (string out);\n"
      "};");
  const Method& method = m.interfaces[0].methods[0];
  EXPECT_EQ(method.name, "EchoString");
  EXPECT_EQ(method.params.size(), 1u);
  EXPECT_EQ(method.params[0].name, "in");
  EXPECT(method.params[0].type.kind == TypeKind::kString);
  EXPECT(method.has_response);
  EXPECT_EQ(method.response_params.size(), 1u);
  EXPECT_EQ(method.response_params[0].name, "out");
}

TEST(parser_scalar_types) {
  Module m = ParseSrc(
      "interface T {\n"
      "  M(bool b, int8 a, uint8 c, int16 d, uint16 e, int32 f, uint32 g,\n"
      "    int64 h, uint64 i, float j, double k);\n"
      "};");
  auto& params = m.interfaces[0].methods[0].params;
  EXPECT_EQ(params.size(), 11u);
  EXPECT(params[0].type.kind == TypeKind::kBool);
  EXPECT(params[9].type.kind == TypeKind::kFloat);
  EXPECT(params[10].type.kind == TypeKind::kDouble);
}

TEST(parser_pending_associated_types) {
  Module m = ParseSrc(
      "interface Echo {\n"
      "  SetListener(pending_associated_remote<EchoListener> listener);\n"
      "  TakeReceiver(pending_associated_receiver<EchoListener> r);\n"
      "};");
  auto& methods = m.interfaces[0].methods;
  EXPECT(methods[0].params[0].type.kind ==
         TypeKind::kPendingAssociatedRemote);
  EXPECT_EQ(methods[0].params[0].type.name, "EchoListener");
  EXPECT(methods[1].params[0].type.kind ==
         TypeKind::kPendingAssociatedReceiver);
}

TEST(parser_unknown_type_is_an_external_struct) {
  Module m = ParseSrc("interface Foo { A(bogus x); };");
  EXPECT(m.interfaces[0].methods[0].params[0].type.kind ==
         TypeKind::kStructRef);
  EXPECT_EQ(m.interfaces[0].methods[0].params[0].type.name, "bogus");
}

TEST(parser_non_associated_pending_types) {
  Module m = ParseSrc(
      "interface Watcher {};\n"
      "interface Store {\n"
      "  Watch(pending_remote<Watcher> w);\n"
      "  Serve(pending_receiver<Watcher> r);\n"
      "};");
  auto& methods = m.interfaces[1].methods;
  EXPECT(methods[0].params[0].type.kind == TypeKind::kPendingRemote);
  EXPECT_EQ(methods[0].params[0].type.name, "Watcher");
  EXPECT(methods[1].params[0].type.kind == TypeKind::kPendingReceiver);
}

TEST(parser_enum_implicit_and_explicit_values) {
  Module m = ParseSrc(
      "enum Status {\n"
      "  OK,\n"
      "  WARN = 5,\n"
      "  ERROR,\n"
      "};");
  auto& values = m.enums[0].values;
  EXPECT_EQ(values.size(), 3u);
  EXPECT_EQ(values[0].value, 0);
  EXPECT_EQ(values[1].value, 5);
  EXPECT_EQ(values[2].value, 6);  // continues from previous explicit + 1
}

TEST(parser_enum_negative_value) {
  Module m = ParseSrc("enum E { NEG = -3, NEXT };");
  EXPECT_EQ(m.enums[0].values[0].value, -3);
  EXPECT_EQ(m.enums[0].values[1].value, -2);
}

TEST(parser_struct_fields_and_array) {
  Module m = ParseSrc(
      "struct Entry {\n"
      "  string key;\n"
      "  array<int32> nums;\n"
      "};");
  auto& fields = m.structs[0].fields;
  EXPECT_EQ(fields.size(), 2u);
  EXPECT(fields[0].type.kind == TypeKind::kString);
  EXPECT(fields[1].type.kind == TypeKind::kArray);
  EXPECT(fields[1].type.element->kind == TypeKind::kInt32);
}

TEST(parser_struct_can_reference_earlier_struct_and_enum) {
  Module m = ParseSrc(
      "enum Status { OK };\n"
      "struct Inner { int32 x; };\n"
      "struct Outer {\n"
      "  Inner inner;\n"
      "  Status status;\n"
      "  array<Inner> many;\n"
      "};");
  auto& fields = m.structs[1].fields;
  EXPECT(fields[0].type.kind == TypeKind::kStructRef);
  EXPECT_EQ(fields[0].type.name, "Inner");
  EXPECT(fields[1].type.kind == TypeKind::kEnumRef);
  EXPECT(fields[2].type.kind == TypeKind::kArray);
  EXPECT(fields[2].type.element->kind == TypeKind::kStructRef);
}

TEST(parser_allows_struct_referencing_later_struct) {
  Module m = ParseSrc(
      "struct A { B b; };\n"
      "struct B { int32 x; };");
  EXPECT_EQ(m.structs[0].fields[0].type.name, "B");
  EXPECT_EQ(m.structs[1].name, "B");
}

TEST(parser_allows_struct_referencing_later_struct_via_array) {
  Module m = ParseSrc(
      "struct A { array<B> bs; };\n"
      "struct B { int32 x; };");
  EXPECT(m.structs[0].fields[0].type.kind == TypeKind::kArray);
  EXPECT_EQ(m.structs[0].fields[0].type.element->name, "B");
}

TEST(parser_interfaces_may_reference_each_other_in_either_order) {
  // Unlike structs, interfaces have no declared-before-use restriction --
  // pending_remote/pending_associated_remote only need an incomplete type.
  Module m = ParseSrc(
      "interface A { UseB(pending_remote<B> b); };\n"
      "interface B {};");
  EXPECT_EQ(m.interfaces.size(), 2u);
  EXPECT_EQ(m.interfaces[0].methods[0].params[0].type.name, "B");
}

TEST(parser_const_decl) {
  Module m = ParseSrc("const int32 kMax = 42;\nconst int32 kNeg = -7;");
  EXPECT_EQ(m.consts.size(), 2u);
  EXPECT_EQ(m.consts[0].name, "kMax");
  EXPECT_EQ(m.consts[0].value.int_value, 42);
  EXPECT_EQ(m.consts[1].value.int_value, -7);
}

TEST(parser_union_implicit_and_explicit_tags) {
  Module m = ParseSrc(
      "union Result {\n"
      "  int32 int_value;\n"
      "  string error_message;\n"
      "};");
  EXPECT_EQ(m.unions.size(), 1u);
  auto& fields = m.unions[0].fields;
  EXPECT_EQ(fields.size(), 2u);
  EXPECT_EQ(fields[0].name, "int_value");
  EXPECT(fields[0].type.kind == TypeKind::kInt32);
  EXPECT_EQ(fields[0].tag, 0u);
  EXPECT_EQ(fields[1].tag, 1u);
  EXPECT(!fields[0].has_explicit_tag);
}

TEST(parser_union_explicit_tags) {
  Module m = ParseSrc("union U { int32 a @5; string b @2; };");
  auto& fields = m.unions[0].fields;
  EXPECT_EQ(fields[0].tag, 5u);
  EXPECT_EQ(fields[1].tag, 2u);
  EXPECT(fields[0].has_explicit_tag);
}

TEST(parser_allows_mixed_union_tags) {
  Module m = ParseSrc("union U { int32 a @0; string b; };");
  EXPECT_EQ(m.unions[0].fields[0].tag, 0u);
  EXPECT_EQ(m.unions[0].fields[1].tag, 1u);
  EXPECT(!m.unions[0].fields[1].has_explicit_tag);
}

TEST(parser_rejects_empty_union) {
  bool threw = false;
  try {
    ParseSrc("union U {};");
  } catch (const ParseError&) {
    threw = true;
  }
  EXPECT(threw);
}

TEST(parser_struct_and_union_may_embed_each_other_in_declared_order) {
  Module m = ParseSrc(
      "struct Inner { int32 x; };\n"
      "union Outer1 { Inner inner; string s; };\n"
      "struct Outer2 { Outer1 choice; };");
  EXPECT_EQ(m.unions[0].fields[0].type.kind, TypeKind::kStructRef);
  EXPECT_EQ(m.structs[1].fields[0].type.kind, TypeKind::kUnionRef);
  EXPECT_EQ(m.structs[1].fields[0].type.name, "Outer1");
}

TEST(parser_allows_union_referencing_later_struct) {
  Module m = ParseSrc(
      "union U { B b; };\n"
      "struct B { int32 x; };");
  EXPECT_EQ(m.unions[0].fields[0].type.name, "B");
}

TEST(parser_map_field) {
  Module m = ParseSrc(
      "struct Config {\n"
      "  map<string, int32> values;\n"
      "};");
  auto& f = m.structs[0].fields[0];
  EXPECT(f.type.kind == TypeKind::kMap);
  EXPECT(f.type.key->kind == TypeKind::kString);
  EXPECT(f.type.element->kind == TypeKind::kInt32);
}

TEST(parser_map_as_method_param) {
  Module m = ParseSrc(
      "interface Foo {\n"
      "  Send(map<int32, string> m);\n"
      "};");
  auto& p = m.interfaces[0].methods[0].params[0];
  EXPECT(p.type.kind == TypeKind::kMap);
  EXPECT(p.type.key->kind == TypeKind::kInt32);
  EXPECT(p.type.element->kind == TypeKind::kString);
}

TEST(parser_allows_struct_typed_map_key) {
  Module m = ParseSrc(
      "struct K { int32 x; };\n"
      "interface Foo { Send(map<K, string> m); };");
  EXPECT(m.interfaces[0].methods[0].params[0].type.key->kind ==
         TypeKind::kStructRef);
}

TEST(parser_rejects_float_map_key) {
  bool threw = false;
  try {
    ParseSrc("interface Foo { Send(map<float, string> m); };");
  } catch (const ParseError&) {
    threw = true;
  }
  EXPECT(threw);
}

TEST(parser_import_statements_are_recorded_unresolved) {
  Module m = ParseSrc(
      "module x;\n"
      "import \"common/types.voodoom\";\n"
      "import \"other.voodoom\";\n"
      "struct Foo { int32 x; };");
  EXPECT_EQ(m.imports.size(), 2u);
  EXPECT_EQ(m.imports[0].path, "common/types.voodoom");
  EXPECT_EQ(m.imports[1].path, "other.voodoom");
  EXPECT_EQ(m.structs.size(), 1u);
}

TEST(parser_import_without_module_statement) {
  Module m = ParseSrc("import \"a.voodoom\";\nstruct Foo { int32 x; };");
  EXPECT_EQ(m.imports.size(), 1u);
  EXPECT(m.name.empty());
}

TEST(parser_rejects_import_after_other_decl) {
  bool threw = false;
  try {
    ParseSrc("struct Foo { int32 x; };\nimport \"a.voodoom\";");
  } catch (const ParseError&) {
    threw = true;
  }
  EXPECT(threw);
}

TEST(parser_unresolved_type_is_an_external_struct) {
  Module m = ParseSrc(
      "import \"common/types.voodoom\";\n"
      "struct Foo { CommonType x; };");
  EXPECT(m.structs[0].fields[0].type.kind == TypeKind::kStructRef);
  EXPECT_EQ(m.structs[0].fields[0].type.name, "CommonType");
}

TEST(parser_nullable_string_field) {
  Module m = ParseSrc("struct Foo { string? name; };");
  EXPECT(m.structs[0].fields[0].type.kind == TypeKind::kString);
  EXPECT(m.structs[0].fields[0].type.nullable);
}

TEST(parser_non_nullable_string_field_is_not_nullable) {
  Module m = ParseSrc("struct Foo { string name; };");
  EXPECT(!m.structs[0].fields[0].type.nullable);
}

TEST(parser_nullable_struct_and_union_field) {
  Module m = ParseSrc(
      "struct Inner { int32 x; };\n"
      "union U { int32 a; };\n"
      "struct Outer { Inner? inner; U? choice; };");
  EXPECT(m.structs[1].fields[0].type.nullable);
  EXPECT(m.structs[1].fields[1].type.nullable);
}

TEST(parser_nullable_array_and_map) {
  Module m = ParseSrc(
      "struct Foo {\n"
      "  array<int32>? nums;\n"
      "  map<string, int32>? counts;\n"
      "};");
  EXPECT(m.structs[0].fields[0].type.kind == TypeKind::kArray);
  EXPECT(m.structs[0].fields[0].type.nullable);
  EXPECT(m.structs[0].fields[1].type.kind == TypeKind::kMap);
  EXPECT(m.structs[0].fields[1].type.nullable);
}

TEST(parser_nullable_nesting_inner_vs_outer) {
  Module m = ParseSrc(
      "struct Foo {\n"
      "  array<string?> inner_nullable;\n"
      "  array<string>? outer_nullable;\n"
      "};");
  const TypeSpec& inner = m.structs[0].fields[0].type;
  EXPECT(!inner.nullable);              // the array itself isn't nullable
  EXPECT(inner.element->nullable);      // its elements are

  const TypeSpec& outer = m.structs[0].fields[1].type;
  EXPECT(outer.nullable);               // the array itself is nullable
  EXPECT(!outer.element->nullable);     // its elements aren't
}

TEST(parser_allows_nullable_scalar) {
  Module m = ParseSrc("struct Foo { uint32? x = 0; };");
  EXPECT(m.structs[0].fields[0].type.nullable);
  EXPECT(m.structs[0].fields[0].type.kind == TypeKind::kUint32);
  EXPECT(m.structs[0].fields[0].default_value.has_value);
  EXPECT_EQ(m.structs[0].fields[0].default_value.int_value, 0);
}

TEST(parser_allows_nullable_enum) {
  Module m = ParseSrc("enum E { OK };\nstruct Foo { E? x; };");
  EXPECT(m.structs[0].fields[0].type.nullable);
  EXPECT(m.structs[0].fields[0].type.kind == TypeKind::kEnumRef);
}

TEST(parser_allows_nullable_pending_remote) {
  // v14: pending_remote/receiver/associated_* are now legal nullable_type
  // targets -- PendingRemote<T>::is_valid() etc. already give them a real
  // absent state, matching real mojom's optional-interface parameters.
  Module m = ParseSrc(
      "interface I {};\n"
      "interface Foo { M(pending_remote<I>? r); };");
  EXPECT(m.interfaces[1].methods[0].params[0].type.nullable);
}

TEST(parser_allows_nullable_pending_receiver_and_associated_kinds) {
  Module m = ParseSrc(
      "interface I {};\n"
      "interface Foo {\n"
      "  M(pending_receiver<I>? a,\n"
      "    pending_associated_remote<I>? b,\n"
      "    pending_associated_receiver<I>? c);\n"
      "};");
  for (const Param& p : m.interfaces[1].methods[0].params) {
    EXPECT(p.type.nullable);
  }
}

TEST(parser_rejects_nullable_map_key) {
  bool threw = false;
  try {
    ParseSrc("interface Foo { M(map<string?, int32> m); };");
  } catch (const ParseError&) {
    threw = true;
  }
  EXPECT(threw);
}

TEST(parser_nullable_param_and_response) {
  Module m = ParseSrc(
      "struct S { int32 x; };\n"
      "interface Foo { M(S? in) => (string? out); };");
  auto& method = m.interfaces[0].methods[0];
  EXPECT(method.params[0].type.nullable);
  EXPECT(method.response_params[0].type.nullable);
}

TEST(parser_dotted_module_name) {
  Module m = ParseSrc("module foo.bar.baz;\nstruct S { int32 x; };");
  EXPECT_EQ(m.name, "foo.bar.baz");
}

TEST(parser_single_segment_module_name_unchanged) {
  Module m = ParseSrc("module echo;\nstruct S { int32 x; };");
  EXPECT_EQ(m.name, "echo");
}

TEST(parser_bool_field_default) {
  Module m = ParseSrc("struct Foo { bool flag = true; bool other = false; };");
  auto& fields = m.structs[0].fields;
  EXPECT(fields[0].default_value.has_value);
  EXPECT(fields[0].default_value.bool_value);
  EXPECT(fields[1].default_value.has_value);
  EXPECT(!fields[1].default_value.bool_value);
}

TEST(parser_int_field_default) {
  Module m = ParseSrc("struct Foo { int32 x = 42; int32 y = -7; };");
  auto& fields = m.structs[0].fields;
  EXPECT_EQ(fields[0].default_value.int_value, 42);
  EXPECT_EQ(fields[1].default_value.int_value, -7);
}

TEST(parser_string_field_default) {
  Module m = ParseSrc("struct Foo { string name = \"Bob\"; };");
  EXPECT_EQ(m.structs[0].fields[0].default_value.string_value, "Bob");
}

TEST(parser_field_without_default_has_no_value) {
  Module m = ParseSrc("struct Foo { int32 x; };");
  EXPECT(!m.structs[0].fields[0].default_value.has_value);
}

TEST(parser_rejects_bool_field_default_wrong_literal) {
  bool threw = false;
  try {
    ParseSrc("struct Foo { bool flag = 1; };");
  } catch (const ParseError&) {
    threw = true;
  }
  EXPECT(threw);
}

TEST(parser_enum_field_default) {
  Module m = ParseSrc(
      "enum Status { OK, WARN, ERROR };\n"
      "struct Foo { Status s = WARN; };");
  const DefaultValue& d = m.structs[0].fields[0].default_value;
  EXPECT(d.has_value);
  EXPECT_EQ(d.int_value, 1);
  EXPECT_EQ(d.named_expr, "Status::WARN");
}

TEST(parser_enum_field_default_is_order_independent) {
  // The struct comes BEFORE the enum it defaults from -- must still
  // resolve, same as an ordinary Status-typed (non-defaulted) field
  // already can regardless of order.
  Module m = ParseSrc(
      "struct Foo { Status s = ERROR; };\n"
      "enum Status { OK, WARN, ERROR };");
  EXPECT_EQ(m.structs[0].fields[0].default_value.named_expr, "Status::ERROR");
}

TEST(parser_unknown_enum_value_as_default_is_kept) {
  Module m = ParseSrc("enum Status { OK };\nstruct Foo { Status s = BOGUS; };");
  EXPECT_EQ(m.structs[0].fields[0].default_value.named_expr, "Status::BOGUS");
}

TEST(parser_const_reference_field_default) {
  Module m = ParseSrc("const int32 kMax = 42;\nstruct Foo { int32 x = kMax; };");
  const DefaultValue& d = m.structs[0].fields[0].default_value;
  EXPECT(d.has_value);
  EXPECT_EQ(d.int_value, 42);
  EXPECT_EQ(d.named_expr, "kMax");
}

TEST(parser_bool_const_reference_field_default) {
  // (real-mojom-parity phase 2) `const bool` now requires 'true'/'false'
  // (previously always parsed a raw integer literal regardless of the
  // const's declared type, so `= 1;` used to "work" for a bool const only
  // because nothing validated the value shape against the type at all).
  Module m = ParseSrc(
      "const bool kEnabled = true;\nstruct Foo { bool flag = kEnabled; };");
  EXPECT(m.structs[0].fields[0].default_value.bool_value);
  EXPECT_EQ(m.structs[0].fields[0].default_value.named_expr, "kEnabled");
}

TEST(parser_rejects_const_field_default_declared_after_use) {
  // Unlike enums, consts are NOT pre-scanned -- must be declared earlier
  // in the file (same restriction struct/union embedding already has, for
  // a different reason -- see const_by_name_'s comment in parser.cc).
  bool threw = false;
  try {
    ParseSrc("struct Foo { int32 x = kMax; };\nconst int32 kMax = 42;");
  } catch (const ParseError&) {
    threw = true;
  }
  EXPECT(threw);
}

TEST(parser_rejects_unknown_const_name_as_default) {
  bool threw = false;
  try {
    ParseSrc("struct Foo { int32 x = kNoSuchConst; };");
  } catch (const ParseError&) {
    threw = true;
  }
  EXPECT(threw);
}

TEST(parser_rejects_const_field_default_with_mismatched_type) {
  bool threw = false;
  try {
    ParseSrc("const int64 kMax = 42;\nstruct Foo { int32 x = kMax; };");
  } catch (const ParseError&) {
    threw = true;
  }
  EXPECT(threw);
}

TEST(parser_allows_nullable_field_default) {
  Module m = ParseSrc("struct Foo { string? x = \"hi\"; };");
  EXPECT(m.structs[0].fields[0].type.nullable);
  EXPECT_EQ(m.structs[0].fields[0].default_value.string_value, "hi");
}

TEST(parser_enum_qualified_field_default) {
  Module m = ParseSrc(
      "enum E { kNone, kAll };\n"
      "struct Foo { E sandbox = E.kNone; };");
  EXPECT_EQ(m.structs[0].fields[0].default_value.named_expr, "E::kNone");
}

TEST(parser_same_module_qualified_type) {
  Module m = ParseSrc(
      "module network.mojom;\n"
      "enum CoopAccessReportType { kA };\n"
      "struct S { network.mojom.CoopAccessReportType report_type; };");
  EXPECT(m.structs[0].fields[0].type.kind == TypeKind::kEnumRef);
  EXPECT_EQ(m.structs[0].fields[0].type.name, "CoopAccessReportType");
}

TEST(parser_pending_qualified_interface) {
  Module m = ParseSrc(
      "interface Blob {\n"
      "  AsDataPipeGetter(pending_receiver<network.mojom.DataPipeGetter> g);\n"
      "};");
  EXPECT_EQ(m.interfaces[0].methods[0].params[0].type.name, "DataPipeGetter");
  EXPECT_EQ(m.interfaces[0].methods[0].params[0].type.owner_namespace,
            "network.mojom");
}

TEST(parser_dotted_attribute_value) {
  Module m = ParseSrc(
      "[RequireContext=sandbox.mojom.Context.kBrowser]\n"
      "interface Foo { M(); };");
  EXPECT_EQ(m.interfaces[0].name, "Foo");
}

TEST(parser_struct_map_key) {
  Module m = ParseSrc(
      "struct Site { string s; };\n"
      "struct C { map<Site, int32> customizations; };");
  EXPECT(m.structs[1].fields[0].type.kind == TypeKind::kMap);
  EXPECT(m.structs[1].fields[0].type.key->kind == TypeKind::kStructRef);
}

TEST(parser_rejects_array_field_default) {
  bool threw = false;
  try {
    ParseSrc("struct Foo { array<int32> x = 1; };");
  } catch (const ParseError&) {
    threw = true;
  }
  EXPECT(threw);
}

// (real-mojom-parity phase 2) float/double field defaults are supported
// now that the lexer can tokenize float literals (see lexer_test.cc's
// lexer_float_* cases) -- replaces the old parser_rejects_float_field_default
// test, which asserted the (now-obsolete) "not supported yet" error.
TEST(parser_float_field_default) {
  Module m = ParseSrc("struct Foo { float x = 1.5; };");
  const DefaultValue& d = m.structs[0].fields[0].default_value;
  EXPECT(d.has_value);
  EXPECT_EQ(d.float_value, 1.5);
}

TEST(parser_double_field_default_from_plain_integer) {
  // An integer literal is a legal float/double value too (e.g. `= 5;`),
  // matching real mojom's literal grammar (float/int are both plain
  // alternatives of the same 'literal' production).
  Module m = ParseSrc("struct Foo { double x = 5; };");
  EXPECT_EQ(m.structs[0].fields[0].default_value.float_value, 5.0);
}

TEST(parser_float_field_default_negative) {
  Module m = ParseSrc("struct Foo { float x = -2.5; };");
  EXPECT_EQ(m.structs[0].fields[0].default_value.float_value, -2.5);
}

TEST(parser_sync_method) {
  Module m = ParseSrc("interface Foo { [Sync] Add(int32 a) => (int32 sum); };");
  EXPECT(m.interfaces[0].methods[0].is_sync);
}

TEST(parser_non_sync_method_defaults_false) {
  Module m = ParseSrc("interface Foo { Get() => (int32 x); };");
  EXPECT(!m.interfaces[0].methods[0].is_sync);
}

TEST(parser_rejects_sync_method_without_response) {
  bool threw = false;
  try {
    ParseSrc("interface Foo { [Sync] Fire(); };");
  } catch (const ParseError&) {
    threw = true;
  }
  EXPECT(threw);
}

// (real-mojom-parity phase 3) An unrecognized attribute is no longer a
// parse error -- accepted and silently ignored, the same way this
// compiler needs to tolerate real .mojom files' Chromium-specific
// attributes ([ServiceSandbox], [Uuid], [EnableIf=...], ...) it has no
// semantic meaning for. Replaces the old parser_rejects_unknown_attribute
// test, which asserted the (now-obsolete) rejection.
TEST(parser_unknown_attribute_is_accepted_and_ignored) {
  Module m = ParseSrc("interface Foo { [Bogus] Get() => (int32 x); };");
  EXPECT_EQ(m.interfaces[0].methods[0].name, "Get");
}

TEST(parser_unknown_attribute_with_identifier_value_is_accepted) {
  Module m = ParseSrc(
      "[Bogus=some_flag] interface Foo { Get() => (int32 x); };");
  EXPECT_EQ(m.interfaces[0].name, "Foo");
}

TEST(parser_unknown_attribute_with_pipe_list_value_is_accepted) {
  Module m = ParseSrc(
      "[EnableIf=a|b|c] interface Foo { Get() => (int32 x); };");
  EXPECT_EQ(m.interfaces[0].name, "Foo");
}

TEST(parser_unknown_attribute_with_amp_list_value_is_accepted) {
  Module m = ParseSrc(
      "[EnableIf=a&b] interface Foo { Get() => (int32 x); };");
  EXPECT_EQ(m.interfaces[0].name, "Foo");
}

TEST(parser_unknown_attribute_with_string_value_is_accepted) {
  Module m = ParseSrc(
      "[Uuid=\"12345678-1234-1234-1234-123456789abc\"] "
      "interface Foo { Get() => (int32 x); };");
  EXPECT_EQ(m.interfaces[0].name, "Foo");
}

TEST(parser_unknown_attribute_with_float_value_is_accepted) {
  Module m = ParseSrc("[Bogus=1.5] interface Foo { Get() => (int32 x); };");
  EXPECT_EQ(m.interfaces[0].name, "Foo");
}

TEST(parser_sync_attribute_with_ordinal_and_multiple_methods) {
  Module m = ParseSrc(
      "interface Foo {\n"
      "  [Sync] A() @0 => (int32 x);\n"
      "  B() @1;\n"
      "};");
  EXPECT(m.interfaces[0].methods[0].is_sync);
  EXPECT(!m.interfaces[0].methods[1].is_sync);
  EXPECT_EQ(m.interfaces[0].methods[0].ordinal, 0u);
  EXPECT_EQ(m.interfaces[0].methods[1].ordinal, 1u);
}

TEST(parser_allows_handle_bearing_struct_field) {
  Module m = ParseSrc(
      "interface Watcher {};\n"
      "struct Entry { pending_remote<Watcher> w; };");
  EXPECT(m.structs[0].fields[0].type.kind == TypeKind::kPendingRemote);
  EXPECT_EQ(m.structs[0].fields[0].type.name, "Watcher");
}

TEST(parser_allows_handle_bearing_union_field) {
  Module m = ParseSrc(
      "interface Watcher {};\n"
      "union U { pending_remote<Watcher> w; };");
  EXPECT(m.unions[0].fields[0].type.kind == TypeKind::kPendingRemote);
}

TEST(parser_allows_handle_bearing_array_element) {
  Module m = ParseSrc(
      "interface Watcher {};\n"
      "interface Foo { M(array<pending_remote<Watcher>> ws); };");
  EXPECT(m.interfaces[1].methods[0].params[0].type.kind == TypeKind::kArray);
  EXPECT(m.interfaces[1].methods[0].params[0].type.element->kind ==
         TypeKind::kPendingRemote);
}

TEST(parser_allows_handle_bearing_map_value) {
  Module m = ParseSrc(
      "interface Watcher {};\n"
      "interface Foo { M(map<string, pending_remote<Watcher>> ws); };");
  EXPECT(m.interfaces[1].methods[0].params[0].type.kind == TypeKind::kMap);
  EXPECT(m.interfaces[1].methods[0].params[0].type.element->kind ==
         TypeKind::kPendingRemote);
}

TEST(parser_allows_handle_bearing_nested_array_element) {
  Module m = ParseSrc(
      "interface Watcher {};\n"
      "interface Foo { M(array<array<pending_remote<Watcher>>> ws); };");
  EXPECT(m.interfaces[1].methods[0].params[0].type.element->element->kind ==
         TypeKind::kPendingRemote);
}

TEST(parser_allows_top_level_pending_remote_param) {
  // The one place these types *are* allowed: a bare top-level method
  // parameter -- must not be broken by the handle-bearing-container check.
  Module m = ParseSrc(
      "interface Watcher {};\n"
      "interface Foo { M(pending_remote<Watcher> w); };");
  EXPECT(m.interfaces[1].methods[0].params[0].type.kind ==
         TypeKind::kPendingRemote);
}

TEST(parser_allows_struct_referenced_from_array_that_has_no_handles) {
  // Confirms the fix isn't over-broad -- a struct (with only ordinary
  // fields) is still fine as an array element.
  Module m = ParseSrc(
      "struct Entry { int32 x; };\n"
      "interface Foo { M(array<Entry> es); };");
  EXPECT(m.interfaces[0].methods[0].params[0].type.kind == TypeKind::kArray);
}

TEST(parser_struct_field_min_version) {
  Module m = ParseSrc(
      "struct Widget {\n"
      "  string name;\n"
      "  [MinVersion=1] string color;\n"
      "};");
  auto& fields = m.structs[0].fields;
  EXPECT_EQ(fields[0].min_version, 0u);
  EXPECT_EQ(fields[1].min_version, 1u);
}

TEST(parser_struct_version_is_max_field_min_version) {
  Module m = ParseSrc(
      "struct Widget {\n"
      "  string name;\n"
      "  [MinVersion=1] string color;\n"
      "  [MinVersion=3] int32 weight;\n"
      "};");
  EXPECT_EQ(m.structs[0].version, 3u);
}

TEST(parser_struct_with_no_versioned_fields_has_version_zero) {
  Module m = ParseSrc("struct Widget { string name; int32 x; };");
  EXPECT_EQ(m.structs[0].version, 0u);
}

TEST(parser_allows_decreasing_min_version_order) {
  Module m = ParseSrc(
      "struct Widget {\n"
      "  [MinVersion=2] string a;\n"
      "  [MinVersion=1] string b;\n"
      "};");
  EXPECT_EQ(m.structs[0].fields[0].min_version, 2u);
  EXPECT_EQ(m.structs[0].fields[1].min_version, 1u);
  EXPECT_EQ(m.structs[0].version, 2u);
}

TEST(parser_allows_equal_min_version_on_consecutive_fields) {
  Module m = ParseSrc(
      "struct Widget {\n"
      "  [MinVersion=1] string a;\n"
      "  [MinVersion=1] string b;\n"
      "};");
  EXPECT_EQ(m.structs[0].fields[0].min_version, 1u);
  EXPECT_EQ(m.structs[0].fields[1].min_version, 1u);
}

TEST(parser_rejects_min_version_without_value) {
  bool threw = false;
  try {
    ParseSrc("struct Widget { [MinVersion] string a; };");
  } catch (const ParseError&) {
    threw = true;
  }
  EXPECT(threw);
}

TEST(parser_rejects_negative_min_version) {
  bool threw = false;
  try {
    ParseSrc("struct Widget { [MinVersion=-1] string a; };");
  } catch (const ParseError&) {
    threw = true;
  }
  EXPECT(threw);
}

// (real-mojom-parity phase 3) See
// parser_unknown_attribute_is_accepted_and_ignored's comment.
TEST(parser_unsupported_field_attribute_is_accepted_and_ignored) {
  Module m = ParseSrc("struct Widget { [Bogus] string a; };");
  EXPECT_EQ(m.structs[0].fields[0].name, "a");
}

TEST(parser_rejects_sync_with_value) {
  bool threw = false;
  try {
    ParseSrc("interface Foo { [Sync=1] M() => (int32 x); };");
  } catch (const ParseError&) {
    threw = true;
  }
  EXPECT(threw);
}

TEST(parser_interface_extensible_attribute) {
  Module m = ParseSrc("[Extensible]\ninterface Foo { M() => (int32 x); };");
  EXPECT(m.interfaces[0].is_extensible);
}

TEST(parser_interface_not_extensible_by_default) {
  Module m = ParseSrc("interface Foo { M() => (int32 x); };");
  EXPECT(!m.interfaces[0].is_extensible);
}

TEST(parser_method_min_version_and_interface_version) {
  Module m = ParseSrc(
      "interface Foo {\n"
      "  A() => (int32 x);\n"
      "  [MinVersion=2] B() => (int32 x);\n"
      "  [MinVersion=5] C() => (int32 x);\n"
      "};");
  auto& methods = m.interfaces[0].methods;
  EXPECT_EQ(methods[0].min_version, 0u);
  EXPECT_EQ(methods[1].min_version, 2u);
  EXPECT_EQ(methods[2].min_version, 5u);
  EXPECT_EQ(m.interfaces[0].version, 5u);
}

TEST(parser_method_min_version_order_is_unrestricted) {
  // Unlike struct fields, methods have no wire-layout reason to require
  // non-decreasing MinVersion order -- dispatch is ordinal-based, not
  // positional.
  Module m = ParseSrc(
      "interface Foo {\n"
      "  [MinVersion=5] A() => (int32 x);\n"
      "  [MinVersion=1] B() => (int32 x);\n"
      "};");
  EXPECT_EQ(m.interfaces[0].methods[0].min_version, 5u);
  EXPECT_EQ(m.interfaces[0].methods[1].min_version, 1u);
  EXPECT_EQ(m.interfaces[0].version, 5u);
}

TEST(parser_sync_and_min_version_combine_on_one_method) {
  Module m = ParseSrc(
      "interface Foo { [Sync, MinVersion=2] M() => (int32 x); };");
  EXPECT(m.interfaces[0].methods[0].is_sync);
  EXPECT_EQ(m.interfaces[0].methods[0].min_version, 2u);
}

TEST(parser_rejects_extensible_with_value) {
  bool threw = false;
  try {
    ParseSrc("[Extensible=1]\ninterface Foo { M() => (int32 x); };");
  } catch (const ParseError&) {
    threw = true;
  }
  EXPECT(threw);
}

// (real-mojom-parity phase 3) See
// parser_unknown_attribute_is_accepted_and_ignored's comment.
TEST(parser_unsupported_interface_attribute_is_accepted_and_ignored) {
  Module m = ParseSrc("[Bogus]\ninterface Foo { M() => (int32 x); };");
  EXPECT_EQ(m.interfaces[0].name, "Foo");
}

TEST(parser_rejects_method_min_version_without_value) {
  bool threw = false;
  try {
    ParseSrc("interface Foo { [MinVersion] M() => (int32 x); };");
  } catch (const ParseError&) {
    threw = true;
  }
  EXPECT(threw);
}

TEST(parser_rejects_negative_method_min_version) {
  bool threw = false;
  try {
    ParseSrc("interface Foo { [MinVersion=-1] M() => (int32 x); };");
  } catch (const ParseError&) {
    threw = true;
  }
  EXPECT(threw);
}

TEST(parser_union_extensible_attribute) {
  Module m = ParseSrc("[Extensible]\nunion U { int32 a; string b; };");
  EXPECT(m.unions[0].is_extensible);
}

TEST(parser_union_not_extensible_by_default) {
  Module m = ParseSrc("union U { int32 a; string b; };");
  EXPECT(!m.unions[0].is_extensible);
}

TEST(parser_union_field_min_version_and_union_version) {
  Module m = ParseSrc(
      "union U {\n"
      "  int32 a;\n"
      "  [MinVersion=1] string b;\n"
      "  [MinVersion=3] bool c;\n"
      "};");
  auto& fields = m.unions[0].fields;
  EXPECT_EQ(fields[0].min_version, 0u);
  EXPECT_EQ(fields[1].min_version, 1u);
  EXPECT_EQ(fields[2].min_version, 3u);
  EXPECT_EQ(m.unions[0].version, 3u);
}

TEST(parser_union_field_min_version_order_is_unrestricted) {
  // Like methods (and unlike struct fields), a union only ever writes
  // its one active field -- no positional wire layout to keep in sync.
  Module m = ParseSrc(
      "union U {\n"
      "  [MinVersion=3] int32 a;\n"
      "  [MinVersion=1] string b;\n"
      "};");
  EXPECT_EQ(m.unions[0].fields[0].min_version, 3u);
  EXPECT_EQ(m.unions[0].fields[1].min_version, 1u);
}

TEST(parser_top_level_bracket_disambiguates_interface_vs_union) {
  Module m = ParseSrc(
      "[Extensible]\ninterface Foo { M() => (int32 x); };\n"
      "[Extensible]\nunion Bar { int32 a; string b; };");
  EXPECT(m.interfaces[0].is_extensible);
  EXPECT(m.unions[0].is_extensible);
}

TEST(parser_enum_extensible_attribute) {
  Module m = ParseSrc("[Extensible]\nenum Color { kRed, kGreen, kBlue };");
  EXPECT(m.enums[0].is_extensible);
}

TEST(parser_enum_not_extensible_by_default) {
  Module m = ParseSrc("enum Color { kRed, kGreen, kBlue };");
  EXPECT(!m.enums[0].is_extensible);
}

TEST(parser_top_level_bracket_disambiguates_interface_union_enum) {
  Module m = ParseSrc(
      "[Extensible]\ninterface Foo { M() => (int32 x); };\n"
      "[Extensible]\nunion Bar { int32 a; string b; };\n"
      "[Extensible]\nenum Color { kRed, kGreen };");
  EXPECT(m.interfaces[0].is_extensible);
  EXPECT(m.unions[0].is_extensible);
  EXPECT(m.enums[0].is_extensible);
}

TEST(parser_rejects_enum_extensible_with_value) {
  bool threw = false;
  try {
    ParseSrc("[Extensible=1]\nenum Color { kRed };");
  } catch (const ParseError&) {
    threw = true;
  }
  EXPECT(threw);
}

// (real-mojom-parity phase 3) See
// parser_unknown_attribute_is_accepted_and_ignored's comment.
TEST(parser_unsupported_enum_attribute_is_accepted_and_ignored) {
  Module m = ParseSrc("[Bogus]\nenum Color { kRed };");
  EXPECT_EQ(m.enums[0].name, "Color");
}

TEST(parser_rejects_union_extensible_with_value) {
  bool threw = false;
  try {
    ParseSrc("[Extensible=1]\nunion U { int32 a; string b; };");
  } catch (const ParseError&) {
    threw = true;
  }
  EXPECT(threw);
}

// (real-mojom-parity phase 3) See
// parser_unknown_attribute_is_accepted_and_ignored's comment.
TEST(parser_unsupported_union_attribute_is_accepted_and_ignored) {
  Module m = ParseSrc("[Bogus]\nunion U { int32 a; string b; };");
  EXPECT_EQ(m.unions[0].name, "U");
}

TEST(parser_unsupported_union_field_attribute_is_accepted_and_ignored) {
  Module m = ParseSrc("union U { [Bogus] int32 a; string b; };");
  EXPECT_EQ(m.unions[0].fields[0].name, "a");
}

// (real-mojom-parity phase 3) `[Extensible]` before `struct` used to be a
// parse error (struct had no attribute_list production at the top level
// at all) -- real mojom's own struct_decl grammar does allow an
// attribute_section, and this compiler now accepts (and, since no
// struct-level attribute is acted on, ignores) one too, consistent with
// interface/union/enum/const. Replaces the old
// parser_rejects_bracket_not_followed_by_interface_or_union test, whose
// premise (this specific case was rejected) is now obsolete.
TEST(parser_struct_attribute_is_accepted_and_ignored) {
  Module m = ParseSrc("[Extensible]\nstruct Foo { int32 x; };");
  EXPECT_EQ(m.structs[0].name, "Foo");
}

TEST(parser_attribute_on_import_is_accepted) {
  Module m = ParseSrc("[EnableIf=flag]\nimport \"x.voodoom\";\ninterface Foo {};");
  EXPECT_EQ(m.imports.size(), 1u);
  EXPECT_EQ(m.imports[0].path, "x.voodoom");
}

TEST(parser_rejects_ordinal_colliding_with_reserved_run_message_id) {
  bool threw = false;
  try {
    ParseSrc("interface Foo { M() @4294967295; };");
  } catch (const ParseError&) {
    threw = true;
  }
  EXPECT(threw);
}

TEST(parser_rejects_ordinal_colliding_with_reserved_run_or_close_pipe_id) {
  bool threw = false;
  try {
    ParseSrc("interface Foo { M() @4294967294; };");
  } catch (const ParseError&) {
    threw = true;
  }
  EXPECT(threw);
}

TEST(parser_allows_ordinal_just_below_reserved_range) {
  Module m = ParseSrc("interface Foo { M() @4294967293; };");
  EXPECT_EQ(m.interfaces[0].methods[0].ordinal, 4294967293u);
}

TEST(parser_enum_typed_map_key) {
  Module m = ParseSrc(
      "enum Status { OK, ERROR };\n"
      "interface Foo { Send(map<Status, string> m); };");
  auto& p = m.interfaces[0].methods[0].params[0];
  EXPECT(p.type.key->kind == TypeKind::kEnumRef);
}

// (v15) Cross-namespace import: TypeSpec::owner_namespace/
// DefaultValue::named_expr_owner_namespace.

TEST(parser_local_struct_ref_has_empty_owner_namespace) {
  Module m = ParseSrc("struct Shared { int32 x; };\nstruct Outer { Shared s; };");
  EXPECT(m.structs[1].fields[0].type.owner_namespace.empty());
}

TEST(parser_prelude_struct_ref_gets_owner_namespace) {
  Module m = ParseWithPrelude("struct Outer { Shared s; };", MakeCommonPrelude());
  EXPECT_EQ(m.structs[0].fields[0].type.owner_namespace, "common");
}

TEST(parser_prelude_enum_ref_gets_owner_namespace) {
  Module m = ParseWithPrelude("struct Outer { Status s; };", MakeCommonPrelude());
  EXPECT(m.structs[0].fields[0].type.kind == TypeKind::kEnumRef);
  EXPECT_EQ(m.structs[0].fields[0].type.owner_namespace, "common");
}

TEST(parser_prelude_pending_remote_gets_owner_namespace) {
  Module m = ParseWithPrelude(
      "interface Foo { M(pending_remote<CommonIface> p); };",
      MakeCommonPrelude());
  const TypeSpec& t = m.interfaces[0].methods[0].params[0].type;
  EXPECT(t.kind == TypeKind::kPendingRemote);
  EXPECT_EQ(t.name, "CommonIface");
  EXPECT_EQ(t.owner_namespace, "common");
}

TEST(parser_local_pending_remote_interface_has_empty_owner_namespace) {
  // Interfaces have no ordering restriction -- Bar is referenced before its
  // own declaration here, same as an enum reference could be.
  Module m = ParseSrc("interface Foo { M(pending_remote<Bar> p); };\n"
                       "interface Bar {};");
  const TypeSpec& t = m.interfaces[0].methods[0].params[0].type;
  EXPECT_EQ(t.name, "Bar");
  EXPECT(t.owner_namespace.empty());
}

TEST(parser_pending_remote_unresolved_interface_stays_unqualified) {
  // An interface name that resolves to neither this file's own
  // declared_.interfaces nor a prelude's owner_by_name_ is still accepted
  // (same permissiveness as pre-v15 -- any NAME was always accepted here
  // unvalidated) rather than a parse error; it's just left unqualified, so
  // a genuine typo still fails later, as a real C++ compile error against
  // the generated header.
  Module m = ParseSrc("interface Foo { M(pending_remote<NoSuchInterface> p); };");
  const TypeSpec& t = m.interfaces[0].methods[0].params[0].type;
  EXPECT_EQ(t.name, "NoSuchInterface");
  EXPECT(t.owner_namespace.empty());
}

TEST(parser_prelude_const_default_gets_owner_namespace) {
  Module m =
      ParseWithPrelude("struct Outer { int32 x = kMax; };", MakeCommonPrelude());
  const DefaultValue& d = m.structs[0].fields[0].default_value;
  EXPECT_EQ(d.named_expr, "kMax");
  EXPECT_EQ(d.named_expr_owner_namespace, "common");
}

TEST(parser_prelude_enum_default_gets_owner_namespace) {
  Module m =
      ParseWithPrelude("struct Outer { Status s = OK; };", MakeCommonPrelude());
  const DefaultValue& d = m.structs[0].fields[0].default_value;
  EXPECT_EQ(d.named_expr, "Status::OK");
  EXPECT_EQ(d.named_expr_owner_namespace, "common");
}

TEST(parser_local_const_default_has_empty_owner_namespace) {
  Module m = ParseSrc("const int32 kMax = 5;\nstruct Outer { int32 x = kMax; };");
  const DefaultValue& d = m.structs[0].fields[0].default_value;
  EXPECT_EQ(d.named_expr, "kMax");
  EXPECT(d.named_expr_owner_namespace.empty());
}

// (v16) Nested enum -- declared inside an interface or struct body,
// referenced either by bare name (from anywhere in the file -- see
// ContainerOf's comment in parser.cc for why this compiler doesn't
// restrict that to the declaring container only) or the qualified
// `Container.EnumName` form.

TEST(parser_nested_enum_in_interface) {
  Module m = ParseSrc(
      "interface Foo {\n"
      "  enum Status { OK, ERROR };\n"
      "  M(Status s);\n"
      "};");
  EXPECT_EQ(m.interfaces[0].enums.size(), 1u);
  EXPECT_EQ(m.interfaces[0].enums[0].name, "Status");
  const TypeSpec& t = m.interfaces[0].methods[0].params[0].type;
  EXPECT(t.kind == TypeKind::kEnumRef);
  EXPECT_EQ(t.name, "Status");
  EXPECT_EQ(t.owner_container, "Foo");
  EXPECT(t.owner_namespace.empty());
}

TEST(parser_nested_enum_in_struct) {
  Module m = ParseSrc(
      "struct Foo {\n"
      "  enum Status { OK, ERROR };\n"
      "  Status s;\n"
      "};");
  EXPECT_EQ(m.structs[0].enums.size(), 1u);
  EXPECT_EQ(m.structs[0].enums[0].name, "Status");
  const TypeSpec& t = m.structs[0].fields[0].type;
  EXPECT(t.kind == TypeKind::kEnumRef);
  EXPECT_EQ(t.owner_container, "Foo");
}

TEST(parser_nested_enum_extensible_attribute) {
  Module m = ParseSrc(
      "interface Foo {\n"
      "  [Extensible] enum Status { OK, ERROR };\n"
      "  M(Status s);\n"
      "};");
  EXPECT(m.interfaces[0].enums[0].is_extensible);
}

TEST(parser_qualified_nested_enum_reference) {
  Module m = ParseSrc(
      "interface Foo { enum Status { OK, ERROR }; };\n"
      "struct Bar { Foo.Status s; };");
  const TypeSpec& t = m.structs[0].fields[0].type;
  EXPECT(t.kind == TypeKind::kEnumRef);
  EXPECT_EQ(t.name, "Status");
  EXPECT_EQ(t.owner_container, "Foo");
  EXPECT(t.owner_namespace.empty());
}

TEST(parser_qualified_nested_enum_field_default) {
  Module m = ParseSrc(
      "interface Foo { enum Status { OK, ERROR }; };\n"
      "struct Bar { Foo.Status s = OK; };");
  const DefaultValue& d = m.structs[0].fields[0].default_value;
  EXPECT_EQ(d.named_expr, "Status::OK");
  EXPECT_EQ(d.named_expr_owner_container, "Foo");
}

TEST(parser_qualified_reference_to_unknown_container_is_external) {
  Module m = ParseSrc("struct Bar { Unknown.Status s; };");
  EXPECT(m.structs[0].fields[0].type.kind == TypeKind::kStructRef);
  EXPECT_EQ(m.structs[0].fields[0].type.name, "Status");
  EXPECT_EQ(m.structs[0].fields[0].type.owner_namespace, "Unknown");
}

TEST(parser_qualified_reference_to_unknown_nested_enum_is_external) {
  Module m = ParseSrc("interface Foo { enum Status { OK, ERROR }; };\n"
                      "struct Bar { Foo.Bogus s; };");
  EXPECT(m.structs[0].fields[0].type.kind == TypeKind::kStructRef);
  EXPECT_EQ(m.structs[0].fields[0].type.name, "Bogus");
  EXPECT_EQ(m.structs[0].fields[0].type.owner_namespace, "Foo");
}

TEST(parser_prelude_nested_enum_gets_both_qualifiers) {
  // Cross-file *and* nested at once -- MakeCommonPrelude's CommonIface
  // has a nested NestedStatus, owned by "common".
  Module m = ParseWithPrelude("struct Outer { NestedStatus s; };",
                               MakeCommonPrelude());
  const TypeSpec& t = m.structs[0].fields[0].type;
  EXPECT(t.kind == TypeKind::kEnumRef);
  EXPECT_EQ(t.owner_namespace, "common");
  EXPECT_EQ(t.owner_container, "CommonIface");
}

TEST(parser_prelude_qualified_nested_enum_reference) {
  Module m = ParseWithPrelude("struct Outer { CommonIface.NestedStatus s; };",
                               MakeCommonPrelude());
  const TypeSpec& t = m.structs[0].fields[0].type;
  EXPECT(t.kind == TypeKind::kEnumRef);
  EXPECT_EQ(t.name, "NestedStatus");
  EXPECT_EQ(t.owner_namespace, "common");
  EXPECT_EQ(t.owner_container, "CommonIface");
}

// --- real-mojom-parity phase 4: `handle` type ---

TEST(parser_bare_handle_param) {
  Module m = ParseSrc("interface Foo { M(handle h); };");
  EXPECT(m.interfaces[0].methods[0].params[0].type.kind ==
         TypeKind::kHandle);
}

TEST(parser_handle_message_pipe_param) {
  Module m = ParseSrc("interface Foo { M(handle<message_pipe> h); };");
  EXPECT(m.interfaces[0].methods[0].params[0].type.kind ==
         TypeKind::kHandleMessagePipe);
}

TEST(parser_handle_data_pipe_consumer_param) {
  Module m =
      ParseSrc("interface Foo { M(handle<data_pipe_consumer> h); };");
  EXPECT(m.interfaces[0].methods[0].params[0].type.kind ==
         TypeKind::kHandleDataPipeConsumer);
}

TEST(parser_handle_data_pipe_producer_param) {
  Module m =
      ParseSrc("interface Foo { M(handle<data_pipe_producer> h); };");
  EXPECT(m.interfaces[0].methods[0].params[0].type.kind ==
         TypeKind::kHandleDataPipeProducer);
}

TEST(parser_handle_shared_buffer_param) {
  Module m = ParseSrc("interface Foo { M(handle<shared_buffer> h); };");
  EXPECT(m.interfaces[0].methods[0].params[0].type.kind ==
         TypeKind::kHandleSharedBuffer);
}

TEST(parser_allows_handle_platform_subtype) {
  Module m = ParseSrc("interface Foo { M(handle<platform> h); };");
  EXPECT(m.interfaces[0].methods[0].params[0].type.kind ==
         TypeKind::kHandlePlatform);
}

TEST(parser_rejects_handle_unknown_subtype) {
  bool threw = false;
  try {
    ParseSrc("interface Foo { M(handle<bogus> h); };");
  } catch (const ParseError&) {
    threw = true;
  }
  EXPECT(threw);
}

TEST(parser_allows_nullable_handle) {
  Module m = ParseSrc("interface Foo { M(handle<message_pipe>? h); };");
  EXPECT(m.interfaces[0].methods[0].params[0].type.nullable);
}

TEST(parser_allows_bare_handle_struct_field) {
  Module m = ParseSrc("struct Entry { handle h; };");
  EXPECT(m.structs[0].fields[0].type.kind == TypeKind::kHandle);
}

TEST(parser_allows_bare_handle_union_field) {
  Module m = ParseSrc("union U { handle h; };");
  EXPECT(m.unions[0].fields[0].type.kind == TypeKind::kHandle);
}

TEST(parser_allows_bare_handle_array_element) {
  Module m = ParseSrc("interface Foo { M(array<handle> hs); };");
  EXPECT(m.interfaces[0].methods[0].params[0].type.element->kind ==
         TypeKind::kHandle);
}

TEST(parser_allows_bare_handle_map_value) {
  Module m = ParseSrc("interface Foo { M(map<string, handle> hs); };");
  EXPECT(m.interfaces[0].methods[0].params[0].type.element->kind ==
         TypeKind::kHandle);
}

// --- real-mojom-parity phase 5: fixed-size array<T, N> ---

TEST(parser_fixed_size_array_field) {
  Module m = ParseSrc("struct Entry { array<int32, 3> nums; };");
  const TypeSpec& t = m.structs[0].fields[0].type;
  EXPECT(t.kind == TypeKind::kArray);
  EXPECT(t.element->kind == TypeKind::kInt32);
  EXPECT_EQ(t.fixed_array_size, 3);
}

TEST(parser_ordinary_array_has_zero_fixed_size) {
  Module m = ParseSrc("struct Entry { array<int32> nums; };");
  EXPECT_EQ(m.structs[0].fields[0].type.fixed_array_size, 0);
}

TEST(parser_fixed_size_array_of_struct_elements) {
  Module m = ParseSrc(
      "struct Inner { int32 x; };\n"
      "struct Outer { array<Inner, 2> items; };");
  const TypeSpec& t = m.structs[1].fields[0].type;
  EXPECT(t.kind == TypeKind::kArray);
  EXPECT(t.element->kind == TypeKind::kStructRef);
  EXPECT_EQ(t.fixed_array_size, 2);
}

TEST(parser_nullable_fixed_size_array) {
  Module m = ParseSrc("struct Entry { array<int32, 3>? nums; };");
  EXPECT(m.structs[0].fields[0].type.nullable);
  EXPECT_EQ(m.structs[0].fields[0].type.fixed_array_size, 3);
}

TEST(parser_fixed_size_array_top_level_method_param) {
  Module m = ParseSrc("interface Foo { M(array<int32, 4> vals); };");
  EXPECT_EQ(m.interfaces[0].methods[0].params[0].type.fixed_array_size, 4);
}

TEST(parser_rejects_zero_size_fixed_array) {
  bool threw = false;
  try {
    ParseSrc("struct Entry { array<int32, 0> nums; };");
  } catch (const ParseError&) {
    threw = true;
  }
  EXPECT(threw);
}

TEST(parser_rejects_negative_size_fixed_array) {
  bool threw = false;
  try {
    ParseSrc("struct Entry { array<int32, -1> nums; };");
  } catch (const ParseError&) {
    threw = true;
  }
  EXPECT(threw);
}

TEST(parser_allows_handle_bearing_fixed_array_element) {
  Module m = ParseSrc("interface Foo { M(array<handle, 3> hs); };");
  EXPECT(m.interfaces[0].methods[0].params[0].type.element->kind ==
         TypeKind::kHandle);
  EXPECT_EQ(m.interfaces[0].methods[0].params[0].type.fixed_array_size, 3);
}

// --- real-mojom-parity phase 6: `result<T, E>` response type ---

TEST(parser_result_response_desugars_to_one_union_param) {
  Module m = ParseSrc(
      "struct NotFound { string message; };\n"
      "interface Store { Get(string key) => result<string, NotFound>; };");
  const Method& method = m.interfaces[0].methods[0];
  EXPECT(method.has_response);
  EXPECT_EQ(method.response_params.size(), 1u);
  EXPECT_EQ(method.response_params[0].name, "result");
  EXPECT(method.response_params[0].type.kind == TypeKind::kUnionRef);
  EXPECT_EQ(method.response_params[0].type.name, "Store_GetResult");
}

TEST(parser_result_response_synthesizes_union_with_value_and_error_fields) {
  Module m = ParseSrc(
      "struct NotFound { string message; };\n"
      "interface Store { Get(string key) => result<string, NotFound>; };");
  EXPECT_EQ(m.unions.size(), 1u);
  const UnionDecl& u = m.unions[0];
  EXPECT_EQ(u.name, "Store_GetResult");
  EXPECT_EQ(u.fields.size(), 2u);
  EXPECT_EQ(u.fields[0].name, "value");
  EXPECT(u.fields[0].type.kind == TypeKind::kString);
  EXPECT_EQ(u.fields[0].tag, 0u);
  EXPECT_EQ(u.fields[1].name, "error");
  EXPECT(u.fields[1].type.kind == TypeKind::kStructRef);
  EXPECT_EQ(u.fields[1].type.name, "NotFound");
  EXPECT_EQ(u.fields[1].tag, 1u);
}

TEST(parser_result_union_decl_index_orders_after_its_own_error_struct) {
  Module m = ParseSrc(
      "struct NotFound { string message; };\n"
      "interface Store { Get(string key) => result<string, NotFound>; };");
  EXPECT(m.unions[0].decl_index > m.structs[0].decl_index);
}

TEST(parser_result_response_with_two_scalar_types) {
  Module m = ParseSrc("interface Calc { Div(int32 a, int32 b) => result<int32, int32>; };");
  const Method& method = m.interfaces[0].methods[0];
  EXPECT_EQ(method.response_params[0].type.name, "Calc_DivResult");
  EXPECT_EQ(m.unions[0].fields[0].type.kind, m.unions[0].fields[1].type.kind);
}

TEST(parser_result_response_names_are_interface_and_method_qualified) {
  Module m = ParseSrc(
      "interface A { M() => result<int32, int32>; };\n"
      "interface B { M() => result<int32, int32>; };");
  EXPECT_EQ(m.interfaces[0].methods[0].response_params[0].type.name, "A_MResult");
  EXPECT_EQ(m.interfaces[1].methods[0].response_params[0].type.name, "B_MResult");
  EXPECT_EQ(m.unions.size(), 2u);
}

TEST(parser_allows_result_response_referencing_later_struct) {
  Module m = ParseSrc(
      "interface Store { Get(string key) => result<string, NotFound>; };\n"
      "struct NotFound { string message; };");
  EXPECT_EQ(m.unions[0].fields[1].type.name, "NotFound");
}

TEST(parser_rejects_by_value_struct_cycle) {
  bool threw = false;
  std::string message;
  try {
    ParseSrc(
        "struct A { B b; };\n"
        "struct B { A a; };");
  } catch (const ParseError& e) {
    threw = true;
    message = e.what();
  }
  EXPECT(threw);
  EXPECT(message.find("cycle") != std::string::npos);
}

TEST(parser_rejects_self_by_value_struct) {
  bool threw = false;
  try {
    ParseSrc("struct Node { Node next; };");
  } catch (const ParseError&) {
    threw = true;
  }
  EXPECT(threw);
}

TEST(parser_allows_recursive_array_and_map) {
  Module m = ParseSrc(
      "union Value {\n"
      "  DictionaryValue dictionary_value;\n"
      "  ListValue list_value;\n"
      "};\n"
      "struct DictionaryValue { map<string, Value> storage; };\n"
      "struct ListValue { array<Value> storage; };");
  EXPECT_EQ(m.unions[0].name, "Value");
  EXPECT_EQ(m.structs[0].name, "DictionaryValue");
  EXPECT_EQ(m.structs[1].name, "ListValue");
  EXPECT(m.structs[0].fields[0].type.kind == TypeKind::kMap);
  EXPECT_EQ(m.structs[0].fields[0].type.element->name, "Value");
}

TEST(parser_allows_handle_bearing_result_success_type) {
  Module m = ParseSrc(
      "interface Watcher {};\n"
      "interface Store { Get() => result<pending_remote<Watcher>, int32>; };");
  EXPECT(m.unions[0].fields[0].type.kind == TypeKind::kPendingRemote);
}

TEST(parser_allows_handle_bearing_result_error_type) {
  Module m = ParseSrc(
      "interface Watcher {};\n"
      "interface Store { Get() => result<int32, pending_remote<Watcher>>; };");
  EXPECT(m.unions[0].fields[1].type.kind == TypeKind::kPendingRemote);
}

TEST(parser_sync_method_allows_result_response) {
  Module m = ParseSrc(
      "interface Calc { [Sync] Div(int32 a, int32 b) => result<int32, string>; };");
  EXPECT(m.interfaces[0].methods[0].is_sync);
  EXPECT(m.interfaces[0].methods[0].has_response);
}

// --- real-mojom-parity phase 7: `feature NAME { ... };` declarations ---

TEST(parser_feature_with_consts) {
  Module m = ParseSrc(
      "feature MyFeature {\n"
      "  const bool kEnabled = true;\n"
      "  const int32 kMax = 3;\n"
      "};");
  EXPECT_EQ(m.features.size(), 1u);
  EXPECT_EQ(m.features[0].name, "MyFeature");
  EXPECT_EQ(m.features[0].consts.size(), 2u);
  EXPECT_EQ(m.features[0].consts[0].name, "kEnabled");
  EXPECT_EQ(m.features[0].consts[1].name, "kMax");
}

TEST(parser_feature_block_attributes_accepted_and_ignored) {
  Module m = ParseSrc(
      "[Status=STABLE, EnabledStateByDefault=ENABLED_BY_DEFAULT]\n"
      "feature MyFeature {\n"
      "  const bool kEnabled = true;\n"
      "};");
  EXPECT_EQ(m.features[0].name, "MyFeature");
}

TEST(parser_feature_const_attribute_accepted_and_ignored) {
  Module m = ParseSrc(
      "feature MyFeature {\n"
      "  [SomeAttr] const bool kEnabled = true;\n"
      "};");
  EXPECT_EQ(m.features[0].consts[0].name, "kEnabled");
}

TEST(parser_rejects_non_const_inside_feature_body) {
  bool threw = false;
  try {
    ParseSrc("feature Bad { int32 x; };");
  } catch (const ParseError&) {
    threw = true;
  }
  EXPECT(threw);
}

TEST(parser_rejects_empty_feature_body_ok) {
  // Real mojom's own FeatureBody allows zero consts -- no restriction to
  // enforce here, just confirming it doesn't spuriously reject.
  Module m = ParseSrc("feature Empty {};");
  EXPECT_EQ(m.features[0].consts.size(), 0u);
}

TEST(parser_struct_field_default_references_qualified_feature_const) {
  Module m = ParseSrc(
      "feature MyFeature { const int32 kMax = 3; };\n"
      "struct Config { int32 retries = MyFeature.kMax; };");
  const DefaultValue& d = m.structs[0].fields[0].default_value;
  EXPECT(d.has_value);
  EXPECT_EQ(d.named_expr, "kMax");
  EXPECT_EQ(d.named_expr_owner_container, "MyFeature");
}

TEST(parser_feature_const_decl_index_is_ordered) {
  Module m = ParseSrc(
      "const int32 kTop = 1;\n"
      "feature MyFeature { const int32 kMax = 2; };");
  EXPECT(m.features[0].consts[0].decl_index > m.consts[0].decl_index);
}

// --- real-mojom-parity phase 9 (v23) remaining grammar ---

TEST(parser_chromium_method_ordinal_before_paren) {
  Module m = ParseSrc("interface Foo { Ping@0(); Pong@1(int32 x); };");
  EXPECT(m.interfaces[0].methods[0].has_explicit_ordinal);
  EXPECT_EQ(m.interfaces[0].methods[0].ordinal, 0u);
  EXPECT_EQ(m.interfaces[0].methods[1].ordinal, 1u);
}

TEST(parser_legacy_method_ordinal_after_paren_still_works) {
  Module m = ParseSrc("interface Foo { Ping() @0; Pong(int32 x) @1; };");
  EXPECT_EQ(m.interfaces[0].methods[0].ordinal, 0u);
  EXPECT_EQ(m.interfaces[0].methods[1].ordinal, 1u);
}

TEST(parser_rejects_method_ordinal_specified_twice) {
  bool threw = false;
  try {
    ParseSrc("interface Foo { Ping@0() @1; };");
  } catch (const ParseError&) {
    threw = true;
  }
  EXPECT(threw);
}

TEST(parser_struct_field_ordinal) {
  Module m = ParseSrc("struct Entry { int32 x@0; string y@1; };");
  EXPECT(m.structs[0].fields[0].has_explicit_ordinal);
  EXPECT_EQ(m.structs[0].fields[0].ordinal, 0u);
  EXPECT_EQ(m.structs[0].fields[1].ordinal, 1u);
}

TEST(parser_param_min_version_and_ordinal) {
  Module m = ParseSrc(
      "interface Foo { M([MinVersion=1] int32 x@0, string y); };");
  EXPECT_EQ(m.interfaces[0].methods[0].params[0].min_version, 1u);
  EXPECT(m.interfaces[0].methods[0].params[0].has_explicit_ordinal);
  EXPECT_EQ(m.interfaces[0].methods[0].params[0].ordinal, 0u);
}

TEST(parser_hash_map_is_map) {
  Module m = ParseSrc("struct E { hash_map<string, int32> m; };");
  EXPECT(m.structs[0].fields[0].type.kind == TypeKind::kMap);
  EXPECT(m.structs[0].fields[0].type.key->kind == TypeKind::kString);
  EXPECT(m.structs[0].fields[0].type.element->kind == TypeKind::kInt32);
}

TEST(parser_empty_native_struct) {
  Module m = ParseSrc("[Native] struct Foo;");
  EXPECT_EQ(m.structs[0].name, "Foo");
  EXPECT_EQ(m.structs[0].fields.size(), 0u);
  EXPECT(m.structs[0].is_native);
}

TEST(parser_empty_native_enum) {
  Module m = ParseSrc("[Native] enum Bar;");
  EXPECT_EQ(m.enums[0].name, "Bar");
  EXPECT_EQ(m.enums[0].values.size(), 0u);
}

TEST(parser_enum_value_identifier_alias) {
  Module m = ParseSrc("enum Color { kRed = 1, kCrimson = kRed, kBlue };");
  EXPECT_EQ(m.enums[0].values[0].value, 1);
  EXPECT_EQ(m.enums[0].values[1].value, 1);
  EXPECT_EQ(m.enums[0].values[1].name, "kCrimson");
  EXPECT_EQ(m.enums[0].values[2].value, 2);
}

TEST(parser_enum_value_default_and_min_version) {
  Module m = ParseSrc(
      "[Extensible] enum Color { [Default] kUnknown = 0, [MinVersion=1] kBlue = 1 };");
  EXPECT(m.enums[0].values[0].is_default);
  EXPECT_EQ(m.enums[0].values[1].min_version, 1u);
}

TEST(parser_union_default_field) {
  Module m = ParseSrc(
      "[Extensible] union Status { [Default] int32 code; string msg; };");
  EXPECT(m.unions[0].fields[0].is_default);
  EXPECT(!m.unions[0].fields[1].is_default);
}

TEST(parser_rejects_two_union_defaults) {
  bool threw = false;
  try {
    ParseSrc("[Extensible] union U { [Default] int32 a; [Default] int32 b; };");
  } catch (const ParseError&) {
    threw = true;
  }
  EXPECT(threw);
}

TEST(parser_module_attribute_is_accepted) {
  Module m = ParseSrc("[Stable] module foo;\ninterface Bar {};");
  EXPECT_EQ(m.name, "foo");
  EXPECT_EQ(m.interfaces[0].name, "Bar");
}

TEST(parser_nested_struct_in_interface) {
  Module m = ParseSrc(
      "interface Store {\n"
      "  struct Params { int32 n; };\n"
      "  Get(Params p) => (Params out);\n"
      "};");
  EXPECT_EQ(m.interfaces[0].structs.size(), 1u);
  EXPECT_EQ(m.interfaces[0].structs[0].name, "Params");
  EXPECT_EQ(m.interfaces[0].structs[0].owner_container, "Store");
  const TypeSpec& t = m.interfaces[0].methods[0].params[0].type;
  EXPECT(t.kind == TypeKind::kStructRef);
  EXPECT_EQ(t.name, "Params");
  EXPECT_EQ(t.owner_container, "Store");
}

TEST(parser_qualified_nested_struct_reference) {
  Module m = ParseSrc(
      "interface Store { struct Params { int32 n; }; };\n"
      "struct Wrapper { Store.Params p; };");
  const TypeSpec& t = m.structs[0].fields[0].type;
  EXPECT(t.kind == TypeKind::kStructRef);
  EXPECT_EQ(t.name, "Params");
  EXPECT_EQ(t.owner_container, "Store");
}

TEST(parser_nested_union_in_interface) {
  Module m = ParseSrc(
      "interface Pipe {\n"
      "  union Msg { int32 n; string s; };\n"
      "  Send(Msg m);\n"
      "};");
  EXPECT_EQ(m.interfaces[0].unions.size(), 1u);
  EXPECT_EQ(m.interfaces[0].unions[0].name, "Msg");
  EXPECT_EQ(m.interfaces[0].unions[0].owner_container, "Pipe");
  EXPECT(m.interfaces[0].methods[0].params[0].type.kind == TypeKind::kUnionRef);
  EXPECT_EQ(m.interfaces[0].methods[0].params[0].type.owner_container, "Pipe");
}

TEST(parser_handle_mach_subtypes_are_platform) {
  Module m = ParseSrc(
      "struct Ports {\n"
      "  handle<mach_send> send;\n"
      "  handle<mach_receive> recv;\n"
      "  handle<mach_port> port;\n"
      "};");
  EXPECT(m.structs[0].fields[0].type.kind == TypeKind::kHandlePlatform);
  EXPECT(m.structs[0].fields[1].type.kind == TypeKind::kHandlePlatform);
  EXPECT(m.structs[0].fields[2].type.kind == TypeKind::kHandlePlatform);
}

TEST(parser_enable_if_is_captured_on_interface) {
  Module m = ParseSrc(
      "[EnableIf=a|b|c] interface Foo { Get() => (int32 x); };");
  EXPECT(m.interfaces[0].enable_if.active());
  EXPECT(m.interfaces[0].enable_if.enable_if.mode == EnableIfClause::Mode::kAny);
  EXPECT_EQ(m.interfaces[0].enable_if.enable_if.flags.size(), 3u);
}

TEST(parser_enable_if_drops_interface_without_flag) {
  Module m = ParseSrc(
      "[EnableIf=linux] interface Foo { Get() => (int32 x); };\n"
      "interface Bar { Ping(); };");
  ApplyEnableIf(m, {});
  EXPECT_EQ(m.interfaces.size(), 1u);
  EXPECT_EQ(m.interfaces[0].name, "Bar");
}

TEST(parser_enable_if_keeps_interface_with_matching_flag) {
  Module m = ParseSrc(
      "[EnableIf=linux] interface Foo { Get() => (int32 x); };");
  ApplyEnableIf(m, {"linux"});
  EXPECT_EQ(m.interfaces.size(), 1u);
  EXPECT_EQ(m.interfaces[0].name, "Foo");
}

TEST(parser_enable_if_not_drops_when_flag_set) {
  Module m = ParseSrc(
      "[EnableIfNot=is_ios] interface Foo { Get() => (int32 x); };");
  ApplyEnableIf(m, {"is_ios"});
  EXPECT_EQ(m.interfaces.size(), 0u);
  Module kept = ParseSrc(
      "[EnableIfNot=is_ios] interface Foo { Get() => (int32 x); };");
  ApplyEnableIf(kept, {});
  EXPECT_EQ(kept.interfaces.size(), 1u);
}

TEST(parser_enable_if_mutually_exclusive_fields) {
  Module m = ParseSrc(
      "struct FilePath {\n"
      "  [EnableIf=file_path_is_string] string path;\n"
      "  [EnableIf=file_path_is_string16] array<uint16> path;\n"
      "};");
  EXPECT_EQ(m.structs[0].fields.size(), 2u);
  ApplyEnableIf(m, {"file_path_is_string"});
  EXPECT_EQ(m.structs[0].fields.size(), 1u);
  EXPECT(m.structs[0].fields[0].type.kind == TypeKind::kString);
}

TEST(parser_enable_if_all_flags_required) {
  Module m = ParseSrc(
      "[EnableIf=a&b] interface Foo { Get() => (int32 x); };");
  ApplyEnableIf(m, {"a"});
  EXPECT_EQ(m.interfaces.size(), 0u);
  Module both = ParseSrc(
      "[EnableIf=a&b] interface Foo { Get() => (int32 x); };");
  ApplyEnableIf(both, {"a", "b"});
  EXPECT_EQ(both.interfaces.size(), 1u);
}

TEST(parser_associated_shorthand_remote_and_receiver) {
  Module m = ParseSrc(
      "interface Listener {};\n"
      "interface Host {\n"
      "  Set(associated Listener l);\n"
      "  Take(associated Listener& r);\n"
      "};");
  EXPECT(m.interfaces[1].methods[0].params[0].type.kind ==
         TypeKind::kPendingAssociatedRemote);
  EXPECT_EQ(m.interfaces[1].methods[0].params[0].type.name, "Listener");
  EXPECT(m.interfaces[1].methods[1].params[0].type.kind ==
         TypeKind::kPendingAssociatedReceiver);
  EXPECT_EQ(m.interfaces[1].methods[1].params[0].type.name, "Listener");
}

TEST(parser_enable_if_drops_method_keeps_ordinal_hole) {
  Module m = ParseSrc(
      "interface Foo {\n"
      "  A();\n"
      "  [EnableIf=opt] B();\n"
      "  C();\n"
      "};");
  EXPECT_EQ(m.interfaces[0].methods[1].ordinal, 1u);
  EXPECT_EQ(m.interfaces[0].methods[2].ordinal, 2u);
  ApplyEnableIf(m, {});
  EXPECT_EQ(m.interfaces[0].methods.size(), 2u);
  EXPECT_EQ(m.interfaces[0].methods[0].name, "A");
  EXPECT_EQ(m.interfaces[0].methods[1].name, "C");
  EXPECT_EQ(m.interfaces[0].methods[1].ordinal, 2u);
}
