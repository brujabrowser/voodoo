// Second brujac codegen backend -- see cpp_generator_v8.h. Structurally
// mirrors cpp_generator.cc (same GenContext/CppType-family helper shapes,
// same per-interface naming discipline: every emitted top-level symbol is
// prefixed with the interface name, since two independently-generated
// headers commonly share one C++ namespace -- WASMv16's CMakeLists.txt
// generates console_gen.h/navigator_gen.h/document_gen.h/processes_gen.h
// all under the same default `bruja_generated` namespace, exactly the
// collision this backend must not introduce a bare/generic symbol into).
//
// What this backend supports: interface inheritance (real C++ inheritance
// for the pure-virtual classes, flattened-per-concrete-interface JS
// bindings -- same shape as cpp_generator.cc), interface-typed values
// (nullable or not; a param accepts any descendant of its declared type, a
// return value always wraps as its statically declared type -- see
// EmitInterfaceValueConversion/EmitInterfaceReturn), methods, readonly/
// read-write attributes, and consts, over TypeKind's scalar set (void/
// boolean/byte/octet/short/unsignedShort/long/unsignedLong/longLong/
// unsignedLongLong/float/double) plus DOMString/USVString, nullable or not.
// This is the feature surface WASMv16's four flat .bruja files (console/
// navigator/document/processes) plus WASMBlinker's LocalFrameImpl (which
// needs a real Node/Element/EventTarget-shaped Window binding -- see
// examples/dom/dom.bruja) need for their non-callback, non-sequence,
// non-dictionary, non-union, non-Promise, non-constructor surface.
// `constructor(...)`, and every other TypeKind (sequences, enums,
// dictionaries, callbacks, unions, promises) still throw a clear
// std::runtime_error at generation time (caught by main.cc) instead of
// silently mis-generating -- the same "documented, loud gap" spirit as
// ast.h's own "Not yet supported" comment block for the grammar as a
// whole. Extending this backend to cover the rest is real, valuable
// follow-up work once something actually needs it, not a speculative
// build-out now.
//
// Tag-range assignment for interface inheritance: v8-sandbox.h's own file
// comment on CppHeapPointerTag describes the real V8 algorithm this uses
// unmodified -- assign contiguous tag ids via a pre-order walk of the type
// tree so every supertype's range exactly covers its subtypes' -- combined
// with AllocateCppHeapPointerTagRange (a runtime-allocated *base* for the
// whole module's forest, needed for the same cross-generated-header
// uniqueness reason EmitTagAndTemplateGlobals's comment documents for the
// single-tag case). See ComputeTagIndices/EmitTagAndTemplateGlobals.
//
// The other structural difference from cpp_generator.cc, forced by the
// facade rather than chosen: this facade has no Isolate::ThrowException()/
// Exception::Error() yet (see WASMv8bindings/include/v8-exception.h's file
// comment -- TryCatch can only *catch*, nothing in this facade can *throw*
// from a callback). Every generated callback here follows the same
// no-throw, sentinel-return convention already established throughout
// WASMExtWrench's own hand-written bindings (see e.g.
// WASMExtWrench/src/bindings/runtime.h's file comment): a missing/
// wrong-typed argument or a call against an unwrapped `this` silently
// returns `undefined`/a default value rather than throwing a JS TypeError
// the way the quickjs backend's JS_ThrowTypeError calls do. This is an
// accurate reflection of what the facade can do today, not a narrowing of
// this backend's own scope -- once Isolate::ThrowException() lands, these
// generated callbacks can start throwing for real.
#include "cpp_generator_v8.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace bruja {
namespace {

struct GenContext {
  const Module* module = nullptr;
  std::unordered_map<std::string, const Interface*> interfaces_by_name;
  std::unordered_map<std::string, const Dictionary*> dictionaries_by_name;
  // base interface name -> names of interfaces directly declaring it as base.
  std::unordered_map<std::string, std::vector<std::string>> children;
};

GenContext BuildContext(const Module& module) {
  GenContext ctx;
  ctx.module = &module;
  for (const Interface& iface : module.interfaces) ctx.interfaces_by_name[iface.name] = &iface;
  for (const Dictionary& dict : module.dictionaries) ctx.dictionaries_by_name[dict.name] = &dict;
  for (const Interface& iface : module.interfaces) {
    if (!iface.base_name.empty()) ctx.children[iface.base_name].push_back(iface.name);
  }
  return ctx;
}

// Same shape as TopoSortByBase, for dictionaries -- so `struct
// CustomEventInit : public EventInit` is emitted after EventInit is
// already a complete type. Identical to cpp_generator.cc's own
// TopoSortDictionariesByBase.
std::vector<const Dictionary*> TopoSortDictionariesByBase(const GenContext& ctx) {
  std::vector<const Dictionary*> out;
  std::unordered_map<std::string, bool> emitted;
  for (const Dictionary& dict : ctx.module->dictionaries) emitted[dict.name] = false;
  for (const Dictionary& dict : ctx.module->dictionaries) {
    std::vector<const Dictionary*> chain;
    const Dictionary* cur = &dict;
    while (cur != nullptr && !emitted.at(cur->name)) {
      chain.push_back(cur);
      cur = cur->base_name.empty() ? nullptr : ctx.dictionaries_by_name.at(cur->base_name);
    }
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
      if (!emitted.at((*it)->name)) {
        emitted.at((*it)->name) = true;
        out.push_back(*it);
      }
    }
  }
  return out;
}

// Self plus every ancestor dictionary's fields -- all read off the same
// flat JS object (a dictionary's inheritance is a JS-level illusion; the
// caller passes one object spanning the whole chain). Identical to
// cpp_generator.cc's own CollectEffectiveFields.
std::vector<const DictField*> CollectEffectiveFields(const GenContext& ctx, const Dictionary& dict) {
  std::vector<const DictField*> out;
  std::vector<const Dictionary*> ancestors;
  const Dictionary* cur = &dict;
  while (cur != nullptr) {
    ancestors.push_back(cur);
    cur = cur->base_name.empty() ? nullptr : ctx.dictionaries_by_name.at(cur->base_name);
  }
  for (auto it = ancestors.rbegin(); it != ancestors.rend(); ++it) {
    for (const DictField& f : (*it)->fields) out.push_back(&f);
  }
  return out;
}

// Base-before-derived order (a valid linearization of the `base_name`
// forest), so every interface's C++ base class is a complete type by the
// time its own class body is emitted. Identical in shape to
// cpp_generator.cc's own TopoSortByBase.
std::vector<const Interface*> TopoSortByBase(const GenContext& ctx) {
  std::vector<const Interface*> out;
  std::unordered_map<std::string, bool> emitted;
  for (const Interface& iface : ctx.module->interfaces) emitted[iface.name] = false;
  for (const Interface& iface : ctx.module->interfaces) {
    std::vector<const Interface*> chain;
    const Interface* cur = &iface;
    while (cur != nullptr && !emitted.at(cur->name)) {
      chain.push_back(cur);
      cur = cur->base_name.empty() ? nullptr : ctx.interfaces_by_name.at(cur->base_name);
    }
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
      if (!emitted.at((*it)->name)) {
        emitted.at((*it)->name) = true;
        out.push_back(*it);
      }
    }
  }
  return out;
}

// Self plus every ancestor, self first.
std::vector<const Interface*> AncestorsInclusive(const GenContext& ctx, const Interface& iface) {
  std::vector<const Interface*> out;
  const Interface* cur = &iface;
  while (cur != nullptr) {
    out.push_back(cur);
    cur = cur->base_name.empty() ? nullptr : ctx.interfaces_by_name.at(cur->base_name);
  }
  return out;
}

struct EffectiveMembers {
  std::vector<const Method*> methods;
  std::vector<const Attribute*> attributes;
  std::vector<const Const*> consts;
};

// A concrete interface's own members plus every ancestor's -- what its JS
// wrapper actually exposes. Each entry gets its own generated JS callback
// scoped to `iface`'s own class id, same as cpp_generator.cc's own
// CollectEffectiveMembers.
EffectiveMembers CollectEffectiveMembers(const GenContext& ctx, const Interface& iface) {
  EffectiveMembers out;
  for (const Interface* anc : AncestorsInclusive(ctx, iface)) {
    for (const Method& m : anc->methods) out.methods.push_back(&m);
    for (const Attribute& a : anc->attributes) out.attributes.push_back(&a);
    for (const Const& c : anc->consts) out.consts.push_back(&c);
  }
  return out;
}

// Pre-order tag-index assignment over the whole module's base_name forest
// (real, unmodified V8 algorithm -- see this file's top comment): each
// interface gets a 0-based local_index, and [subtree_min, subtree_max]
// covering itself and every transitive descendant. A flat (non-inheriting)
// interface has subtree_min == subtree_max == its own local_index, so this
// generalizes the pre-inheritance single-tag case exactly.
struct TagIndex {
  int local_index = 0;
  int subtree_min = 0;
  int subtree_max = 0;
};

int AssignTagIndicesRec(const GenContext& ctx, const Interface& iface, int* next_index,
                        std::unordered_map<std::string, TagIndex>* out) {
  int self_index = (*next_index)++;
  int max_index = self_index;
  auto it = ctx.children.find(iface.name);
  if (it != ctx.children.end()) {
    for (const std::string& child_name : it->second) {
      const Interface& child = *ctx.interfaces_by_name.at(child_name);
      max_index = std::max(max_index, AssignTagIndicesRec(ctx, child, next_index, out));
    }
  }
  (*out)[iface.name] = TagIndex{self_index, self_index, max_index};
  return max_index;
}

std::unordered_map<std::string, TagIndex> ComputeTagIndices(const GenContext& ctx) {
  std::unordered_map<std::string, TagIndex> out;
  int next_index = 0;
  for (const Interface& iface : ctx.module->interfaces) {
    if (iface.base_name.empty()) AssignTagIndicesRec(ctx, iface, &next_index, &out);
  }
  return out;
}

std::string PascalCase(const std::string& name) {
  if (name.empty()) return name;
  std::string out = name;
  out[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(out[0])));
  return out;
}

// The pure-virtual method's C++ return type is `void` either because the
// IDL return type literally is `void`, or because it's `Promise<void>`
// (Promise<T> unwraps to T at the C++ level -- see cpp_generator.h's
// identical reasoning and EmitPromiseReturnConversion below).
bool ReturnsVoid(const TypeSpec& type) {
  if (type.kind == TypeKind::kVoid) return true;
  if (type.kind == TypeKind::kPromise) return type.element_type->kind == TypeKind::kVoid;
  return false;
}

size_t RequiredArgCount(const std::vector<Param>& params) {
  size_t count = params.size();
  while (count > 0 && (params[count - 1].optional || params[count - 1].variadic)) {
    --count;
  }
  return count;
}

