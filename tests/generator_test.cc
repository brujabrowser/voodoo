// Sanity checks on the generator's textual output. The real proof that
// this is *correct*, not just plausible-looking, is tests/golden -- a
// separate binary that compiles and runs the generated code against real
// WASMCadidumBindings (only built when that sibling checkout is present).
#include "test.h"

#include "../src/cpp_generator.h"
#include "../src/lexer.h"
#include "../src/parser.h"

using namespace voodoom;

namespace {
Module ParseSrc(const std::string& src) { return Parse(Tokenize(src)); }

bool Contains(const std::string& haystack, const std::string& needle) {
  return haystack.find(needle) != std::string::npos;
}
}  // namespace

TEST(generator_emits_expected_shapes) {
  Module m = ParseSrc(
      "module echo;\n"
      "interface EchoListener {\n"
      "  OnEcho(string value);\n"
      "};\n"
      "interface Echo {\n"
      "  EchoString(string in) => (string out);\n"
      "  SetListener(pending_associated_remote<EchoListener> listener);\n"
      "};");
  std::string h = GenerateCppHeader(m, "ECHO_GEN_H_", "echo", "echo.voodoom");

  EXPECT(Contains(h, "#ifndef ECHO_GEN_H_"));
  EXPECT(Contains(h, "namespace echo {"));
  EXPECT(Contains(h, "class EchoListener {"));
  EXPECT(Contains(h, "virtual void OnEcho(const std::string& value) = 0;"));
  EXPECT(Contains(h, "static constexpr uint32_t kOnEchoName = 0;"));
  EXPECT(Contains(h, "class Echo {"));
  EXPECT(Contains(h,
                   "virtual void EchoString(const std::string& in, "
                   "base::OnceCallback<void(std::string)> callback) = 0;"));
  EXPECT(Contains(h, "static constexpr uint32_t kEchoStringName = 0;"));
  EXPECT(Contains(h, "static constexpr uint32_t kSetListenerName = 1;"));
  EXPECT(Contains(h, "class Echo::Proxy_ : public Echo"));
  EXPECT(Contains(h, "static const void* type_key()"));
  EXPECT(Contains(h, "return \"interface:echo.Echo\""));
  EXPECT(Contains(h, "mojo::internal::TypeTag(type_key())"));
  EXPECT(Contains(h, "Echo* FromHandle"));
  EXPECT(Contains(h, "mojo::internal::ResponseDispatcher responses_;"));
  // EchoListener has no response-bearing *application* methods, but
  // every Proxy_ now declares responses_ unconditionally regardless --
  // QueryVersion (see "Interface versioning") always expects one.
  EXPECT(Contains(h, "class EchoListener::Proxy_"));
  size_t echo_listener_proxy = h.find("class EchoListener::Proxy_");
  size_t echo_proxy = h.find("class Echo::Proxy_");
  std::string listener_proxy_section =
      h.substr(echo_listener_proxy, echo_proxy - echo_listener_proxy);
  EXPECT(Contains(listener_proxy_section, "ResponseDispatcher responses_;"));
  EXPECT(Contains(listener_proxy_section, "void QueryVersion("));

  // Braces balance -- a cheap syntactic smoke test.
  int depth = 0;
  for (char c : h) {
    if (c == '{') ++depth;
    if (c == '}') --depth;
  }
  EXPECT_EQ(depth, 0);
}

TEST(generator_forward_declares_all_interfaces_up_front) {
  // Interfaces have no declared-before-use restriction: every interface
  // name is forward-declared before any full definition, so a
  // pending_(associated_)remote/receiver<T> parameter can reference an
  // interface declared later in the file. Here Echo (which references
  // EchoListener) comes FIRST in the file -- the reverse of echo.voodoom.
  Module m = ParseSrc(
      "interface Echo { SetListener(pending_associated_remote<EchoListener> "
      "l); };\n"
      "interface EchoListener { OnEcho(string value); };");
  std::string h = GenerateCppHeader(m, "G_H_", "ns", "x.voodoom");
  EXPECT(Contains(h, "class Echo;"));
  EXPECT(Contains(h, "class EchoListener;"));
  size_t fwd_echo = h.find("class Echo;");
  size_t fwd_listener = h.find("class EchoListener;");
  size_t full_echo = h.find("class Echo {");
  size_t full_listener = h.find("class EchoListener {");
  EXPECT(fwd_echo < full_echo);
  EXPECT(fwd_listener < full_echo);   // forward decl precedes Echo's body
  EXPECT(fwd_listener < full_listener);
}

TEST(generator_emits_enum) {
  Module m = ParseSrc("enum Status { OK, WARN = 5, ERROR };");
  std::string h = GenerateCppHeader(m, "G_H_", "ns", "x.voodoom");
  EXPECT(Contains(h, "enum class Status : int32_t {"));
  EXPECT(Contains(h, "OK = 0,"));
  EXPECT(Contains(h, "WARN = 5,"));
  EXPECT(Contains(h, "ERROR = 6,"));
}

TEST(generator_emits_const) {
  Module m = ParseSrc("const int32 kMax = 42;");
  std::string h = GenerateCppHeader(m, "G_H_", "ns", "x.voodoom");
  EXPECT(Contains(h, "inline constexpr int32_t kMax = 42;"));
}

TEST(generator_emits_struct_with_write_read_functions) {
  Module m = ParseSrc(
      "enum Status { OK };\n"
      "struct Entry {\n"
      "  string key;\n"
      "  Status status;\n"
      "  array<int32> nums;\n"
      "};");
  std::string h = GenerateCppHeader(m, "G_H_", "ns", "x.voodoom");
  EXPECT(Contains(h, "struct Entry {"));
  EXPECT(Contains(h, "std::string key{};"));
  EXPECT(Contains(h, "Status status{};"));
  EXPECT(Contains(h, "v8::internal::CageVector<int32_t> nums{};"));
  EXPECT(Contains(h, "inline void WriteEntry(mojo::Message& message, const "
                     "Entry& value) {"));
  EXPECT(Contains(h, "inline bool ReadEntry(const mojo::Message& message, "
                     "size_t* offset, Entry* out) {"));
  // The struct's own Read function must use its `offset` PARAMETER
  // directly (it's already size_t*), not "&offset" -- that was a real bug
  // caught by tests/golden (offset is a local size_t only at the top
  // level, inside Stub_::AcceptX / the Proxy_ response lambda).
  size_t read_fn = h.find("inline bool ReadEntry(");
  std::string read_body = h.substr(read_fn, h.find("\n}\n", read_fn) - read_fn);
  EXPECT(!Contains(read_body, "&offset"));
  EXPECT(Contains(read_body, ", offset,"));

  int depth = 0;
  for (char c : h) {
    if (c == '{') ++depth;
    if (c == '}') --depth;
  }
  EXPECT_EQ(depth, 0);
}

