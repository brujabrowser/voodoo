#include "cpp_generator.h"

#include <cctype>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace bruja {
namespace {

// ---- Lookup context, built once per GenerateCppHeader call --------------

struct GenContext {
  const Module* module = nullptr;
  std::unordered_map<std::string, const Interface*> interfaces_by_name;
  std::unordered_map<std::string, const EnumDecl*> enums_by_name;
  std::unordered_map<std::string, const Dictionary*> dictionaries_by_name;
  // base interface name -> names of interfaces directly declaring it as base.
  std::unordered_map<std::string, std::vector<std::string>> children;
};

GenContext BuildContext(const Module& module) {
  GenContext ctx;
  ctx.module = &module;
  for (const Interface& iface : module.interfaces) {
    ctx.interfaces_by_name[iface.name] = &iface;
  }
  for (const EnumDecl& e : module.enums) ctx.enums_by_name[e.name] = &e;
  for (const Dictionary& d : module.dictionaries) {
    ctx.dictionaries_by_name[d.name] = &d;
  }
  for (const Interface& iface : module.interfaces) {
    if (!iface.base_name.empty()) ctx.children[iface.base_name].push_back(iface.name);
  }
  return ctx;
}

// Self + every transitive descendant (self first). Used to build the set
// of concrete classes a polymorphic interface-typed argument may actually
// be wrapping.
std::vector<std::string> DescendantsInclusive(const GenContext& ctx,
                                               const std::string& name) {
  std::vector<std::string> out;
  std::vector<std::string> stack{name};
  while (!stack.empty()) {
    std::string cur = stack.back();
    stack.pop_back();
    out.push_back(cur);
    auto it = ctx.children.find(cur);
    if (it != ctx.children.end()) {
      for (const std::string& child : it->second) stack.push_back(child);
    }
  }
  return out;
}

// Base-before-derived order (a valid linearization of the `base_name`
// forest), so every interface's C++ base class is a complete type by the
// time its own class body is emitted.
std::vector<const Interface*> TopoSortByBase(const GenContext& ctx) {
  std::vector<const Interface*> out;
  std::unordered_set<std::string> emitted;
  for (const Interface& iface : ctx.module->interfaces) {
    std::vector<const Interface*> chain;
    const Interface* cur = &iface;
    while (cur != nullptr && !emitted.count(cur->name)) {
      chain.push_back(cur);
      cur = cur->base_name.empty() ? nullptr
                                    : ctx.interfaces_by_name.at(cur->base_name);
    }
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
      if (emitted.insert((*it)->name).second) out.push_back(*it);
    }
  }
  return out;
}

// Self plus every ancestor, self first.
std::vector<const Interface*> AncestorsInclusive(const GenContext& ctx,
                                                  const Interface& iface) {
  std::vector<const Interface*> out;
  const Interface* cur = &iface;
  while (cur != nullptr) {
    out.push_back(cur);
    cur = cur->base_name.empty() ? nullptr
                                  : ctx.interfaces_by_name.at(cur->base_name);
  }
  return out;
}

// Same shape as TopoSortByBase, for dictionaries -- so
// `struct CustomEventInit : public EventInit` is emitted after EventInit
// is already a complete type.
std::vector<const Dictionary*> TopoSortDictionariesByBase(const GenContext& ctx) {
  std::vector<const Dictionary*> out;
  std::unordered_set<std::string> emitted;
  for (const Dictionary& dict : ctx.module->dictionaries) {
    std::vector<const Dictionary*> chain;
    const Dictionary* cur = &dict;
    while (cur != nullptr && !emitted.count(cur->name)) {
      chain.push_back(cur);
      cur = cur->base_name.empty() ? nullptr
                                    : ctx.dictionaries_by_name.at(cur->base_name);
    }
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
      if (emitted.insert((*it)->name).second) out.push_back(*it);
    }
  }
  return out;
}