// ---- Scope check: throws a clear error for anything this backend doesn't
// support yet, instead of silently mis-generating. Called on every
// TypeSpec/Interface this backend is about to touch.

void CheckSupportedType(const TypeSpec& type, const std::string& where) {
  switch (type.kind) {
    case TypeKind::kVoid:
    case TypeKind::kBoolean:
    case TypeKind::kByte:
    case TypeKind::kOctet:
    case TypeKind::kShort:
    case TypeKind::kUnsignedShort:
    case TypeKind::kLong:
    case TypeKind::kUnsignedLong:
    case TypeKind::kLongLong:
    case TypeKind::kUnsignedLongLong:
    case TypeKind::kFloat:
    case TypeKind::kDouble:
    case TypeKind::kDOMString:
    case TypeKind::kUSVString:
    case TypeKind::kInterfaceRef:
    case TypeKind::kCallbackRef:
    case TypeKind::kAny:
    case TypeKind::kDictionaryRef:
    case TypeKind::kEnumRef:
      return;
    case TypeKind::kSequence:
      // No v8::Array in the facade yet -- reaches past it to raw quickjs
      // JS_NewArray/JS_GetPropertyUint32/JS_SetPropertyUint32 directly
      // (same escape-hatch spirit as engine.cc's RunPendingJobs and
      // mojo_bindings.cc's Promise handling elsewhere in this family --
      // see this file's top comment). Real materialized JS Arrays either
      // way (indexing/iteration/.length all work), not a stand-in.
      CheckSupportedType(*type.element_type, where + " sequence element");
      return;
    case TypeKind::kPromise:
      // Resolver.cc already restricts Promise<T> to method-return-type
      // position (see resolver.cc's ResolveType allow_promise param), so
      // this only ever sees it there -- see EmitPromiseReturnConversion.
      CheckSupportedType(*type.element_type, where + " promise element");
      return;
    case TypeKind::kUnion:
      // Parameter-typed only (see EmitUnionValueConversion) -- same
      // restriction cpp_generator.cc's own union support has, for the
      // same reason (nothing in this backend's real consumers returns
      // one -- see this file's top comment).
      for (const TypeSpec& member : *type.union_members) {
        CheckSupportedType(member, where + " union member");
      }
      return;
    case TypeKind::kUnresolvedRef:
      throw std::runtime_error(where + ": internal error: unresolved type reached codegen");
  }
}

void CheckSupportedInterface(const Interface& iface) {
  if (iface.has_constructor) {
    for (const Param& p : iface.constructor_params) {
      if (p.variadic) {
        throw std::runtime_error("interface '" + iface.name +
                                 "': the V8 backend does not yet support variadic "
                                 "constructor parameters");
      }
      CheckSupportedType(p.type,
                        "interface '" + iface.name + "' constructor parameter '" + p.name + "'");
    }
  }
  for (const Method& m : iface.methods) {
    CheckSupportedType(m.return_type, "interface '" + iface.name + "' method '" + m.name + "'");
    for (const Param& p : m.params) {
      CheckSupportedType(p.type, "interface '" + iface.name + "' method '" + m.name +
                                     "' parameter '" + p.name + "'");
    }
  }
  for (const Attribute& a : iface.attributes) {
    CheckSupportedType(a.type, "interface '" + iface.name + "' attribute '" + a.name + "'");
  }
  for (const Const& c : iface.consts) {
    CheckSupportedType(c.type, "interface '" + iface.name + "' const '" + c.name + "'");
  }
}

// ---- C++ type spelling (mirrors cpp_generator.cc's CppBaseType/CppType,
// narrowed to the TypeKind subset CheckSupportedType allows through) -------

bool UsesOptionalWrapper(TypeKind kind) {
  // Interface pointers are nullable-capable already (nullptr IS the null
  // state); callback isn't nullable in this grammar at all; `any`'s own
  // v8::Local<v8::Value> already spans null/undefined -- matches
  // cpp_generator.cc's own UsesOptionalWrapper exactly (any/dictionary/
  // callback don't support nullable in that generator either).
  // Promise<T> is unwrapped to its inner type before this ever matters
  // (see CppBaseType's kPromise case) -- matches cpp_generator.cc's own
  // exclusion.
  return kind != TypeKind::kVoid && kind != TypeKind::kInterfaceRef &&
         kind != TypeKind::kCallbackRef && kind != TypeKind::kAny &&
         kind != TypeKind::kDictionaryRef && kind != TypeKind::kPromise;
}

// Forward declared: CppBaseType's kUnion case needs the full (possibly
// std::optional-wrapped) spelling of each member, i.e. CppType, not
// CppBaseType -- matches cpp_generator.cc's own forward-declaration
// comment for the identical reason.
std::string CppType(const TypeSpec& type);

std::string CppBaseType(const TypeSpec& type) {
  switch (type.kind) {
    case TypeKind::kVoid: return "void";
    case TypeKind::kBoolean: return "bool";
    case TypeKind::kByte: return "int8_t";
    case TypeKind::kOctet: return "uint8_t";
    case TypeKind::kShort: return "int16_t";
    case TypeKind::kUnsignedShort: return "uint16_t";
    case TypeKind::kLong: return "int32_t";
    case TypeKind::kUnsignedLong: return "uint32_t";
    case TypeKind::kLongLong: return "int64_t";
    case TypeKind::kUnsignedLongLong: return "uint64_t";
    case TypeKind::kFloat: return "float";
    case TypeKind::kDouble: return "double";
    case TypeKind::kDOMString:
    case TypeKind::kUSVString:
      return "std::string";
    case TypeKind::kInterfaceRef:
      return type.ref_name + "*";
    case TypeKind::kCallbackRef:
      return "std::shared_ptr<" + type.ref_name + "Callback>";
    case TypeKind::kAny:
      return "v8::Local<v8::Value>";
    case TypeKind::kDictionaryRef:
      return type.ref_name;
    case TypeKind::kEnumRef:
      // No enum-value validation on the way in (see EmitBaseValueConversion's
      // kEnumRef case) -- with no Isolate::ThrowException() yet, rejecting
      // an invalid string can't surface as a real JS error anyway, so this
      // is just a validated-free std::string, same representation
      // cpp_generator.cc uses (a real, validated enum there).
      return "std::string";
    case TypeKind::kPromise:
      // The pure-virtual method returns the inner type synchronously; only
      // the JS-facing wrapper (EmitPromiseReturnConversion) deals in real
      // JS Promise objects -- see cpp_generator.h's identical reasoning.
      return CppBaseType(*type.element_type);
    case TypeKind::kSequence:
      return "std::vector<" + CppBaseType(*type.element_type) + ">";
    case TypeKind::kUnion: {
      std::string s = "std::variant<";
      for (size_t i = 0; i < type.union_members->size(); ++i) {
        if (i > 0) s += ", ";
        s += CppType((*type.union_members)[i]);
      }
      s += ">";
      return s;
    }
    default:
      return "void";  // unreachable: CheckSupportedType already rejected the rest
  }
}

std::string CppType(const TypeSpec& type) {
  std::string base = CppBaseType(type);
  if (type.nullable && UsesOptionalWrapper(type.kind)) {
    return "std::optional<" + base + ">";
  }
  return base;
}

std::string CppParamType(const TypeSpec& type) {
  std::string t = CppType(type);
  bool by_ref = type.kind == TypeKind::kDOMString || type.kind == TypeKind::kUSVString ||
                type.kind == TypeKind::kEnumRef || type.kind == TypeKind::kDictionaryRef ||
                (type.nullable && UsesOptionalWrapper(type.kind));
  return by_ref ? ("const " + t + "&") : t;
}

// The pure-virtual method's own parameter type for a variadic param: a
// `Type... name` reads as `std::vector<T>` on the impl side (no v8::Array
// needed for this -- the JS side is still just plain trailing positional
// arguments, see EmitParamConversions) -- matches cpp_generator.cc's own
// InterfaceClassParamType.
std::string InterfaceClassParamType(const Param& param) {
  if (param.variadic) return "const std::vector<" + CppType(param.type) + ">&";
  return CppParamType(param.type);
}

// ---- Naming ----------------------------------------------------------------

std::string CFunctionName(const std::string& iface, const std::string& method_name) {
  return "JsV8" + iface + "_" + method_name;
}
std::string AttrGetterCppName(const Attribute& attr) { return PascalCase(attr.name); }
std::string AttrSetterCppName(const Attribute& attr) { return "Set" + PascalCase(attr.name); }
std::string AttrGetterJsFunctionName(const std::string& iface, const std::string& attr_name) {
  return "JsV8" + iface + "_get_" + attr_name;
}
std::string AttrSetterJsFunctionName(const std::string& iface, const std::string& attr_name) {
  return "JsV8" + iface + "_set_" + attr_name;
}

// ---- JS -> C++ value conversion --------------------------------------------
//
// Shared per-header helper functions (ToCppBool/ToCppInt32/.../ToCppString),
// emitted once, used by every interface's method/setter callbacks -- see
// EmitConversionHelpers. Each takes a v8::Local<v8::Value> and a default,
// and never throws (see this file's top comment): a wrong-typed value just
// yields the default, the same "sentinel on failure" convention as
// WASMExtWrench/src/bindings/runtime.h's Arg* helpers (which these are
// structurally identical to, just generated inline instead of hand-written,
// since brujac's generated headers are meant to be self-contained -- no
// dependency on any embedder-specific runtime support library).

void EmitConversionHelpers(std::ostringstream& out) {
  out << "inline bool ToCppBool(v8::Local<v8::Value> v, bool default_value) {\n";
  out << "  return v->IsBoolean() ? v.As<v8::Boolean>()->Value() : default_value;\n";
  out << "}\n";
  out << "inline double ToCppDouble(v8::Local<v8::Value> v, double default_value) {\n";
  out << "  return v->IsNumber() ? v.As<v8::Number>()->Value() : default_value;\n";
  out << "}\n";
  out << "inline std::string ToCppString(v8::Isolate* isolate, v8::Local<v8::Value> v,\n";
  out << "                               const std::string& default_value) {\n";
  out << "  if (!v->IsString()) return default_value;\n";
  out << "  v8::String::Utf8Value utf8(isolate, v);\n";
  out << "  return std::string(*utf8, static_cast<size_t>(utf8.length()));\n";
  out << "}\n\n";
}

