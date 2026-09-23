// The IR a .voodoom file parses into. v2: adds enum, struct, array<T>, and
// module/interface-level const on top of v1's interface/method/param
// surface. v3: adds union and map<K, V>. v4: adds import -- see
// src/module_loader.h for how a whole import graph gets merged into one
// Module. v5: adds nullable ('?') types and dotted `module a.b.c;` names.
// v6: adds struct field default values (bool/integer/string literals).
// v7: adds the `[Sync]` method attribute. v8: adds enum-value and
// const-name field defaults. v9: adds struct field versioning
// (`[MinVersion=N]`) -- see StructField::min_version/StructDecl::version
// and cpp_generator.cc's EmitStruct. v10: adds method `[MinVersion=N]`
// (metadata -> Interface::version) and interface `[Extensible]` (real
// receiver-tolerance behavior) -- see Method::min_version/
// Interface::version/Interface::is_extensible and cpp_generator.cc's
// EmitInterface. v11: adds union `[MinVersion=N]`/`[Extensible]` -- see
// UnionField::min_version/UnionDecl::version/UnionDecl::is_extensible and
// cpp_generator.cc's EmitUnion. v12: adds the interface-level
// QueryVersion/RequireVersion control-message protocol (no AST change --
// generated unconditionally, not schema-driven) and the router-level
// associated-endpoint peer-closed notification (WASMCadidumBindings-only,
// no AST or codegen change at all). v13: adds enum `[Extensible]` -- see
// EnumDecl::is_extensible and cpp_generator.cc's EmitEnum/EmitReadInto.
// v14: adds nullable pending_remote/receiver/associated_* method
// parameters -- see TypeSpec::nullable's comment and
// cpp_generator.cc's EmitTopWritePrelude/EmitTopWritePayload/
// EmitTopReadParam. v15: real per-file cross-namespace codegen -- import no
// longer flattens the whole graph into one Module/one namespace (see
// module_loader.h). TypeSpec::owner_namespace and
// DefaultValue::named_expr_owner_namespace record which *other* file's
// namespace a cross-file reference needs qualifying with (empty means
// "declared in the file being generated right now"); ImportDecl::resolved_path
// is the resolved path module_loader.cc found for that import, used to
// derive the #include this file's generated header emits for it. See
// cpp_generator.cc's NamespacePrefix and README's "Imports". v16: adds
// nested enum -- an `enum` may now be declared inside an `interface` or
// `struct` body (Interface::enums/StructDecl::enums), generating a C++
// enum + its IsKnownX helper as a plain, name-mangled, namespace-scope
// declaration (`Foo_Status`, never a true nested `Foo::Status` -- see
// cpp_generator.cc's MangledNestedName for why), referenced from elsewhere
// via a qualified `Container.EnumName` in .voodoom source.
// TypeSpec::owner_container/DefaultValue::named_expr_owner_container carry
// which container (if any) a referenced enum is nested inside, composing
// with owner_namespace/named_expr_owner_namespace the same way (a nested
// enum can also be cross-file) -- see cpp_generator.cc's QualifierPrefix
// and parser.cc's ContainerOf. v17 (real-mojom-parity phase 1/2, working
// toward accepting real .mojom files -- see the project's own roadmap
// plan, not tracked in this file): phase 1 widened the lexer's number/
// string literal grammar (hex, float/double, octal rejection, decimal/hex
// string escapes -- no AST change, see lexer.h/.cc); phase 2 adds nested
// `const` (Interface::consts/StructDecl::consts, same mangled-name
// treatment as v16's nested enum -- see ConstDecl::decl_index's comment
// for why emission order needs special care here that enums didn't) and
// widens both ConstDecl and DefaultValue: a const's type is no longer
// integer-only (ConstDecl::value now reuses DefaultValue wholesale, adding
// DefaultValue::float_value alongside the existing bool/int/string/
// named-reference fields), and float/double are now legal for both consts
// and struct field defaults. v18 (real-mojom-parity phase 4): adds the
// `handle` type -- TypeKind::kHandle/kHandleMessagePipe/
// kHandleDataPipeConsumer/kHandleDataPipeProducer/kHandleSharedBuffer
// (bare `handle` and each `handle<subtype>` this WASM-hosted stack has a
// C++ type for -- `handle<platform>` is deliberately unsupported, see the
// enum's own comment). Method-parameter-only, same as the pending_* kinds
// (parser.cc's CheckNotHandleBearing), and nullable the same
// non-`std::optional`, is_valid()-based way pending_* already is (see
// cpp_generator.cc's CppValueType/EmitTopWritePrelude/
// EmitTopWritePayload/EmitTopReadParam) -- no new TypeSpec field needed,
// since (unlike pending_*) a handle isn't bound to a specific interface
// name. v19 (real-mojom-parity phase 5): adds fixed-size arrays --
// `array<T, N>`, real mojom's compile-time-known-length array form, no
// length prefix on the wire (see TypeSpec::fixed_array_size's own
// comment). v20 (real-mojom-parity phase 6): adds `result<T, E>`
// responses -- no AST change at all, since parser.cc's MakeResultUnion
// desugars one entirely into a plain, synthesized UnionDecl and a
// single kUnionRef response Param, both of which this file's existing
// Method/UnionDecl shapes already represent perfectly (see README's
// "`result<T, E>` responses"). v21 (real-mojom-parity phase 7): adds
// `feature NAME { ... };` top-level declarations -- Module::features, a
// new FeatureDecl (see its own comment) holding only the `const`s
// declared inside it, the one thing real mojom's own FeatureBody grammar
// allows there. v22 (real-mojom-parity phase 8, real-file corpus): adds
// DefaultValue::float_special for real mojom's `float.INFINITY`/
// `double.NAN`-style special float constant literals, discovered via a
// real vendored .mojom file -- see its own comment. v23 (real-mojom-parity
// phase 9): Chromium method ordinal position (`Name@N(...)` plus the
// existing `Name(...)@N`), struct-field/parameter `@N` and parameter
// `[MinVersion]`, `hash_map` as a `map` alias, empty native struct/enum
// (`struct Foo;`), enum-value identifier aliases and `[MinVersion]`/
// `[Default]`, `[Default]` on an extensible union field, handle-bearing
// struct/union/array/map values, and attribute lists on `module`/`import`.
#ifndef WVC_SRC_AST_H_
#define WVC_SRC_AST_H_

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace voodoom {

enum class TypeKind {
  kBool,
  kInt8,
  kUint8,
  kInt16,
  kUint16,
  kInt32,
  kUint32,
  kInt64,
  kUint64,
  kFloat,
  kDouble,
  kString,
  kEnumRef,    // name = the enum's name
  kStructRef,  // name = the struct's name
  kUnionRef,   // name = the union's name
  kArray,      // element = the element TypeSpec
  kMap,        // key = the key TypeSpec, element = the value TypeSpec
  kPendingRemote,
  kPendingReceiver,
  kPendingAssociatedRemote,
  kPendingAssociatedReceiver,  // name = interface name, for all four above
  // (real-mojom-parity phase 4) A raw handle -- `name` unused (unlike the
  // pending_* kinds above, a handle is never bound to a specific
  // interface). v23: legal as a struct/union field, array element, or
  // map value (same as pending_*), not only a top-level method param.
  // Map keys still reject every handle kind (see CheckValidMapKeyType).
  kHandle,                   // generic `handle`
  kHandleMessagePipe,        // `handle<message_pipe>`
  kHandleDataPipeConsumer,   // `handle<data_pipe_consumer>`
  kHandleDataPipeProducer,   // `handle<data_pipe_producer>`
  kHandleSharedBuffer,       // `handle<shared_buffer>`
  kHandlePlatform,           // `handle<platform>` -- mojo::PlatformHandle
                             // (CadidumKernel sys:: wrapping Thunker's c::
                             // MojoPlatformHandle)
};

struct TypeSpec {
  TypeKind kind = TypeKind::kBool;
  std::string name;
  std::shared_ptr<TypeSpec> element;  // kArray's element, or kMap's value
  std::shared_ptr<TypeSpec> key;      // only meaningful for kMap
  // A trailing '?' -- only legal on kString/kArray/kMap/kStructRef/
  // kUnionRef, and (v14) the four pending_* kinds -- mojom's "reference"
  // types, the ones with a meaningful absent state (enforced at parse
  // time -- see parser.cc's CheckNullableAllowed). Scalars and enums can
  // never be nullable here, matching mojom. A nullable pending_* type is
  // represented as that exact same C++ type (PendingRemote<T>::is_valid()
  // etc. already distinguish present/absent -- see cpp_generator.cc's
  // CppValueType), unlike the other nullable kinds, which get wrapped in
  // std::optional<...>.
  bool nullable = false;
  // (v15) Only meaningful for kEnumRef/kStructRef/kUnionRef and the four
  // pending_* kinds (the kinds whose `name` names a struct/union/enum/
  // interface declared *somewhere*, as opposed to a builtin). Empty means
  // "declared in the file being generated right now" -- no qualification
  // needed. Otherwise, the raw `module` statement (dotted or already
  // "::"-separated) of the *other* file that declared it, set by the
  // parser's ParseTypeSpecInner from Prelude::owner_by_name (see parser.cc's
  // SeedFromPrelude) -- cpp_generator.cc's NamespacePrefix converts this
  // into the "a::b::" prefix actually emitted in front of `name`.
  std::string owner_namespace;
  // (v16) For kEnumRef, the interface/struct this enum is nested inside
  // (see Interface::enums/StructDecl::enums); (v24) also for kStructRef/
  // kUnionRef nested inside an interface (Interface::structs/unions).
  // "" for a top-level type. Set by parser.cc's ParseTypeSpecInner (via
  // ContainerOf) whether the reference was a bare NAME (resolved from
  // anywhere in the file -- see ContainerOf's own comment on why this
  // compiler doesn't restrict that to the declaring container only) or the
  // qualified `Container.Name` form. Composes with owner_namespace:
  // cpp_generator.cc's QualifierPrefix emits owner_namespace's "a::b::"
  // prefix first, then the mangled "owner_container_" -- e.g.
  // `common::Foo_Status` / `common::Foo_Params` for a nested type that's
  // also cross-file.
  std::string owner_container;
  // (real-mojom-parity phase 5) Only meaningful for kArray. 0 means an
  // ordinary length-prefixed `array<T>`; a positive N means `array<T, N>`
  // -- real mojom's fixed-size array, a compile-time-known element count
  // with no length prefix on the wire (the reader already knows N from
  // the type itself) -- see cpp_generator.cc's CppValueType (std::array<T,
  // N> instead of CageVector<T>) and EmitWriteValue/EmitReadInto (N
  // back-to-back elements, no size field). Set by parser.cc's
  // ParseTypeSpecInner from the optional `, N` after the element type.
  int fixed_array_size = 0;
};

// Unique C++ / complete-type-graph name for a struct or union: the bare
// name if top-level, `Container_Name` if nested in an interface (v24 --
// same mangling nested enums already use). Two interfaces may each nest a
// `struct Params`; the embed key distinguishes them.
inline std::string EmbedDeclKey(const std::string& owner_container,
                                const std::string& name) {
  return owner_container.empty() ? name : owner_container + "_" + name;
}

// Struct/union names that must be a *complete* C++ type wherever `type`
// appears as a class member. array<T> (CageVector) and map<K, T>
// (CageMap) may hold an incomplete T -- those do not contribute. A
// by-value / optional / array<T,N> struct-or-union does. Used to (1)
// reject true by-value cycles at parse time and (2) reorder emission so
// complete-type dependencies are defined first (values.mojom: emit
// DictionaryValue/ListValue while Value is still a forward-decl, then
// Value).
inline void AppendCompleteTypeDeps(const TypeSpec& type,
                                   std::vector<std::string>* out) {
  if (type.kind == TypeKind::kMap) {
    return;
  }
  if (type.kind == TypeKind::kArray && type.fixed_array_size == 0) {
    return;
  }
  if (type.kind == TypeKind::kArray) {
    AppendCompleteTypeDeps(*type.element, out);
    return;
  }
  if (type.kind == TypeKind::kStructRef || type.kind == TypeKind::kUnionRef) {
    // A nullable struct/union is absent-or-present. A cycle that only
    // closes through `T?` (AIPageContentPopup) is legal mojom.
    if (type.nullable) return;
    // A type owned by another module is defined in that module's header.
    // Matching it against a same-named type in this file (struct Url
    // containing url.mojom.Url) is not a cycle in this file.
    if (!type.owner_namespace.empty()) return;
    out->push_back(EmbedDeclKey(type.owner_container, type.name));
  }
}

// `[EnableIf=flag]` / `[EnableIf=a|b]` (any) / `[EnableIf=a&b]` (all) /
// `[EnableIfNot=...]`. `ApplyEnableIf` (parser.h) drops a decl when the
// clause does not match `--enable-if=FLAG`. An inactive spec is always
// kept. Mutually exclusive fields (file_path.mojom's two `path`s) rely
// on this, not on silently ignoring the attribute.
struct EnableIfClause {
  enum class Mode { kNone, kAny, kAll };
  Mode mode = Mode::kNone;
  std::vector<std::string> flags;
};

struct EnableIf {
  EnableIfClause enable_if;
  EnableIfClause enable_if_not;
  bool active() const {
    return enable_if.mode != EnableIfClause::Mode::kNone ||
           enable_if_not.mode != EnableIfClause::Mode::kNone;
  }
};

struct Param {
  TypeSpec type;
  std::string name;
  // `[MinVersion=N]` on a parameter -- metadata only (drives nothing on
  // the wire here; params are still written/read in declaration order).
  // Parsed so real .mojom files that version a parameter don't fail.
  uint32_t min_version = 0;
  // `@N` after a parameter name -- accepted (real mojom grammar) and
  // ignored for layout; this compiler's params are declaration-ordered.
  uint32_t ordinal = 0;
  bool has_explicit_ordinal = false;
  EnableIf enable_if;
};

struct Method {
  std::string name;
  uint32_t ordinal = 0;  // assignment: explicit @N, else declaration index.
  bool has_explicit_ordinal = false;
  std::vector<Param> params;
  bool has_response = false;
  std::vector<Param> response_params;
  // `=> result<T, E>` -- wire is still the synthesized two-arm union in
  // response_params (value/error); the C++ API is base::expected<T, E>
  // (Bindings is the base:: rung). result_success / result_error are T
  // and E.
  bool is_result_response = false;
  TypeSpec result_success;
  TypeSpec result_error;
  // `[Sync]` (see parser.cc's ParseAttributeList) -- only legal on a
  // method with a response (parse error otherwise; a fire-and-forget
  // message has nothing to block for). Doesn't change Stub_/the impl side
  // at all -- only Proxy_ gets an additional blocking overload, on top of
  // the normal callback-taking one, built on WASMCadidumBindings'
  // Connector::SyncWaitFor. See cpp_generator.cc's EmitInterface and
  // README's "Sync methods".
  bool is_sync = false;
  // `[MinVersion=N]` -- this method was added in the interface's version
  // N (0, the default, means "present since the interface's very first
  // version"). Purely metadata in this compiler: it drives
  // Interface::version (max over all methods) and the generated
  // `kVersion` constant, but changes nothing about the wire format or
  // dispatch -- unlike a struct field's MinVersion, there's no per-call
  // header to gate on. What *does* change receiver behavior for a method
  // ordinal the receiver doesn't recognize is Interface::is_extensible,
  // below -- a separate mechanism. See README's "Interface versioning".
  uint32_t min_version = 0;
  EnableIf enable_if;
};

// A field's `= ...` default (StructField::default_value) -- also reused
// (real-mojom-parity phase 2) as ConstDecl::value, since a const's own
// value and a field's default are structurally the same thing: a literal
// or a reference to a previously-declared name. Legal on non-nullable
// bool/integer/float/double/string/enum fields -- see parser.cc's
// ParseDefaultValue -- so, when has_value is true, exactly one of
// bool_value/int_value/float_value/string_value is meaningful, picked by
// the field's own TypeSpec::kind (kBool / int8..uint64 / kFloat/kDouble /
// kString / kEnumRef respectively); has_value false means "no default,
// value-initialize as always" (StructField's own zero/empty state -- always
// true, unused, for a ConstDecl's own value).
//
// A bool/integer/float/double field's default may instead be a previously-
// declared const's NAME (`int32 x = kMax;`), and an enum field's default is
// always a NAME naming one of its own enum's values (`Status s = OK;`) --
// when the default came from a NAME either way, `named_expr` holds the
// exact C++ expression to emit for it ("kMax", "Status::OK") and takes
// priority over bool_value/int_value/float_value/string_value for codegen;
// those are still populated with the resolved value too (informational --
// e.g. so a test can check the resolved value without re-deriving it from
// named_expr).
//
// struct/union, array/map, and nullable fields still can't have a default
// in this compiler -- see README's "Known simplifications" (would need
// either a dedicated null-literal token or per-field-type nested-literal
// parsing, neither implemented).
struct DefaultValue {
  bool has_value = false;
  bool bool_value = false;
  int64_t int_value = 0;
  // (real-mojom-parity phase 2) meaningful for kFloat/kDouble only --
  // always the wider `double`, narrowed to `float` at codegen time when
  // the field/const's own type is `float` (see cpp_generator.cc's
  // CppFieldInitializer/EmitConst). When float_special (below) is set,
  // this still holds the same value informationally (e.g.
  // std::numeric_limits<double>::infinity()) -- codegen uses
  // float_special, not this, in that case (a raw "inf"/"nan" literal
  // isn't valid C++ source text).
  double float_value = 0.0;
  // (real-mojom-parity phase 8) Real mojom's special float/double
  // constant literals -- `float.INFINITY`/`double.INFINITY`,
  // `float.NEGATIVE_INFINITY`/`double.NEGATIVE_INFINITY`, and
  // `float.NAN`/`double.NAN` (found via a real vendored .mojom file
  // during phase 8's real-file corpus work -- see
  // tests/real_mojom/battery_status.mojom). kNone for every other
  // kFloat/kDouble default (the ordinary numeric-literal or
  // const-reference forms, using float_value/named_expr as already
  // documented). See cpp_generator.cc's DefaultValueExpr, the only site
  // that reads this.
  enum class FloatSpecial { kNone, kInfinity, kNegativeInfinity, kNaN };
  FloatSpecial float_special = FloatSpecial::kNone;
  std::string string_value;
  std::string named_expr;
  // (v15) Same convention as TypeSpec::owner_namespace above -- empty
  // unless `named_expr` names a const or enum value declared in another
  // file, in which case this is that file's raw `module` statement.
  // cpp_generator.cc's NamespacePrefix qualifies `named_expr` with it at
  // the single site that emits it (EmitFieldInitializer's default-value
  // case).
  std::string named_expr_owner_namespace;
  // (v16) Same convention as TypeSpec::owner_container -- only meaningful
  // when `named_expr` names a nested enum's value ("Status::OK") or a
  // nested const (real-mojom-parity phase 2), in which case this is that
  // enum/const's declaring interface/struct name.
  std::string named_expr_owner_container;
};

// (real-mojom-parity phase 2) A const's own type is no longer restricted to
// integers -- bool/int8..uint64/float/double/string/enum are all legal now
// (matching real mojom's grammar-unrestricted `const typename NAME = ...;`,
// bounded here to the same kinds a field default can be, since a const is
// just a named, reusable field-default-shaped value). `value` reuses
// DefaultValue wholesale: `value.has_value` is always true for a real
// ConstDecl (unused otherwise); `value.named_expr*` lets one const's value
// reference an earlier-declared const or (for an enum-typed const) an enum
// value, the same as a field default can.
struct ConstDecl {
  TypeSpec type;
  std::string name;
  DefaultValue value;
  // (real-mojom-parity phase 2) Position among *every* const in the file,
  // top-level and nested combined (a single counter, distinct from
  // StructDecl/UnionDecl::decl_index's own) -- a const's value can
  // reference an earlier-declared const regardless of whether either one
  // is top-level or nested in a struct/interface, so C++ emission order
  // has to follow this real, whole-file order, not "all top-level, then
  // all of struct X's, then all of interface Y's" (which could emit a
  // later-in-the-real-order const before one it depends on). See
  // cpp_generator.cc's GenerateCppHeader, which sorts by this the same
  // way it already does for StructDecl/UnionDecl::decl_index.
  int decl_index = 0;
  EnableIf enable_if;
};

struct EnumValue {
  std::string name;
  int32_t value = 0;
  // `[MinVersion=N]` on an individual enum value. IsKnownX still
  // accepts every declared value; IsKnownXAsOf(v, version) additionally
  // requires version >= min_version (used when reading a struct field).
  uint32_t min_version = 0;
  // `[Default]` -- when an extensible enum's read sees an unrecognized
  // wire value, codegen substitutes this value instead of storing the
  // raw unknown number. At most one value per enum may be marked.
  bool is_default = false;
  EnableIf enable_if;
};

// Moved above Interface/StructDecl (v16) so both can hold a
// std::vector<EnumDecl> of their own nested enums.
struct EnumDecl {
  std::string name;
  std::vector<EnumValue> values;
  // `[Extensible]` (see parser.cc's ParseEnum) -- when true, ReadX code
  // for a field/param/array-element/... of this enum type tolerates a
  // wire value that doesn't match any of `values` (kept as-is, via the
  // same static_cast this compiler always used); when false (the
  // default), that same situation is a real read failure (`return
  // false;`), matching real mojom's default-closed enum validation. See
  // cpp_generator.cc's EmitEnum (the generated `IsKnownEnumValue`
  // helper) and EmitReadInto's kEnumRef case, and README's "Enum
  // versioning".
  bool is_extensible = false;
  // `[Native] enum Foo;` -- empty body; `--typemap` emits a using-alias.
  bool is_native = false;
  EnableIf enable_if;
};

struct StructDecl;
struct UnionDecl;

struct Interface {
  std::string name;
  std::vector<Method> methods;
  // (v16) Enums declared inside this interface's own body (`interface Foo
  // { enum Status {...}; ... };`) -- emitted nested in Foo's own C++ class
  // scope (Foo::Status) by cpp_generator.cc's EmitInterface. No ordering
  // restriction among themselves or relative to methods, same as a
  // top-level enum has none.
  std::vector<EnumDecl> enums;
  // (real-mojom-parity phase 2) Consts declared inside this interface's own
  // body -- same nesting/qualification treatment as `enums` above, just for
  // `const` instead of `enum`. Unlike enums, order-dependent (see
  // parser.cc's const_by_name_/const_container_): only resolvable, bare or
  // qualified, after its own declaration point -- the same rule a
  // top-level const already has.
  std::vector<ConstDecl> consts;
  // (v24) Structs/unions declared inside this interface's own body
  // (`interface Foo { struct Params { int32 n; }; M(Params p); };`) --
  // generated namespace-scope as Foo_Params (same mangling as nested
  // enums) with `using Params = Foo_Params` inside class Foo. See
  // StructDecl::owner_container.
  std::vector<StructDecl> structs;
  std::vector<UnionDecl> unions;
  // max(method.min_version) across all methods, 0 if none are explicitly
  // versioned. Computed by the parser (ParseInterface), not
  // hand-specified.
  uint32_t version = 0;
  // `[Extensible]` (see parser.cc's ParseInterface) -- when true,
  // Stub_::Accept treats a message ordinal it doesn't recognize as
  // "tolerated, not an error": it returns true (silently drops that one
  // message) instead of false (which WASMCadidumBindings' Connector
  // treats as a protocol error and raises a connection error over).
  // Real mojom's full extensibility model also includes a
  // QueryVersion/RequireVersion control-message negotiation this
  // compiler doesn't implement (WASMCadidumBindings has already
  // documented "no pipe-control-message protocol" as a simplification of
  // its own) -- this is the receiver-tolerance half only. See
  // cpp_generator.cc's EmitInterface and README's "Interface versioning".
  bool is_extensible = false;
  EnableIf enable_if;
};

// Struct fields may be scalar/string/enum/struct/union/array/map, and
// (v23) pending_*/handle kinds -- matching real mojom. Map keys still
// cannot be handle-bearing (CheckValidMapKeyType).
struct StructField {
  TypeSpec type;
  std::string name;
  DefaultValue default_value;
  // `[MinVersion=N]` (0 if unmarked, meaning "present since the struct's
  // very first version"). C++ members stay in declaration order; Write/
  // Read emit fields in non-decreasing min_version order so an old
  // reader still sees version-0 fields first. See README "Struct
  // versioning".
  uint32_t min_version = 0;
  // `@N` after the field name -- accepted (real mojom grammar) and
  // ignored for layout.
  uint32_t ordinal = 0;
  bool has_explicit_ordinal = false;
  EnableIf enable_if;
};

struct StructDecl {
  std::string name;
  std::vector<StructField> fields;
  // (v16) Enums declared inside this struct's own body (`struct Foo {
  // enum Status {...}; Status s; };`) -- emitted nested in Foo's own C++
  // struct scope (Foo::Status) by cpp_generator.cc's EmitStruct. No
  // ordering restriction relative to fields, same as a top-level enum.
  std::vector<EnumDecl> enums;
  // (real-mojom-parity phase 2) Consts declared inside this struct's own
  // body -- see Interface::consts' comment above (same rules).
  std::vector<ConstDecl> consts;
  // Position among all struct/union declarations in file order. Emission
  // order is a complete-type topological sort (see
  // AppendCompleteTypeDeps); decl_index is the stable tie-break when two
  // types are independent. Set by the parser.
  int decl_index = 0;
  // max(field.min_version) across all fields, 0 if none are explicitly
  // versioned -- this struct's own wire version, written into every
  // instance's StructHeader (see cpp_generator.cc's EmitStruct). Computed
  // by the parser, not hand-specified.
  uint32_t version = 0;
  // (v24) Interface name this struct is nested inside, or "" if top-level.
  // Drives EmbedDeclKey / generated C++ mangling (`Foo_Params`).
  std::string owner_container;
  // `[Native] struct Foo;` -- empty body; `--typemap=Foo=::T` emits
  // `using Foo = ::T` plus NativeTraits Write/Read instead of fields.
  bool is_native = false;
  EnableIf enable_if;
};

// Union fields follow the same all-explicit-or-all-implicit @N tag rule as
// interface methods (see Method::ordinal). A field's type is restricted the
// same way a struct field's is -- see StructField above.
struct UnionField {
  TypeSpec type;
  std::string name;
  uint32_t tag = 0;
  bool has_explicit_tag = false;
  // `[MinVersion=N]` (0 if unmarked). Unlike a struct field's min_version,
  // this is purely metadata -- it drives UnionDecl::version, but changes
  // nothing about the wire format: a union only ever writes its *one*
  // active field, so there's no positional/sequential layout for field
  // order to have to agree with (unlike structs, no non-decreasing-order
  // restriction is needed or enforced here). See README's "Union
  // versioning".
  uint32_t min_version = 0;
  // `[Default]` -- when an `[Extensible]` union's read sees an
  // unrecognized tag, codegen selects this field (default-constructed)
  // instead of Tag::kUnknown. At most one field per union may be marked.
  bool is_default = false;
  EnableIf enable_if;
};

struct UnionDecl {
  std::string name;
  std::vector<UnionField> fields;
  int decl_index = 0;  // see StructDecl::decl_index
  // max(field.min_version) across all fields, 0 if none are explicitly
  // versioned. Computed by the parser, not hand-specified.
  uint32_t version = 0;
  // `[Extensible]` (see parser.cc's ParseUnion) -- when true, an
  // unrecognized tag on read isn't a failure: which() reports a
  // synthetic Tag::kUnknown and the union's bytes are skipped (using the
  // same size-prefixed wire framing every union now has, extensible or
  // not) rather than the read failing outright. See cpp_generator.cc's
  // EmitUnion and README's "Union versioning" -- this is the same
  // receiver-tolerance-only scope as Interface::is_extensible. v23 also
  // implements the designated-default-field convention (`[Default]` on
  // a UnionField): an unrecognized tag selects that field instead of
  // Tag::kUnknown when one is marked.
  bool is_extensible = false;
  // (v24) Interface name this union is nested inside, or "" if top-level.
  std::string owner_container;
  EnableIf enable_if;
};

// `import "path/to/other.voodoom";` -- `path` is exactly the string
// literal's contents (forward slashes recommended; see module_loader.h for
// how it's resolved to a file). `resolved_path` starts empty from a bare
// Parser::ParseHeader() call and is filled in by module_loader.cc once it
// resolves this import to an actual file -- see LoadModuleGraph's
// LoadedFile::module.imports. (v15) Each generated file's header now
// `#include`s its own direct imports' generated headers, derived from
// `resolved_path` -- see cpp_generator.cc's GenerateCppHeader and its
// `imported_headers` parameter.
struct ImportDecl {
  std::string path;
  int line = 0;
  std::string resolved_path;
};

// `feature NAME { ... };` -- real mojom's grammar ties this to Chromium's
// base::Feature runtime flag system (attributes like `[Status=STABLE,
// EnabledStateByDefault=ENABLED_BY_DEFAULT]`, none of which this compiler
// acts on -- see parser.cc's ParseFeature), which has no equivalent
// outside an actual Chromium build. Real mojom's own FeatureBody grammar
// only ever allows `const` inside one (never a method, field, or nested
// enum) -- this compiler enforces the same restriction, and generates
// only the consts themselves (same mangled-namespace-scope treatment
// Interface::consts/StructDecl::consts already get -- see
// cpp_generator.cc's MangledNestedName), never anything
// runtime-meaningful for the feature flag itself. See README's "`feature`
// declarations".
struct FeatureDecl {
  std::string name;
  std::vector<ConstDecl> consts;
  EnableIf enable_if;
};

struct Module {
  std::string name;  // may be empty: `module` statement is optional.
  std::vector<ImportDecl> imports;
  std::vector<EnumDecl> enums;
  std::vector<StructDecl> structs;  // see StructDecl::decl_index for order
  std::vector<UnionDecl> unions;    // see UnionDecl::decl_index for order
  std::vector<Interface> interfaces;
  std::vector<ConstDecl> consts;
  std::vector<FeatureDecl> features;
};

}  // namespace voodoom

#endif  // WVC_SRC_AST_H_