// Self plus every ancestor dictionary's fields -- all read off the same
// flat JS object (a dictionary's inheritance is a JS-level illusion; the
// caller passes one object spanning the whole chain). Same shape as
// CollectEffectiveMembers.
std::vector<const DictField*> CollectEffectiveFields(const GenContext& ctx,
                                                       const Dictionary& dict) {
  std::vector<const DictField*> out;
  const Dictionary* cur = &dict;
  std::vector<const Dictionary*> ancestors;
  while (cur != nullptr) {
    ancestors.push_back(cur);
    cur = cur->base_name.empty() ? nullptr : ctx.dictionaries_by_name.at(cur->base_name);
  }
  for (auto it = ancestors.rbegin(); it != ancestors.rend(); ++it) {
    for (const DictField& f : (*it)->fields) out.push_back(&f);
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
// scoped to `iface`'s own class id (see cpp_generator.h).
EffectiveMembers CollectEffectiveMembers(const GenContext& ctx,
                                          const Interface& iface) {
  EffectiveMembers out;
  for (const Interface* anc : AncestorsInclusive(ctx, iface)) {
    for (const Method& m : anc->methods) out.methods.push_back(&m);
    for (const Attribute& a : anc->attributes) out.attributes.push_back(&a);
    for (const Const& c : anc->consts) out.consts.push_back(&c);
  }
  return out;
}

// The pure-virtual method's C++ return type is `void` either because the
// IDL return type literally is `void`, or because it's `Promise<void>`
// (Promise<T> unwraps to T at the C++ level -- see cpp_generator.h).
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

std::string PascalCase(const std::string& name) {
  if (name.empty()) return name;
  std::string out = name;
  out[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(out[0])));
  return out;
}

// ---- C++ type spelling ----------------------------------------------------

bool UsesOptionalWrapper(TypeKind kind) {
  switch (kind) {
    case TypeKind::kInterfaceRef:
    case TypeKind::kAny:
    case TypeKind::kDictionaryRef:
    case TypeKind::kCallbackRef:
    case TypeKind::kVoid:
    case TypeKind::kUnresolvedRef:
    case TypeKind::kPromise:  // unwrapped to its inner type before this matters
      return false;
    default:
      return true;
  }
}

// Forward declared: CppBaseType's kUnion case needs the full (possibly
// std::optional-wrapped) spelling of each member, i.e. CppType, not
// CppBaseType.
std::string CppType(const TypeSpec& type, const GenContext& ctx);

std::string CppBaseType(const TypeSpec& type, const GenContext& ctx) {
  switch (type.kind) {
    case TypeKind::kVoid:
      return "void";
    case TypeKind::kBoolean:
      return "bool";
    case TypeKind::kByte:
      return "int8_t";
    case TypeKind::kOctet:
      return "uint8_t";
    case TypeKind::kShort:
      return "int16_t";
    case TypeKind::kUnsignedShort:
      return "uint16_t";
    case TypeKind::kLong:
      return "int32_t";
    case TypeKind::kUnsignedLong:
      return "uint32_t";
    case TypeKind::kLongLong:
      return "int64_t";
    case TypeKind::kUnsignedLongLong:
      return "uint64_t";
    case TypeKind::kFloat:
      return "float";
    case TypeKind::kDouble:
      return "double";
    case TypeKind::kDOMString:
    case TypeKind::kUSVString:
      return "std::string";
    case TypeKind::kAny:
      return "JSValue";
    case TypeKind::kSequence:
      return "std::vector<" + CppBaseType(*type.element_type, ctx) + ">";
    case TypeKind::kInterfaceRef:
      return type.ref_name + "*";
    case TypeKind::kEnumRef:
      return "std::string";
    case TypeKind::kDictionaryRef:
      return type.ref_name;
    case TypeKind::kCallbackRef:
      return "std::shared_ptr<" + type.ref_name + "Callback>";
    case TypeKind::kPromise:
      // The pure-virtual method returns the inner type synchronously; only
      // the JS-facing wrapper (EmitReturnConversion) deals in real
      // JS Promise objects. Unwrap here so a caller that forgets to unwrap
      // still gets something sane rather than "void".
      return CppBaseType(*type.element_type, ctx);
    case TypeKind::kUnion: {
      std::string s = "std::variant<";
      for (size_t i = 0; i < type.union_members->size(); ++i) {
        if (i > 0) s += ", ";
        s += CppType((*type.union_members)[i], ctx);
      }
      s += ">";
      return s;
    }
    case TypeKind::kUnresolvedRef:
      return "void";  // unreachable post-Resolve()
  }
  return "void";
}

// The type as it appears in a variable declaration / return type: wraps
// nullable scalars/strings/sequences/enums in std::optional (interface
// pointers are nullable-capable already; any/dictionary/callback don't
// support nullable in this generator).
std::string CppType(const TypeSpec& type, const GenContext& ctx) {
  std::string base = CppBaseType(type, ctx);
  if (type.nullable && UsesOptionalWrapper(type.kind)) {
    return "std::optional<" + base + ">";
  }
  return base;
}

// The type as it appears in a pure-virtual method/setter parameter: by
// const-ref for anything heavier than a scalar/pointer/JSValue.
std::string CppParamType(const TypeSpec& type, const GenContext& ctx) {
  std::string t = CppType(type, ctx);
  bool by_ref = type.kind == TypeKind::kDOMString || type.kind == TypeKind::kUSVString ||
                type.kind == TypeKind::kEnumRef || type.kind == TypeKind::kSequence ||
                type.kind == TypeKind::kDictionaryRef ||
                (type.nullable && UsesOptionalWrapper(type.kind));
  return by_ref ? ("const " + t + "&") : t;
}

std::string InterfaceClassParamType(const Param& param, const GenContext& ctx) {
  if (param.variadic) return "const std::vector<" + CppType(param.type, ctx) + ">&";
  return CppParamType(param.type, ctx);
}

// ---- JS -> C++ value conversion --------------------------------------------

void EmitValueConversion(std::ostringstream& out, const GenContext& ctx,
                          const TypeSpec& type, const std::string& out_var,
                          const std::string& source);

// Interface-typed values are the one case that's inherently polymorphic:
// a JS wrapper's opaque pointer was stored under *some* concrete
// interface's class id, and any of X's descendants (computed here from the
// whole module's inheritance graph) is an acceptable X. Probes each
// candidate's class id in turn with the non-throwing JS_GetOpaque (not
// JS_GetOpaque2 -- a mismatch here isn't necessarily an error, just "try
// the next candidate"), casting through the exact matched type so C++
// applies any base-subobject pointer adjustment correctly. A JS `null`
// always becomes `nullptr` here regardless of the type's own declared
// nullability -- a minor, deliberate simplification (see README).
void EmitInterfaceProbe(std::ostringstream& out, const GenContext& ctx,
                         const TypeSpec& type, const std::string& out_var,
                         const std::string& source) {
  out << "  " << type.ref_name << "* " << out_var << " = nullptr;\n";
  out << "  if (!JS_IsNull(" << source << ")) {\n";
  out << "    void* " << out_var << "_opaque = nullptr;\n";
  for (const std::string& cand : DescendantsInclusive(ctx, type.ref_name)) {
    out << "    if (!" << out_var << ") {\n";
    out << "      " << out_var << "_opaque = JS_GetOpaque(" << source << ", g_"
        << cand << "_class_id);\n";
    out << "      if (" << out_var << "_opaque) " << out_var
        << " = static_cast<" << type.ref_name << "*>(static_cast<" << cand
        << "*>(" << out_var << "_opaque));\n";
    out << "    }\n";
  }
  out << "    if (!" << out_var << ") return JS_ThrowTypeError(ctx, \"expected a "
      << type.ref_name << "\");\n";
  out << "  }\n";
}

// Union types (`(A or B)`) are parameter-only -- see cpp_generator.h.
// Interface-typed members are tried first via the same non-throwing
// JS_GetOpaque descendant probing EmitInterfaceProbe uses (its own loop
// here, since a non-match must fall through to the next member instead
// of throwing); the first interface member that matches wins. The first
// *non*-interface member (if any) is tried last as the fallback. This
// covers this generator's real target shape -- `(InterfaceA or ... or
// DOMString)` -- not the full Web IDL "distinguishable types" algorithm
// for arbitrary multi-primitive unions.
void EmitUnionValueConversion(std::ostringstream& out, const GenContext& ctx,
                               const TypeSpec& type, const std::string& out_var,
                               const std::string& source) {
  out << "  " << CppBaseType(type, ctx) << " " << out_var << ";\n";
  out << "  {\n";
  out << "    bool " << out_var << "_matched = false;\n";
  const TypeSpec* fallback = nullptr;
  for (const TypeSpec& member : *type.union_members) {
    if (member.kind == TypeKind::kInterfaceRef) {
      out << "    if (!" << out_var << "_matched) {\n";
      out << "      void* " << out_var << "_opaque = nullptr;\n";
      for (const std::string& cand : DescendantsInclusive(ctx, member.ref_name)) {
        out << "      if (!" << out_var << "_opaque) {\n";
        out << "        " << out_var << "_opaque = JS_GetOpaque(" << source << ", g_"
            << cand << "_class_id);\n";
        out << "        if (" << out_var << "_opaque) { " << out_var
            << " = static_cast<" << member.ref_name << "*>(static_cast<" << cand
            << "*>(" << out_var << "_opaque)); " << out_var << "_matched = true; }\n";
        out << "      }\n";
      }
      out << "    }\n";
    } else if (fallback == nullptr) {
      fallback = &member;
    }
  }
  if (fallback != nullptr) {
    out << "    if (!" << out_var << "_matched) {\n";
    EmitValueConversion(out, ctx, *fallback, out_var + "_fallback", source);
    out << "      " << out_var << " = std::move(" << out_var << "_fallback);\n";
    out << "      " << out_var << "_matched = true;\n";
    out << "    }\n";
  }
  out << "    if (!" << out_var << "_matched) {\n";
  out << "      return JS_ThrowTypeError(ctx, \"value doesn't match any union member\");\n";
  out << "    }\n";
  out << "  }\n";
}

void EmitDictionaryFromJs(std::ostringstream& out, const GenContext& ctx,
                           const TypeSpec& type, const std::string& out_var,
                           const std::string& source) {
  const Dictionary* dict = ctx.dictionaries_by_name.at(type.ref_name);
  out << "  " << type.ref_name << " " << out_var << ";\n";
  out << "  {\n";
  // Inherited fields (if any) come from the same flat JS object as this
  // dictionary's own -- see CollectEffectiveFields.
  for (const DictField* field_ptr : CollectEffectiveFields(ctx, *dict)) {
    const DictField& field = *field_ptr;
    std::string field_js = out_var + "_" + field.name + "_js";
    out << "    JSValue " << field_js << " = JS_GetPropertyStr(ctx, " << source
        << ", \"" << field.name << "\");\n";
    out << "    if (!JS_IsUndefined(" << field_js << ")) {\n";
    EmitValueConversion(out, ctx, field.type, out_var + "_" + field.name + "_val",
                         field_js);
    out << "      " << out_var << "." << field.name << " = std::move("
        << out_var << "_" << field.name << "_val);\n";
    out << "    }\n";
    out << "    JS_FreeValue(ctx, " << field_js << ");\n";
  }
  out << "  }\n";
}

// Converts a non-nullable, non-interface-typed JS value into a new C++
// local named `out_var`.
void EmitBaseValueConversion(std::ostringstream& out, const GenContext& ctx,
                              const TypeSpec& type, const std::string& out_var,
                              const std::string& source) {
  switch (type.kind) {
    case TypeKind::kVoid:
    case TypeKind::kUnresolvedRef:
    case TypeKind::kInterfaceRef:
    case TypeKind::kPromise:
      break;  // never reached here (kInterfaceRef handled by the caller;
              // kPromise is return-type-only, see EmitReturnConversion)
    case TypeKind::kBoolean: {
      out << "  int " << out_var << "_raw = JS_ToBool(ctx, " << source << ");\n";
      out << "  if (" << out_var << "_raw < 0) return JS_EXCEPTION;\n";
      out << "  bool " << out_var << " = " << out_var << "_raw != 0;\n";
      break;
    }
    case TypeKind::kByte:
    case TypeKind::kShort:
    case TypeKind::kLong: {
      out << "  int32_t " << out_var << "_raw = 0;\n";
      out << "  if (JS_ToInt32(ctx, &" << out_var << "_raw, " << source
          << ")) return JS_EXCEPTION;\n";
      out << "  " << CppBaseType(type, ctx) << " " << out_var
          << " = static_cast<" << CppBaseType(type, ctx) << ">(" << out_var
          << "_raw);\n";
      break;
    }
    case TypeKind::kOctet:
    case TypeKind::kUnsignedShort: {
      out << "  uint32_t " << out_var << "_raw = 0;\n";
      out << "  if (JS_ToUint32(ctx, &" << out_var << "_raw, " << source
          << ")) return JS_EXCEPTION;\n";
      out << "  " << CppBaseType(type, ctx) << " " << out_var
          << " = static_cast<" << CppBaseType(type, ctx) << ">(" << out_var
          << "_raw);\n";
      break;
    }
    case TypeKind::kUnsignedLong: {
      out << "  uint32_t " << out_var << " = 0;\n";
      out << "  if (JS_ToUint32(ctx, &" << out_var << ", " << source
          << ")) return JS_EXCEPTION;\n";
      break;
    }
    case TypeKind::kLongLong: {
      out << "  int64_t " << out_var << " = 0;\n";
      out << "  if (JS_ToInt64(ctx, &" << out_var << ", " << source
          << ")) return JS_EXCEPTION;\n";
      break;
    }
    case TypeKind::kUnsignedLongLong: {
      out << "  int64_t " << out_var << "_raw = 0;\n";
      out << "  if (JS_ToIndex(ctx, &" << out_var << "_raw, " << source
          << ")) return JS_EXCEPTION;\n";
      out << "  uint64_t " << out_var << " = static_cast<uint64_t>(" << out_var
          << "_raw);\n";
      break;
    }
    case TypeKind::kFloat:
    case TypeKind::kDouble: {
      out << "  double " << out_var << "_raw = 0;\n";
      out << "  if (JS_ToFloat64(ctx, &" << out_var << "_raw, " << source
          << ")) return JS_EXCEPTION;\n";
      out << "  " << CppBaseType(type, ctx) << " " << out_var
          << " = static_cast<" << CppBaseType(type, ctx) << ">(" << out_var
          << "_raw);\n";
      break;
    }
    case TypeKind::kDOMString:
    case TypeKind::kUSVString: {
      out << "  const char* " << out_var << "_cstr = JS_ToCString(ctx, "
          << source << ");\n";
      out << "  if (!" << out_var << "_cstr) return JS_EXCEPTION;\n";
      out << "  std::string " << out_var << "(" << out_var << "_cstr);\n";
      out << "  JS_FreeCString(ctx, " << out_var << "_cstr);\n";
      break;
    }
    case TypeKind::kAny: {
      out << "  JSValue " << out_var << " = JS_DupValue(ctx, " << source << ");\n";
      break;
    }
    case TypeKind::kSequence: {
      out << "  " << CppBaseType(type, ctx) << " " << out_var << ";\n";
      out << "  {\n";
      out << "    int64_t " << out_var << "_len = 0;\n";
      out << "    JSValue " << out_var << "_len_val = JS_GetPropertyStr(ctx, "
          << source << ", \"length\");\n";
      out << "    if (JS_ToInt64(ctx, &" << out_var << "_len, " << out_var
          << "_len_val)) { JS_FreeValue(ctx, " << out_var
          << "_len_val); return JS_EXCEPTION; }\n";
      out << "    JS_FreeValue(ctx, " << out_var << "_len_val);\n";
      out << "    for (int64_t " << out_var << "_i = 0; " << out_var << "_i < "
          << out_var << "_len; ++" << out_var << "_i) {\n";
      out << "      JSValue " << out_var << "_elem_js = JS_GetPropertyUint32(ctx, "
          << source << ", static_cast<uint32_t>(" << out_var << "_i));\n";
      EmitValueConversion(out, ctx, *type.element_type, out_var + "_elem",
                           out_var + "_elem_js");
      out << "      JS_FreeValue(ctx, " << out_var << "_elem_js);\n";
      out << "      " << out_var << ".push_back(std::move(" << out_var
          << "_elem));\n";
      out << "    }\n";
      out << "  }\n";
      break;
    }
    case TypeKind::kEnumRef: {
      out << "  const char* " << out_var << "_cstr = JS_ToCString(ctx, "
          << source << ");\n";
      out << "  if (!" << out_var << "_cstr) return JS_EXCEPTION;\n";
      out << "  std::string " << out_var << "(" << out_var << "_cstr);\n";
      out << "  JS_FreeCString(ctx, " << out_var << "_cstr);\n";
      const EnumDecl* decl = ctx.enums_by_name.at(type.ref_name);
      out << "  if (";
      for (size_t i = 0; i < decl->values.size(); ++i) {
        if (i > 0) out << " && ";
        out << out_var << " != \"" << decl->values[i] << "\"";
      }
      out << ") {\n";
      out << "    return JS_ThrowTypeError(ctx, \"invalid value for enum "
          << type.ref_name << "\");\n";
      out << "  }\n";
      break;
    }
    case TypeKind::kDictionaryRef:
      EmitDictionaryFromJs(out, ctx, type, out_var, source);
      break;
    case TypeKind::kCallbackRef:
      out << "  auto " << out_var << " = std::make_shared<" << type.ref_name
          << "Callback>(ctx, " << source << ");\n";
      break;
    case TypeKind::kUnion:
      EmitUnionValueConversion(out, ctx, type, out_var, source);
      break;
  }
}

void EmitValueConversion(std::ostringstream& out, const GenContext& ctx,
                          const TypeSpec& type, const std::string& out_var,
                          const std::string& source) {
  if (type.kind == TypeKind::kInterfaceRef) {
    EmitInterfaceProbe(out, ctx, type, out_var, source);
    return;
  }
  if (type.nullable) {
    out << "  " << CppType(type, ctx) << " " << out_var << " = std::nullopt;\n";
    out << "  if (!JS_IsNull(" << source << ") && !JS_IsUndefined(" << source
        << ")) {\n";
    TypeSpec non_null = type;
    non_null.nullable = false;
    EmitBaseValueConversion(out, ctx, non_null, out_var + "_present", source);
    out << "    " << out_var << " = std::move(" << out_var << "_present);\n";
    out << "  }\n";
    return;
  }
  EmitBaseValueConversion(out, ctx, type, out_var, source);
}

// ---- C++ -> JS value conversion --------------------------------------------

void EmitToJs(std::ostringstream& out, const GenContext& ctx,
              const TypeSpec& type, const std::string& out_var,
              const std::string& cpp_expr);

// Interface-typed return/attribute values always wrap via the statically
// declared type's own CreateXBinding -- not the value's real most-derived
// type. This binding generator has no dynamic wrapper-identity registry
// (see cpp_generator.h), so e.g. `Node* parentNode()` always yields a
// plain Node-shaped JS wrapper even when the real object is an Element.
void EmitInterfaceToJs(std::ostringstream& out, const TypeSpec& type,
                        const std::string& out_var,
                        const std::string& cpp_expr) {
  out << "  JSValue " << out_var << " = (" << cpp_expr << " == nullptr) ? JS_NULL"
      << " : Create" << type.ref_name << "Binding(ctx, " << cpp_expr << ");\n";
}

void EmitBaseToJs(std::ostringstream& out, const GenContext& ctx,
                   const TypeSpec& type, const std::string& out_var,
                   const std::string& cpp_expr) {
  switch (type.kind) {
    case TypeKind::kVoid:
    case TypeKind::kUnresolvedRef:
    case TypeKind::kInterfaceRef:
    case TypeKind::kPromise:
      break;  // never reached here (kInterfaceRef handled by the caller;
              // kPromise is return-type-only, see EmitReturnConversion)
    case TypeKind::kBoolean:
      out << "  JSValue " << out_var << " = JS_NewBool(ctx, " << cpp_expr << ");\n";
      break;
    case TypeKind::kByte:
    case TypeKind::kShort:
    case TypeKind::kLong:
      out << "  JSValue " << out_var << " = JS_NewInt32(ctx, static_cast<int32_t>("
          << cpp_expr << "));\n";
      break;
    case TypeKind::kOctet:
    case TypeKind::kUnsignedShort:
    case TypeKind::kUnsignedLong:
      out << "  JSValue " << out_var
          << " = JS_NewUint32(ctx, static_cast<uint32_t>(" << cpp_expr << "));\n";
      break;
    case TypeKind::kLongLong:
      out << "  JSValue " << out_var << " = JS_NewInt64(ctx, static_cast<int64_t>("
          << cpp_expr << "));\n";
      break;
    case TypeKind::kUnsignedLongLong:
    case TypeKind::kFloat:
    case TypeKind::kDouble:
      out << "  JSValue " << out_var << " = JS_NewFloat64(ctx, static_cast<double>("
          << cpp_expr << "));\n";
      break;
    case TypeKind::kDOMString:
    case TypeKind::kUSVString:
    case TypeKind::kEnumRef:
      out << "  JSValue " << out_var << " = JS_NewStringLen(ctx, (" << cpp_expr
          << ").data(), (" << cpp_expr << ").size());\n";
      break;
    case TypeKind::kAny:
      out << "  JSValue " << out_var << " = JS_DupValue(ctx, " << cpp_expr << ");\n";
      break;
    case TypeKind::kSequence: {
      out << "  JSValue " << out_var << " = JS_NewArray(ctx);\n";
      out << "  {\n";
      out << "    uint32_t " << out_var << "_i = 0;\n";
      out << "    for (const auto& " << out_var << "_elem : " << cpp_expr
          << ") {\n";
      EmitToJs(out, ctx, *type.element_type, out_var + "_elem_js",
               out_var + "_elem");
      out << "      JS_SetPropertyUint32(ctx, " << out_var << ", " << out_var
          << "_i++, " << out_var << "_elem_js);\n";
      out << "    }\n";
      out << "  }\n";
      break;
    }
    case TypeKind::kDictionaryRef: {
      // Mirror of EmitDictionaryFromJs, other direction: build a plain JS
      // object and set each effective field (own + inherited, same flat
      // shape EmitDictionaryFromJs reads) by converting the C++ member with
      // EmitToJs. Lets a dictionary be used as a method/attribute return
      // type, not just a parameter type.
      const Dictionary* dict = ctx.dictionaries_by_name.at(type.ref_name);
      out << "  JSValue " << out_var << " = JS_NewObject(ctx);\n";
      for (const DictField* field_ptr : CollectEffectiveFields(ctx, *dict)) {
        const DictField& field = *field_ptr;
        std::string field_js = out_var + "_" + field.name + "_js";
        EmitToJs(out, ctx, field.type, field_js,
                 "(" + cpp_expr + ")." + field.name);
        out << "  JS_SetPropertyStr(ctx, " << out_var << ", \"" << field.name
            << "\", " << field_js << ");\n";
      }
      break;
    }
    case TypeKind::kCallbackRef:
      // Callbacks are param-only in this generator -- never returned.
      out << "  JSValue " << out_var << " = JS_UNDEFINED;\n";
      break;
    case TypeKind::kUnion:
      // Union types are param-only in this generator (see cpp_generator.h)
      // -- nothing in the target content returns one, so the C++->JS
      // (std::visit-based) direction isn't built yet.
      out << "  JSValue " << out_var << " = JS_UNDEFINED;\n";
      break;
  }
}

void EmitToJs(std::ostringstream& out, const GenContext& ctx,
              const TypeSpec& type, const std::string& out_var,
              const std::string& cpp_expr) {
  if (type.kind == TypeKind::kInterfaceRef) {
    EmitInterfaceToJs(out, type, out_var, cpp_expr);
    return;
  }
  if (type.nullable) {
    out << "  JSValue " << out_var << ";\n";
    out << "  if (!(" << cpp_expr << ").has_value()) {\n";
    out << "    " << out_var << " = JS_NULL;\n";
    out << "  } else {\n";
    EmitBaseToJs(out, ctx, type, out_var + "_present", "(*(" + cpp_expr + "))");
    out << "    " << out_var << " = " << out_var << "_present;\n";
    out << "  }\n";
    return;
  }
  EmitBaseToJs(out, ctx, type, out_var, cpp_expr);
}

// Promise<T> methods stay fully synchronous at the C++ level (the impl
// still just returns T, like every other method) -- only the JS-facing
// wrapper differs, building an already-resolved Promise via
// JS_NewPromiseCapability and resolving it immediately with the converted
// value. Same "deliberately simpler, no async plumbing" spirit as
// `callback` (see cpp_generator.h).
void EmitPromiseReturnConversion(std::ostringstream& out, const GenContext& ctx,
                                  const TypeSpec& type, const std::string& result) {
  out << "  JSValue resolving_funcs[2];\n";
  out << "  JSValue promise = JS_NewPromiseCapability(ctx, resolving_funcs);\n";
  out << "  if (JS_IsException(promise)) return promise;\n";
  if (type.element_type->kind == TypeKind::kVoid) {
    out << "  JSValueConst resolve_argv[] = {JS_UNDEFINED};\n";
    out << "  JSValue resolve_result = JS_Call(ctx, resolving_funcs[0], JS_UNDEFINED, 1, "
           "resolve_argv);\n";
  } else {
    EmitToJs(out, ctx, *type.element_type, "js_result", result);
    out << "  JSValueConst resolve_argv[] = {js_result};\n";
    out << "  JSValue resolve_result = JS_Call(ctx, resolving_funcs[0], JS_UNDEFINED, 1, "
           "resolve_argv);\n";
    out << "  JS_FreeValue(ctx, js_result);\n";
  }
  out << "  JS_FreeValue(ctx, resolve_result);\n";
  out << "  JS_FreeValue(ctx, resolving_funcs[0]);\n";
  out << "  JS_FreeValue(ctx, resolving_funcs[1]);\n";
  out << "  return promise;\n";
}

void EmitReturnConversion(std::ostringstream& out, const GenContext& ctx,
                           const TypeSpec& type, const std::string& result) {
  if (type.kind == TypeKind::kVoid) {
    out << "  return JS_UNDEFINED;\n";
    return;
  }
  if (type.kind == TypeKind::kPromise) {
    EmitPromiseReturnConversion(out, ctx, type, result);
    return;
  }
  EmitToJs(out, ctx, type, "js_result", result);
  out << "  return js_result;\n";
}

std::string ConstJsCtor(const Const& c) {
  switch (c.type.kind) {
    case TypeKind::kBoolean:
      return "JS_NewBool(ctx, " + c.value_literal + ")";
    case TypeKind::kByte:
    case TypeKind::kShort:
    case TypeKind::kLong:
      return "JS_NewInt32(ctx, " + c.value_literal + ")";
    case TypeKind::kOctet:
    case TypeKind::kUnsignedShort:
    case TypeKind::kUnsignedLong:
      return "JS_NewUint32(ctx, " + c.value_literal + ")";
    case TypeKind::kLongLong:
      return "JS_NewInt64(ctx, " + c.value_literal + ")";
    case TypeKind::kUnsignedLongLong:
    case TypeKind::kFloat:
    case TypeKind::kDouble:
      return "JS_NewFloat64(ctx, " + c.value_literal + ")";
    case TypeKind::kDOMString:
    case TypeKind::kUSVString:
    case TypeKind::kEnumRef:
      return "JS_NewString(ctx, " + c.value_literal + ")";
    default:
      return "JS_UNDEFINED";
  }
}

// ---- Naming ----------------------------------------------------------------

std::string CFunctionName(const std::string& concrete_iface,
                           const std::string& method_name) {
  return "Js" + concrete_iface + "_" + method_name;
}

std::string AttrGetterCppName(const Attribute& attr) { return PascalCase(attr.name); }

std::string AttrSetterCppName(const Attribute& attr) {
  return "Set" + PascalCase(attr.name);
}

std::string AttrGetterJsFunctionName(const std::string& concrete_iface,
                                      const std::string& attr_name) {
  return "Js" + concrete_iface + "_get_" + attr_name;
}

std::string AttrSetterJsFunctionName(const std::string& concrete_iface,
                                      const std::string& attr_name) {
  return "Js" + concrete_iface + "_set_" + attr_name;
}

// ---- Emitters ---------------------------------------------------------------

void EmitDictionaryStruct(std::ostringstream& out, const GenContext& ctx,
                           const Dictionary& dict) {
  out << "struct " << dict.name;
  if (!dict.base_name.empty()) out << " : public " << dict.base_name;
  out << " {\n";
  // Own fields only -- inherited ones already exist via the C++ base,
  // same as EmitInterfaceClass only declaring an interface's own newly-
  // introduced members.
  for (const DictField& field : dict.fields) {
    out << "  " << CppType(field.type, ctx) << " " << field.name;
    if (field.has_default) out << " = " << field.default_literal;
    out << ";\n";
  }
  out << "};\n\n";
}

void EmitInterfaceClass(std::ostringstream& out, const GenContext& ctx,
                         const Interface& iface) {
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
    out << "  virtual " << CppType(method.return_type, ctx) << " "
        << PascalCase(method.name) << "(";
    for (size_t i = 0; i < method.params.size(); ++i) {
      if (i > 0) out << ", ";
      out << InterfaceClassParamType(method.params[i], ctx) << " "
          << method.params[i].name;
    }
    out << ") = 0;\n";
  }
  for (const Attribute& attr : iface.attributes) {
    out << "  virtual " << CppType(attr.type, ctx) << " " << AttrGetterCppName(attr)
        << "() = 0;\n";
    if (!attr.readonly) {
      out << "  virtual void " << AttrSetterCppName(attr) << "("
          << CppParamType(attr.type, ctx) << " value) = 0;\n";
    }
  }
  for (const Const& c : iface.consts) {
    out << "  static constexpr " << CppBaseType(c.type, ctx) << " " << c.name
        << " = " << c.value_literal << ";\n";
  }
  out << "};\n\n";
}