// Tag-range C++ expression for `iface_name`'s whole subtree -- accepts a
// wrapped instance of that interface OR any descendant, safe because
// Object::Unwrap<T>'s `static_cast<T*>(void*)` needs no offset correction
// for single, non-virtual inheritance (the Itanium ABI keeps a derived
// object's address equal to its unique base subobject's -- true throughout
// this whole chain, since brujac's grammar has no multiple/virtual
// inheritance). TagBaseValue() is the module's own runtime-allocated block
// base -- see EmitTagAndTemplateGlobals.
std::string TagRangeExpr(const std::string& iface_name,
                         const std::unordered_map<std::string, TagIndex>& tag_indices) {
  const TagIndex& info = tag_indices.at(iface_name);
  std::ostringstream s;
  s << "v8::CppHeapPointerTagRange("
    << "static_cast<v8::CppHeapPointerTag>(static_cast<uint16_t>(TagBaseValue()) + "
    << info.subtree_min << "), "
    << "static_cast<v8::CppHeapPointerTag>(static_cast<uint16_t>(TagBaseValue()) + "
    << info.subtree_max << "))";
  return s.str();
}

// Interface-typed values are the one place this backend is genuinely
// polymorphism-aware on the *parameter* side: probes the whole declared
// type's subtree in one range check (see TagRangeExpr) rather than
// cpp_generator.cc's per-descendant JS_GetOpaque loop -- same net effect
// (accept any descendant), simpler here because CppHeapPointerTagRange
// already does a single-comparison range check instead of needing N
// separate class-id probes. A JS `null`/`undefined`/non-object always
// becomes `nullptr` here regardless of the type's own declared
// nullability -- matches cpp_generator.cc's own EmitInterfaceProbe
// simplification exactly.
void EmitInterfaceValueConversion(std::ostringstream& out, const TypeSpec& type,
                                  const std::string& out_var, const std::string& source,
                                  const std::unordered_map<std::string, TagIndex>& tag_indices) {
  out << "  " << type.ref_name << "* " << out_var << " = nullptr;\n";
  out << "  if ((" << source << ")->IsObject()) {\n";
  out << "    " << out_var << " = v8::Object::Unwrap<" << type.ref_name << ">((" << source
      << ").As<v8::Object>(), " << TagRangeExpr(type.ref_name, tag_indices) << ");\n";
  out << "  }\n";
}

void EmitValueConversion(std::ostringstream& out, const GenContext& ctx, const TypeSpec& type,
                        const std::string& out_var, const std::string& source,
                        const std::string& isolate_expr,
                        const std::unordered_map<std::string, TagIndex>& tag_indices);

// Reads each effective field (own + inherited -- CollectEffectiveFields)
// off the same flat JS object `source`, converting each with
// EmitValueConversion. Mirrors cpp_generator.cc's own EmitDictionaryFromJs.
void EmitDictionaryFromJs(std::ostringstream& out, const GenContext& ctx, const TypeSpec& type,
                          const std::string& out_var, const std::string& source,
                          const std::string& isolate_expr,
                          const std::unordered_map<std::string, TagIndex>& tag_indices) {
  const Dictionary& dict = *ctx.dictionaries_by_name.at(type.ref_name);
  out << "  " << type.ref_name << " " << out_var << ";\n";
  out << "  if ((" << source << ")->IsObject()) {\n";
  out << "    v8::Local<v8::Object> " << out_var << "_obj = (" << source << ").As<v8::Object>();\n";
  for (const DictField* field_ptr : CollectEffectiveFields(ctx, dict)) {
    const DictField& field = *field_ptr;
    std::string field_js = out_var + "_" + field.name + "_js";
    out << "    v8::Local<v8::Value> " << field_js << ";\n";
    out << "    if (" << out_var << "_obj->Get(" << isolate_expr << ", \"" << field.name
        << "\").ToLocal(&" << field_js << ") && !" << field_js << "->IsUndefined()) {\n";
    out << "      ";
    EmitValueConversion(out, ctx, field.type, out_var + "_" + field.name + "_val", field_js,
                       isolate_expr, tag_indices);
    out << "      " << out_var << "." << field.name << " = std::move(" << out_var << "_"
        << field.name << "_val);\n";
    out << "    }\n";
  }
  out << "  }\n";
}

// A union parameter `(A or B or ...)` converts JS->C++ by trying its
// interface-typed members first, in declared order, via the same
// non-throwing tag-range probe interface params already use (a genuine
// "is this JS value one of these classes" check, not a guess); the first
// *non*-interface member (if any -- realistically a DOMString fallback, as
// in `(Node or DOMString)`) is tried last, only if no interface member
// matched. Covers this backend's actual target shape, not the full Web
// IDL "distinguishable types" algorithm for arbitrary multi-primitive
// unions -- identical scope and reasoning to cpp_generator.cc's own
// EmitUnionValueConversion. If literally nothing matches (only reachable
// with a non-interface, non-fallback-eligible member arrangement this
// grammar doesn't actually produce), `out_var` is left value-initialized
// -- no throw available (see this file's top comment).
void EmitUnionValueConversion(std::ostringstream& out, const GenContext& ctx, const TypeSpec& type,
                             const std::string& out_var, const std::string& source,
                             const std::string& isolate_expr,
                             const std::unordered_map<std::string, TagIndex>& tag_indices) {
  out << "  " << CppBaseType(type) << " " << out_var << ";\n";
  out << "  {\n";
  out << "    bool " << out_var << "_matched = false;\n";
  const TypeSpec* fallback = nullptr;
  for (const TypeSpec& member : *type.union_members) {
    if (member.kind == TypeKind::kInterfaceRef) {
      out << "    if (!" << out_var << "_matched && (" << source << ")->IsObject()) {\n";
      out << "      auto* " << out_var << "_probe = v8::Object::Unwrap<" << member.ref_name
          << ">((" << source << ").As<v8::Object>(), "
          << TagRangeExpr(member.ref_name, tag_indices) << ");\n";
      out << "      if (" << out_var << "_probe) {\n";
      out << "        " << out_var << " = " << out_var << "_probe;\n";
      out << "        " << out_var << "_matched = true;\n";
      out << "      }\n";
      out << "    }\n";
    } else if (fallback == nullptr) {
      fallback = &member;
    }
  }
  if (fallback != nullptr) {
    out << "    if (!" << out_var << "_matched) {\n";
    EmitValueConversion(out, ctx, *fallback, out_var + "_fallback", source, isolate_expr,
                       tag_indices);
    out << "      " << out_var << " = std::move(" << out_var << "_fallback);\n";
    out << "      " << out_var << "_matched = true;\n";
    out << "    }\n";
  }
  out << "  }\n";
}

// Converts a non-nullable value of an allowed TypeSpec (scalar/string, or
// -- unlike cpp_generator.cc's identically-named helper -- also `any` and
// `dictionary`, both easy value-type conversions in this facade: `any` is
// already a v8::Local<v8::Value>, a real independent reference with no
// dup/free bookkeeping needed (see v8-local-handle.h's file comment), so
// it's just copied; dictionary reads its effective fields off the same
// flat JS object real Web IDL dictionaries use -- see EmitDictionaryFromJs).
// `source` is a C++ expression yielding a v8::Local<v8::Value>.
void EmitBaseValueConversion(std::ostringstream& out, const GenContext& ctx, const TypeSpec& type,
                            const std::string& out_var, const std::string& source,
                            const std::string& isolate_expr,
                            const std::unordered_map<std::string, TagIndex>& tag_indices) {
  switch (type.kind) {
    case TypeKind::kBoolean:
      out << "  bool " << out_var << " = ToCppBool(" << source << ", false);\n";
      break;
    case TypeKind::kDOMString:
    case TypeKind::kUSVString:
    case TypeKind::kEnumRef:
      out << "  std::string " << out_var << " = ToCppString(" << isolate_expr << ", " << source
          << ", std::string());\n";
      break;
    case TypeKind::kAny:
      out << "  v8::Local<v8::Value> " << out_var << " = " << source << ";\n";
      break;
    case TypeKind::kDictionaryRef:
      EmitDictionaryFromJs(out, ctx, type, out_var, source, isolate_expr, tag_indices);
      break;
    case TypeKind::kSequence: {
      // No v8::Array in the facade -- reaches past it to raw quickjs
      // (JS_GetPropertyStr for "length", JS_GetPropertyUint32 per index),
      // same shape as cpp_generator.cc's own kSequence case. `source`
      // already carries its own JSContext* (Local<Value>::
      // context_for_wasmv8_internal()), so no separate context param is
      // needed here.
      out << "  " << CppBaseType(type) << " " << out_var << ";\n";
      out << "  {\n";
      out << "    JSContext* " << out_var << "_ctx = (" << source
          << ").context_for_wasmv8_internal();\n";
      out << "    JSValue " << out_var << "_raw = (" << source
          << ").value_for_wasmv8_internal();\n";
      out << "    int64_t " << out_var << "_len = 0;\n";
      out << "    JSValue " << out_var << "_len_val = JS_GetPropertyStr(" << out_var << "_ctx, "
          << out_var << "_raw, \"length\");\n";
      out << "    JS_ToInt64(" << out_var << "_ctx, &" << out_var << "_len, " << out_var
          << "_len_val);\n";
      out << "    JS_FreeValue(" << out_var << "_ctx, " << out_var << "_len_val);\n";
      out << "    for (int64_t " << out_var << "_i = 0; " << out_var << "_i < " << out_var
          << "_len; ++" << out_var << "_i) {\n";
      out << "      JSValue " << out_var << "_elem_raw = JS_GetPropertyUint32(" << out_var
          << "_ctx, " << out_var << "_raw, static_cast<uint32_t>(" << out_var << "_i));\n";
      out << "      v8::Local<v8::Value> " << out_var << "_elem_local = "
             "v8::Local<v8::Value>::Adopt(" << out_var << "_ctx, " << out_var << "_elem_raw);\n";
      EmitValueConversion(out, ctx, *type.element_type, out_var + "_elem",
                          out_var + "_elem_local", isolate_expr, tag_indices);
      out << "      " << out_var << ".push_back(std::move(" << out_var << "_elem));\n";
      out << "    }\n";
      out << "  }\n";
      break;
    }
    case TypeKind::kUnion:
      EmitUnionValueConversion(out, ctx, type, out_var, source, isolate_expr, tag_indices);
      break;
    default: {
      // Every remaining allowed kind is a numeric scalar: route through
      // ToCppDouble and narrow -- JS numbers are IEEE-754 doubles regardless
      // of the IDL-declared width, same spirit as ArgNumber's own doc
      // comment in WASMExtWrench/src/bindings/runtime.h.
      out << "  " << CppBaseType(type) << " " << out_var << " = static_cast<"
          << CppBaseType(type) << ">(ToCppDouble(" << source << ", 0));\n";
      break;
    }
  }
}

