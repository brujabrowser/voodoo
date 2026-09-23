// Recursive-descent parser: tokens -> voodoom::Module. Grammar (subset of
// mojom's, see mojo/public/tools/mojom/mojom/parse/parser.py for the real
// one):
//
//   voodoom_file  := module_stmt? import_stmt* top_level_decl*
//   module_stmt   := 'module' NAME ('.' NAME)* ';'
//   import_stmt   := 'import' STRING ';'
//   top_level_decl:= interface_decl | struct_decl | union_decl | enum_decl
//                   | const_decl
//
//   interface_decl:= 'interface' NAME '{' (enum_decl | method_decl)* '}' ';'
//                   -- (v16) nested enum; see enum_decl and type_spec's
//                      qualified NAME form below
//   method_decl   := attribute_list? NAME '(' param_list? ')' ordinal?
//                    response? ';'
//   attribute_list:= '[' attribute (',' attribute)* ']'
//   attribute     := NAME ('=' '-'? NUMBER)?
//                    -- the grammar itself is general (any NAME, an
//                       optional numeric value), but each attribute's
//                       *legal use site* is special-cased, not general:
//                       'Sync' (no value) only on a method_decl (see
//                       README's "Sync methods"); 'MinVersion=N' (value
//                       required, N >= 0) only on a struct_field (see
//                       README's "Struct versioning"). Any other
//                       name/site/value-shape combination is a parse
//                       error, not a silent no-op.
//   ordinal       := '@' NUMBER
//   response      := '=>' '(' param_list? ')'
//   param_list    := param (',' param)*
//   param         := type_spec NAME
//
//   struct_decl   := 'struct' NAME '{' (enum_decl | struct_field)* '}' ';'
//                   -- (v16) nested enum, same as interface_decl above
//   struct_field  := attribute_list? type_spec NAME
//                    ('=' field_default)? ';'
//   field_default := 'true' | 'false' | '-'? NUMBER | STRING | NAME
//                   -- only legal when type_spec is a non-nullable
//                      bool/integer/string/enum; a bare NAME is either a
//                      previously-declared const of the field's exact
//                      type (bool/integer fields only) or -- always, for
//                      an enum field -- one of that field's own enum's
//                      declared value names (see ast.h's DefaultValue and
//                      parser.cc's ParseDefaultValue/ResolveConstDefault)
//
//   union_decl    := 'union' NAME '{' union_field* '}' ';'
//   union_field   := type_spec NAME ordinal? ';'
//
//   enum_decl     := 'enum' NAME '{' enum_value_list? '}' ';'
//   enum_value_list := enum_value (',' enum_value)* ','?
//   enum_value    := NAME ('=' '-'? NUMBER)?
//
//   const_decl    := 'const' scalar_type NAME '=' '-'? NUMBER ';'
//
//   type_spec     := nullable_type '?'?
//   nullable_type := 'string' | scalar
//                   | 'array' '<' type_spec '>'
//                   | 'map' '<' map_key_type ',' type_spec '>'
//                   | pending_kind '<' NAME '>'
//                   | NAME   -- a previously-declared struct, union, or enum
//                   | NAME '.' NAME   -- (v16) a qualified reference to a
//                      nested enum: the first NAME names the declaring
//                      interface/struct, the second its nested enum. See
//                      ContainerOf/ContainerEnums in parser.cc; this can
//                      never collide with module_stmt's own dotted name
//                      (a completely different grammar position). A bare
//                      (unqualified) NAME also resolves to a nested enum
//                      declared anywhere in the file, not just from within
//                      its own declaring interface/struct -- a deliberate
//                      permissiveness beyond real mojom's actual scoping
//                      rule; see README's "Known simplifications".
//   map_key_type  := 'string' | scalar | NAME  -- NAME must name an enum
//   scalar        := 'bool' | 'int8' | 'uint8' | 'int16' | 'uint16'
//                   | 'int32' | 'uint32' | 'int64' | 'uint64'
//                   | 'float' | 'double'
//   pending_kind  := 'pending_remote' | 'pending_receiver'
//                   | 'pending_associated_remote' | 'pending_associated_receiver'
//
// The trailing '?' is only legal on 'string', 'array<...>', 'map<...>',
// and a struct/union NAME -- mojom's "reference" types, the only ones with
// a meaningful "absent" state. A '?' on a scalar, an enum NAME, or a
// pending_kind is a parse error (scalars/enums have no null representation
// in mojom either; pending_kind nullability is a documented v5
// simplification this compiler doesn't support yet -- see README). '?'
// applies to whatever type_spec it immediately follows, so it nests
// naturally: `array<string?>` is a non-nullable array of nullable strings,
// `array<string>?` is a nullable array of non-nullable strings.
//
// Ordinal rule (matches mojom): within one interface, either every method
// has an explicit @N or none do -- no mixing. Same rule for union fields'
// tags. Unordinaled methods/fields number sequentially from 0 in
// declaration order. Same rule for enum values: an omitted value continues
// from the previous value + 1 (0 if first).
//
// `[Sync]` on a method requires a response (=> (...)) -- a parse error
// otherwise, since there'd be nothing for a blocking call to wait for. It
// changes nothing about the interface's pure-virtual method or Stub_
// (impl_'s side is identical whether or not the caller happened to block);
// it only makes Proxy_ emit an *additional* overload that blocks for the
// response instead of taking a callback -- see cpp_generator.cc's
// EmitInterface and README's "Sync methods".
//
// `[MinVersion=N]` on a struct field means that field was added in the
// struct's version N (0 -- the default for an unmarked field -- means
// "present since the struct's very first version"). A struct's own
// version is max(field.min_version) across all its fields (0 if none are
// marked). Fields must be declared in non-decreasing MinVersion order --
// a parse error otherwise -- because this compiler emits struct fields on
// the wire in non-decreasing min_version order (declaration order is the
// C++ member order; a reorder pass in cpp_generator.cc aligns the wire
// with version order, matching real mojom). See README's "Struct
// versioning" for the wire format.
//
// Struct/union fields are restricted to scalar/string/enum/struct/union/
// array/map thereof -- no pending_remote/receiver or pending_associated_*
// fields (those stay method-parameter-only). A by-value (or optional /
// array<T,N>) struct-or-union field needs a complete C++ type, so a cycle
// of those is a parse error. array<T> and map<K, T> may reference T while
// T is still incomplete (generated as WASMSafeSpace CageVector/CageMap);
// the generator forward-declares every struct/union and reorders emission
// by complete-type dependencies. A map's key type is further restricted to
// scalar/string/enum (never struct/union/array/map/pending -- matches
// mojom's own map-key rule). Interfaces and enums have no such ordering
// restriction: interfaces are all forward-declared before any full
// definition, and enums are self-contained, so a .voodoom file's
// interface/enum declarations may reference each other regardless of
// textual order.
//
// A dotted module_stmt (`module a.b.c;`) produces nested C++ namespaces
// (`namespace a { namespace b { namespace c { ... } } }`) -- see
// cpp_generator.cc's SplitNamespace/GenerateCppHeader. FileHeader and
// Module both just store the dotted spelling verbatim ("a.b.c"); nothing
// about parsing or type resolution cares how many segments it has.
//
// import_stmt must come right after module_stmt (if any) and before every
// other top-level decl -- a real parse-time restriction, not just a style
// preference, since it keeps "what's already visible when I start reading
// this file's own decls" unambiguous. A single Parse() call never resolves
// imports itself (it has no filesystem access) -- it just records them in
// Module::imports; see module_loader.h for a full multi-file compile,
// which uses the lower-level Parser::ParseHeader()/ParseBody() split below
// to seed each file's type resolution with its already-loaded imports.
#ifndef WVC_SRC_PARSER_H_
#define WVC_SRC_PARSER_H_