// This generator has no notion of a concrete implementation class for an
// interface (impls are always abstract, embedder-supplied) -- so a
// `constructor(...)` can't `new` anything on its own. Instead it exposes a
// factory-function *slot* the embedder fills in via InstallXConstructor
// (below); `new X(...)` from JS looks the factory up in that slot and
// calls it. Same "one shared global" idiom this generator already uses
// for `g_X_class_id`, not a new pattern -- calling InstallXConstructor
// again simply replaces the factory, process-wide, not per-JSContext.
void EmitConstructorFactoryAlias(std::ostringstream& out, const GenContext& ctx,
                                  const Interface& iface) {
  if (!iface.has_constructor) return;
  out << "using " << iface.name << "Factory = std::function<" << iface.name
      << "*(JSContext*";
  for (const Param& param : iface.constructor_params) {
    out << ", "
        << (param.variadic ? ("const std::vector<" + CppType(param.type, ctx) + ">&")
                            : CppParamType(param.type, ctx));
  }
  out << ")>;\n";
  out << "inline " << iface.name << "Factory& " << iface.name << "FactorySlot() {\n";
  out << "  static " << iface.name << "Factory factory;\n";
  out << "  return factory;\n";
  out << "}\n\n";
}

void EmitClassDecl(std::ostringstream& out, const Interface& iface) {
  out << "inline JSClassID g_" << iface.name << "_class_id;\n";
  out << "inline JSClassDef g_" << iface.name << "_class_def = {\"" << iface.name
      << "\", nullptr};\n\n";
}