void EmitValueConversion(std::ostringstream& out, const GenContext& ctx, const TypeSpec& type,
                        const std::string& out_var, const std::string& source,
                        const std::string& isolate_expr,
                        const std::unordered_map<std::string, TagIndex>& tag_indices) {
  if (type.kind == TypeKind::kInterfaceRef) {
    EmitInterfaceValueConversion(out, type, out_var, source, tag_indices);
    return;
  }
  if (type.kind == TypeKind::kCallbackRef) {
    out << "  auto " << out_var << " = std::make_shared<" << type.ref_name << "Callback>("
        << isolate_expr << ", " << source << ");\n";
    return;
  }
  if (type.nullable) {
    out << "  " << CppType(type) << " " << out_var << " = std::nullopt;\n";
    out << "  if (!(" << source << ")->IsNullOrUndefined()) {\n";
    TypeSpec non_null = type;
    non_null.nullable = false;
    EmitBaseValueConversion(out, ctx, non_null, out_var + "_present", source, isolate_expr,
                           tag_indices);
    out << "    " << out_var << " = std::move(" << out_var << "_present);\n";
    out << "  }\n";
    return;
  }
  EmitBaseValueConversion(out, ctx, type, out_var, source, isolate_expr, tag_indices);
}

// ---- C++ -> JS return conversion -------------------------------------------
//
// Writes directly into `info.GetReturnValue()` (`ret_expr`) rather than
// building an intermediate JSValue-equivalent local -- ReturnValue<T>::Set
// already overloads on Local<Value>/bool/double/int32_t (v8-function-
// callback.h), which covers every scalar/string kind this backend supports:
// strings and booleans use their own overload directly; every numeric kind
// (including the ones with no dedicated overload, e.g. int64_t/uint32_t)
// routes through the double overload, same reasoning as ToCppDouble above.

// Interface-typed return/attribute values generally wrap via the
// statically declared type's own CreateXBinding -- not the value's real
// most-derived type. This backend has no *general* dynamic
// wrapper-identity registry (same documented simplification as
// cpp_generator.cc's own EmitInterfaceToJs -- see this file's top
// comment), so e.g. a `Node* parentNode()` still always yields a plain
// Node-shaped JS wrapper even when the real object is an Element.
//
// `Element`-typed returns are the one exception, since they're the most
// common case real DOM code actually hits (document.getElementById,
// querySelector, etc.) and were silently losing every HTML-subtype-
// specific property (img.naturalWidth, a.href, input.value, ...) as a
// result: instead of the generic CreateElementBinding, these route
// through WrapElementForJs (bruja_dom/dom_v8_impl.h), a hand-maintained
// dynamic_cast chain mirroring DocumentImpl::MakeElementForTag's
// tag->concrete-type mapping (not derivable from the IDL itself, so not
// something this generator can produce on its own -- see
// WrapElementForJs's own comment).
void EmitInterfaceReturn(std::ostringstream& out, const TypeSpec& type, const std::string& ret_expr,
                         const std::string& cpp_expr, const std::string& isolate_expr,
                         const std::string& context_expr) {
  const std::string wrap_fn =
      type.ref_name == "Element" ? "WrapElementForJs" : "Create" + type.ref_name + "Binding";
  out << "  if ((" << cpp_expr << ") == nullptr) {\n";
  out << "    " << ret_expr << ".SetNull();\n";
  out << "  } else {\n";
  out << "    " << ret_expr << ".Set(" << wrap_fn << "(" << isolate_expr << ", " << context_expr
      << ", (" << cpp_expr << ")));\n";
  out << "  }\n";
}

void EmitToJsVariable(std::ostringstream& out, const GenContext& ctx, const TypeSpec& type,
                     const std::string& out_var, const std::string& cpp_expr,
                     const std::string& isolate_expr, const std::string& context_expr,
                     const std::unordered_map<std::string, TagIndex>& tag_indices);

// Mirror of EmitDictionaryFromJs, other direction: builds a plain JS
// object and sets each effective field (own + inherited, same flat shape
// EmitDictionaryFromJs reads) by converting the C++ member with
// EmitToJsVariable. Mirrors cpp_generator.cc's own EmitToJs kDictionaryRef
// case.
void EmitDictionaryToJsVariable(std::ostringstream& out, const GenContext& ctx,
                                const TypeSpec& type, const std::string& out_var,
                                const std::string& cpp_expr, const std::string& isolate_expr,
                                const std::string& context_expr,
                                const std::unordered_map<std::string, TagIndex>& tag_indices) {
  const Dictionary& dict = *ctx.dictionaries_by_name.at(type.ref_name);
  out << "  v8::Local<v8::Object> " << out_var << " = v8::Object::New(" << isolate_expr << ");\n";
  for (const DictField* field_ptr : CollectEffectiveFields(ctx, dict)) {
    const DictField& field = *field_ptr;
    std::string field_js = out_var + "_" + field.name + "_js";
    EmitToJsVariable(out, ctx, field.type, field_js, "(" + cpp_expr + ")." + field.name,
                    isolate_expr, context_expr, tag_indices);
    out << "  " << out_var << "->Set(" << isolate_expr << ", \"" << field.name << "\", "
        << field_js << ");\n";
  }
}

// Mirror of the kSequence case in EmitBaseValueConversion, other direction:
// builds a real materialized JS Array (JS_NewArray -- no v8::Array in the
// facade, same escape-hatch reasoning as this file's top comment) and
// pushes each element, converted via EmitToJsVariable. `context_expr`
// yields a v8::Local<v8::Context> whose context_for_wasmv8_internal()
// gives the raw JSContext* the quickjs array calls need.
void EmitSequenceToJsVariable(std::ostringstream& out, const GenContext& ctx, const TypeSpec& type,
                              const std::string& out_var, const std::string& cpp_expr,
                              const std::string& isolate_expr, const std::string& context_expr,
                              const std::unordered_map<std::string, TagIndex>& tag_indices) {
  out << "  JSContext* " << out_var << "_ctx = (" << context_expr
      << ").context_for_wasmv8_internal();\n";
  out << "  JSValue " << out_var << "_arr_raw = JS_NewArray(" << out_var << "_ctx);\n";
  out << "  {\n";
  out << "    uint32_t " << out_var << "_i = 0;\n";
  out << "    for (const auto& " << out_var << "_elem : (" << cpp_expr << ")) {\n";
  EmitToJsVariable(out, ctx, *type.element_type, out_var + "_elem_js", out_var + "_elem",
                  isolate_expr, context_expr, tag_indices);
  out << "      JS_SetPropertyUint32(" << out_var << "_ctx, " << out_var << "_arr_raw, " << out_var
      << "_i++, JS_DupValue(" << out_var << "_ctx, (" << out_var
      << "_elem_js).value_for_wasmv8_internal()));\n";
  out << "    }\n";
  out << "  }\n";
  out << "  v8::Local<v8::Value> " << out_var << " = v8::Local<v8::Value>::Adopt(" << out_var
      << "_ctx, " << out_var << "_arr_raw);\n";
}

void EmitBaseReturn(std::ostringstream& out, const GenContext& ctx, const TypeSpec& type,
                    const std::string& ret_expr, const std::string& cpp_expr,
                    const std::string& isolate_expr, const std::string& context_expr,
                    const std::unordered_map<std::string, TagIndex>& tag_indices) {
  switch (type.kind) {
    case TypeKind::kVoid:
      out << "  " << ret_expr << ".SetUndefined();\n";
      return;
    case TypeKind::kBoolean:
      out << "  " << ret_expr << ".Set(static_cast<bool>(" << cpp_expr << "));\n";
      return;
    case TypeKind::kDOMString:
    case TypeKind::kUSVString:
    case TypeKind::kEnumRef:
      out << "  " << ret_expr << ".Set(v8::String::NewFromUtf8(" << isolate_expr << ", ("
          << cpp_expr << ").c_str()).ToLocalChecked());\n";
      return;
    case TypeKind::kAny:
      out << "  " << ret_expr << ".Set(" << cpp_expr << ");\n";
      return;
    case TypeKind::kDictionaryRef: {
      EmitDictionaryToJsVariable(out, ctx, type, "dict_obj", cpp_expr, isolate_expr, context_expr,
                                tag_indices);
      out << "  " << ret_expr << ".Set(dict_obj);\n";
      return;
    }
    case TypeKind::kSequence: {
      EmitSequenceToJsVariable(out, ctx, type, "seq_val", cpp_expr, isolate_expr, context_expr,
                              tag_indices);
      out << "  " << ret_expr << ".Set(seq_val);\n";
      return;
    }
    case TypeKind::kUnion:
      // Union types are param-only in this backend (see
      // EmitUnionValueConversion) -- nothing in this backend's real
      // consumers returns one, so the C++->JS (std::visit-based) direction
      // isn't built, matching cpp_generator.cc's own identical choice.
      out << "  " << ret_expr << ".SetUndefined();\n";
      return;
    default:
      out << "  " << ret_expr << ".Set(static_cast<double>(" << cpp_expr << "));\n";
      return;
  }
}

// Promise<T> methods stay fully synchronous at the C++ level (the impl
// still just returns T, like every other method) -- only the JS-facing
// wrapper differs, building an already-resolved Promise via
// JS_NewPromiseCapability and resolving it immediately with the converted
// value. Same "deliberately simpler, no async plumbing" spirit as
// `callback`, and identical to cpp_generator.cc's own
// EmitPromiseReturnConversion -- reaches past the facade to raw quickjs
// since v8::Promise isn't ported yet (see this file's top comment).
void EmitPromiseReturnConversion(std::ostringstream& out, const GenContext& ctx,
                                 const TypeSpec& type, const std::string& ret_expr,
                                 const std::string& cpp_expr, const std::string& isolate_expr,
                                 const std::string& context_expr,
                                 const std::unordered_map<std::string, TagIndex>& tag_indices) {
  out << "  JSContext* promise_ctx = (" << context_expr << ").context_for_wasmv8_internal();\n";
  out << "  JSValue promise_resolving_funcs[2];\n";
  out << "  JSValue promise_raw = JS_NewPromiseCapability(promise_ctx, promise_resolving_funcs);\n";
  if (type.element_type->kind == TypeKind::kVoid) {
    out << "  JSValue promise_resolve_argv[] = {JS_UNDEFINED};\n";
  } else {
    EmitToJsVariable(out, ctx, *type.element_type, "promise_result_js", cpp_expr, isolate_expr,
                    context_expr, tag_indices);
    out << "  JSValue promise_resolve_argv[] = {(promise_result_js).value_for_wasmv8_internal()};\n";
  }
  out << "  JSValue promise_resolve_result = JS_Call(promise_ctx, promise_resolving_funcs[0], "
         "JS_UNDEFINED, 1, promise_resolve_argv);\n";
  out << "  JS_FreeValue(promise_ctx, promise_resolve_result);\n";
  out << "  JS_FreeValue(promise_ctx, promise_resolving_funcs[0]);\n";
  out << "  JS_FreeValue(promise_ctx, promise_resolving_funcs[1]);\n";
  out << "  " << ret_expr << ".Set(v8::Local<v8::Value>::Adopt(promise_ctx, promise_raw));\n";
}

