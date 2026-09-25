// The IR: a Web-IDL-shaped subset covering what real HTML5 DOM interfaces
// (Node/Element/Document/Window/EventTarget and friends) actually use --
// interface inheritance, object-reference types, nullability,
// sequences/enums/dictionaries/callbacks, and optional/variadic
// parameters, on top of v1's operations+attributes. See the top of
// cpp_generator.h for what each node becomes in generated C++.
//
// Not yet supported: extended attributes (`[Foo]`), `static` interface
// members, iterable/maplike/setlike, multi-file imports. Union types
// (`(A or B)`) are supported as parameter types only -- see
// cpp_generator.h for why the return-type direction isn't built. None of
// the rest were needed to express the target HTML5 interface set (see
// examples/dom/dom.bruja) -- see WASMBruja/README.md for the reasoning.
#ifndef BRUJA_AST_H_
#define BRUJA_AST_H_

#include <memory>
#include <string>
#include <vector>

namespace bruja {

enum class TypeKind {
  kVoid,
  kBoolean,
  kByte,             // -> int8_t
  kOctet,             // -> uint8_t
  kShort,             // -> int16_t
  kUnsignedShort,     // -> uint16_t
  kLong,              // -> int32_t
  kUnsignedLong,      // -> uint32_t
  kLongLong,          // -> int64_t
  kUnsignedLongLong,  // -> uint64_t
  kFloat,             // -> float (also covers `unrestricted float`)
  kDouble,            // -> double (also covers `unrestricted double`)
  kDOMString,         // -> std::string
  kUSVString,         // -> std::string (alias of DOMString here)
  kAny,               // -> JSValue passthrough (also covers `object`)
  kSequence,          // -> std::vector<element_type>
  kInterfaceRef,      // -> Name* (non-owning), resolved from ref_name
  kEnumRef,           // -> std::string, validated against the enum's values
  kDictionaryRef,     // -> a generated struct, resolved from ref_name
  kCallbackRef,       // -> std::shared_ptr<NameCallback>, resolved from ref_name
  kPromise,           // method-return-type only -> a JS Promise resolved synchronously
  kUnion,             // parameter-type only -> std::variant<member types...>, see union_members

  // Parser-only placeholder for a bare identifier type name; the resolver
  // rewrites every occurrence to kInterfaceRef/kEnumRef/kDictionaryRef/
  // kCallbackRef once it knows which kind of declaration the name refers
  // to. Never seen by the codegen.
  kUnresolvedRef,
};

struct TypeSpec {
  TypeKind kind = TypeKind::kVoid;
  bool nullable = false;

  // Only meaningful for kSequence/kPromise (the element/resolved type).
  std::shared_ptr<TypeSpec> element_type;

  // Only meaningful for kUnion -- e.g. `(Node or DOMString)` -> a 2-entry
  // vector. shared_ptr for the same reason element_type is one (a
  // self-referential incomplete type).
  std::shared_ptr<std::vector<TypeSpec>> union_members;

  // Only meaningful for kInterfaceRef/kEnumRef/kDictionaryRef/
  // kCallbackRef/kUnresolvedRef -- the referenced declaration's name.
  std::string ref_name;
};

struct Param {
  TypeSpec type;
  std::string name;
  bool optional = false;
  bool variadic = false;  // must be the last param; type is the element type
  bool has_default = false;
  std::string default_literal;  // raw text, spliced into generated C++
};

struct Method {
  std::string name;  // JS-visible name, as written in the IDL (lowerCamelCase)
  TypeSpec return_type;
  std::vector<Param> params;
};

struct Attribute {
  TypeSpec type;
  std::string name;  // JS-visible name, as written in the IDL (lowerCamelCase)
  bool readonly = false;
};

struct Const {
  TypeSpec type;
  std::string name;
  std::string value_literal;  // raw text, spliced into generated C++
};

struct Interface {
  std::string name;
  std::string base_name;  // empty if this interface has no base
  std::vector<Method> methods;
  std::vector<Attribute> attributes;
  std::vector<Const> consts;
  bool has_constructor = false;
  std::vector<Param> constructor_params;  // optional/has_default meaningful; variadic unused
};

// `interface mixin Name { ... };` -- never instantiable and never a valid
// TypeSpec target on its own; the resolver copies a mixin's members
// directly into whichever interfaces name it in an IncludesDecl, so
// codegen never sees MixinDecl/IncludesDecl at all (see resolver.cc).
struct MixinDecl {
  std::string name;
  std::vector<Method> methods;
  std::vector<Attribute> attributes;
  std::vector<Const> consts;
};

// `Target includes MixinName;`
struct IncludesDecl {
  std::string target;
  std::string mixin_name;
};

struct EnumDecl {
  std::string name;
  std::vector<std::string> values;
};

struct DictField {
  TypeSpec type;
  std::string name;
  bool has_default = false;
  std::string default_literal;  // raw text, spliced into generated C++
};

struct Dictionary {
  std::string name;
  std::string base_name;  // empty if this dictionary has no base
  std::vector<DictField> fields;
};

struct CallbackDecl {
  std::string name;
  TypeSpec return_type;
  std::vector<Param> params;  // optional/variadic/has_default unused here
};

struct Module {
  std::vector<Interface> interfaces;
  std::vector<EnumDecl> enums;
  std::vector<Dictionary> dictionaries;
  std::vector<CallbackDecl> callbacks;
  std::vector<MixinDecl> mixins;
  std::vector<IncludesDecl> includes_decls;
};

}  // namespace bruja

#endif  // BRUJA_AST_H_