void EmitCallbackWrapperClass(std::ostringstream& out, const GenContext& ctx,
                               const CallbackDecl& decl) {
  const std::string cls = decl.name + "Callback";
  out << "// Wraps a JS function value passed where the IDL expects a `"
      << decl.name << "` callback -- holds a duplicated reference so an\n"
      << "// implementation (e.g. an EventTarget storing listeners) can call it\n"
      << "// later, fully synchronously (JS_Call, no async plumbing).\n";
  out << "class " << cls << " {\n";
  out << " public:\n";
  out << "  " << cls << "(JSContext* ctx, JSValueConst fn) : ctx_(ctx), fn_(JS_DupValue(ctx, fn)) {}\n";
  out << "  ~" << cls << "() { JS_FreeValue(ctx_, fn_); }\n";
  out << "  " << cls << "(const " << cls << "&) = delete;\n";
  out << "  " << cls << "& operator=(const " << cls << "&) = delete;\n\n";
  out << "  // Identity comparison against another wrapper for the same\n"
      << "  // underlying JS function -- what a removeEventListener-style caller\n"
      << "  // needs to find a previously-stored listener again.\n";
  out << "  bool Matches(const " << cls << "& other) const {\n";
  out << "    return JS_VALUE_GET_PTR(fn_) == JS_VALUE_GET_PTR(other.fn_);\n";
  out << "  }\n\n";

  out << "  " << CppType(decl.return_type, ctx) << " Call(";
  for (size_t i = 0; i < decl.params.size(); ++i) {
    if (i > 0) out << ", ";
    out << CppParamType(decl.params[i].type, ctx) << " " << decl.params[i].name;
  }
  out << ") {\n";
  // EmitToJs/EmitValueConversion generate code against a variable literally
  // named `ctx` (matching the JS callback functions they're normally used
  // from) -- alias the member here rather than special-casing them.
  out << "    JSContext* ctx = ctx_;\n";
  std::vector<std::string> arg_vars;
  for (size_t i = 0; i < decl.params.size(); ++i) {
    std::string arg_var = "js_arg" + std::to_string(i);
    EmitToJs(out, ctx, decl.params[i].type, arg_var, decl.params[i].name);
    arg_vars.push_back(arg_var);
  }
  if (arg_vars.empty()) {
    out << "    JSValue call_result = JS_Call(ctx_, fn_, JS_UNDEFINED, 0, nullptr);\n";
  } else {
    out << "    JSValueConst argv[] = {";
    for (size_t i = 0; i < arg_vars.size(); ++i) {
      if (i > 0) out << ", ";
      out << arg_vars[i];
    }
    out << "};\n";
    out << "    JSValue call_result = JS_Call(ctx_, fn_, JS_UNDEFINED, "
        << arg_vars.size() << ", argv);\n";
  }
  for (const std::string& arg_var : arg_vars) {
    out << "    JS_FreeValue(ctx_, " << arg_var << ");\n";
  }
  out << "    if (JS_IsException(call_result)) {\n";
  out << "      JSValue exc = JS_GetException(ctx_);\n";
  out << "      JS_FreeValue(ctx_, exc);\n";
  out << "      JS_FreeValue(ctx_, call_result);\n";
  if (decl.return_type.kind != TypeKind::kVoid) {
    out << "      return " << CppType(decl.return_type, ctx) << "{};\n";
  } else {
    out << "      return;\n";
  }
  out << "    }\n";
  if (decl.return_type.kind != TypeKind::kVoid) {
    out << "    ";
    EmitValueConversion(out, ctx, decl.return_type, "cpp_result", "call_result");
    out << "    JS_FreeValue(ctx_, call_result);\n";
    out << "    return cpp_result;\n";
  } else {
    out << "    JS_FreeValue(ctx_, call_result);\n";
  }
  out << "  }\n\n";
  out << " private:\n";
  out << "  JSContext* ctx_;\n";
  out << "  JSValue fn_;\n";
  out << "};\n\n";
}