void EmitReturnConversion(std::ostringstream& out, const GenContext& ctx, const TypeSpec& type,
                         const std::string& ret_expr, const std::string& cpp_expr,
                         const std::string& isolate_expr, const std::string& context_expr,
                         const std::unordered_map<std::string, TagIndex>& tag_indices) {
  if (type.kind == TypeKind::kVoid) {
    out << "  " << ret_expr << ".SetUndefined();\n";
    return;
  }
  if (type.kind == TypeKind::kPromise) {
    EmitPromiseReturnConversion(out, ctx, type, ret_expr, cpp_expr, isolate_expr, context_expr,
                               tag_indices);
    return;
  }
  if (type.kind == TypeKind::kInterfaceRef) {
    EmitInterfaceReturn(out, type, ret_expr, cpp_expr, isolate_expr, context_expr);
    return;
  }
  if (type.nullable) {
    out << "  if (!(" << cpp_expr << ").has_value()) {\n";
    out << "    " << ret_expr << ".SetNull();\n";
    out << "  } else {\n";
    TypeSpec non_null = type;
    non_null.nullable = false;
    EmitBaseReturn(out, ctx, non_null, ret_expr, "(*(" + cpp_expr + "))", isolate_expr,
                  context_expr, tag_indices);
    out << "  }\n";
    return;
  }
  EmitBaseReturn(out, ctx, type, ret_expr, cpp_expr, isolate_expr, context_expr, tag_indices);
}

// ---- C++ -> new v8::Local<v8::Value> variable (mirror image of
// EmitBaseReturn/EmitInterfaceReturn, but building a fresh Local<Value>
// local instead of writing into a ReturnValue<T> -- what a callback
// wrapper's Call() needs when handing C++-side arguments to the wrapped JS
// function; see EmitCallbackWrapperClass) --------------------------------

void EmitBaseToJsVariable(std::ostringstream& out, const GenContext& ctx, const TypeSpec& type,
                          const std::string& out_var, const std::string& cpp_expr,
                          const std::string& isolate_expr, const std::string& context_expr,
                          const std::unordered_map<std::string, TagIndex>& tag_indices) {
  switch (type.kind) {
    case TypeKind::kInterfaceRef: {
      // Same Element-specific real-most-derived-type dispatch
      // EmitInterfaceReturn documents and uses -- this path is what a
      // sequence<Element> (getElementsByTagName, children, ...) converts
      // each item through, so it needs the identical fix or those
      // collections would keep handing back plain Element-shaped items.
      const std::string wrap_fn =
          type.ref_name == "Element" ? "WrapElementForJs" : "Create" + type.ref_name + "Binding";
      out << "  v8::Local<v8::Value> " << out_var << ";\n";
      out << "  if ((" << cpp_expr << ") == nullptr) {\n";
      out << "    " << out_var << " = v8::Local<v8::Value>::Adopt((" << context_expr
          << ").context_for_wasmv8_internal(), JS_NULL);\n";
      out << "  } else {\n";
      out << "    " << out_var << " = " << wrap_fn << "(" << isolate_expr << ", " << context_expr
          << ", (" << cpp_expr << "));\n";
      out << "  }\n";
      return;
    }
    case TypeKind::kBoolean:
      out << "  v8::Local<v8::Value> " << out_var << " = v8::Boolean::New(" << isolate_expr
          << ", static_cast<bool>(" << cpp_expr << "));\n";
      return;
    case TypeKind::kDOMString:
    case TypeKind::kUSVString:
    case TypeKind::kEnumRef:
      out << "  v8::Local<v8::Value> " << out_var << " = v8::String::NewFromUtf8(" << isolate_expr
          << ", (" << cpp_expr << ").c_str()).ToLocalChecked();\n";
      return;
    case TypeKind::kAny:
      out << "  v8::Local<v8::Value> " << out_var << " = " << cpp_expr << ";\n";
      return;
    case TypeKind::kDictionaryRef:
      EmitDictionaryToJsVariable(out, ctx, type, out_var, cpp_expr, isolate_expr, context_expr,
                                tag_indices);
      return;
    case TypeKind::kSequence:
      EmitSequenceToJsVariable(out, ctx, type, out_var, cpp_expr, isolate_expr, context_expr,
                              tag_indices);
      return;
    case TypeKind::kUnion:
      // See EmitBaseReturn's identical kUnion case -- param-only in this
      // backend, matching cpp_generator.cc.
      out << "  v8::Local<v8::Value> " << out_var << " = v8::Local<v8::Value>();\n";
      return;
    default:
      out << "  v8::Local<v8::Value> " << out_var << " = v8::Number::New(" << isolate_expr
          << ", static_cast<double>(" << cpp_expr << "));\n";
      return;
  }
}

void EmitToJsVariable(std::ostringstream& out, const GenContext& ctx, const TypeSpec& type,
                     const std::string& out_var, const std::string& cpp_expr,
                     const std::string& isolate_expr, const std::string& context_expr,
                     const std::unordered_map<std::string, TagIndex>& tag_indices) {
  if (type.nullable && UsesOptionalWrapper(type.kind)) {
    out << "  v8::Local<v8::Value> " << out_var << ";\n";
    out << "  if (!(" << cpp_expr << ").has_value()) {\n";
    out << "    " << out_var << " = v8::Local<v8::Value>::Adopt((" << context_expr
        << ").context_for_wasmv8_internal(), JS_NULL);\n";
    out << "  } else {\n";
    TypeSpec non_null = type;
    non_null.nullable = false;
    EmitBaseToJsVariable(out, ctx, non_null, out_var + "_present", "(*(" + cpp_expr + "))",
                        isolate_expr, context_expr, tag_indices);
    out << "    " << out_var << " = " << out_var << "_present;\n";
    out << "  }\n";
    return;
  }
  EmitBaseToJsVariable(out, ctx, type, out_var, cpp_expr, isolate_expr, context_expr, tag_indices);
}

// A JS function value passed where the IDL expects a `callback` -- holds a
// real reference (a v8::Local<v8::Value> here already IS a real,
// independently-owned reference in this facade -- see v8-local-handle.h's
// file comment -- so unlike cpp_generator.cc's own JS_DupValue, no explicit
// dup call is needed) so an implementation (e.g. an EventTarget storing
// listeners) can call it later, fully synchronously (Function::Call, no
// async plumbing) -- same "deliberately simpler than a real callback"
// spirit as cpp_generator.cc's own EmitCallbackWrapperClass.
void EmitCallbackWrapperClass(std::ostringstream& out, const GenContext& ctx,
                              const CallbackDecl& decl,
                              const std::unordered_map<std::string, TagIndex>& tag_indices) {
  const std::string cls = decl.name + "Callback";
  out << "class " << cls << " {\n";
  out << " public:\n";
  out << "  " << cls << "(v8::Isolate* isolate, v8::Local<v8::Value> fn) : isolate_(isolate), fn_(fn) {}\n\n";
  out << "  // Identity comparison against another wrapper for the same\n";
  out << "  // underlying JS function -- what a removeEventListener-style caller\n";
  out << "  // needs to find a previously-stored listener again.\n";
  out << "  bool Matches(const " << cls << "& other) const {\n";
  out << "    return JS_VALUE_GET_PTR(fn_.value_for_wasmv8_internal()) ==\n";
  out << "           JS_VALUE_GET_PTR(other.fn_.value_for_wasmv8_internal());\n";
  out << "  }\n\n";

  out << "  " << CppType(decl.return_type) << " Call(";
  for (size_t i = 0; i < decl.params.size(); ++i) {
    if (i > 0) out << ", ";
    out << CppParamType(decl.params[i].type) << " " << decl.params[i].name;
  }
  out << ") {\n";
  out << "    if (!fn_->IsFunction()) {\n";
  if (decl.return_type.kind != TypeKind::kVoid) {
    out << "      return " << CppType(decl.return_type) << "{};\n";
  } else {
    out << "      return;\n";
  }
  out << "    }\n";
  out << "    v8::Isolate* isolate = isolate_;\n";
  out << "    v8::Local<v8::Context> js_context = v8::Local<v8::Context>(v8::CurrentContext(isolate));\n";
  std::vector<std::string> arg_vars;
  for (size_t i = 0; i < decl.params.size(); ++i) {
    std::string arg_var = "js_arg" + std::to_string(i);
    EmitToJsVariable(out, ctx, decl.params[i].type, arg_var, decl.params[i].name, "isolate",
                     "js_context", tag_indices);
    arg_vars.push_back(arg_var);
  }
  if (arg_vars.empty()) {
    out << "    v8::Local<v8::Value>* argv = nullptr;\n";
    out << "    v8::MaybeLocal<v8::Value> maybe_result = fn_.As<v8::Function>()->Call(js_context, "
           "v8::Local<v8::Value>(), 0, argv);\n";
  } else {
    out << "    v8::Local<v8::Value> argv[] = {";
    for (size_t i = 0; i < arg_vars.size(); ++i) {
      if (i > 0) out << ", ";
      out << arg_vars[i];
    }
    out << "};\n";
    out << "    v8::MaybeLocal<v8::Value> maybe_result = fn_.As<v8::Function>()->Call(js_context, "
           "v8::Local<v8::Value>(), "
        << arg_vars.size() << ", argv);\n";
  }
  out << "    v8::Local<v8::Value> call_result;\n";
  out << "    if (!maybe_result.ToLocal(&call_result)) {\n";
  if (decl.return_type.kind != TypeKind::kVoid) {
    out << "      return " << CppType(decl.return_type) << "{};\n";
  } else {
    out << "      return;\n";
  }
  out << "    }\n";
  if (decl.return_type.kind != TypeKind::kVoid) {
    EmitValueConversion(out, ctx, decl.return_type, "cpp_result", "call_result", "isolate",
                        tag_indices);
    out << "    return cpp_result;\n";
  }
  out << "  }\n\n";
  out << " private:\n";
  out << "  v8::Isolate* isolate_;\n";
  out << "  v8::Local<v8::Value> fn_;\n";
  out << "};\n\n";
}

std::string ConstCppLiteral(const Const& c) {
  // value_literal is raw IDL text (e.g. "42", "true") -- valid as a C++
  // literal for every scalar kind this backend supports (same assumption
  // cpp_generator.cc's own EmitInterfaceClass const codegen already makes).
  return c.value_literal;
}

// ---- Emitters ---------------------------------------------------------------