TEST(generator_struct_before_interfaces_regardless_of_file_order) {
  // Structs' Write/Read free functions must be emitted before any
  // interface body that calls them, even if the .voodoom file declares
  // the interface first (only struct-vs-struct order is restricted, at
  // parse time -- struct-vs-interface order is always fixed by the
  // generator's phase order).
  Module m = ParseSrc(
      "interface Foo { Send(Entry e); };\n"
      "struct Entry { int32 x; };");
  std::string h = GenerateCppHeader(m, "G_H_", "ns", "x.voodoom");
  EXPECT(h.find("inline void WriteEntry(") < h.find("class Foo {"));
}

TEST(generator_emits_non_associated_pending_handle_attach) {
  Module m = ParseSrc(
      "interface Watcher {};\n"
      "interface Store { Watch(pending_remote<Watcher> w); };");
  std::string h = GenerateCppHeader(m, "G_H_", "ns", "x.voodoom");
  EXPECT(Contains(h, "message.AttachHandle(mojo::ScopedHandle(w_pipe.release()));"));
  EXPECT(Contains(h, "message->TakeHandles();"));
}

TEST(generator_emits_union_with_write_read_functions) {
  Module m = ParseSrc(
      "union Result {\n"
      "  int32 int_value;\n"
      "  string error_message;\n"
      "};");
  std::string h = GenerateCppHeader(m, "G_H_", "ns", "x.voodoom");
  EXPECT(Contains(h, "class Result {"));
  EXPECT(Contains(h, "enum class Tag : int32_t {"));
  EXPECT(Contains(h, "int_value = 0,"));
  EXPECT(Contains(h, "error_message = 1,"));
  EXPECT(Contains(h, "void set_int_value(int32_t value) {"));
  EXPECT(Contains(h, "const int32_t& int_value() const { return int_value_; }"));
  EXPECT(Contains(h, "void set_error_message(std::string value) {"));
  EXPECT(Contains(h, "inline void WriteResult(mojo::Message& message, const "
                     "Result& value) {"));
  EXPECT(Contains(h, "inline bool ReadResult(const mojo::Message& message, "
                     "size_t* offset, Result* out) {"));

  int depth = 0;
  for (char c : h) {
    if (c == '{') ++depth;
    if (c == '}') --depth;
  }
  EXPECT_EQ(depth, 0);
}

TEST(generator_emits_structs_and_unions_in_declared_order) {
  // A union embedding an earlier struct, and a later struct embedding that
  // union, must be emitted Write/Read-function-before-use in file order --
  // not all-structs-then-all-unions (see GenerateCppHeader's merge sort).
  Module m = ParseSrc(
      "struct Inner { int32 x; };\n"
      "union Choice { Inner inner; string s; };\n"
      "struct Outer { Choice choice; };");
  std::string h = GenerateCppHeader(m, "G_H_", "ns", "x.voodoom");
  size_t inner = h.find("inline void WriteInner(");
  size_t choice = h.find("inline void WriteChoice(");
  size_t outer = h.find("inline void WriteOuter(");
  EXPECT(inner != std::string::npos);
  EXPECT(choice != std::string::npos);
  EXPECT(outer != std::string::npos);
  EXPECT(inner < choice);
  EXPECT(choice < outer);
}

TEST(generator_forward_declares_structs_and_unions) {
  Module m = ParseSrc(
      "struct Inner { int32 x; };\n"
      "union Choice { Inner inner; string s; };");
  std::string h = GenerateCppHeader(m, "G_H_", "ns", "x.voodoom");
  EXPECT(Contains(h, "struct Inner;"));
  EXPECT(Contains(h, "class Choice;"));
  EXPECT(h.find("struct Inner;") < h.find("struct Inner {"));
  EXPECT(h.find("class Choice;") < h.find("class Choice {"));
}

TEST(generator_emits_later_struct_before_by_value_user) {
  Module m = ParseSrc(
      "struct A { B b; };\n"
      "struct B { int32 x; };");
  std::string h = GenerateCppHeader(m, "G_H_", "ns", "x.voodoom");
  EXPECT(h.find("inline void WriteB(") < h.find("inline void WriteA("));
}

TEST(generator_recursive_map_emits_dictionary_before_value) {
  Module m = ParseSrc(
      "union Value { DictionaryValue dictionary_value; ListValue list_value; };\n"
      "struct DictionaryValue { map<string, Value> storage; };\n"
      "struct ListValue { array<Value> storage; };");
  std::string h = GenerateCppHeader(m, "G_H_", "ns", "x.voodoom");
  EXPECT(Contains(h, "#include \"src/sandbox/cage-allocator.h\""));
  EXPECT(Contains(h, "v8::internal::CageMap<std::string, Value> storage{}"));
  EXPECT(Contains(h, "v8::internal::CageVector<Value> storage{}"));
  size_t dict = h.find("inline void WriteDictionaryValue(");
  size_t list = h.find("inline void WriteListValue(");
  size_t value = h.find("inline void WriteValue(");
  EXPECT(dict != std::string::npos);
  EXPECT(list != std::string::npos);
  EXPECT(value != std::string::npos);
  EXPECT(dict < value);
  EXPECT(list < value);
  // Write/Read iterate the map/array of Value, so Value's class must be
  // complete before DictionaryValue's serializers -- not merely the
  // CageMap member, which is legal while Value is still incomplete.
  EXPECT(h.find("class Value {") < dict);
}

TEST(generator_emits_map_field) {
  Module m = ParseSrc(
      "struct Config {\n"
      "  map<string, int32> values;\n"
      "};");
  std::string h = GenerateCppHeader(m, "G_H_", "ns", "x.voodoom");
  EXPECT(Contains(h, "v8::internal::CageMap<std::string, int32_t> values{};"));
  EXPECT(Contains(h, "WritePayload"));
  EXPECT(Contains(h, ".emplace(std::move("));

  int depth = 0;
  for (char c : h) {
    if (c == '{') ++depth;
    if (c == '}') --depth;
  }
  EXPECT_EQ(depth, 0);
}

TEST(generator_map_param_is_passed_by_const_ref) {
  Module m = ParseSrc("interface Foo { Send(map<int32, string> m); };");
  std::string h = GenerateCppHeader(m, "G_H_", "ns", "x.voodoom");
  EXPECT(Contains(h,
                   "virtual void Send(const v8::internal::CageMap<int32_t, "
                   "std::string>& m) = 0;"));
}

TEST(generator_nullable_field_wraps_in_optional) {
  Module m = ParseSrc("struct Foo { string? name; };");
  std::string h = GenerateCppHeader(m, "G_H_", "ns", "x.voodoom");
  EXPECT(Contains(h, "std::optional<std::string> name{};"));
  EXPECT(Contains(h, "#include <optional>"));

  int depth = 0;
  for (char c : h) {
    if (c == '{') ++depth;
    if (c == '}') --depth;
  }
  EXPECT_EQ(depth, 0);
}