// Shared by method callbacks and constructor trampolines: converts each of
// `params` from `argv[i]` (handling variadic/optional/required exactly the
// same way in both), returning the C++ expression to pass for each.
std::vector<std::string> EmitParamConversions(std::ostringstream& out,
                                               const GenContext& ctx,
                                               const std::vector<Param>& params) {
  std::vector<std::string> call_args;
  for (size_t i = 0; i < params.size(); ++i) {
    const Param& param = params[i];
    if (param.variadic) {
      out << "  std::vector<" << CppType(param.type, ctx) << "> " << param.name
          << ";\n";
      out << "  for (int " << param.name << "_i = " << i << "; " << param.name
          << "_i < argc; ++" << param.name << "_i) {\n";
      EmitValueConversion(out, ctx, param.type, param.name + "_elem",
                           "argv[" + param.name + "_i]");
      out << "    " << param.name << ".push_back(std::move(" << param.name
          << "_elem));\n";
      out << "  }\n";
      call_args.push_back(param.name);
    } else if (param.optional) {
      out << "  " << CppType(param.type, ctx) << " " << param.name << " = "
          << (param.has_default ? param.default_literal : "{}") << ";\n";
      out << "  if (argc > " << i << ") {\n";
      EmitValueConversion(out, ctx, param.type, param.name + "_conv",
                           "argv[" + std::to_string(i) + "]");
      out << "    " << param.name << " = std::move(" << param.name << "_conv);\n";
      out << "  }\n";
      call_args.push_back(param.name);
    } else {
      EmitValueConversion(out, ctx, param.type, param.name,
                           "argv[" + std::to_string(i) + "]");
      call_args.push_back(param.name);
    }
  }
  return call_args;
}