// Own fields only -- inherited ones already exist via the real C++ base
// (dictionary inheritance uses real C++ inheritance for the generated
// struct, same as interfaces). Identical to cpp_generator.cc's own
// EmitDictionaryStruct.
void EmitDictionaryStruct(std::ostringstream& out, const Dictionary& dict) {
  out << "struct " << dict.name;
  if (!dict.base_name.empty()) out << " : public " << dict.base_name;
  out << " {\n";
  for (const DictField& field : dict.fields) {
    out << "  " << CppType(field.type) << " " << field.name;
    if (field.has_default) {
      // The parser rewrites a literal `null` default (only legal for an
      // `any`-typed field) to the raw text "JS_NULL" -- a real quickjs
      // constant the quickjs backend's own JSValue-typed field can use
      // directly as a member-initializer, but this facade's
      // v8::Local<v8::Value> has no converting constructor from a bare
      // JSValue, and no isolate is in scope for Local<Value>::Adopt() at
      // struct-member-initializer time anyway. A default-constructed
      // Local<Value> (undefined, per v8-local-handle.h's own Value()
      // default ctor) is the closest "no value present" this facade can
      // express without one -- close enough to "null" for a field meant
      // to mean "absent" (see CustomEventInit.detail).
      if (field.type.kind == TypeKind::kAny && field.default_literal == "JS_NULL") {
        out << " = v8::Local<v8::Value>()";
      } else {
        out << " = " << field.default_literal;
      }
    }
    out << ";\n";
  }
  out << "};\n\n";
}

void EmitInterfaceClass(std::ostringstream& out, const Interface& iface) {
  out << "class " << iface.name;
  if (!iface.base_name.empty()) out << " : public " << iface.base_name;
  out << " {\n";
  out << " public:\n";
  if (iface.base_name.empty()) {
    out << "  virtual ~" << iface.name << "() = default;\n";
  } else {
    out << "  ~" << iface.name << "() override = default;\n";
  }
  for (const Method& method : iface.methods) {
    out << "  virtual " << CppType(method.return_type) << " " << PascalCase(method.name) << "(";
    for (size_t i = 0; i < method.params.size(); ++i) {
      if (i > 0) out << ", ";
      out << InterfaceClassParamType(method.params[i]) << " " << method.params[i].name;
    }
    out << ") = 0;\n";
  }
  for (const Attribute& attr : iface.attributes) {
    out << "  virtual " << CppType(attr.type) << " " << AttrGetterCppName(attr) << "() = 0;\n";
    if (!attr.readonly) {
      out << "  virtual void " << AttrSetterCppName(attr) << "(" << CppParamType(attr.type)
          << " value) = 0;\n";
    }
  }
  for (const Const& c : iface.consts) {
    out << "  static constexpr " << CppBaseType(c.type) << " " << c.name << " = "
        << ConstCppLiteral(c) << ";\n";
  }
  out << "};\n\n";
}

// TagBaseValue() (one shared, module-scoped function -- see
// EmitTagBaseFunction) is the runtime-allocated first tag of this whole
// module's forest; `local_index` is this interface's own compile-time
// pre-order position within it (see ComputeTagIndices) -- so its own tag
// is just their sum. The tag value itself is safe to share across isolates
// (see below); only the ObjectTemplate needs to be per-isolate.
void EmitTagAndTemplateGlobals(std::ostringstream& out, const Interface& iface, int local_index) {
  // The ObjectTemplate can NOT be shared across isolates -- confirmed by
  // reading WASMv8bindings/src/v8/template.cc's ObjectTemplate::NewInstance:
  // a wrappable instance is built via
  // `JS_NewObjectClass(ctx, isolate_->wrapper_class_id_for_wasmv8_internal())`,
  // using the *template's own* captured isolate_ (set once, in
  // ObjectTemplate::New(isolate)) -- and wrapper_class_id_ is a fresh
  // JSClassID allocated and registered on that specific isolate's own
  // JSRuntime (see WASMv8bindings/src/v8/isolate.cc's constructor). Handing
  // NewInstance() a Context from a *different* isolate would call
  // JS_NewObjectClass against that isolate's JSContext with a class id
  // that was only ever JS_NewClass'd on the *first* isolate's JSRuntime --
  // an unregistered-class use exactly like the one cpp_generator.cc's own
  // EmitCreateBindingFunction comment already documents as real, silent,
  // nondeterministic corruption for the quickjs backend's analogous
  // per-JSRuntime class-registration hazard. WASMv16 runs multiple
  // concurrent v8::Isolates (one per tab -- see WASMv16/include/wasmv16/
  // engine.h), so this isn't a theoretical concern. Keyed by Isolate*
  // instead, built lazily per isolate.
  //
  // The tag itself CAN be one process-wide value once computed: each
  // v8::Isolate owns its own separate CppHeapPointerTable (see
  // WASMv8bindings/include/v8-isolate.h), so identical tag *numbers* in two
  // different isolates' tables never collide with each other.
  out << "inline v8::CppHeapPointerTag g_v8_" << iface.name
      << "_tag = v8::CppHeapPointerTag::kNullTag;\n";
  out << "inline constexpr int kV8" << iface.name << "LocalIndex = " << local_index << ";\n";
  out << "inline std::unordered_map<v8::Isolate*, v8::Local<v8::ObjectTemplate>>& "
      << iface.name << "V8TemplatesByIsolate() {\n";
  out << "  static std::unordered_map<v8::Isolate*, v8::Local<v8::ObjectTemplate>> map;\n";
  out << "  return map;\n";
  out << "}\n";
  // Shared by EnsureV8XTemplate (the plain wrap-existing path) and, for a
  // constructible interface, EnsureV8XConstructorTemplate too -- either
  // one might run first depending on whether the embedder wraps an
  // existing impl or the first live use is `new X(...)` from JS.
  out << "inline void EnsureV8" << iface.name << "Tag() {\n";
  out << "  if (g_v8_" << iface.name << "_tag == v8::CppHeapPointerTag::kNullTag) {\n";
  out << "    g_v8_" << iface.name
      << "_tag = static_cast<v8::CppHeapPointerTag>(static_cast<uint16_t>(TagBaseValue()) + "
         "kV8" << iface.name << "LocalIndex);\n";
  out << "  }\n";
  out << "}\n\n";
}

// Converts each of `params` from `info[i]` (a plain positional index --
// used for both method calls and `new X(...)` constructors, whose argv
// share the same shape), returning the C++ expression to pass for each.
// Required params convert unconditionally; an optional one first checks
// `info.Length() > i`, defaulting to its declared literal (or `{}`) when
// the caller omitted it -- same two shapes cpp_generator.cc's own
// EmitParamConversions handles, minus variadic (not supported by this
// backend yet -- see CheckSupportedInterface).
std::vector<std::string> EmitParamConversions(
    std::ostringstream& out, const GenContext& ctx, const std::vector<Param>& params,
    const std::unordered_map<std::string, TagIndex>& tag_indices) {
  std::vector<std::string> call_args;
  for (size_t i = 0; i < params.size(); ++i) {
    const Param& param = params[i];
    const std::string source = "info[" + std::to_string(i) + "]";
    if (param.variadic) {
      // Must be the last param (grammar-enforced) -- collects every
      // trailing positional argument from `i` on into a std::vector, same
      // "no v8::Array, plain positional args" shape as
      // cpp_generator.cc's own variadic handling.
      out << "  std::vector<" << CppType(param.type) << "> " << param.name << ";\n";
      out << "  for (int " << param.name << "_i = " << i << "; " << param.name
          << "_i < info.Length(); ++" << param.name << "_i) {\n";
      EmitValueConversion(out, ctx, param.type, param.name + "_elem",
                         "info[" + param.name + "_i]", "isolate", tag_indices);
      out << "    " << param.name << ".push_back(std::move(" << param.name << "_elem));\n";
      out << "  }\n";
      call_args.push_back(param.name);
      break;  // variadic is always last
    }
    if (param.optional) {
      out << "  " << CppType(param.type) << " " << param.name << " = "
          << (param.has_default ? param.default_literal : "{}") << ";\n";
      out << "  if (info.Length() > " << i << ") {\n";
      EmitValueConversion(out, ctx, param.type, param.name + "_conv", source, "isolate",
                         tag_indices);
      out << "    " << param.name << " = std::move(" << param.name << "_conv);\n";
      out << "  }\n";
    } else {
      EmitValueConversion(out, ctx, param.type, param.name, source, "isolate", tag_indices);
    }
    call_args.push_back(param.name);
  }
  return call_args;
}

void EmitMethodCallback(std::ostringstream& out, const GenContext& ctx,
                        const std::string& iface_name, const Method& method,
                        const std::unordered_map<std::string, TagIndex>& tag_indices) {
  size_t required_count = RequiredArgCount(method.params);
  out << "inline void " << CFunctionName(iface_name, method.name)
      << "(const v8::FunctionCallbackInfo<v8::Value>& info) {\n";
  out << "  v8::Isolate* isolate = info.GetIsolate();\n";
  out << "  v8::Local<v8::Context> js_context = v8::Local<v8::Context>(v8::CurrentContext(isolate));\n";
  out << "  auto* impl = v8::Object::Unwrap<" << iface_name
      << ">(info.This(), v8::CppHeapPointerTagRange(g_v8_" << iface_name << "_tag));\n";
  out << "  if (!impl) { info.GetReturnValue().SetUndefined(); return; }\n";
  if (required_count > 0) {
    out << "  if (info.Length() < " << required_count << ") {\n";
    out << "    info.GetReturnValue().SetUndefined();\n";
    out << "    return;\n";
    out << "  }\n";
  }
  std::vector<std::string> call_args = EmitParamConversions(out, ctx, method.params, tag_indices);
  const bool has_return = !ReturnsVoid(method.return_type);
  out << "  " << (has_return ? "auto result = " : "") << "impl->" << PascalCase(method.name)
      << "(";
  for (size_t i = 0; i < call_args.size(); ++i) {
    if (i > 0) out << ", ";
    out << call_args[i];
  }
  out << ");\n";
  EmitReturnConversion(out, ctx, method.return_type, "info.GetReturnValue()", "result", "isolate",
                      "js_context", tag_indices);
  out << "}\n\n";
}