TEST(generator_nullable_field_write_emits_presence_flag) {
  Module m = ParseSrc("struct Foo { string? name; };");
  std::string h = GenerateCppHeader(m, "G_H_", "ns", "x.voodoom");
  EXPECT(Contains(h, ".has_value();"));
  EXPECT(Contains(h, "mojo::internal::WriteScalar"));
  EXPECT(Contains(h, "mojo::internal::WriteString"));
}

TEST(generator_nullable_param_is_passed_by_const_ref) {
  Module m = ParseSrc("interface Foo { Send(string? s); };");
  std::string h = GenerateCppHeader(m, "G_H_", "ns", "x.voodoom");
  EXPECT(Contains(
      h, "virtual void Send(const std::optional<std::string>& s) = 0;"));
}

TEST(generator_dotted_module_produces_nested_namespaces) {
  Module m = ParseSrc("module foo.bar.baz;\nstruct S { int32 x; };");
  std::string h = GenerateCppHeader(m, "G_H_", m.name, "x.voodoom");
  EXPECT(Contains(h, "namespace foo {\nnamespace bar {\nnamespace baz {\n"));
  EXPECT(Contains(h, "}  // namespace baz\n}  // namespace bar\n}  // "
                     "namespace foo\n"));

  int depth = 0;
  for (char c : h) {
    if (c == '{') ++depth;
    if (c == '}') --depth;
  }
  EXPECT_EQ(depth, 0);
}

TEST(generator_single_segment_namespace_unchanged) {
  Module m = ParseSrc("module echo;\nstruct S { int32 x; };");
  std::string h = GenerateCppHeader(m, "G_H_", m.name, "x.voodoom");
  EXPECT(Contains(h, "namespace echo {\n"));
  EXPECT(Contains(h, "}  // namespace echo\n"));
  EXPECT(!Contains(h, "namespace echo {\nnamespace"));
}

TEST(generator_field_defaults_emitted_as_brace_init) {
  Module m = ParseSrc(
      "struct Foo {\n"
      "  bool flag = true;\n"
      "  int32 x = 42;\n"
      "  string name = \"Bob\";\n"
      "  int32 y;\n"
      "};");
  std::string h = GenerateCppHeader(m, "G_H_", "ns", "x.voodoom");
  EXPECT(Contains(h, "bool flag{true};"));
  EXPECT(Contains(h, "int32_t x{42};"));
  EXPECT(Contains(h, "std::string name{\"Bob\"};"));
  EXPECT(Contains(h, "int32_t y{};"));
}

TEST(generator_field_default_string_escaping) {
  Module m = ParseSrc("struct Foo { string s = \"a\\\"b\\\\c\"; };");
  std::string h = GenerateCppHeader(m, "G_H_", "ns", "x.voodoom");
  EXPECT(Contains(h, "std::string s{\"a\\\"b\\\\c\"};"));
}

TEST(generator_sync_method_emits_blocking_overload) {
  Module m = ParseSrc(
      "interface Calc { [Sync] Add(int32 a, int32 b) => (int32 sum); };");
  std::string h = GenerateCppHeader(m, "G_H_", "ns", "x.voodoom");
  EXPECT(Contains(
      h, "[[nodiscard]] bool Add(int32_t a, int32_t b, int32_t* sum) {"));
  EXPECT(Contains(h, "router_->connector().SyncWaitFor("));
  EXPECT(Contains(h, "*sum = std::move(wvc_sync_sum);"));
  // The normal callback-taking overload must still be there too -- [Sync]
  // adds an overload, it doesn't replace the async one.
  EXPECT(Contains(
      h,
      "void Add(int32_t a, int32_t b, base::OnceCallback<void(int32_t)> "
      "callback) override {"));

  int depth = 0;
  for (char c : h) {
    if (c == '{') ++depth;
    if (c == '}') --depth;
  }
  EXPECT_EQ(depth, 0);
}

TEST(generator_non_sync_method_has_no_blocking_overload) {
  Module m = ParseSrc("interface Foo { Get() => (int32 x); };");
  std::string h = GenerateCppHeader(m, "G_H_", "ns", "x.voodoom");
  EXPECT(!Contains(h, "SyncWaitFor"));
}

TEST(generator_enum_field_default_emits_qualified_value) {
  Module m = ParseSrc(
      "enum Status { OK, WARN, ERROR };\n"
      "struct Foo { Status s = WARN; };");
  std::string h = GenerateCppHeader(m, "G_H_", "ns", "x.voodoom");
  EXPECT(Contains(h, "Status s{Status::WARN};"));
}

TEST(generator_const_field_default_emits_const_name) {
  Module m = ParseSrc(
      "const int32 kMax = 42;\nstruct Foo { int32 x = kMax; };");
  std::string h = GenerateCppHeader(m, "G_H_", "ns", "x.voodoom");
  EXPECT(Contains(h, "inline constexpr int32_t kMax = 42;"));
  EXPECT(Contains(h, "int32_t x{kMax};"));
}

TEST(generator_versioned_struct_emits_header_and_guarded_field) {
  Module m = ParseSrc(
      "struct Widget {\n"
      "  string name;\n"
      "  [MinVersion=1] string color;\n"
      "};");
  std::string h = GenerateCppHeader(m, "G_H_", "ns", "x.voodoom");
  EXPECT(Contains(h, "mojo::internal::WriteStructHeader(&message, 0, 0);"));
  EXPECT(Contains(h, "mojo::internal::PatchStructHeader("));
  EXPECT(Contains(h, "if (wvc_version >= 1u) {"));
  // The unversioned field must NOT get a tautological "version >= 0u"
  // guard (a real -Wtype-limits warning downstream).
  EXPECT(!Contains(h, ">= 0u"));

  int depth = 0;
  for (char c : h) {
    if (c == '{') ++depth;
    if (c == '}') --depth;
  }
  EXPECT_EQ(depth, 0);
}

TEST(generator_unversioned_struct_still_gets_a_header) {
  // Every struct is wire-extensible, even with no [MinVersion] fields at
  // all -- version 0, but still a real StructHeader on the wire.
  Module m = ParseSrc("struct Widget { string name; };");
  std::string h = GenerateCppHeader(m, "G_H_", "ns", "x.voodoom");
  EXPECT(Contains(h, "mojo::internal::ReadStructHeader(message, offset,"));
  EXPECT(!Contains(h, "wvc_version >="));
}

TEST(generator_struct_header_include_present) {
  Module m = ParseSrc("struct Widget { string name; };");
  std::string h = GenerateCppHeader(m, "G_H_", "ns", "x.voodoom");
  EXPECT(Contains(h, "#include <cstring>"));
}