void EmitMethodCallback(std::ostringstream& out, const GenContext& ctx,
                         const std::string& concrete_iface, const Method& method) {
  size_t required_count = RequiredArgCount(method.params);
  out << "inline JSValue " << CFunctionName(concrete_iface, method.name)
      << "(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {\n";
  out << "  auto* impl = static_cast<" << concrete_iface
      << "*>(JS_GetOpaque2(ctx, this_val, g_" << concrete_iface << "_class_id));\n";
  out << "  if (!impl) return JS_EXCEPTION;\n";
  if (required_count > 0) {
    out << "  if (argc < " << required_count << ") {\n";
    out << "    return JS_ThrowTypeError(ctx, \"" << method.name << "() requires "
        << required_count << " argument(s)\");\n";
    out << "  }\n";
  }
  std::vector<std::string> call_args = EmitParamConversions(out, ctx, method.params);
  const bool has_return = !ReturnsVoid(method.return_type);
  out << "  " << (has_return ? "auto result = " : "") << "impl->"
      << PascalCase(method.name) << "(";
  for (size_t i = 0; i < call_args.size(); ++i) {
    if (i > 0) out << ", ";
    out << call_args[i];
  }
  out << ");\n";
  EmitReturnConversion(out, ctx, method.return_type, "result");
  out << "}\n\n";
}