void EmitAttributeCallbacks(std::ostringstream& out, const GenContext& ctx,
                            const std::string& iface_name, const Attribute& attr,
                            const std::unordered_map<std::string, TagIndex>& tag_indices) {
  out << "inline void " << AttrGetterJsFunctionName(iface_name, attr.name)
      << "(v8::Local<v8::String>, const v8::PropertyCallbackInfo<v8::Value>& info) {\n";
  out << "  v8::Isolate* isolate = info.GetIsolate();\n";
  out << "  v8::Local<v8::Context> js_context = v8::Local<v8::Context>(v8::CurrentContext(isolate));\n";
  out << "  auto* impl = v8::Object::Unwrap<" << iface_name
      << ">(info.This(), v8::CppHeapPointerTagRange(g_v8_" << iface_name << "_tag));\n";
  out << "  if (!impl) { info.GetReturnValue().SetUndefined(); return; }\n";
  out << "  auto result = impl->" << AttrGetterCppName(attr) << "();\n";
  EmitReturnConversion(out, ctx, attr.type, "info.GetReturnValue()", "result", "isolate",
                      "js_context", tag_indices);
  out << "}\n\n";

  if (attr.readonly) return;  // ObjectTemplate::SetAccessor's setter defaults to
                              // nullptr for a readonly attribute -- see EnsureXTemplate.
  // The incoming JS value is named `js_new_value`, never the attribute's
  // own name -- an attribute literally named "value" (DOMTokenList.value,
  // HTMLInputElement.value, ...) would otherwise collide with a fixed
  // parameter name here, shadowing it (a real bug this exact naming
  // choice avoids, found generating examples/dom/dom.bruja's *.value
  // attributes through this backend for the first time).
  out << "inline void " << AttrSetterJsFunctionName(iface_name, attr.name)
      << "(v8::Local<v8::String>, v8::Local<v8::Value> js_new_value,\n";
  out << "                    const v8::PropertyCallbackInfo<void>& info) {\n";
  out << "  v8::Isolate* isolate = info.GetIsolate();\n";
  out << "  auto* impl = v8::Object::Unwrap<" << iface_name
      << ">(info.This(), v8::CppHeapPointerTagRange(g_v8_" << iface_name << "_tag));\n";
  out << "  if (!impl) return;\n";
  EmitValueConversion(out, ctx, attr.type, attr.name, "js_new_value", "isolate", tag_indices);
  out << "  impl->" << AttrSetterCppName(attr) << "(" << attr.name << ");\n";
  out << "}\n\n";
}

void EmitEnsureTemplateFunction(std::ostringstream& out, const GenContext& ctx,
                                const Interface& iface) {
  EffectiveMembers eff = CollectEffectiveMembers(ctx, iface);
  out << "// Builds this interface's ObjectTemplate for `isolate` (exposing\n";
  out << "// its own methods/attributes and every ancestor's, flattened) and\n";
  out << "// allocates its (process-wide) CppHeapPointerTag, both exactly once\n";
  out << "// (guarded by \"already built for this isolate\") -- mirrors\n";
  out << "// cpp_generator.cc's own g_X_class_id lazy-allocation pattern, but\n";
  out << "// keyed per v8::Isolate* -- see " << iface.name
      << "V8TemplatesByIsolate's comment\n";
  out << "// (EmitTagAndTemplateGlobals in cpp_generator_v8.cc) for why a single\n";
  out << "// shared ObjectTemplate across isolates would silently corrupt state.\n";
  out << "inline void EnsureV8" << iface.name << "Template(v8::Isolate* isolate) {\n";
  out << "  auto& templates = " << iface.name << "V8TemplatesByIsolate();\n";
  out << "  if (templates.count(isolate)) return;\n";
  out << "  EnsureV8" << iface.name << "Tag();\n";
  out << "  v8::Local<v8::ObjectTemplate> tmpl = v8::ObjectTemplate::New(isolate);\n";
  out << "  tmpl->SetInternalFieldCount(1);\n";
  for (const Method* m : eff.methods) {
    out << "  tmpl->Set(\"" << m->name << "\", v8::FunctionTemplate::New(isolate, "
        << CFunctionName(iface.name, m->name) << "));\n";
  }
  for (const Attribute* a : eff.attributes) {
    out << "  tmpl->SetAccessor(\"" << a->name << "\", "
        << AttrGetterJsFunctionName(iface.name, a->name);
    if (!a->readonly) out << ", " << AttrSetterJsFunctionName(iface.name, a->name);
    out << ");\n";
  }
  out << "  templates[isolate] = tmpl;\n";
  out << "}\n\n";
}

void EmitCreateBindingFunction(std::ostringstream& out, const GenContext& ctx,
                               const Interface& iface) {
  EffectiveMembers eff = CollectEffectiveMembers(ctx, iface);
  out << "// Wraps a non-owning `" << iface.name
      << "*` in a real V8-facade object exposing its (and its ancestors')\n";
  out << "// methods/attributes/consts under their JS-visible (lowerCamelCase)\n";
  out << "// names. `impl` must outlive the returned object -- same\n";
  out << "// non-owning-wrapper contract as cpp_generator.cc's CreateXBinding.\n";
  out << "// `context` must belong to `isolate` (real V8's own Context/Isolate\n";
  out << "// relationship).\n";
  out << "inline v8::Local<v8::Object> Create" << iface.name
      << "Binding(v8::Isolate* isolate, v8::Local<v8::Context> context, " << iface.name
      << "* impl) {\n";
  out << "  EnsureV8" << iface.name << "Template(isolate);\n";
  out << "  v8::Local<v8::Object> obj = " << iface.name
      << "V8TemplatesByIsolate()[isolate]->NewInstance(context).ToLocalChecked();\n";
  out << "  v8::Object::Wrap(isolate, obj, impl, g_v8_" << iface.name << "_tag);\n";
  for (const Const* c_ptr : eff.consts) {
    const Const& c = *c_ptr;
    out << "  obj->Set(isolate, \"" << c.name << "\", ";
    switch (c.type.kind) {
      case TypeKind::kBoolean:
        out << "v8::Boolean::New(isolate, " << ConstCppLiteral(c) << ")";
        break;
      case TypeKind::kDOMString:
      case TypeKind::kUSVString:
        out << "v8::String::NewFromUtf8(isolate, " << ConstCppLiteral(c)
            << ").ToLocalChecked()";
        break;
      default:
        out << "v8::Number::New(isolate, static_cast<double>(" << ConstCppLiteral(c) << "))";
        break;
    }
    out << ");\n";
  }
  out << "  return obj;\n";
  out << "}\n\n";
}

// This generator has no notion of a concrete implementation class for an
// interface (impls are always abstract, embedder-supplied) -- so a
// `constructor(...)` can't `new` anything on its own. Instead it exposes a
// factory-function *slot* the embedder fills in via InstallV8XConstructor
// (below); `new X(...)` from JS looks the factory up in that slot and
// calls it. Same "one shared global, replaced on re-install" idiom this
// generator already uses for the tag/template maps -- mirrors
// cpp_generator.cc's own EmitConstructorFactoryAlias.
void EmitConstructorFactoryAlias(std::ostringstream& out, const Interface& iface) {
  if (!iface.has_constructor) return;
  out << "using " << iface.name << "Factory = std::function<" << iface.name
      << "*(v8::Isolate*";
  for (const Param& param : iface.constructor_params) {
    out << ", " << CppParamType(param.type);
  }
  out << ")>;\n";
  out << "inline " << iface.name << "Factory& " << iface.name << "FactorySlot() {\n";
  out << "  static " << iface.name << "Factory factory;\n";
  out << "  return factory;\n";
  out << "}\n\n";
}

// `new X(...)`: converts argv exactly like a method call (EmitParamConversions),
// calls whatever factory InstallV8XConstructor installed, and wraps the
// (still non-owning) result directly onto info.This() -- the instance the
// facade's own constructor machinery already built via `new_target.prototype`
// (see WASMv8bindings/src/v8/template.cc's InvokeConstructor; ~that's also
// why this callback never touches info.GetReturnValue() -- InvokeConstructor
// always yields `instance` itself regardless of what a constructor callback
// returns, same real ECMAScript rule real V8 follows).
//
// A `new`-created instance needs the SAME effective methods/accessors as
// one wrapped via CreateXBinding, but reached through a *different* route:
// CreateXBinding's plain ObjectTemplate is never involved in a `new` call at
// all (the facade builds the instance itself, from `new_target.prototype`)
// -- so this interface's constructor FunctionTemplate gets its own
// PrototypeTemplate() populated with the identical member set, via a
// second, separate per-isolate template map (EnsureV8XConstructorTemplate).
void EmitConstructorFunctions(std::ostringstream& out, const GenContext& ctx,
                              const Interface& iface,
                              const std::unordered_map<std::string, TagIndex>& tag_indices) {
  if (!iface.has_constructor) return;
  EffectiveMembers eff = CollectEffectiveMembers(ctx, iface);
  size_t required_count = RequiredArgCount(iface.constructor_params);

  out << "inline void JsV8" << iface.name
      << "_construct(const v8::FunctionCallbackInfo<v8::Value>& info) {\n";
  out << "  v8::Isolate* isolate = info.GetIsolate();\n";
  if (required_count > 0) {
    out << "  if (info.Length() < " << required_count << ") {\n";
    out << "    info.GetReturnValue().SetUndefined();\n";
    out << "    return;\n";
    out << "  }\n";
  }
  std::vector<std::string> call_args =
      EmitParamConversions(out, ctx, iface.constructor_params, tag_indices);
  out << "  " << iface.name << "* impl = " << iface.name << "FactorySlot()(isolate";
  for (const std::string& arg : call_args) out << ", " << arg;
  out << ");\n";
  out << "  if (!impl) { info.GetReturnValue().SetUndefined(); return; }\n";
  out << "  EnsureV8" << iface.name << "Tag();\n";
  out << "  v8::Object::Wrap(isolate, info.This(), impl, g_v8_" << iface.name << "_tag);\n";
  out << "}\n\n";

  out << "inline std::unordered_map<v8::Isolate*, v8::Local<v8::FunctionTemplate>>& "
      << iface.name << "V8ConstructorsByIsolate() {\n";
  out << "  static std::unordered_map<v8::Isolate*, v8::Local<v8::FunctionTemplate>> map;\n";
  out << "  return map;\n";
  out << "}\n\n";

  out << "inline v8::Local<v8::FunctionTemplate> EnsureV8" << iface.name
      << "ConstructorTemplate(v8::Isolate* isolate) {\n";
  out << "  auto& ctors = " << iface.name << "V8ConstructorsByIsolate();\n";
  out << "  auto it = ctors.find(isolate);\n";
  out << "  if (it != ctors.end()) return it->second;\n";
  out << "  v8::Local<v8::FunctionTemplate> ctor = v8::FunctionTemplate::New(isolate, JsV8"
      << iface.name << "_construct);\n";
  out << "  ctor->InstanceTemplate()->SetInternalFieldCount(1);\n";
  out << "  v8::Local<v8::ObjectTemplate> proto_tmpl = ctor->PrototypeTemplate();\n";
  for (const Method* m : eff.methods) {
    out << "  proto_tmpl->Set(\"" << m->name << "\", v8::FunctionTemplate::New(isolate, "
        << CFunctionName(iface.name, m->name) << "));\n";
  }
  for (const Attribute* a : eff.attributes) {
    out << "  proto_tmpl->SetAccessor(\"" << a->name << "\", "
        << AttrGetterJsFunctionName(iface.name, a->name);
    if (!a->readonly) out << ", " << AttrSetterJsFunctionName(iface.name, a->name);
    out << ");\n";
  }
  out << "  ctors[isolate] = ctor;\n";
  out << "  return ctor;\n";
  out << "}\n\n";

  out << "// Installs a JS `" << iface.name
      << "` constructor function on `target` (typically the JS global\n";
  out << "// object) backed by `factory`; `new " << iface.name
      << "(...)` from JS calls it. `context`\n";
  out << "// must belong to `isolate`.\n";
  out << "inline void InstallV8" << iface.name
      << "Constructor(v8::Isolate* isolate, v8::Local<v8::Context> context,\n";
  out << "                                 v8::Local<v8::Object> target, " << iface.name
      << "Factory factory) {\n";
  out << "  " << iface.name << "FactorySlot() = std::move(factory);\n";
  out << "  v8::Local<v8::FunctionTemplate> ctor = EnsureV8" << iface.name
      << "ConstructorTemplate(isolate);\n";
  out << "  v8::Local<v8::Function> fn = ctor->GetFunction(context).ToLocalChecked();\n";
  out << "  target->Set(isolate, \"" << iface.name << "\", fn);\n";
  out << "}\n\n";
}