TEST(generator_interface_kversion_constant) {
  Module m = ParseSrc(
      "interface Foo {\n"
      "  A() => (int32 x);\n"
      "  [MinVersion=3] B() => (int32 x);\n"
      "};");
  std::string h = GenerateCppHeader(m, "G_H_", "ns", "x.voodoom");
  EXPECT(Contains(h, "static constexpr uint32_t kVersion = 3u;"));
}

TEST(generator_extensible_interface_stub_tolerates_unknown_ordinal) {
  Module m = ParseSrc("[Extensible]\ninterface Foo { M() => (int32 x); };");
  std::string h = GenerateCppHeader(m, "G_H_", "ns", "x.voodoom");
  // The exact fallthrough branch EmitInterface emits for [Extensible]
  // (see cpp_generator.cc) -- unambiguous, unlike grepping for a bare
  // "return true;" which also appears elsewhere (response handlers).
  EXPECT(Contains(h, "// [Extensible]: an unrecognized message ordinal is"));

  int depth = 0;
  for (char c : h) {
    if (c == '{') ++depth;
    if (c == '}') --depth;
  }
  EXPECT_EQ(depth, 0);
}

TEST(generator_non_extensible_interface_stub_rejects_unknown_ordinal) {
  Module m = ParseSrc("interface Foo { M() => (int32 x); };");
  std::string h = GenerateCppHeader(m, "G_H_", "ns", "x.voodoom");
  EXPECT(!Contains(h, "[Extensible]"));
}

TEST(generator_extensible_union_gets_kunknown_tag_and_size_framing) {
  Module m = ParseSrc(
      "[Extensible]\nunion U { int32 a; [MinVersion=1] string b; };");
  std::string h = GenerateCppHeader(m, "G_H_", "ns", "x.voodoom");
  EXPECT(Contains(h, "kUnknown = -1,"));
  EXPECT(Contains(h, "void set_unknown() { tag_ = Tag::kUnknown; }"));
  EXPECT(Contains(h, "mojo::internal::WriteScalar(&message, static_cast<uint32_t>(0));"));
  EXPECT(Contains(h, "mojo::internal::PatchUint32("));
  EXPECT(Contains(h, "out->set_unknown();"));
  EXPECT(Contains(h, "static constexpr uint32_t kVersion = 1u;"));

  int depth = 0;
  for (char c : h) {
    if (c == '{') ++depth;
    if (c == '}') --depth;
  }
  EXPECT_EQ(depth, 0);
}

TEST(generator_non_extensible_union_has_no_kunknown_tag) {
  Module m = ParseSrc("union U { int32 a; string b; };");
  std::string h = GenerateCppHeader(m, "G_H_", "ns", "x.voodoom");
  EXPECT(!Contains(h, "kUnknown"));
  EXPECT(!Contains(h, "set_unknown"));
  // Still gets the size-prefixed wire framing (all unions do, regardless
  // of extensibility -- see EmitUnion's header comment).
  EXPECT(Contains(h, "mojo::internal::WriteScalar(&message, static_cast<uint32_t>(0));"));
  EXPECT(Contains(h, "mojo::internal::PatchUint32("));
  EXPECT(Contains(h, "static constexpr uint32_t kVersion = 0u;"));
}

TEST(generator_emits_is_known_enum_helper) {
  Module m = ParseSrc("enum Color { kRed, kGreen, kBlue };");
  std::string h = GenerateCppHeader(m, "G_H_", "ns", "x.voodoom");
  EXPECT(Contains(h, "inline bool IsKnownColor(int32_t wvc_value) {"));
  EXPECT(Contains(h, "    case 0:\n"));
  EXPECT(Contains(h, "    case 1:\n"));
  EXPECT(Contains(h, "    case 2:\n"));
  EXPECT(Contains(h, "      return true;\n"));

  int depth = 0;
  for (char c : h) {
    if (c == '{') ++depth;
    if (c == '}') --depth;
  }
  EXPECT_EQ(depth, 0);
}

TEST(generator_is_known_enum_helper_dedups_aliased_values) {
  // Two differently-named values sharing the same underlying number (legal
  // -- see ParseEnumValue) must not produce a duplicate `case` label.
  Module m = ParseSrc("enum Color { kRed = 0, kAlsoRed = 0, kGreen = 1 };");
  std::string h = GenerateCppHeader(m, "G_H_", "ns", "x.voodoom");
  EXPECT(Contains(h, "inline bool IsKnownColor(int32_t wvc_value) {"));

  int depth = 0;
  for (char c : h) {
    if (c == '{') ++depth;
    if (c == '}') --depth;
  }
  EXPECT_EQ(depth, 0);
}

TEST(generator_non_extensible_enum_field_read_validates_value) {
  Module m = ParseSrc(
      "enum Color { kRed, kGreen, kBlue };\nstruct S { Color c; };");
  std::string h = GenerateCppHeader(m, "G_H_", "ns", "x.voodoom");
  EXPECT(Contains(h, "if (!IsKnownColorAsOf("));
}

TEST(generator_extensible_enum_field_read_skips_validation) {
  Module m = ParseSrc(
      "[Extensible]\nenum Color { kRed, kGreen, kBlue };\n"
      "struct S { Color c; };");
  std::string h = GenerateCppHeader(m, "G_H_", "ns", "x.voodoom");
  EXPECT(!Contains(h, "if (!IsKnownColor("));
  // The helper is still emitted -- just never called from the read path.
  EXPECT(Contains(h, "inline bool IsKnownColor(int32_t wvc_value) {"));

  int depth = 0;
  for (char c : h) {
    if (c == '{') ++depth;
    if (c == '}') --depth;
  }
  EXPECT_EQ(depth, 0);
}

TEST(generator_stub_intercepts_control_ordinals_before_method_dispatch) {
  Module m = ParseSrc("interface Foo { M() => (int32 x); };");
  std::string h = GenerateCppHeader(m, "G_H_", "ns", "x.voodoom");
  EXPECT(Contains(h, "if (message->name() == mojo::internal::kRunMessageId) {"));
  EXPECT(Contains(
      h, "mojo::internal::HandleQueryVersionMessage(\n            message, "
         "router_, id_, kVersion);"));
  EXPECT(Contains(
      h, "if (message->name() == "
         "mojo::internal::kRunOrClosePipeMessageId) {"));
  EXPECT(Contains(h, "mojo::internal::HandleRequireVersionMessage(\n         "
                     "   *message, kVersion);"));

  // Control-message checks must come before the ordinary method-ordinal
  // dispatch *within Stub_::Accept's own body*, matching real Mojo's own
  // precedence (kMName itself is declared much earlier, as an
  // Interface-level constant -- not what's being checked here).
  size_t stub_accept =
      h.find("[[nodiscard]] bool Accept(mojo::Message* message) override {");
  size_t control_check = h.find("kRunMessageId", stub_accept);
  size_t method_check = h.find("if (message->name() == kMName)", stub_accept);
  EXPECT(stub_accept != std::string::npos);
  EXPECT(control_check != std::string::npos);
  EXPECT(method_check != std::string::npos);
  EXPECT(control_check < method_check);

  int depth = 0;
  for (char c : h) {
    if (c == '{') ++depth;
    if (c == '}') --depth;
  }
  EXPECT_EQ(depth, 0);
}