void EmitAttributeCallbacks(std::ostringstream& out, const GenContext& ctx,
                             const std::string& concrete_iface,
                             const Attribute& attr) {
  out << "inline JSValue " << AttrGetterJsFunctionName(concrete_iface, attr.name)
      << "(JSContext* ctx, JSValueConst this_val) {\n";
  out << "  auto* impl = static_cast<" << concrete_iface
      << "*>(JS_GetOpaque2(ctx, this_val, g_" << concrete_iface << "_class_id));\n";
  out << "  if (!impl) return JS_EXCEPTION;\n";
  out << "  auto result = impl->" << AttrGetterCppName(attr) << "();\n";
  EmitReturnConversion(out, ctx, attr.type, "result");
  out << "}\n\n";

  out << "inline JSValue " << AttrSetterJsFunctionName(concrete_iface, attr.name)
      << "(JSContext* ctx, JSValueConst this_val, JSValueConst val) {\n";
  out << "  auto* impl = static_cast<" << concrete_iface
      << "*>(JS_GetOpaque2(ctx, this_val, g_" << concrete_iface << "_class_id));\n";
  out << "  if (!impl) return JS_EXCEPTION;\n";
  if (attr.readonly) {
    out << "  (void)val;\n";
    out << "  return JS_ThrowTypeError(ctx, \"'" << attr.name << "' is read-only\");\n";
  } else {
    EmitValueConversion(out, ctx, attr.type, attr.name, "val");
    out << "  impl->" << AttrSetterCppName(attr) << "(" << attr.name << ");\n";
    out << "  return JS_UNDEFINED;\n";
  }
  out << "}\n\n";
}

void EmitCreateBindingFunction(std::ostringstream& out, const GenContext& ctx,
                                const Interface& iface) {
  EffectiveMembers eff = CollectEffectiveMembers(ctx, iface);
  out << "// Wraps a non-owning `" << iface.name
      << "*` in a quickjs object exposing its (and its ancestors') methods\n"
         "// under their JS-visible (lowerCamelCase) names. Call once per\n"
         "// JSContext; `impl` must outlive the returned object.\n";
  out << "inline JSValue Create" << iface.name << "Binding(JSContext* ctx, "
      << iface.name << "* impl) {\n";
  out << "  JSRuntime* rt = JS_GetRuntime(ctx);\n";
  // JSClassID allocation (JS_NewClassID) is process-global and must only
  // happen once ever -- but JS_NewClass registers that ID's vtable
  // *within one JSRuntime*, and a class ID allocated by an earlier
  // JSRuntime is never auto-registered on a later, independent one (e.g.
  // a second wasmv16::Engine/LocalFrameImpl in the same process). Gating
  // JS_NewClass on "is the id nonzero yet" (rather than "is it registered
  // on *this* rt") skipped registration for every runtime after the
  // first, leaving JS_NewObjectClass building objects against an
  // unregistered class -- silent corruption that crashed later, inside
  // quickjs-ng's own property machinery, nondeterministically depending
  // on heap layout.
  out << "  if (g_" << iface.name << "_class_id == 0) {\n";
  out << "    JS_NewClassID(rt, &g_" << iface.name << "_class_id);\n";
  out << "  }\n";
  out << "  if (!JS_IsRegisteredClass(rt, g_" << iface.name << "_class_id)) {\n";
  out << "    JS_NewClass(rt, g_" << iface.name << "_class_id, &g_" << iface.name
      << "_class_def);\n";
  out << "  }\n\n";
  out << "  JSValue obj = JS_NewObjectClass(ctx, g_" << iface.name << "_class_id);\n";
  out << "  if (JS_IsException(obj)) return obj;\n";
  out << "  JS_SetOpaque(obj, impl);\n\n";
  out << "  static const JSCFunctionListEntry kFuncs[] = {\n";
  for (const Method* m : eff.methods) {
    out << "    JS_CFUNC_DEF(\"" << m->name << "\", " << RequiredArgCount(m->params)
        << ", " << CFunctionName(iface.name, m->name) << "),\n";
  }
  for (const Attribute* a : eff.attributes) {
    out << "    JS_CGETSET_DEF(\"" << a->name << "\", "
        << AttrGetterJsFunctionName(iface.name, a->name) << ", "
        << AttrSetterJsFunctionName(iface.name, a->name) << "),\n";
  }
  out << "  };\n";
  out << "  JS_SetPropertyFunctionList(ctx, obj, kFuncs, sizeof(kFuncs) / sizeof(kFuncs[0]));\n";
  if (!eff.consts.empty()) {
    out << "\n";
    for (const Const* c : eff.consts) {
      out << "  JS_DefinePropertyValueStr(ctx, obj, \"" << c->name << "\", "
          << ConstJsCtor(*c) << ", JS_PROP_ENUMERABLE);\n";
    }
  }
  out << "  return obj;\n";
  out << "}\n\n";
}