#include "ast.h"
#include "lexer.h"

#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace voodoom {

class ParseError : public std::runtime_error {
 public:
  ParseError(const std::string& msg, int line)
      : std::runtime_error(msg + " (line " + std::to_string(line) + ")"),
        line(line) {}
  int line;
};

// module_stmt? import_stmt*, nothing more -- what a caller needs to know
// before it can resolve this file's imports and hand back a prelude Module
// for ParseBody().
struct FileHeader {
  std::string module_name;
  std::vector<ImportDecl> imports;
};

// (v15) Everything already known to be visible in the file about to be
// parsed, built by module_loader.cc from every file loaded earlier in the
// import graph (see module_loader.h's LoadModuleGraph doc comment --
// deliberately every earlier-loaded file, not just this file's own direct
// imports). `module` is the flat merge of their struct/union/enum/
// interface/const declarations (same accumulation ParseBody has always
// used to seed name resolution and struct/union embedding order).
// `owner_by_name` is new in v15: for every name in `module`, the raw
// `module` statement (dotted or "::"-separated, verbatim) of the specific
// file that actually declared it -- ParseTypeSpecInner/ResolveConstDefault/
// ParseDefaultValue use this to set TypeSpec::owner_namespace and
// DefaultValue::named_expr_owner_namespace so cpp_generator.cc knows which
// cross-file references need a namespace qualifier and which don't (a name
// with no entry here, but present in `module`, was declared in the *same*
// file as some other prelude entry sharing... no such case arises in
// practice: every name in `module` has a corresponding owner_by_name
// entry, since `module` only ever contains other files' declarations, never
// the file about to be parsed by this call).
struct Prelude {
  Module module;
  std::unordered_map<std::string, std::string> owner_by_name;
};