void EmitTeardownFunction(std::ostringstream& out, const Interface& iface) {
  out << "// Clears the ObjectTemplate(s) this interface built for `isolate`\n";
  out << "// (a no-op for whichever it never built there). Must be called\n";
  out << "// before that specific v8::Isolate is disposed -- a static\n";
  out << "// ObjectTemplate/FunctionTemplate left holding a live quickjs JSValue\n";
  out << "// past Dispose() aborts inside JS_FreeRuntime (see WASMExtWrench/\n";
  out << "// src/bindings/gfx_bindings.cc's file comment for the full story on\n";
  out << "// why this facade needs explicit teardown at all). In a\n";
  out << "// multi-isolate program (see EmitTagAndTemplateGlobals's comment),\n";
  out << "// call this once per isolate as each one is torn down -- it only\n";
  out << "// ever touches that one isolate's own entries, never any other\n";
  out << "// isolate's still-live templates.\n";
  out << "inline void Teardown" << iface.name << "V8Binding(v8::Isolate* isolate) {\n";
  out << "  " << iface.name << "V8TemplatesByIsolate().erase(isolate);\n";
  if (iface.has_constructor) {
    out << "  " << iface.name << "V8ConstructorsByIsolate().erase(isolate);\n";
  }
  out << "}\n\n";
}

// The whole module's forest shares one runtime-allocated tag block (see
// v8-sandbox.h's AllocateCppHeapPointerTagRange) -- allocated lazily, once,
// sized to cover every interface in the module so each interface's own
// [local_index, local_index] and any ancestor's [subtree_min, subtree_max]
// range (see ComputeTagIndices/TagRangeExpr) both land inside it.
void EmitTagBaseFunction(std::ostringstream& out, int interface_count) {
  out << "// Lazily allocates this module's whole tag block, once -- see\n";
  out << "// this file's top comment and v8-sandbox.h's\n";
  out << "// AllocateCppHeapPointerTagRange.\n";
  out << "inline v8::CppHeapPointerTag TagBaseValue() {\n";
  out << "  static v8::CppHeapPointerTag base = v8::AllocateCppHeapPointerTagRange("
      << interface_count << ");\n";
  out << "  return base;\n";
  out << "}\n\n";
}

}  // namespace

std::string GenerateV8CppHeader(const Module& module, const std::string& header_guard,
                                const std::string& cpp_namespace,
                                const std::string& source_filename) {
  for (const Interface& iface : module.interfaces) CheckSupportedInterface(iface);
  // `enum` declarations themselves need no codegen of their own: kEnumRef
  // maps directly to std::string wherever referenced (see CppBaseType's
  // kEnumRef case) -- unlike cpp_generator.cc, this backend doesn't
  // validate a string against the enum's declared values, since with no
  // Isolate::ThrowException() yet (see this file's top comment) an
  // invalid value couldn't surface as a real JS error anyway. A real,
  // documented simplification, not a hidden gap.
  for (const Dictionary& dict : module.dictionaries) {
    for (const DictField& f : dict.fields) {
      CheckSupportedType(f.type, "dictionary '" + dict.name + "' field '" + f.name + "'");
    }
  }
  // Mixins/`includes` need no check of their own: resolver.cc already
  // copies a mixin's members directly into every interface that
  // `includes` it, before codegen ever runs -- module.mixins/
  // module.includes_decls are invisible to codegen from here on, same as
  // cpp_generator.cc's own GenerateCppHeader never touches them either.
  for (const CallbackDecl& decl : module.callbacks) {
    CheckSupportedType(decl.return_type, "callback '" + decl.name + "'");
    for (const Param& p : decl.params) {
      CheckSupportedType(p.type, "callback '" + decl.name + "' parameter '" + p.name + "'");
    }
  }

  GenContext ctx = BuildContext(module);
  std::unordered_map<std::string, TagIndex> tag_indices = ComputeTagIndices(ctx);

  std::ostringstream out;
  out << "// Generated by brujac (--backend=v8) from " << source_filename << ". DO NOT EDIT.\n";
  out << "#ifndef " << header_guard << "\n";
  out << "#define " << header_guard << "\n\n";
  out << "#include \"v8.h\"\n\n";
  out << "#include <quickjs.h>\n\n";
  out << "#include <cstdint>\n";
  out << "#include <functional>\n";
  out << "#include <memory>\n";
  out << "#include <optional>\n";
  out << "#include <string>\n";
  out << "#include <unordered_map>\n";
  out << "#include <variant>\n";
  out << "#include <vector>\n\n";
  out << "namespace " << cpp_namespace << " {\n\n";

  // Forward declarations first -- interface class bodies can reference each
  // other (or a callback wrapper) by pointer/shared_ptr (e.g.
  // EventTarget::dispatchEvent(Event event), addEventListener's
  // std::shared_ptr<EventListenerCallback>) with no relation to the
  // base_name inheritance graph, so every name must exist before any class
  // body is emitted, not only the ones a topological base-sort would order
  // first. Same reasoning as cpp_generator.cc's own step 0.
  for (const Interface& iface : module.interfaces) out << "class " << iface.name << ";\n";
  for (const CallbackDecl& decl : module.callbacks) out << "class " << decl.name << "Callback;\n";
  for (const Dictionary& dict : module.dictionaries) out << "struct " << dict.name << ";\n";
  out << "\n";

  // CreateXBinding forward declarations too, this early -- a callback
  // wrapper's Call() (emitted right after interface classes, below) can
  // itself need to wrap an interface-typed argument for the JS function it
  // invokes (e.g. EventListener's `Event event` param), so these must be
  // visible before callback wrapper classes, not just before method/
  // attribute callbacks -- same reasoning as cpp_generator.cc's own step 4,
  // just earlier in this backend's overall ordering.
  for (const Interface& iface : module.interfaces) {
    out << "inline v8::Local<v8::Object> Create" << iface.name
        << "Binding(v8::Isolate* isolate, v8::Local<v8::Context> context, " << iface.name
        << "* impl);\n";
  }
  // WrapElementForJs (bruja_dom/dom_v8_impl.h -- hand-maintained, not
  // generated, see its own comment) is what EmitInterfaceReturn/
  // EmitBaseToJsVariable emit calls to instead of CreateElementBinding
  // for a statically-`Element`-typed return, so its real most-derived
  // type (HTMLImageElement, HTMLAnchorElement, ...) actually gets used.
  // Only a forward declaration here (this generated file's own functions
  // reference it before dom_v8_impl.h -- the one place it's actually
  // defined -- has been included); every real consumer includes that
  // header alongside this generated one, same as CreateElementBinding's
  // own real definition further down never needing dom_v8_impl.h either.
  for (const Interface& iface : module.interfaces) {
    if (iface.name == "Element") {
      out << "v8::Local<v8::Object> WrapElementForJs(v8::Isolate* isolate, "
             "v8::Local<v8::Context> context, Element* el);\n";
      break;
    }
  }
  out << "\n";

  EmitConversionHelpers(out);
  EmitTagBaseFunction(out, static_cast<int>(module.interfaces.size()));

  // Dictionary structs, base-before-derived (real C++ inheritance) --
  // before interface classes, since a pure-virtual method signature can
  // take one by const-ref (CustomEventInit needs EventInit already
  // complete for its own base-class subobject, same reasoning as
  // interfaces).
  for (const Dictionary* dict : TopoSortDictionariesByBase(ctx)) EmitDictionaryStruct(out, *dict);

  // Interface classes, base-before-derived (real C++ inheritance), each
  // immediately followed by its constructor factory alias/slot if it has
  // one (needs the class to already be a complete type).
  for (const Interface* iface : TopoSortByBase(ctx)) {
    EmitInterfaceClass(out, *iface);
    EmitConstructorFactoryAlias(out, *iface);
  }

  // Callback wrapper classes -- after interface classes (a pure-virtual
  // method signature only needs shared_ptr<XCallback> forward-declared,
  // already satisfied above) but before anything that actually
  // std::make_shared<XCallback>()s one, which needs it complete.
  for (const CallbackDecl& decl : module.callbacks) {
    EmitCallbackWrapperClass(out, ctx, decl, tag_indices);
  }

  for (const Interface& iface : module.interfaces) {
    EmitTagAndTemplateGlobals(out, iface, tag_indices.at(iface.name).local_index);
  }

  // Method/attribute JS callback functions, generated once per concrete
  // interface against its own class id -- including inherited members
  // (see cpp_generator.h's equivalent step for the quickjs backend).
  for (const Interface& iface : module.interfaces) {
    EffectiveMembers eff = CollectEffectiveMembers(ctx, iface);
    for (const Method* m : eff.methods) EmitMethodCallback(out, ctx, iface.name, *m, tag_indices);
    for (const Attribute* a : eff.attributes) {
      EmitAttributeCallbacks(out, ctx, iface.name, *a, tag_indices);
    }
  }
  for (const Interface& iface : module.interfaces) EmitEnsureTemplateFunction(out, ctx, iface);
  for (const Interface& iface : module.interfaces) EmitCreateBindingFunction(out, ctx, iface);
  // InstallXConstructor definitions (needs CreateXBinding's tag/template
  // machinery already declared/ensured above, and EffectiveMembers for the
  // constructor's own PrototypeTemplate).
  for (const Interface& iface : module.interfaces) {
    EmitConstructorFunctions(out, ctx, iface, tag_indices);
  }
  for (const Interface& iface : module.interfaces) EmitTeardownFunction(out, iface);

  out << "}  // namespace " << cpp_namespace << "\n\n";
  out << "#endif  // " << header_guard << "\n";
  return out.str();
}

}  // namespace bruja