TEST(generator_proxy_gets_query_and_require_version_unconditionally) {
  // No response-bearing application method at all -- QueryVersion/
  // RequireVersion must still be emitted, since they're protocol-level,
  // not gated on the interface's own methods.
  Module m = ParseSrc("interface Foo { M(); };");
  std::string h = GenerateCppHeader(m, "G_H_", "ns", "x.voodoom");
  EXPECT(Contains(h, "void QueryVersion(base::OnceCallback<void(uint32_t)> "
                     "callback) {"));
  EXPECT(Contains(h, "void RequireVersion(uint32_t version) {"));
  EXPECT(Contains(h, "mojo::internal::ResponseDispatcher responses_;"));
}

TEST(generator_non_nullable_pending_remote_is_never_wrapped_in_optional) {
  Module m = ParseSrc(
      "interface Watcher {};\n"
      "interface Store { Watch(pending_remote<Watcher> w); };");
  std::string h = GenerateCppHeader(m, "G_H_", "ns", "x.voodoom");
  EXPECT(!Contains(h, "std::optional<mojo::PendingRemote"));
}

TEST(generator_nullable_pending_remote_is_never_wrapped_in_optional) {
  // v14: nullable pending_* stays the bare PendingRemote<T> spelling too
  // -- is_valid() is already its absent state, no std::optional needed.
  Module m = ParseSrc(
      "interface Watcher {};\n"
      "interface Store { Watch(pending_remote<Watcher>? w); };");
  std::string h = GenerateCppHeader(m, "G_H_", "ns", "x.voodoom");
  EXPECT(!Contains(h, "std::optional<mojo::PendingRemote"));
  EXPECT(Contains(h, "mojo::PendingRemote<Watcher> w) override {"));
}

TEST(generator_nullable_pending_remote_gets_presence_flag_framing) {
  Module m = ParseSrc(
      "interface Watcher {};\n"
      "interface Store { Watch(pending_remote<Watcher>? w); };");
  std::string h = GenerateCppHeader(m, "G_H_", "ns", "x.voodoom");
  EXPECT(Contains(h, "bool w_present = w.is_valid();"));
  EXPECT(Contains(h, "message.WritePayload(&w_present, sizeof(w_present));"));
  EXPECT(Contains(h, "if (w_present) {"));
  // The reader mirrors it: read the flag first, declare w default
  // (absent), then only construct/assign it inside the guard.
  EXPECT(Contains(h,
      "bool w_present = false;\n"
      "      if (!mojo::internal::ReadScalar(*message, &offset, "
      "&w_present)) return false;\n"
      "      mojo::PendingRemote<Watcher> w;\n"
      "      if (w_present) {"));

  int depth = 0;
  for (char c : h) {
    if (c == '{') ++depth;
    if (c == '}') --depth;
  }
  EXPECT_EQ(depth, 0);
}

TEST(generator_nullable_pending_associated_remote_gets_presence_flag_framing) {
  Module m = ParseSrc(
      "interface Listener {};\n"
      "interface Echo { SetListener(pending_associated_remote<Listener>? "
      "l); };");
  std::string h = GenerateCppHeader(m, "G_H_", "ns", "x.voodoom");
  EXPECT(Contains(h, "bool l_present = l.is_valid();"));
  EXPECT(Contains(h, "message.WritePayload(&l_present, sizeof(l_present));"));
  EXPECT(Contains(h, "if (l_present) {"));
  EXPECT(Contains(h, "mojo::PendingAssociatedRemote<Listener> l;"));

  int depth = 0;
  for (char c : h) {
    if (c == '{') ++depth;
    if (c == '}') --depth;
  }
  EXPECT_EQ(depth, 0);
}

// (v15) Cross-namespace import: a TypeSpec/DefaultValue with a non-empty
// owner_namespace (as module_loader.cc/parser.cc would produce for a
// reference to something declared in another file) must get qualified at
// every emission site -- built by hand here rather than via ParseSrc, since
// a single ParseSrc call has no cross-file references to produce.

TEST(generator_qualifies_cross_namespace_struct_field) {
  Module m;
  m.name = "logging";
  StructDecl outer;
  outer.name = "Outer";
  StructField f;
  f.type = TypeSpec{TypeKind::kStructRef, "Shared", nullptr, nullptr, false,
                     "common"};
  f.name = "s";
  outer.decl_index = 0;
  outer.fields.push_back(f);
  m.structs.push_back(outer);

  std::string h = GenerateCppHeader(m, "G_H_", "logging", "logging.voodoom");
  EXPECT(Contains(h, "common::Shared s{};"));
  EXPECT(Contains(h, "common::WriteShared("));
  EXPECT(Contains(h, "common::ReadShared("));
}

TEST(generator_qualifies_cross_namespace_enum_field) {
  Module m;
  m.name = "logging";
  StructDecl outer;
  outer.name = "Outer";
  StructField f;
  f.type = TypeSpec{TypeKind::kEnumRef, "Status", nullptr, nullptr, false,
                     "common"};
  f.name = "s";
  outer.decl_index = 0;
  outer.fields.push_back(f);
  m.structs.push_back(outer);

  std::string h = GenerateCppHeader(m, "G_H_", "logging", "logging.voodoom");
  EXPECT(Contains(h, "common::Status s{};"));
  EXPECT(Contains(h, "static_cast<common::Status>("));
  EXPECT(Contains(h, "common::IsKnownStatusAsOf("));
}

TEST(generator_qualifies_cross_namespace_pending_remote_param) {
  Module m;
  m.name = "logging";
  Interface iface;
  iface.name = "Foo";
  Method meth;
  meth.name = "M";
  Param p;
  p.type = TypeSpec{TypeKind::kPendingRemote, "CommonIface", nullptr, nullptr,
                     false, "common"};
  p.name = "p";
  meth.params.push_back(p);
  iface.methods.push_back(meth);
  m.interfaces.push_back(iface);

  std::string h = GenerateCppHeader(m, "G_H_", "logging", "logging.voodoom");
  EXPECT(Contains(h, "mojo::PendingRemote<common::CommonIface> p"));
}