// Stateful across ParseHeader() then ParseBody(): both operate on the same
// token stream and cursor, in that order. Move-only isn't required (no
// copies expected) but not copyable either since it holds a reference to
// its token vector.
class Parser {
 public:
  explicit Parser(const std::vector<Token>& tokens);
  ~Parser();
  Parser(const Parser&) = delete;
  Parser& operator=(const Parser&) = delete;
  Parser(Parser&&) noexcept;
  Parser& operator=(Parser&&) noexcept;

  FileHeader ParseHeader();

  // `prelude`, if non-null, is everything already known to be visible in
  // this file -- normally the result of resolving this file's own imports
  // (see module_loader.h and Prelude's own comment above). Its struct/
  // union/enum/interface names become resolvable types here (imported
  // structs/unions are already complete -- they're in another file's
  // generated header, #included first), its enum values and consts become
  // resolvable field defaults, and every one of those gets an
  // owner_namespace/named_expr_owner_namespace stamped on wherever this
  // file references it (empty for anything declared in this file itself).
  // Must be called exactly once, after ParseHeader().
  Module ParseBody(const Prelude* prelude);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// Convenience wrapper for single-file, import-free parsing (what every
// existing caller of this function wants): ParseHeader() + ParseBody(),
// with an empty prelude. If the file has import statements, they're parsed
// into the returned Module::imports but never resolved -- referencing a
// type that only an import would provide fails as "unknown type", same as
// any other undeclared name.
Module Parse(const std::vector<Token>& tokens);

// `[EnableIf]` / `[EnableIfNot]` evaluation against `--enable-if=FLAG`.
// An inactive spec (no EnableIf attributes) is always kept. kAny is a
// pipe-list or a single identifier; kAll is an ampersand-list.
bool IsDeclEnabled(const EnableIf& spec,
                   const std::unordered_set<std::string>& enabled);

// Drops decls/fields/methods whose EnableIf does not match `enabled`.
// Recomputes struct/interface/union version as max remaining min_version.
// LoadModuleGraph runs this after each file's ParseBody so voodoomc
// actually omits gated Chromium decls (file_path.mojom's two `path`
// fields). Parse() itself does not filter -- tests inspect the raw AST.
void ApplyEnableIf(Module& module,
                   const std::unordered_set<std::string>& enabled);

}  // namespace voodoom

#endif  // WVC_SRC_PARSER_H_