// Trampoline for `new X(...)`: converts argv exactly like a method
// callback (reusing EmitParamConversions), calls whatever factory was
// installed via InstallXConstructor, and wraps the (still non-owning --
// see EmitConstructorFactoryAlias) result via the already-generated
// CreateXBinding, same as any other interface-typed return value.
void EmitInstallConstructorFunction(std::ostringstream& out, const GenContext& ctx,
                                     const Interface& iface) {
  if (!iface.has_constructor) return;
  size_t required_count = RequiredArgCount(iface.constructor_params);
  out << "inline JSValue Js" << iface.name
      << "_construct(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {\n";
  out << "  (void)this_val;\n";
  if (required_count > 0) {
    out << "  if (argc < " << required_count << ") {\n";
    out << "    return JS_ThrowTypeError(ctx, \"" << iface.name << "() requires "
        << required_count << " argument(s)\");\n";
    out << "  }\n";
  }
  std::vector<std::string> call_args =
      EmitParamConversions(out, ctx, iface.constructor_params);
  out << "  " << iface.name << "* impl = " << iface.name << "FactorySlot()(ctx";
  for (const std::string& arg : call_args) out << ", " << arg;
  out << ");\n";
  out << "  if (!impl) return JS_ThrowTypeError(ctx, \"" << iface.name
      << " construction failed\");\n";
  out << "  return Create" << iface.name << "Binding(ctx, impl);\n";
  out << "}\n\n";

  out << "// Installs a JS `" << iface.name
      << "` constructor function on `target` (typically the JS global\n"
         "// object) backed by `factory`; `new "
      << iface.name << "(...)` from JS calls it.\n";
  out << "inline JSValue Install" << iface.name
      << "Constructor(JSContext* ctx, JSValue target, " << iface.name
      << "Factory factory) {\n";
  out << "  " << iface.name << "FactorySlot() = std::move(factory);\n";
  out << "  JSValue ctor = JS_NewCFunction2(ctx, Js" << iface.name
      << "_construct, \"" << iface.name << "\", " << required_count
      << ", JS_CFUNC_constructor, 0);\n";
  out << "  JS_SetPropertyStr(ctx, target, \"" << iface.name << "\", ctor);\n";
  out << "  return JS_UNDEFINED;\n";
  out << "}\n\n";
}

}  // namespace

std::string GenerateCppHeader(const Module& module, const std::string& header_guard,
                               const std::string& cpp_namespace,
                               const std::string& source_filename) {
  GenContext ctx = BuildContext(module);
  std::ostringstream out;
  out << "// Generated by brujac from " << source_filename << ". DO NOT EDIT.\n";
  out << "#ifndef " << header_guard << "\n";
  out << "#define " << header_guard << "\n\n";
  out << "#include <quickjs.h>\n\n";
  out << "#include <cstdint>\n";
  out << "#include <functional>\n";
  out << "#include <memory>\n";
  out << "#include <optional>\n";
  out << "#include <string>\n";
  out << "#include <variant>\n";
  out << "#include <vector>\n\n";
  out << "namespace " << cpp_namespace << " {\n\n";

  // 0. Forward declarations for every interface/dictionary/callback-wrapper
  // name. Interface class bodies (step 1, next) can reference each other
  // by pointer/shared_ptr/const-ref (e.g. EventTarget's own methods use
  // `Event*` and `std::shared_ptr<EventListenerCallback>`) with no
  // relation to the base_name inheritance graph, so these must all be at
  // least forward-declared before any class body is emitted, not only the
  // ones a topological base-sort would order first.
  for (const Interface& iface : module.interfaces) out << "class " << iface.name << ";\n";
  for (const Dictionary& dict : module.dictionaries) out << "struct " << dict.name << ";\n";
  for (const CallbackDecl& decl : module.callbacks) {
    out << "class " << decl.name << "Callback;\n";
  }
  out << "\n";

  // 1. Interface classes, base-before-derived (real C++ inheritance), each
  // immediately followed by its constructor factory alias/slot if it has
  // one (needs the class to already be a complete type).
  for (const Interface* iface : TopoSortByBase(ctx)) {
    EmitInterfaceClass(out, ctx, *iface);
    EmitConstructorFactoryAlias(out, ctx, *iface);
  }

  // 2. Dictionary structs, base-before-derived (real C++ inheritance).
  for (const Dictionary* dict : TopoSortDictionariesByBase(ctx)) {
    EmitDictionaryStruct(out, ctx, *dict);
  }

  // 3. Class id/def globals -- declared before any body that references them.
  for (const Interface& iface : module.interfaces) EmitClassDecl(out, iface);

  // 4. CreateXBinding forward declarations, so any interface-typed
  // conversion (in a method callback or a callback wrapper) can call any
  // other interface's binding constructor regardless of declaration order.
  for (const Interface& iface : module.interfaces) {
    out << "inline JSValue Create" << iface.name << "Binding(JSContext* ctx, "
        << iface.name << "* impl);\n";
    if (iface.has_constructor) {
      out << "inline JSValue Install" << iface.name
          << "Constructor(JSContext* ctx, JSValue target, " << iface.name
          << "Factory factory);\n";
    }
  }
  out << "\n";

  // 5. Callback wrapper classes.
  for (const CallbackDecl& decl : module.callbacks) {
    EmitCallbackWrapperClass(out, ctx, decl);
  }

  // 6. Method/attribute JS callback functions, generated once per concrete
  // interface against its own class id -- including inherited members
  // (see cpp_generator.h).
  for (const Interface& iface : module.interfaces) {
    EffectiveMembers eff = CollectEffectiveMembers(ctx, iface);
    for (const Method* m : eff.methods) {
      EmitMethodCallback(out, ctx, iface.name, *m);
    }
    for (const Attribute* a : eff.attributes) {
      EmitAttributeCallbacks(out, ctx, iface.name, *a);
    }
  }

  // 7. CreateXBinding definitions.
  for (const Interface& iface : module.interfaces) {
    EmitCreateBindingFunction(out, ctx, iface);
  }

  // 8. InstallXConstructor definitions (needs CreateXBinding, just emitted).
  for (const Interface& iface : module.interfaces) {
    EmitInstallConstructorFunction(out, ctx, iface);
  }

  out << "}  // namespace " << cpp_namespace << "\n\n";
  out << "#endif  // " << header_guard << "\n";
  return out.str();
}

}  // namespace bruja