TEST(generator_qualifies_cross_namespace_field_default) {
  Module m;
  m.name = "logging";
  StructDecl outer;
  outer.name = "Outer";
  StructField f;
  f.type = TypeSpec{TypeKind::kInt32, "", nullptr, nullptr, false};
  f.name = "x";
  f.default_value.has_value = true;
  f.default_value.named_expr = "kMax";
  f.default_value.named_expr_owner_namespace = "common";
  outer.decl_index = 0;
  outer.fields.push_back(f);
  m.structs.push_back(outer);

  std::string h = GenerateCppHeader(m, "G_H_", "logging", "logging.voodoom");
  EXPECT(Contains(h, "int32_t x{common::kMax};"));
}

TEST(generator_emits_imported_headers_includes) {
  Module m;
  m.name = "logging";
  std::string h = GenerateCppHeader(m, "G_H_", "logging", "logging.voodoom",
                                     {"types_gen.h", "other_gen.h"});
  EXPECT(Contains(h, "#include \"types_gen.h\""));
  EXPECT(Contains(h, "#include \"other_gen.h\""));
}

TEST(generator_no_imported_headers_means_no_extra_includes) {
  Module m = ParseSrc("module echo;\ninterface Foo {};");
  std::string h = GenerateCppHeader(m, "G_H_", "echo", "echo.voodoom");
  EXPECT(!Contains(h, "_gen.h"));
}

// (v16) Nested enum -- a plain, name-mangled, namespace-scope type
// (Foo_Status, never a true C++ nested type Foo::Status -- see
// cpp_generator.cc's MangledNestedEnumName for why), emitted in the same
// early, ordering-free pass top-level enums already get, so referencing it
// never depends on whether its declaring interface/struct happens to be
// textually emitted before or after whatever references it.

TEST(generator_js_method_table) {
  Module m = ParseSrc(
      "module echo;\ninterface Echo { EchoString(string s) => (string r); };\n");
  std::string js = GenerateJsHeader(m, "echo", "echo.voodoom");
  EXPECT(Contains(js, "echoString"));
  EXPECT(Contains(js, "EchoString"));
  EXPECT(Contains(js, "kEchoJsMethods"));
  EXPECT(Contains(js, "kEchoJsName"));
  EXPECT(Contains(js, "kJsInterfaceNames"));
}

TEST(generator_gn_source_set) {
  std::string gn = voodoom::GenerateGnBuild("echo", "echo_interface_gen.h");
  EXPECT(Contains(gn, "source_set(\"echo\")"));
  EXPECT(Contains(gn, "echo_interface_gen.h"));
}

TEST(generator_nested_enum_in_interface_is_mangled) {
  Module m = ParseSrc(
      "interface Foo {\n"
      "  enum Status { OK, ERROR };\n"
      "  M(Status s);\n"
      "};");
  std::string h = GenerateCppHeader(m, "G_H_", "ns", "x.voodoom");
  EXPECT(Contains(h, "enum class Foo_Status : int32_t {"));
  EXPECT(Contains(h, "inline bool IsKnownFoo_Status(int32_t wvc_value) {"));
  EXPECT(Contains(h, "using Status = Foo_Status;"));
  EXPECT(Contains(h, "virtual void M(Foo_Status s) = 0;"));
  // Nested enum class lives at namespace scope; the using-alias inside
  // class Foo is how callers write Foo::Status (not a true nested enum).
  EXPECT(!Contains(h, "enum class Status"));
  EXPECT(h.find("enum class Foo_Status") < h.find("class Foo {"));
  EXPECT(h.find("class Foo {") < h.find("using Status = Foo_Status;"));
}

TEST(generator_nested_enum_in_struct_is_mangled) {
  Module m = ParseSrc(
      "struct Foo {\n"
      "  enum Status { OK, ERROR };\n"
      "  Status s;\n"
      "};");
  std::string h = GenerateCppHeader(m, "G_H_", "ns", "x.voodoom");
  EXPECT(Contains(h, "enum class Foo_Status : int32_t {"));
  EXPECT(Contains(h, "using Status = Foo_Status;"));
  EXPECT(Contains(h, "Foo_Status s{};"));
  EXPECT(h.find("enum class Foo_Status") < h.find("struct Foo {"));
}

TEST(generator_qualified_nested_enum_cross_container_reference) {
  // Bar (a struct) references Foo's (an interface's) nested enum, via
  // the qualified `Foo.Status` .voodoom syntax -- structs are always
  // emitted before interfaces (see GenerateCppHeader), so this is exactly
  // the ordering case the mangled-name design exists for.
  Module m = ParseSrc(
      "interface Foo { enum Status { OK, ERROR }; };\n"
      "struct Bar { Foo.Status s; };");
  std::string h = GenerateCppHeader(m, "G_H_", "ns", "x.voodoom");
  EXPECT(Contains(h, "Foo_Status s{};"));
  EXPECT(Contains(h, "IsKnownFoo_Status("));
  EXPECT(Contains(h, "static_cast<Foo_Status>("));
  // The enum definition must precede Bar's struct body in the output.
  EXPECT(h.find("enum class Foo_Status") < h.find("struct Bar {"));
}

// --- real-mojom-parity phase 4: `handle` type ---

TEST(generator_bare_handle_param_type) {
  Module m = ParseSrc("interface Foo { M(handle h); };");
  std::string h = GenerateCppHeader(m, "G_H_", "ns", "x.voodoom");
  EXPECT(Contains(h, "mojo::ScopedHandle h"));
}

TEST(generator_handle_message_pipe_param_type) {
  Module m = ParseSrc("interface Foo { M(handle<message_pipe> h); };");
  std::string h = GenerateCppHeader(m, "G_H_", "ns", "x.voodoom");
  EXPECT(Contains(h, "mojo::ScopedMessagePipeHandle h"));
}

TEST(generator_handle_data_pipe_consumer_param_type) {
  Module m = ParseSrc("interface Foo { M(handle<data_pipe_consumer> h); };");
  std::string h = GenerateCppHeader(m, "G_H_", "ns", "x.voodoom");
  EXPECT(Contains(h, "mojo::ScopedDataPipeConsumerHandle h"));
}

TEST(generator_handle_data_pipe_producer_param_type) {
  Module m = ParseSrc("interface Foo { M(handle<data_pipe_producer> h); };");
  std::string h = GenerateCppHeader(m, "G_H_", "ns", "x.voodoom");
  EXPECT(Contains(h, "mojo::ScopedDataPipeProducerHandle h"));
}

TEST(generator_handle_shared_buffer_param_type) {
  Module m = ParseSrc("interface Foo { M(handle<shared_buffer> h); };");
  std::string h = GenerateCppHeader(m, "G_H_", "ns", "x.voodoom");
  EXPECT(Contains(h, "mojo::ScopedSharedBufferHandle h"));
}

TEST(generator_handle_platform_param_type) {
  Module m = ParseSrc("interface Foo { M(handle<platform> h); };");
  std::string h = GenerateCppHeader(m, "G_H_", "ns", "x.voodoom");
  EXPECT(Contains(h, "mojo::PlatformHandle h"));
}

TEST(generator_emits_handle_attach_on_write) {
  Module m = ParseSrc("interface Foo { M(handle<message_pipe> h); };");
  std::string h = GenerateCppHeader(m, "G_H_", "ns", "x.voodoom");
  EXPECT(Contains(h, "message.AttachHandle(mojo::ScopedHandle(h.release()));"));
  EXPECT(Contains(h, "message->TakeHandles();"));
}

TEST(generator_handle_never_wrapped_in_optional) {
  // Nullable handles use the type's own is_valid()-based "absent" state
  // (matching pending_remote/receiver), never std::optional.
  Module m = ParseSrc("interface Foo { M(handle<message_pipe>? h); };");
  std::string h = GenerateCppHeader(m, "G_H_", "ns", "x.voodoom");
  EXPECT(!Contains(h, "std::optional<mojo::ScopedMessagePipeHandle>"));
  EXPECT(Contains(h, "mojo::ScopedMessagePipeHandle h) override {"));
}

TEST(generator_nullable_handle_gets_presence_flag_framing) {
  Module m = ParseSrc("interface Foo { M(handle<message_pipe>? h); };");
  std::string h = GenerateCppHeader(m, "G_H_", "ns", "x.voodoom");
  EXPECT(Contains(h, "h_present"));
  EXPECT(Contains(h, "if (h_present) {"));
}

// --- real-mojom-parity phase 5: fixed-size array<T, N> ---

TEST(generator_fixed_size_array_field_type) {
  Module m = ParseSrc("struct Entry { array<int32, 3> nums; };");
  std::string h = GenerateCppHeader(m, "G_H_", "ns", "x.voodoom");
  EXPECT(Contains(h, "std::array<int32_t, 3> nums{};"));
}

TEST(generator_ordinary_array_field_type_unchanged) {
  Module m = ParseSrc("struct Entry { array<int32> nums; };");
  std::string h = GenerateCppHeader(m, "G_H_", "ns", "x.voodoom");
  EXPECT(Contains(h, "v8::internal::CageVector<int32_t> nums{};"));
}

TEST(generator_fixed_size_array_write_has_no_length_prefix) {
  Module m = ParseSrc("struct Entry { array<int32, 3> nums; };");
  std::string h = GenerateCppHeader(m, "G_H_", "ns", "x.voodoom");
  EXPECT(!Contains(h, "static_cast<uint32_t>(value.nums.size())"));
  EXPECT(Contains(h, "for (const auto& wvc_e0 : value.nums) {"));
}

TEST(generator_fixed_size_array_read_loops_over_constant) {
  Module m = ParseSrc("struct Entry { array<int32, 3> nums; };");
  std::string h = GenerateCppHeader(m, "G_H_", "ns", "x.voodoom");
  EXPECT(Contains(h, "wvc_i0 < 3u"));
  EXPECT(!Contains(h, "(out->nums).resize("));
}

TEST(generator_nullable_fixed_size_array_wrapped_in_optional) {
  Module m = ParseSrc("struct Entry { array<int32, 3>? nums; };");
  std::string h = GenerateCppHeader(m, "G_H_", "ns", "x.voodoom");
  EXPECT(Contains(h, "std::optional<std::array<int32_t, 3>> nums{};"));
}

TEST(generator_fixed_size_array_param_passed_by_const_ref) {
  Module m = ParseSrc("interface Foo { M(array<int32, 4> vals); };");
  std::string h = GenerateCppHeader(m, "G_H_", "ns", "x.voodoom");
  EXPECT(Contains(h, "const std::array<int32_t, 4>& vals"));
}

// --- real-mojom-parity phase 6: `result<T, E>` response type ---

TEST(generator_result_response_emits_synthesized_union_class) {
  Module m = ParseSrc(
      "struct NotFound { string message; };\n"
      "interface Store { Get(string key) => result<string, NotFound>; };");
  std::string h = GenerateCppHeader(m, "G_H_", "ns", "x.voodoom");
  EXPECT(Contains(h, "class Store_GetResult {"));
  EXPECT(Contains(h, "value = 0,"));
  EXPECT(Contains(h, "error = 1,"));
  EXPECT(Contains(h, "void set_value(std::string value) {"));
  EXPECT(Contains(h, "void set_error(NotFound value) {"));
  EXPECT(Contains(h, "inline void WriteStore_GetResult("));
  EXPECT(Contains(h, "inline bool ReadStore_GetResult("));
  // Types in complete-type order, then Write/Read, then the interface
  // that calls them. WriteX is a second pass (needs every struct/union
  // complete so it can iterate CageMap/CageVector of those types).
  EXPECT(h.find("struct NotFound {") < h.find("class Store_GetResult {"));
  EXPECT(h.find("class Store_GetResult {") < h.find("inline void WriteNotFound("));
  EXPECT(h.find("inline void WriteNotFound(") < h.find("class Store {"));
  EXPECT(h.find("inline void WriteStore_GetResult(") < h.find("class Store {"));
  EXPECT(h.find("class Store_GetResult {") < h.find("class Store {"));
}

TEST(generator_result_response_callback_takes_base_expected) {
  Module m = ParseSrc("interface Store { Get() => result<int32, string>; };");
  std::string h = GenerateCppHeader(m, "G_H_", "ns", "x.voodoom");
  EXPECT(Contains(h,
                   "virtual void Get(base::OnceCallback<void(base::expected<"
                   "int32_t, std::string>)> callback) = 0;"));
}

TEST(generator_result_response_stub_writes_result_on_response) {
  Module m = ParseSrc("interface Store { Get() => result<int32, string>; };");
  std::string h = GenerateCppHeader(m, "G_H_", "ns", "x.voodoom");
  EXPECT(Contains(h,
                   "[router, id, request_id](base::expected<int32_t, "
                   "std::string> result) {"));
  EXPECT(Contains(h, "WriteStore_GetResult(response, wvc_wire);"));
}

TEST(generator_result_response_proxy_reads_result_from_response) {
  Module m = ParseSrc("interface Store { Get() => result<int32, string>; };");
  std::string h = GenerateCppHeader(m, "G_H_", "ns", "x.voodoom");
  EXPECT(Contains(h, "if (!ReadStore_GetResult(*response, &offset, &(result))) return false;"));
}

// --- real-mojom-parity phase 7: `feature NAME { ... };` declarations ---

TEST(generator_feature_consts_are_mangled_namespace_scope) {
  Module m = ParseSrc(
      "feature MyFeature {\n"
      "  const bool kEnabled = true;\n"
      "  const int32 kMax = 3;\n"
      "};");
  std::string h = GenerateCppHeader(m, "G_H_", "ns", "x.voodoom");
  EXPECT(Contains(h, "inline constexpr bool MyFeature_kEnabled = true;"));
  EXPECT(Contains(h, "inline constexpr int32_t MyFeature_kMax = 3;"));
  // A `feature` block itself generates no C++ type of its own -- only its
  // consts appear at all.
  EXPECT(!Contains(h, "class MyFeature"));
  EXPECT(!Contains(h, "struct MyFeature"));
}

TEST(generator_struct_field_default_resolves_feature_const) {
  Module m = ParseSrc(
      "feature MyFeature { const int32 kMax = 3; };\n"
      "struct Config { int32 retries = MyFeature.kMax; };");
  std::string h = GenerateCppHeader(m, "G_H_", "ns", "x.voodoom");
  EXPECT(Contains(h, "int32_t retries{MyFeature_kMax};"));
}

TEST(generator_feature_const_emitted_before_referencing_struct) {
  Module m = ParseSrc(
      "feature MyFeature { const int32 kMax = 3; };\n"
      "struct Config { int32 retries = MyFeature.kMax; };");
  std::string h = GenerateCppHeader(m, "G_H_", "ns", "x.voodoom");
  EXPECT(h.find("MyFeature_kMax = 3;") < h.find("struct Config {"));
}

// --- real-mojom-parity phase 9 (v23) ---

TEST(generator_handle_bearing_struct_write_takes_nonconst_ref) {
  Module m = ParseSrc(
      "interface Watcher {};\n"
      "struct Bundle { pending_remote<Watcher> w; int32 n; };");
  std::string h = GenerateCppHeader(m, "G_H_", "ns", "x.voodoom");
  EXPECT(Contains(h, "inline void WriteBundle(mojo::Message& message, Bundle& value)"));
  EXPECT(Contains(h, "PassPipe()"));
  EXPECT(Contains(h, "ReadBundle(const mojo::Message& message, size_t* offset, Bundle* out,"));
  EXPECT(Contains(h, "std::vector<mojo::ScopedHandle>* wvc_handles"));
}

TEST(generator_handle_free_struct_write_stays_const_ref) {
  Module m = ParseSrc("struct Entry { int32 x; };");
  std::string h = GenerateCppHeader(m, "G_H_", "ns", "x.voodoom");
  EXPECT(Contains(h, "inline void WriteEntry(mojo::Message& message, const Entry& value)"));
  EXPECT(Contains(h, "inline bool ReadEntry(const mojo::Message& message, size_t* offset, Entry* out)"));
}

TEST(generator_hash_map_emits_unordered_map) {
  Module m = ParseSrc("struct E { hash_map<string, int32> m; };");
  std::string h = GenerateCppHeader(m, "G_H_", "ns", "x.voodoom");
  EXPECT(Contains(h, "v8::internal::CageMap<std::string, int32_t> m{}"));
}

TEST(generator_union_default_field_used_on_unknown_tag) {
  Module m = ParseSrc(
      "[Extensible] union Status { [Default] int32 code; string msg; };");
  std::string h = GenerateCppHeader(m, "G_H_", "ns", "x.voodoom");
  EXPECT(Contains(h, "out->set_code(int32_t{});"));
}

TEST(generator_empty_native_struct) {
  Module m = ParseSrc("[Native] struct Foo;");
  std::string h = GenerateCppHeader(m, "G_H_", "ns", "x.voodoom");
  EXPECT(Contains(h, "struct Foo {"));
  EXPECT(Contains(h, "inline void WriteFoo("));
}

TEST(generator_nested_struct_in_interface_mangles_and_aliases) {
  Module m = ParseSrc(
      "interface Store {\n"
      "  struct Params { int32 n; };\n"
      "  Get(Params p) => (Params out);\n"
      "};");
  std::string h = GenerateCppHeader(m, "G_H_", "ns", "x.voodoom");
  EXPECT(Contains(h, "struct Store_Params;"));
  EXPECT(Contains(h, "struct Store_Params {"));
  EXPECT(Contains(h, "inline void WriteStore_Params("));
  EXPECT(Contains(h, "using Params = Store_Params;"));
  EXPECT(h.find("struct Store_Params {") < h.find("class Store {"));
}

TEST(generator_typemap_emits_using_and_native_traits) {
  Module m = ParseSrc("[Native] struct Time;");
  GeneratorOptions opt;
  opt.typemaps["Time"] = "::base::Time";
  opt.extra_includes.push_back("base/time/time.h");
  std::string h = GenerateCppHeader(m, "G_H_", "ns", "x.voodoom", {}, opt);
  EXPECT(Contains(h, "#include \"base/time/time.h\""));
  EXPECT(Contains(h, "using Time = ::base::Time;"));
  EXPECT(Contains(h, "mojo::NativeTraits<Time>::Write(message, value)"));
  EXPECT(Contains(h, "mojo::NativeTraits<Time>::Read(message, offset, out)"));
  EXPECT(!Contains(h, "struct Time {"));
}

TEST(generator_min_version_reorders_wire_fields) {
  Module m = ParseSrc(
      "struct Widget {\n"
      "  [MinVersion=1] string color;\n"
      "  string name;\n"
      "};");
  std::string h = GenerateCppHeader(m, "G_H_", "ns", "x.voodoom");
  size_t name_w = h.find("value.name");
  size_t color_w = h.find("value.color");
  EXPECT(name_w != std::string::npos);
  EXPECT(color_w != std::string::npos);
  EXPECT(name_w < color_w);
  size_t name_r = h.find("out->name");
  size_t color_r = h.find("out->color");
  EXPECT(name_r != std::string::npos);
  EXPECT(color_r != std::string::npos);
  EXPECT(name_r < color_r);
}

TEST(generator_enum_emits_isknown_as_of) {
  Module m = ParseSrc(
      "[Extensible] enum Color { kRed = 0, [MinVersion=1] kBlue = 1 };");
  std::string h = GenerateCppHeader(m, "G_H_", "ns", "x.voodoom");
  EXPECT(Contains(h, "inline bool IsKnownColorAsOf(int32_t wvc_value, uint32_t wvc_version)"));
  EXPECT(Contains(h, "return wvc_version >= 1u;"));
}

TEST(generator_associated_shorthand_emits_pending_associated) {
  Module m = ParseSrc(
      "interface Listener {};\n"
      "interface Host { Set(associated Listener l); };");
  std::string h = GenerateCppHeader(m, "G_H_", "ns", "x.voodoom");
  EXPECT(Contains(h, "mojo::PendingAssociatedRemote<Listener>"));
}
