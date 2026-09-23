#include "cpp_generator.h"
#include "type_intern.h"

#include <algorithm>
#include <iomanip>
#include <limits>
#include <numeric>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace voodoom {

namespace {

using EnumExtensibleMap = std::unordered_map<std::string, bool>;

const std::string* LookupTypemap(const GeneratorOptions& opt,
                                 const std::string& name,
                                 const std::string& owner_container,
                                 const std::string& module_ns) {
  auto find = [&](const std::string& key) -> const std::string* {
    auto it = opt.typemaps.find(key);
    return it == opt.typemaps.end() ? nullptr : &it->second;
  };
  if (const std::string* m = find(name)) return m;
  if (!owner_container.empty()) {
    if (const std::string* m = find(owner_container + "_" + name)) return m;
  }
  if (!module_ns.empty()) {
    if (const std::string* m = find(module_ns + "." + name)) return m;
  }
  return nullptr;
}

std::vector<size_t> WireFieldOrder(const StructDecl& s) {
  std::vector<size_t> idx(s.fields.size());
  std::iota(idx.begin(), idx.end(), 0);
  std::stable_sort(idx.begin(), idx.end(), [&](size_t a, size_t b) {
    if (s.fields[a].min_version != s.fields[b].min_version) {
      return s.fields[a].min_version < s.fields[b].min_version;
    }
    return a < b;
  });
  return idx;
}

bool IsHandleBearing(TypeKind k) {
  return k == TypeKind::kPendingRemote || k == TypeKind::kPendingReceiver ||
         k == TypeKind::kPendingAssociatedRemote ||
         k == TypeKind::kPendingAssociatedReceiver ||
         k == TypeKind::kHandle || k == TypeKind::kHandleMessagePipe ||
         k == TypeKind::kHandleDataPipeConsumer ||
         k == TypeKind::kHandleDataPipeProducer ||
         k == TypeKind::kHandleSharedBuffer ||
         k == TypeKind::kHandlePlatform;
}

// Struct/union names whose generated WriteX/ReadX take handle-context
// extra arguments (a handle vector + index + MultiplexRouter*), because
// they -- or something they embed -- carry a pending_*/handle field.
using HandleCtxSet = std::unordered_set<std::string>;

std::string CppEmbedName(const TypeSpec& t);
std::string CppEmbedName(const StructDecl& s);
std::string CppEmbedName(const UnionDecl& u);
std::vector<const StructDecl*> AllStructs(const Module& module);
std::vector<const UnionDecl*> AllUnions(const Module& module);

struct GenEnv {
  EnumExtensibleMap enum_extensible;
  HandleCtxSet handle_ctx;
};

bool TypeNeedsHandleCtx(const TypeSpec& t, const HandleCtxSet& ctx) {
  if (IsHandleBearing(t.kind)) return true;
  if (t.kind == TypeKind::kStructRef || t.kind == TypeKind::kUnionRef) {
    return ctx.count(CppEmbedName(t)) != 0;
  }
  if (t.kind == TypeKind::kArray) return TypeNeedsHandleCtx(*t.element, ctx);
  if (t.kind == TypeKind::kMap) return TypeNeedsHandleCtx(*t.element, ctx);
  return false;
}

HandleCtxSet BuildHandleCtx(const Module& module) {
  HandleCtxSet ctx;
  auto field_needs = [&](const TypeSpec& t) -> bool {
    if (IsHandleBearing(t.kind)) return true;
    if (t.kind == TypeKind::kStructRef || t.kind == TypeKind::kUnionRef) {
      return ctx.count(CppEmbedName(t)) != 0;
    }
    if (t.kind == TypeKind::kArray) return TypeNeedsHandleCtx(*t.element, ctx);
    if (t.kind == TypeKind::kMap) return TypeNeedsHandleCtx(*t.element, ctx);
    return false;
  };
  bool changed = true;
  while (changed) {
    changed = false;
    for (const StructDecl* s : AllStructs(module)) {
      const std::string key = CppEmbedName(*s);
      if (ctx.count(key)) continue;
      for (const StructField& f : s->fields) {
        if (field_needs(f.type)) {
          ctx.insert(key);
          changed = true;
          break;
        }
      }
    }
    for (const UnionDecl* u : AllUnions(module)) {
      const std::string key = CppEmbedName(*u);
      if (ctx.count(key)) continue;
      for (const UnionField& f : u->fields) {
        if (field_needs(f.type)) {
          ctx.insert(key);
          changed = true;
          break;
        }
      }
    }
  }
  return ctx;
}

void EmitHandleContextDecls(std::ostream& os, const std::string& ind,
                             const std::string& msg_ptr) {
  os << ind << "std::vector<mojo::ScopedHandle> wvc_handles_store = "
     << msg_ptr << "->TakeHandles();\n";
  os << ind
     << "std::vector<mojo::ScopedHandle>* wvc_handles = &wvc_handles_store;\n";
  os << ind << "size_t wvc_handle_idx_store = 0;\n";
  os << ind << "size_t* wvc_handle_idx = &wvc_handle_idx_store;\n";
  os << ind << "mojo::MultiplexRouter* router = router_;\n";
}

// (real-mojom-parity phase 4) The C++ value type and, for the read path,
// the raw (non-scoped) handle class WASMCadidumKernel's
// mojo/public/cpp/system/*.h headers actually define for each handle
// TypeKind -- see cpp_type/raw_class's own call sites
// (CppValueType/EmitTopReadParam) for how each is used. Never called for
// anything but the 5 handle kinds.
const char* HandleCppType(TypeKind k) {
  switch (k) {
    case TypeKind::kHandle:
      return "mojo::ScopedHandle";
    case TypeKind::kHandleMessagePipe:
      return "mojo::ScopedMessagePipeHandle";
    case TypeKind::kHandleDataPipeConsumer:
      return "mojo::ScopedDataPipeConsumerHandle";
    case TypeKind::kHandleDataPipeProducer:
      return "mojo::ScopedDataPipeProducerHandle";
    case TypeKind::kHandleSharedBuffer:
      return "mojo::ScopedSharedBufferHandle";
    case TypeKind::kHandlePlatform:
      return "mojo::PlatformHandle";
    default:
      throw std::logic_error("HandleCppType: not a handle TypeKind");
  }
}

const char* HandleRawClassName(TypeKind k) {
  switch (k) {
    case TypeKind::kHandle:
      return "mojo::Handle";
    case TypeKind::kHandleMessagePipe:
      return "mojo::MessagePipeHandle";
    case TypeKind::kHandleDataPipeConsumer:
      return "mojo::DataPipeConsumerHandle";
    case TypeKind::kHandleDataPipeProducer:
      return "mojo::DataPipeProducerHandle";
    case TypeKind::kHandleSharedBuffer:
      return "mojo::SharedBufferHandle";
    case TypeKind::kHandlePlatform:
      return "mojo::Handle";
    default:
      throw std::logic_error("HandleRawClassName: not a handle TypeKind");
  }
}

bool IsEnumExtensible(const EnumExtensibleMap& enum_extensible,
                       const std::string& enum_name) {
  auto it = enum_extensible.find(enum_name);
  return it != enum_extensible.end() && it->second;
}

std::string CppScalarType(TypeKind kind) {
  switch (kind) {
    case TypeKind::kBool: return "bool";
    case TypeKind::kInt8: return "int8_t";
    case TypeKind::kUint8: return "uint8_t";
    case TypeKind::kInt16: return "int16_t";
    case TypeKind::kUint16: return "uint16_t";
    case TypeKind::kInt32: return "int32_t";
    case TypeKind::kUint32: return "uint32_t";
    case TypeKind::kInt64: return "int64_t";
    case TypeKind::kUint64: return "uint64_t";
    case TypeKind::kFloat: return "float";
    case TypeKind::kDouble: return "double";
    default:
      throw std::logic_error("CppScalarType: not a scalar TypeKind");
  }
}

// (v15) "" -> "", "common" -> "common::", "a.b.c" -> "a::b::c::" -- the
// qualifier CppValueType/EmitWriteValue/EmitReadInto/EmitTopReadParam
// prepend to a cross-file type/interface/const/enum-value name.
// `owner_namespace` (TypeSpec::owner_namespace / DefaultValue::
// named_expr_owner_namespace) is always a raw `module` statement, which the
// grammar only ever lets be dot-separated (`module a.b.c;`, never spelled
// with "::") -- unlike SplitNamespace below, which also has to accept a
// "::"-spelled `--namespace` CLI override, so a plain '.'->':'':' replace is
// enough here.
std::string NamespacePrefix(const std::string& owner_namespace) {
  if (owner_namespace.empty()) return "";
  std::string prefix;
  prefix.reserve(owner_namespace.size() + 8);
  for (char c : owner_namespace) {
    if (c == '.') {
      prefix += "::";
    } else {
      prefix.push_back(c);
    }
  }
  prefix += "::";
  return prefix;
}

// (v16) "Foo" + "Status" -> "Foo_Status" -- a nested enum's *real* C++
// spelling. This compiler does not emit a nested enum as a genuine C++
// nested type (`class Foo { enum class Status; ... };` used from outside
// Foo needs Foo's *entire* class body -- virtual methods, Stub_, Proxy_,
// all of it -- to already be textually complete, and once a struct or a
// *different* interface can reference Foo's nested enum via the qualified
// `Foo.Status` .voodoom syntax, there's no single emission order that
// keeps that satisfied in general: structs must precede interfaces
// (an interface's inline Stub_/Proxy_ bodies need structs already
// complete), but a struct can now need an interface's nested enum before
// that interface's own body appears, and two interfaces can reference each
// other's nested enums regardless of their declaration order. Rather than
// build a real cross-kind dependency-ordering pass (this compiler only has
// one for struct/union complete-type embedding -- see parser.cc's
// CheckCompleteTypeAcyclic), a nested enum (or, real-mojom-
// parity phase 2, a nested const -- same reasoning applies identically)
// instead becomes a plain, flat, namespace-scope name under this mangled
// spelling -- see EmitNestedEnumDefinition/EmitNestedConstDefinition,
// emitted in the same ordering-free early pass top-level enums/consts
// already get (GenerateCppHeader), so it's always already defined by the
// time anything can reference it. `Foo.Status`/`Foo.SOME_CONST` in
// .voodoom source still mean exactly what they say; only the *generated
// C++* spelling differs from what real mojom's own generator would
// produce (`Foo::Status`) -- documented in README's "Known
// simplifications".
std::string MangledNestedName(const std::string& container,
                               const std::string& name) {
  return container + "_" + name;
}

std::string CppEmbedName(const std::string& owner_container,
                         const std::string& name) {
  return owner_container.empty() ? name
                                 : MangledNestedName(owner_container, name);
}
std::string CppEmbedName(const StructDecl& s) {
  return CppEmbedName(s.owner_container, s.name);
}
std::string CppEmbedName(const UnionDecl& u) {
  return CppEmbedName(u.owner_container, u.name);
}
std::string CppEmbedName(const TypeSpec& t) {
  return CppEmbedName(t.owner_container, t.name);
}

std::vector<const StructDecl*> AllStructs(const Module& module) {
  std::vector<const StructDecl*> out;
  out.reserve(module.structs.size());
  for (const StructDecl& s : module.structs) out.push_back(&s);
  for (const Interface& iface : module.interfaces) {
    for (const StructDecl& s : iface.structs) out.push_back(&s);
  }
  return out;
}
std::vector<const UnionDecl*> AllUnions(const Module& module) {
  std::vector<const UnionDecl*> out;
  out.reserve(module.unions.size());
  for (const UnionDecl& u : module.unions) out.push_back(&u);
  for (const Interface& iface : module.interfaces) {
    for (const UnionDecl& u : iface.unions) out.push_back(&u);
  }
  return out;
}

// "" or "Container_" -- the mangling prefix ContainerMangle above applies
// to a nested enum/const's bare name. Used two different ways below:
// directly adjacent to the bare name (CppValueType, a static_cast, a
// qualified enum-value/const default -- "Foo_" + "Status" = "Foo_Status",
// "Foo_" + "Status::OK" = "Foo_Status::OK"), or inserted *between* a verb
// and the bare name for a generated free-function call (EmitReadInto's
// "IsKnown" + "Foo_" + "Status" = "IsKnownFoo_Status", matching
// EmitNestedEnumDefinition's actual "IsKnown" + MangledNestedName(...)
// naming) -- never adjacent to a verb the way NamespacePrefix's "::" is,
// since this is real name-mangling, not scope qualification.
std::string ContainerMangle(const std::string& owner_container) {
  return owner_container.empty() ? "" : owner_container + "_";
}

// NamespacePrefix's cross-file qualifier composed with ContainerMangle's
// cross-container one -- "" for a top-level reference, "common::" for a
// cross-file top-level one, "Foo_" for a same-file nested one,
// "common::Foo_" for a cross-file *and* nested one. Meant to sit
// immediately before a bare name (CppValueType, a static_cast, a
// qualified default's named_expr) -- see ContainerMangle's comment for why
// a verb-prefixed free-function call (IsKnownX) needs the two composed
// differently, not via this helper.
std::string QualifierPrefix(const std::string& owner_namespace,
                             const std::string& owner_container) {
  return NamespacePrefix(owner_namespace) + ContainerMangle(owner_container);
}

// C++ type for a value of this type sitting in a local variable / struct
// field / callback argument (as opposed to how it's spelled in a virtual
// method's parameter list -- see CppParamDecl). A nullable
// kString/kArray/kMap/kStructRef/kUnionRef (see the parser's
// CheckNullableAllowed) wraps the ordinary spelling in std::optional<...>;
// nothing else about it changes. A nullable pending_* type instead stays
// exactly the same C++ spelling as its non-nullable form -- PendingRemote<T>
// etc. already have a real invalid/default-constructed state
// (is_valid() == false) that a std::optional wrapper would only duplicate;
// see EmitTopWritePrelude/EmitTopWritePayload/EmitTopReadParam below for
// the presence-flag wire framing that state drives.
std::string CppValueType(const TypeSpec& t) {
  std::string base;
  switch (t.kind) {
    case TypeKind::kString:
      base = "std::string";
      break;
    case TypeKind::kEnumRef:
    case TypeKind::kStructRef:
    case TypeKind::kUnionRef:
      base = QualifierPrefix(t.owner_namespace, t.owner_container) + t.name;
      break;
    case TypeKind::kArray:
      // (real-mojom-parity phase 5) A fixed-size array<T, N> maps to
      // std::array<T, N> (a compile-time-known element count, matching
      // real mojom's own generator); an ordinary array<T> is
      // v8::internal::CageVector<T> -- WASMSafeSpace CageAllocator so the
      // backing store lives in the cage, and T may be incomplete (C++17).
      base = t.fixed_array_size > 0
                 ? "std::array<" + CppValueType(*t.element) + ", " +
                       std::to_string(t.fixed_array_size) + ">"
                 : "v8::internal::CageVector<" + CppValueType(*t.element) +
                       ">";
      break;
    case TypeKind::kMap:
      // Occupancy-side map: CageAllocator node store (WASMSafeSpace).
      // Incomplete mapped types are not ISO-guaranteed for unordered_map
      // but this family's libstdc++ 16 / libc++ 23 accept them, which is
      // what values.mojom's map<string, Value> needs.
      base = "v8::internal::CageMap<" + CppValueType(*t.key) + ", " +
             CppValueType(*t.element) + ">";
      break;
    case TypeKind::kPendingRemote:
      return "mojo::PendingRemote<" +
             QualifierPrefix(t.owner_namespace, t.owner_container) + t.name +
             ">";
    case TypeKind::kPendingReceiver:
      return "mojo::PendingReceiver<" +
             QualifierPrefix(t.owner_namespace, t.owner_container) + t.name +
             ">";
    case TypeKind::kPendingAssociatedRemote:
      return "mojo::PendingAssociatedRemote<" +
             QualifierPrefix(t.owner_namespace, t.owner_container) + t.name +
             ">";
    case TypeKind::kPendingAssociatedReceiver:
      return "mojo::PendingAssociatedReceiver<" +
             QualifierPrefix(t.owner_namespace, t.owner_container) + t.name +
             ">";
    case TypeKind::kHandle:
    case TypeKind::kHandleMessagePipe:
    case TypeKind::kHandleDataPipeConsumer:
    case TypeKind::kHandleDataPipeProducer:
    case TypeKind::kHandleSharedBuffer:
    case TypeKind::kHandlePlatform:
      return HandleCppType(t.kind);
    default:
      base = CppScalarType(t.kind);  // never nullable (see CheckNullableAllowed)
      break;
  }
  return t.nullable ? "std::optional<" + base + ">" : base;
}

std::string CppParamDecl(const Param& p, const HandleCtxSet& ctx) {
  switch (p.type.kind) {
    case TypeKind::kString:
    case TypeKind::kArray:
    case TypeKind::kMap:
    case TypeKind::kStructRef:
    case TypeKind::kUnionRef:
      if (TypeNeedsHandleCtx(p.type, ctx)) {
        return CppValueType(p.type) + " " + p.name;
      }
      return "const " + CppValueType(p.type) + "& " + p.name;
    default:
      return CppValueType(p.type) + " " + p.name;
  }
}

std::string ExpectedCppType(const Method& m) {
  return "base::expected<" + CppValueType(m.result_success) + ", " +
         CppValueType(m.result_error) + ">";
}

std::string ResponseValueType(const Method& m, const Param& rp) {
  if (m.is_result_response) return ExpectedCppType(m);
  return CppValueType(rp.type);
}

std::string ResponseCallbackType(const Method& m) {
  std::ostringstream os;
  os << "base::OnceCallback<void(";
  if (m.is_result_response) {
    os << ExpectedCppType(m);
  } else {
    for (size_t i = 0; i < m.response_params.size(); ++i) {
      if (i) os << ", ";
      os << CppValueType(m.response_params[i].type);
    }
  }
  os << ")>";
  return os.str();
}

// Move-only param kinds (pending_remote/receiver, pending_associated_*)
// must be std::move()'d into impl_->Method(...) -- the pure-virtual
// signature takes them by value and their copy constructor is deleted.
std::string ParamPassExpr(const Param& p, const HandleCtxSet& ctx) {
  if (IsHandleBearing(p.type.kind) || TypeNeedsHandleCtx(p.type, ctx)) {
    return "std::move(" + p.name + ")";
  }
  return p.name;
}

// ---------------------------------------------------------------------
// Generic value read/write: scalars, string, enum, struct, union,
// array<T>, map<K, V>, and (v23) pending_*/handle kinds. Reused for
// struct/union fields, array elements, map keys/values, and ordinary
// method parameters.
// ---------------------------------------------------------------------

void EmitHandleWritePayload(std::ostream& os, const std::string& ind,
                             const std::string& msg, const TypeSpec& type,
                             const std::string& expr, int* uid) {
  int id = (*uid)++;
  std::string pfx = "wvc_h" + std::to_string(id);
  switch (type.kind) {
    case TypeKind::kPendingAssociatedRemote: {
      os << ind << "uint32_t " << pfx << "_version = " << expr
         << ".version();\n";
      os << ind << "mojo::ScopedInterfaceEndpointHandle " << pfx
         << "_handle = " << expr << ".PassHandle();\n";
      os << ind << "uint32_t " << pfx << "_iid = " << pfx
         << "_handle.ReleaseWithoutClosing();\n";
      os << ind << msg << ".WritePayload(&" << pfx << "_iid, sizeof(" << pfx
         << "_iid));\n";
      os << ind << msg << ".WritePayload(&" << pfx << "_version, sizeof("
         << pfx << "_version));\n";
      break;
    }
    case TypeKind::kPendingAssociatedReceiver: {
      os << ind << "mojo::ScopedInterfaceEndpointHandle " << pfx
         << "_handle = " << expr << ".PassHandle();\n";
      os << ind << "uint32_t " << pfx << "_iid = " << pfx
         << "_handle.ReleaseWithoutClosing();\n";
      os << ind << msg << ".WritePayload(&" << pfx << "_iid, sizeof(" << pfx
         << "_iid));\n";
      break;
    }
    case TypeKind::kPendingRemote: {
      os << ind << "uint32_t " << pfx << "_version = " << expr
         << ".version();\n";
      os << ind << "mojo::ScopedMessagePipeHandle " << pfx << "_pipe = "
         << expr << ".PassPipe();\n";
      os << ind << msg << ".AttachHandle(mojo::ScopedHandle(" << pfx
         << "_pipe.release()));\n";
      os << ind << msg << ".WritePayload(&" << pfx << "_version, sizeof("
         << pfx << "_version));\n";
      break;
    }
    case TypeKind::kPendingReceiver: {
      os << ind << "mojo::ScopedMessagePipeHandle " << pfx << "_pipe = "
         << expr << ".PassPipe();\n";
      os << ind << msg << ".AttachHandle(mojo::ScopedHandle(" << pfx
         << "_pipe.release()));\n";
      break;
    }
    case TypeKind::kHandle:
    case TypeKind::kHandleMessagePipe:
    case TypeKind::kHandleDataPipeConsumer:
    case TypeKind::kHandleDataPipeProducer:
    case TypeKind::kHandleSharedBuffer:
    case TypeKind::kHandlePlatform:
      os << ind << msg << ".AttachHandle(mojo::ScopedHandle(" << expr
         << ".release()));\n";
      break;
    default:
      break;
  }
}

// Writes `expr` (an already-in-scope value of this type) into `msg`
// (an object, not a pointer -- a local mojo::Message, or a struct writer's
// mojo::Message& parameter; both support `&msg` and `msg.Method()`).
void EmitWriteValue(std::ostream& os, const std::string& ind,
                     const std::string& msg, const TypeSpec& type,
                     const std::string& expr, int* uid,
                     const HandleCtxSet& ctx) {
  if (IsHandleBearing(type.kind)) {
    if (type.nullable) {
      int id = (*uid)++;
      std::string has = "wvc_has" + std::to_string(id);
      os << ind << "{\n";
      os << ind << "  bool " << has << " = " << expr << ".is_valid();\n";
      os << ind << "  mojo::internal::WriteScalar(&" << msg << ", " << has
         << ");\n";
      os << ind << "  if (" << has << ") {\n";
      TypeSpec non_null = type;
      non_null.nullable = false;
      EmitHandleWritePayload(os, ind + "    ", msg, non_null, expr, uid);
      os << ind << "  }\n";
      os << ind << "}\n";
    } else {
      EmitHandleWritePayload(os, ind, msg, type, expr, uid);
    }
    return;
  }
  if (type.nullable) {
    // A presence flag ahead of the value, same flat/sequential style as
    // everything else here -- no offset-based "maybe absent" pointer
    // trick, just "is it there, and if so, here it is" in wire order.
    int id = (*uid)++;
    std::string has = "wvc_has" + std::to_string(id);
    TypeSpec non_null = type;
    non_null.nullable = false;
    os << ind << "{\n";
    os << ind << "  bool " << has << " = " << expr << ".has_value();\n";
    os << ind << "  mojo::internal::WriteScalar(&" << msg << ", " << has
       << ");\n";
    os << ind << "  if (" << has << ") {\n";
    EmitWriteValue(os, ind + "    ", msg, non_null, "(*" + expr + ")", uid,
                   ctx);
    os << ind << "  }\n";
    os << ind << "}\n";
    return;
  }
  switch (type.kind) {
    case TypeKind::kString:
      os << ind << "mojo::internal::WriteString(&" << msg << ", " << expr
         << ");\n";
      break;
    case TypeKind::kEnumRef:
      os << ind << "mojo::internal::WriteScalar(&" << msg
         << ", static_cast<int32_t>(" << expr << "));\n";
      break;
    case TypeKind::kStructRef:
    case TypeKind::kUnionRef:
      // The qualifier goes before "Write", not between it and the type
      // name -- WriteLogEntry(...) is generated *inside* `namespace common
      // { ... }` when LogEntry is foreign, so the call is
      // common::WriteLogEntry(...), never Write + "common::" + "LogEntry".
      os << ind << NamespacePrefix(type.owner_namespace) << "Write"
         << CppEmbedName(type) << "(" << msg << ", " << expr << ");\n";
      break;
    case TypeKind::kArray: {
      int id = (*uid)++;
      std::string n = "wvc_n" + std::to_string(id);
      std::string e = "wvc_e" + std::to_string(id);
      os << ind << "{\n";
      // (real-mojom-parity phase 5) A fixed-size array<T, N> has no
      // length prefix on the wire -- the reader already knows N from the
      // type itself (see CppValueType's std::array<T, N> spelling).
      if (type.fixed_array_size == 0) {
        os << ind << "  uint32_t " << n << " = static_cast<uint32_t>(" << expr
           << ".size());\n";
        os << ind << "  " << msg << ".WritePayload(&" << n << ", sizeof(" << n
           << "));\n";
      }
      const char* ref = TypeNeedsHandleCtx(*type.element, ctx) ? "auto& "
                                                               : "const auto& ";
      os << ind << "  for (" << ref << e << " : " << expr << ") {\n";
      EmitWriteValue(os, ind + "    ", msg, *type.element, e, uid, ctx);
      os << ind << "  }\n";
      os << ind << "}\n";
      break;
    }
    case TypeKind::kMap: {
      int id = (*uid)++;
      std::string n = "wvc_n" + std::to_string(id);
      std::string e = "wvc_p" + std::to_string(id);
      os << ind << "{\n";
      os << ind << "  uint32_t " << n << " = static_cast<uint32_t>(" << expr
         << ".size());\n";
      os << ind << "  " << msg << ".WritePayload(&" << n << ", sizeof(" << n
         << "));\n";
      const char* ref = TypeNeedsHandleCtx(*type.element, ctx) ? "auto& "
                                                               : "const auto& ";
      os << ind << "  for (" << ref << e << " : " << expr << ") {\n";
      EmitWriteValue(os, ind + "    ", msg, *type.key, e + ".first", uid, ctx);
      EmitWriteValue(os, ind + "    ", msg, *type.element, e + ".second", uid,
                     ctx);
      os << ind << "  }\n";
      os << ind << "}\n";
      break;
    }
    default:  // scalar
      os << ind << "mojo::internal::WriteScalar(&" << msg << ", " << expr
         << ");\n";
      break;
  }
}

// Reads a value of this type from `msg_expr` (an already-dereferenced
// mojo::Message lvalue: "*message" at the top level, or a struct reader's
// `message` const-ref parameter) into the existing lvalue `target_expr`
// (a bare local name, or "out->field", or "vec[i]"). `offset_expr` is how
// to pass the running byte offset to ReadScalar/ReadString/ReadX (both of
// which want a `size_t*`): "&offset" at the top level, where `offset` is a
// local size_t -- but plain "offset" inside a generated struct ReadX
// function, where `offset` is already the size_t* parameter. Emits
// `return false;` on truncation -- callers must be inside a function
// returning bool.
void EmitHandleReadInto(std::ostream& os, const std::string& ind,
                         const std::string& msg_expr,
                         const std::string& offset_expr, const TypeSpec& type,
                         const std::string& target_expr) {
  std::string take =
      "(*wvc_handles)[(*wvc_handle_idx)++].release().value()";
  switch (type.kind) {
    case TypeKind::kPendingAssociatedRemote: {
      os << ind << "{\n";
      os << ind << "  uint32_t wvc_iid = 0;\n";
      os << ind << "  uint32_t wvc_version = 0;\n";
      os << ind << "  if (!mojo::internal::ReadScalar(" << msg_expr << ", "
         << offset_expr << ", &wvc_iid)) return false;\n";
      os << ind << "  if (!mojo::internal::ReadScalar(" << msg_expr << ", "
         << offset_expr << ", &wvc_version)) return false;\n";
      os << ind << "  " << target_expr << " = mojo::PendingAssociatedRemote<"
         << QualifierPrefix(type.owner_namespace, type.owner_container)
         << type.name << ">(router->CreateLocalEndpointHandle(wvc_iid), "
         << "wvc_version);\n";
      os << ind << "}\n";
      break;
    }
    case TypeKind::kPendingAssociatedReceiver: {
      os << ind << "{\n";
      os << ind << "  uint32_t wvc_iid = 0;\n";
      os << ind << "  if (!mojo::internal::ReadScalar(" << msg_expr << ", "
         << offset_expr << ", &wvc_iid)) return false;\n";
      os << ind << "  " << target_expr
         << " = mojo::PendingAssociatedReceiver<"
         << QualifierPrefix(type.owner_namespace, type.owner_container)
         << type.name << ">(router->CreateLocalEndpointHandle(wvc_iid));\n";
      os << ind << "}\n";
      break;
    }
    case TypeKind::kPendingRemote: {
      os << ind << "{\n";
      os << ind << "  uint32_t wvc_version = 0;\n";
      os << ind << "  if (!mojo::internal::ReadScalar(" << msg_expr << ", "
         << offset_expr << ", &wvc_version)) return false;\n";
      os << ind << "  " << target_expr << " = mojo::PendingRemote<"
         << QualifierPrefix(type.owner_namespace, type.owner_container)
         << type.name
         << ">(mojo::ScopedMessagePipeHandle(mojo::MessagePipeHandle("
         << take << ")), wvc_version);\n";
      os << ind << "}\n";
      break;
    }
    case TypeKind::kPendingReceiver:
      os << ind << target_expr << " = mojo::PendingReceiver<"
         << QualifierPrefix(type.owner_namespace, type.owner_container)
         << type.name
         << ">(mojo::ScopedMessagePipeHandle(mojo::MessagePipeHandle("
         << take << ")));\n";
      break;
    case TypeKind::kHandle:
    case TypeKind::kHandleMessagePipe:
    case TypeKind::kHandleDataPipeConsumer:
    case TypeKind::kHandleDataPipeProducer:
    case TypeKind::kHandleSharedBuffer:
    case TypeKind::kHandlePlatform:
      os << ind << target_expr << " = " << HandleCppType(type.kind) << "("
         << HandleRawClassName(type.kind) << "(" << take << "));\n";
      break;
    default:
      break;
  }
}

void EmitReadInto(std::ostream& os, const std::string& ind,
                   const std::string& msg_expr, const std::string& offset_expr,
                   const TypeSpec& type, const std::string& target_expr,
                   int* uid, const EnumExtensibleMap& enum_extensible,
                   const HandleCtxSet& ctx, const char* version_expr) {
  if (IsHandleBearing(type.kind)) {
    if (type.nullable) {
      int id = (*uid)++;
      std::string has = "wvc_has" + std::to_string(id);
      os << ind << "{\n";
      os << ind << "  bool " << has << " = false;\n";
      os << ind << "  if (!mojo::internal::ReadScalar(" << msg_expr << ", "
         << offset_expr << ", &" << has << ")) return false;\n";
      os << ind << "  if (" << has << ") {\n";
      TypeSpec non_null = type;
      non_null.nullable = false;
      EmitHandleReadInto(os, ind + "    ", msg_expr, offset_expr, non_null,
                         target_expr);
      os << ind << "  }\n";
      os << ind << "}\n";
    } else {
      EmitHandleReadInto(os, ind, msg_expr, offset_expr, type, target_expr);
    }
    return;
  }
  if (type.nullable) {
    int id = (*uid)++;
    std::string has = "wvc_has" + std::to_string(id);
    std::string tmp = "wvc_opt" + std::to_string(id);
    TypeSpec non_null = type;
    non_null.nullable = false;
    os << ind << "{\n";
    os << ind << "  bool " << has << " = false;\n";
    os << ind << "  if (!mojo::internal::ReadScalar(" << msg_expr << ", "
       << offset_expr << ", &" << has << ")) return false;\n";
    os << ind << "  if (" << has << ") {\n";
    os << ind << "    " << CppValueType(non_null) << " " << tmp << "{};\n";
    EmitReadInto(os, ind + "    ", msg_expr, offset_expr, non_null, tmp, uid,
                 enum_extensible, ctx, version_expr);
    os << ind << "    " << target_expr << " = std::move(" << tmp << ");\n";
    os << ind << "  } else {\n";
    os << ind << "    " << target_expr << " = std::nullopt;\n";
    os << ind << "  }\n";
    os << ind << "}\n";
    return;
  }
  switch (type.kind) {
    case TypeKind::kString:
      os << ind << "if (!mojo::internal::ReadString(" << msg_expr << ", "
         << offset_expr << ", &(" << target_expr << "))) return false;\n";
      break;
    case TypeKind::kEnumRef: {
      int id = (*uid)++;
      std::string tmp = "wvc_v" + std::to_string(id);
      os << ind << "int32_t " << tmp << " = 0;\n";
      os << ind << "if (!mojo::internal::ReadScalar(" << msg_expr << ", "
         << offset_expr << ", &" << tmp << ")) return false;\n";
      if (!IsEnumExtensible(enum_extensible, type.name)) {
        // "IsKnown" sits between the cross-file "::" qualifier and the
        // (possibly name-mangled, not "::"-qualified) type name -- see
        // ContainerMangle's comment for why this can't just be
        // QualifierPrefix(...) + "IsKnown" + type.name the way the
        // struct/union ReadX call below can.
        os << ind << "if (!" << NamespacePrefix(type.owner_namespace)
           << "IsKnown" << ContainerMangle(type.owner_container) << type.name
           << (version_expr ? "AsOf(" : "(") << tmp;
        if (version_expr) {
          os << ", " << version_expr;
        }
        os << ")) return false;\n";
      }
      os << ind << target_expr << " = static_cast<"
         << QualifierPrefix(type.owner_namespace, type.owner_container)
         << type.name << ">(" << tmp << ");\n";
      break;
    }
    case TypeKind::kStructRef:
    case TypeKind::kUnionRef:
      os << ind << "if (!" << NamespacePrefix(type.owner_namespace) << "Read"
         << CppEmbedName(type) << "(" << msg_expr << ", " << offset_expr
         << ", &(" << target_expr << ")";
      if (ctx.count(CppEmbedName(type))) {
        os << ", wvc_handles, wvc_handle_idx, router";
      }
      os << ")) return false;\n";
      break;
    case TypeKind::kArray: {
      int id = (*uid)++;
      std::string i = "wvc_i" + std::to_string(id);
      os << ind << "{\n";
      // (real-mojom-parity phase 5) A fixed-size array<T, N> has no
      // length prefix to read -- N is already known at compile time (see
      // EmitWriteValue's matching comment).
      if (type.fixed_array_size == 0) {
        std::string n = "wvc_n" + std::to_string(id);
        os << ind << "  uint32_t " << n << " = 0;\n";
        os << ind << "  if (!mojo::internal::ReadScalar(" << msg_expr << ", "
           << offset_expr << ", &" << n << ")) return false;\n";
        os << ind << "  (" << target_expr << ").resize(" << n << ");\n";
        os << ind << "  for (uint32_t " << i << " = 0; " << i << " < " << n
           << "; ++" << i << ") {\n";
      } else {
        os << ind << "  for (uint32_t " << i << " = 0; " << i << " < "
           << type.fixed_array_size << "u; ++" << i << ") {\n";
      }
      EmitReadInto(os, ind + "    ", msg_expr, offset_expr, *type.element,
                   "(" + target_expr + ")[" + i + "]", uid, enum_extensible,
                   ctx, version_expr);
      os << ind << "  }\n";
      os << ind << "}\n";
      break;
    }
    case TypeKind::kMap: {
      int id = (*uid)++;
      std::string n = "wvc_n" + std::to_string(id);
      std::string i = "wvc_i" + std::to_string(id);
      std::string k = "wvc_k" + std::to_string(id);
      std::string v = "wvc_v" + std::to_string(id);
      os << ind << "{\n";
      os << ind << "  uint32_t " << n << " = 0;\n";
      os << ind << "  if (!mojo::internal::ReadScalar(" << msg_expr << ", "
         << offset_expr << ", &" << n << ")) return false;\n";
      os << ind << "  (" << target_expr << ").clear();\n";
      os << ind << "  for (uint32_t " << i << " = 0; " << i << " < " << n
         << "; ++" << i << ") {\n";
      os << ind << "    " << CppValueType(*type.key) << " " << k << "{};\n";
      EmitReadInto(os, ind + "    ", msg_expr, offset_expr, *type.key, k,
                   uid, enum_extensible, ctx, version_expr);
      os << ind << "    " << CppValueType(*type.element) << " " << v
         << "{};\n";
      EmitReadInto(os, ind + "    ", msg_expr, offset_expr, *type.element, v,
                   uid, enum_extensible, ctx, version_expr);
      os << ind << "    (" << target_expr << ").emplace(std::move(" << k
         << "), std::move(" << v << "));\n";
      os << ind << "  }\n";
      os << ind << "}\n";
      break;
    }
    default:  // scalar
      os << ind << "if (!mojo::internal::ReadScalar(" << msg_expr << ", "
         << offset_expr << ", &(" << target_expr << "))) return false;\n";
      break;
  }
}

// Declares a fresh local `name` of this type, then reads into it. Always
// used at the top level (method params), where `offset` is a local size_t
// -- so offset_expr is always "&offset" here.
void EmitDeclareAndRead(std::ostream& os, const std::string& ind,
                         const std::string& msg_expr, const TypeSpec& type,
                         const std::string& name, int* uid,
                         const EnumExtensibleMap& enum_extensible,
                         const HandleCtxSet& ctx) {
  os << ind << CppValueType(type) << " " << name << "{};\n";
  EmitReadInto(os, ind, msg_expr, "&offset", type, name, uid,
               enum_extensible, ctx, nullptr);
}

// ---------------------------------------------------------------------
// Top-level (method-parameter-only) write/read: adds the four
// handle-bearing kinds on top of the generic value functions above.
// ---------------------------------------------------------------------

// Nullable pending_* params (v14) are represented as the exact same C++
// type as their non-nullable form -- PendingRemote<T>::is_valid() etc.
// already distinguish present/absent, no std::optional wrapper needed
// (see CppValueType's comment). `_present` is captured here, in the
// prelude, *before* the pass/move calls below consume `p.name` --
// PassHandle()/PassPipe() on an already-invalid pending object is itself
// perfectly safe (just moves out an already-invalid handle), so the pass
// calls stay unconditional; only whether EmitTopWritePayload actually
// writes the resulting bytes depends on this flag.
void EmitNullablePresenceDecl(std::ostream& os, const std::string& ind,
                               const Param& p) {
  if (p.type.nullable) {
    os << ind << "bool " << p.name << "_present = " << p.name
       << ".is_valid();\n";
  }
}

// Pre-message-construction bookkeeping for a param about to be written --
// pending_remote/receiver need to peel their pipe out; pending_associated_*
// need to peel their in-process endpoint id out. Both must happen before
// the mojo::Message exists (matching Echo::Proxy_::SetListener).
void EmitTopWritePrelude(std::ostream& os, const std::string& ind,
                          const Param& p) {
  switch (p.type.kind) {
    case TypeKind::kPendingAssociatedRemote:
      EmitNullablePresenceDecl(os, ind, p);
      os << ind << "uint32_t " << p.name << "_version = " << p.name
         << ".version();\n";
      os << ind << "mojo::ScopedInterfaceEndpointHandle " << p.name
         << "_handle = " << p.name << ".PassHandle();\n";
      os << ind << "uint32_t " << p.name << "_iid = " << p.name
         << "_handle.ReleaseWithoutClosing();\n";
      break;
    case TypeKind::kPendingAssociatedReceiver:
      EmitNullablePresenceDecl(os, ind, p);
      os << ind << "mojo::ScopedInterfaceEndpointHandle " << p.name
         << "_handle = " << p.name << ".PassHandle();\n";
      os << ind << "uint32_t " << p.name << "_iid = " << p.name
         << "_handle.ReleaseWithoutClosing();\n";
      break;
    case TypeKind::kPendingRemote:
      EmitNullablePresenceDecl(os, ind, p);
      os << ind << "mojo::ScopedMessagePipeHandle " << p.name
         << "_pipe = " << p.name << ".PassPipe();\n";
      os << ind << "uint32_t " << p.name << "_version = " << p.name
         << ".version();\n";
      break;
    case TypeKind::kPendingReceiver:
      EmitNullablePresenceDecl(os, ind, p);
      os << ind << "mojo::ScopedMessagePipeHandle " << p.name
         << "_pipe = " << p.name << ".PassPipe();\n";
      break;
    case TypeKind::kHandle:
    case TypeKind::kHandleMessagePipe:
    case TypeKind::kHandleDataPipeConsumer:
    case TypeKind::kHandleDataPipeProducer:
    case TypeKind::kHandleSharedBuffer:
    case TypeKind::kHandlePlatform:
      EmitNullablePresenceDecl(os, ind, p);
      break;
    default:
      break;  // handled by EmitWriteValue at the call site
  }
}

// Writes the presence-flag byte for a nullable pending_* param and opens
// the `if (<name>_present) {` guard around the rest of its payload
// bytes; returns the indent the guarded body should use (deeper if
// nullable, unchanged otherwise). No-op (returns `ind` as-is) for a
// non-nullable param. Pair with EmitNullablePayloadGuardClose.
std::string EmitNullablePayloadGuardOpen(std::ostream& os,
                                          const std::string& ind,
                                          const std::string& msg,
                                          const Param& p) {
  if (!p.type.nullable) return ind;
  os << ind << msg << ".WritePayload(&" << p.name << "_present, sizeof("
     << p.name << "_present));\n";
  os << ind << "if (" << p.name << "_present) {\n";
  return ind + "  ";
}

void EmitNullablePayloadGuardClose(std::ostream& os, const std::string& ind,
                                    const Param& p) {
  if (p.type.nullable) os << ind << "}\n";
}

void EmitTopWritePayload(std::ostream& os, const std::string& ind,
                          const std::string& msg, const Param& p, int* uid,
                          const HandleCtxSet& ctx) {
  switch (p.type.kind) {
    case TypeKind::kPendingAssociatedRemote: {
      std::string bi = EmitNullablePayloadGuardOpen(os, ind, msg, p);
      os << bi << msg << ".WritePayload(&" << p.name << "_iid, sizeof("
         << p.name << "_iid));\n";
      os << bi << msg << ".WritePayload(&" << p.name << "_version, sizeof("
         << p.name << "_version));\n";
      EmitNullablePayloadGuardClose(os, ind, p);
      break;
    }
    case TypeKind::kPendingAssociatedReceiver: {
      std::string bi = EmitNullablePayloadGuardOpen(os, ind, msg, p);
      os << bi << msg << ".WritePayload(&" << p.name << "_iid, sizeof("
         << p.name << "_iid));\n";
      EmitNullablePayloadGuardClose(os, ind, p);
      break;
    }
    case TypeKind::kPendingRemote: {
      std::string bi = EmitNullablePayloadGuardOpen(os, ind, msg, p);
      os << bi << msg << ".AttachHandle(mojo::ScopedHandle(" << p.name
         << "_pipe.release()));\n";
      os << bi << msg << ".WritePayload(&" << p.name << "_version, sizeof("
         << p.name << "_version));\n";
      EmitNullablePayloadGuardClose(os, ind, p);
      break;
    }
    case TypeKind::kPendingReceiver: {
      std::string bi = EmitNullablePayloadGuardOpen(os, ind, msg, p);
      os << bi << msg << ".AttachHandle(mojo::ScopedHandle(" << p.name
         << "_pipe.release()));\n";
      EmitNullablePayloadGuardClose(os, ind, p);
      break;
    }
    case TypeKind::kHandle:
    case TypeKind::kHandleMessagePipe:
    case TypeKind::kHandleDataPipeConsumer:
    case TypeKind::kHandleDataPipeProducer:
    case TypeKind::kHandleSharedBuffer:
    case TypeKind::kHandlePlatform: {
      std::string bi = EmitNullablePayloadGuardOpen(os, ind, msg, p);
      os << bi << msg << ".AttachHandle(mojo::ScopedHandle(" << p.name
         << ".release()));\n";
      EmitNullablePayloadGuardClose(os, ind, p);
      break;
    }
    default:
      EmitWriteValue(os, ind, msg, p.type, p.name, uid, ctx);
      break;
  }
}

// Reads a nullable pending_* param's presence-flag byte, declares `p.name`
// default-constructed (the "absent" state -- is_valid() == false), and
// opens an `if (<name>_present) { ... }` guard for the caller to fill in
// with the "actually construct it, assigning into p.name" body. Returns
// the indent that body should use. No-op (returns `ind`, declares
// nothing) for a non-nullable param -- that case's own code declares
// `p.name` inline instead. Pair with EmitNullablePayloadGuardClose (same
// brace/indent contract as the write side).
std::string EmitNullableReadGuardOpen(std::ostream& os, const std::string& ind,
                                       const std::string& msg_expr,
                                       const std::string& cpp_type,
                                       const Param& p) {
  if (!p.type.nullable) return ind;
  os << ind << "bool " << p.name << "_present = false;\n";
  os << ind << "if (!mojo::internal::ReadScalar(" << msg_expr
     << ", &offset, &" << p.name << "_present)) return false;\n";
  os << ind << cpp_type << " " << p.name << ";\n";
  os << ind << "if (" << p.name << "_present) {\n";
  return ind + "  ";
}

// Reads one top-level param out of `msg_expr` ("*message"/"*response"),
// declaring a fresh local named `p.name`. `msg_ptr` is the pointer
// variable ("message"/"response") that owns TakeHandles() -- only called
// (once, lazily) the first time a handle-bearing param is encountered in
// this param list, tracked via `*handles_emitted`.
void EmitTopReadParam(std::ostream& os, const std::string& ind,
                       const std::string& msg_ptr, const std::string& msg_expr,
                       const Param& p, bool* handles_emitted, int* uid,
                       const EnumExtensibleMap& enum_extensible,
                       const HandleCtxSet& ctx) {
  (void)handles_emitted;
  (void)msg_ptr;
  if (IsHandleBearing(p.type.kind) && TypeNeedsHandleCtx(p.type, ctx)) {
    // Handle context (wvc_handles / wvc_handle_idx / router) is emitted
    // by the caller via EmitHandleContextDecls.
  }
  switch (p.type.kind) {
    case TypeKind::kPendingAssociatedRemote: {
      std::string cpp_type =
          "mojo::PendingAssociatedRemote<" +
          QualifierPrefix(p.type.owner_namespace, p.type.owner_container) +
          p.type.name + ">";
      std::string bi =
          EmitNullableReadGuardOpen(os, ind, msg_expr, cpp_type, p);
      os << bi << "uint32_t " << p.name << "_iid = 0;\n";
      os << bi << "uint32_t " << p.name << "_version = 0;\n";
      os << bi << "if (!mojo::internal::ReadScalar(" << msg_expr
         << ", &offset, &" << p.name << "_iid)) return false;\n";
      os << bi << "if (!mojo::internal::ReadScalar(" << msg_expr
         << ", &offset, &" << p.name << "_version)) return false;\n";
      if (p.type.nullable) {
        os << bi << p.name << " = " << cpp_type
           << "(router->CreateLocalEndpointHandle(" << p.name << "_iid), "
           << p.name << "_version);\n";
        EmitNullablePayloadGuardClose(os, ind, p);
      } else {
        os << bi << cpp_type << " " << p.name
           << "(router->CreateLocalEndpointHandle(" << p.name << "_iid), "
           << p.name << "_version);\n";
      }
      break;
    }
    case TypeKind::kPendingAssociatedReceiver: {
      std::string cpp_type =
          "mojo::PendingAssociatedReceiver<" +
          QualifierPrefix(p.type.owner_namespace, p.type.owner_container) +
          p.type.name + ">";
      std::string bi =
          EmitNullableReadGuardOpen(os, ind, msg_expr, cpp_type, p);
      os << bi << "uint32_t " << p.name << "_iid = 0;\n";
      os << bi << "if (!mojo::internal::ReadScalar(" << msg_expr
         << ", &offset, &" << p.name << "_iid)) return false;\n";
      if (p.type.nullable) {
        os << bi << p.name << " = " << cpp_type
           << "(router->CreateLocalEndpointHandle(" << p.name << "_iid));\n";
        EmitNullablePayloadGuardClose(os, ind, p);
      } else {
        os << bi << cpp_type << " " << p.name
           << "(router->CreateLocalEndpointHandle(" << p.name << "_iid));\n";
      }
      break;
    }
    case TypeKind::kPendingRemote: {
      std::string cpp_type =
          "mojo::PendingRemote<" +
          QualifierPrefix(p.type.owner_namespace, p.type.owner_container) +
          p.type.name + ">";
      std::string bi =
          EmitNullableReadGuardOpen(os, ind, msg_expr, cpp_type, p);
      os << bi << "uint32_t " << p.name << "_version = 0;\n";
      os << bi << "if (!mojo::internal::ReadScalar(" << msg_expr
         << ", &offset, &" << p.name << "_version)) return false;\n";
      if (p.type.nullable) {
        os << bi << p.name << " = " << cpp_type
           << "(mojo::ScopedMessagePipeHandle(mojo::MessagePipeHandle(\n"
           << bi << "     (*wvc_handles)[(*wvc_handle_idx)++].release().value())), "
           << p.name << "_version);\n";
        EmitNullablePayloadGuardClose(os, ind, p);
      } else {
        os << bi << cpp_type << " " << p.name
           << "(mojo::ScopedMessagePipeHandle(mojo::MessagePipeHandle(\n"
           << bi << "     (*wvc_handles)[(*wvc_handle_idx)++].release().value())), "
           << p.name << "_version);\n";
      }
      break;
    }
    case TypeKind::kPendingReceiver: {
      std::string cpp_type =
          "mojo::PendingReceiver<" +
          QualifierPrefix(p.type.owner_namespace, p.type.owner_container) +
          p.type.name + ">";
      std::string bi =
          EmitNullableReadGuardOpen(os, ind, msg_expr, cpp_type, p);
      if (p.type.nullable) {
        os << bi << p.name << " = " << cpp_type
           << "(mojo::ScopedMessagePipeHandle(mojo::MessagePipeHandle(\n"
           << bi << "     (*wvc_handles)[(*wvc_handle_idx)++].release().value())));\n";
        EmitNullablePayloadGuardClose(os, ind, p);
      } else {
        os << bi << cpp_type << " " << p.name
           << "(mojo::ScopedMessagePipeHandle(mojo::MessagePipeHandle(\n"
           << bi << "     (*wvc_handles)[(*wvc_handle_idx)++].release().value())));\n";
      }
      break;
    }
    case TypeKind::kHandle:
    case TypeKind::kHandleMessagePipe:
    case TypeKind::kHandleDataPipeConsumer:
    case TypeKind::kHandleDataPipeProducer:
    case TypeKind::kHandleSharedBuffer:
    case TypeKind::kHandlePlatform: {
      std::string cpp_type = HandleCppType(p.type.kind);
      std::string raw_class = HandleRawClassName(p.type.kind);
      std::string bi =
          EmitNullableReadGuardOpen(os, ind, msg_expr, cpp_type, p);
      if (p.type.nullable) {
        os << bi << p.name << " = " << cpp_type << "(" << raw_class
           << "(\n"
           << bi << "     (*wvc_handles)[(*wvc_handle_idx)++].release().value()));\n";
        EmitNullablePayloadGuardClose(os, ind, p);
      } else {
        os << bi << cpp_type << " " << p.name << "(" << raw_class << "(\n"
           << bi << "     (*wvc_handles)[(*wvc_handle_idx)++].release().value()));\n";
      }
      break;
    }
    default:
      EmitDeclareAndRead(os, ind, msg_expr, p.type, p.name, uid,
                         enum_extensible, ctx);
      break;
  }
}

// Alongside the `enum class` itself, every enum gets a generated
// `IsKnownEnumName(int32_t)` helper -- used by EmitReadInto's kEnumRef
// case to validate a wire value against a *non*-extensible enum's actual
// declared values (see EnumDecl::is_extensible), but always emitted
// (even for an extensible enum, which never calls it) since it costs
// nothing and is independently useful/testable on its own.
void EmitTypeInternMembers(std::ostream& os, const std::string& intern_key,
                           const std::string& cpp_type) {
  os << "  // Object Type Identifier: intern_key is the shape string "
        "(Go++ go/types intern);\n";
  os << "  // type_key() is the C++ token (Go++ type_key_of<T>()); "
        "CHPT names objects\n";
  os << "  // with the interned tag (WASMv8Bindings + WASMSafeSpace). "
        "FromHandle fails\n";
  os << "  // unless the tag matches.\n";
  os << "  static constexpr const char* intern_key() { return \""
     << intern_key << "\"; }\n";
  os << "  static const void* type_key() { static char k; return &k; }\n";
  os << "  static v8::CppHeapPointerTag chpt_tag() {\n";
  os << "    return mojo::internal::TypeTag(type_key());\n";
  os << "  }\n";
  os << "  static v8::CppHeapPointerHandle NameOnHeap(" << cpp_type
     << "* p) {\n";
  os << "    return mojo::internal::NameObject(p, type_key());\n";
  os << "  }\n";
  os << "  static " << cpp_type
     << "* FromHandle(v8::CppHeapPointerHandle h) {\n";
  os << "    return static_cast<" << cpp_type
     << "*>(mojo::internal::GetObject(h, type_key()));\n";
  os << "  }\n";
}

void EmitEnum(std::ostream& os, const EnumDecl& e,
              const GeneratorOptions& opt, const std::string& module_ns) {
  if (const std::string* mapped =
          LookupTypemap(opt, e.name, "", module_ns)) {
    os << "using " << e.name << " = " << *mapped << ";\n\n";
    os << "inline bool IsKnown" << e.name << "(int32_t) { return true; }\n";
    os << "inline bool IsKnown" << e.name
       << "AsOf(int32_t, uint32_t) { return true; }\n\n";
    return;
  }
  os << "enum class " << e.name << " : int32_t {\n";
  for (const EnumValue& v : e.values) {
    os << "  " << v.name << " = " << v.value << ",\n";
  }
  os << "};\n\n";
  os << "inline bool IsKnown" << e.name << "(int32_t wvc_value) {\n";
  os << "  switch (wvc_value) {\n";
  {
    std::vector<int32_t> seen;
    for (const EnumValue& v : e.values) {
      if (std::find(seen.begin(), seen.end(), v.value) != seen.end()) {
        continue;
      }
      seen.push_back(v.value);
      os << "    case " << v.value << ":\n";
    }
  }
  os << "      return true;\n";
  os << "    default:\n";
  os << "      return false;\n";
  os << "  }\n";
  os << "}\n\n";
  os << "inline bool IsKnown" << e.name
     << "AsOf(int32_t wvc_value, uint32_t wvc_version) {\n";
  os << "  (void)wvc_version;\n";
  os << "  switch (wvc_value) {\n";
  {
    std::vector<int32_t> seen;
    for (const EnumValue& v : e.values) {
      if (std::find(seen.begin(), seen.end(), v.value) != seen.end()) {
        continue;
      }
      seen.push_back(v.value);
      os << "    case " << v.value << ":\n";
      if (v.min_version == 0) {
        os << "      return true;\n";
      } else {
        os << "      return wvc_version >= " << v.min_version << "u;\n";
      }
    }
  }
  os << "    default:\n";
  os << "      return false;\n";
  os << "  }\n";
  os << "}\n\n";
}

// The nested enum's real `enum class`/`IsKnownX` definition, emitted
// namespace-scope under its mangled name (see MangledNestedName's own
// comment for why), in the same ordering-free early pass top-level enums
// already get -- see GenerateCppHeader. Nothing is emitted inside
// `container`'s own class/struct body at all -- see EmitStruct/
// EmitInterface, which no longer have a nested-enum emission step of their
// own.
void EmitNestedEnumDefinition(std::ostream& os, const std::string& container,
                               const EnumDecl& e,
                               const GeneratorOptions& opt,
                               const std::string& module_ns) {
  EnumDecl mangled = e;
  mangled.name = MangledNestedName(container, e.name);
  EmitEnum(os, mangled, opt, module_ns);
}

// Escapes a raw (already-unescaped-from-source) string for re-emission as
// a C++ string literal's contents -- only backslash and double-quote need
// it, since the lexer already rejected any string literal spanning a raw
// newline (see lexer.cc), so there's no control character to worry about.
std::string CppStringLiteral(const std::string& s) {
  std::string out = "\"";
  for (char c : s) {
    if (c == '\\' || c == '"') out.push_back('\\');
    out.push_back(c);
  }
  out.push_back('"');
  return out;
}

// (real-mojom-parity phase 2) A float/double literal, full round-trip
// precision (max_digits10), always spelled so the C++ compiler reads it as
// a floating-point literal (never a bare integer -- "5" becomes "5.0")
// and, for `float` specifically, with an "f" suffix so it isn't a
// `double` narrowed on assignment.
std::string CppFloatLiteral(double v, bool is_float) {
  std::ostringstream ss;
  ss << std::setprecision(is_float ? std::numeric_limits<float>::max_digits10
                                    : std::numeric_limits<double>::max_digits10)
     << v;
  std::string s = ss.str();
  if (s.find('.') == std::string::npos && s.find('e') == std::string::npos &&
      s.find("inf") == std::string::npos && s.find("nan") == std::string::npos) {
    s += ".0";
  }
  if (is_float) s += "f";
  return s;
}

// The bare C++ expression for a field default or const's own value --
// shared by CppFieldInitializer (wraps it in "{...}") and EmitConst (uses
// it bare, after "= "). A value that came from a NAME (an enum value or a
// const reference -- see ast.h's DefaultValue) is emitted as that exact
// expression ("Status::OK", "kMax"), qualified the same way any other
// cross-file/nested reference is, not its resolved literal value -- more
// readable, and it's what real mojom's own generator does too.
std::string DefaultValueExpr(const TypeSpec& type, const DefaultValue& v) {
  if (!v.named_expr.empty()) {
    return QualifierPrefix(v.named_expr_owner_namespace,
                            v.named_expr_owner_container) +
           v.named_expr;
  }
  // (real-mojom-parity phase 8) Real mojom's `float.INFINITY`/
  // `double.NAN`-style special float constant literals -- a raw "inf"/
  // "nan" token isn't valid C++ source text, so these need their own
  // expression form rather than going through CppFloatLiteral's normal
  // setprecision-based formatting. See DefaultValue::float_special's own
  // comment; the generated header's #include <limits> is what makes
  // these available.
  if (v.float_special != DefaultValue::FloatSpecial::kNone) {
    const char* t = type.kind == TypeKind::kFloat ? "float" : "double";
    switch (v.float_special) {
      case DefaultValue::FloatSpecial::kInfinity:
        return std::string("std::numeric_limits<") + t + ">::infinity()";
      case DefaultValue::FloatSpecial::kNegativeInfinity:
        return std::string("-std::numeric_limits<") + t + ">::infinity()";
      case DefaultValue::FloatSpecial::kNaN:
        return std::string("std::numeric_limits<") + t + ">::quiet_NaN()";
      case DefaultValue::FloatSpecial::kNone:
        break;  // unreachable (guarded above)
    }
  }
  switch (type.kind) {
    case TypeKind::kBool:
      return v.bool_value ? "true" : "false";
    case TypeKind::kString:
      return CppStringLiteral(v.string_value);
    case TypeKind::kFloat:
      return CppFloatLiteral(v.float_value, /*is_float=*/true);
    case TypeKind::kDouble:
      return CppFloatLiteral(v.float_value, /*is_float=*/false);
    case TypeKind::kUint64:
      return std::to_string(static_cast<uint64_t>(v.int_value)) + "ull";
    case TypeKind::kUint32:
    case TypeKind::kUint16:
    case TypeKind::kUint8:
      return std::to_string(static_cast<uint32_t>(v.int_value)) + "u";
    default:  // signed integer scalar
      return std::to_string(v.int_value);
  }
}

// A struct field's brace-init contents: "{}" (value-initialized) unless
// it has a `= literal` default (see ast.h's DefaultValue and
// parser.cc's ParseDefaultValue for which kinds can have one), in which
// case it's "{<expr>}" so the field starts life already set to it.
std::string CppFieldInitializer(const StructField& f) {
  if (!f.default_value.has_value) return "{}";
  return "{" + DefaultValueExpr(f.type, f.default_value) + "}";
}

// (real-mojom-parity phase 2) A const's type is no longer scalar-only --
// CppValueType(c.type) handles string/enum (incl. a possibly-nested,
// possibly-cross-file enum type, already qualified) the same way a field's
// type does. `std::string` specifically uses `inline const`, not `inline
// constexpr` -- constexpr std::string isn't reliably portable pre-C++23
// even under C++20 (heap-allocated contents aren't usable in a constant
// expression on every implementation), so this compiler doesn't rely on it.
void EmitConst(std::ostream& os, const ConstDecl& c) {
  const char* qualifier =
      c.type.kind == TypeKind::kString ? "inline const " : "inline constexpr ";
  os << qualifier << CppValueType(c.type) << " " << c.name << " = "
     << DefaultValueExpr(c.type, c.value) << ";\n";
}

// (real-mojom-parity phase 2) A nested const's real value, emitted
// namespace-scope under its mangled name -- exactly the nested-enum
// treatment (EmitNestedEnumDefinition), for the identical ordering reason
// (see MangledNestedName's comment).
void EmitNestedConstDefinition(std::ostream& os, const std::string& container,
                                const ConstDecl& c) {
  ConstDecl mangled = c;
  mangled.name = MangledNestedName(container, c.name);
  EmitConst(os, mangled);
}

// Every struct is wire-extensible, always -- there's no such thing as a
// "final" struct in mojom, only fields that happen not to have an
// explicit [MinVersion=N] yet (equivalent to N=0). So WriteX always
// starts with a little-endian StructHeader (WriteStructHeader /
// PatchStructHeader, the same 8-byte {num_bytes, version} shape real
// Mojo's own generated code uses -- message_internal.h, already
// transitively included), even for a struct
// whose fields are all min_version 0.
//
// num_bytes isn't known until every field's been written (this compiler
// has no separate size-computation pass the way real Mojo's generator
// does), so it's backpatched: reserve 8 placeholder bytes, write every
// field (a writer always writes everything ITS OWN schema has -- there's
// no "writer's version" narrower than "all the fields it was compiled
// with"), then overwrite the placeholder via mutable_payload() now that
// the final size is known. ReadX does the mirror image: read the header,
// compute where this struct's data ends from num_bytes, read each field
// only if the header's version says it's actually present (fields are
// wire-ordered = declaration-ordered = version-ordered, so a field's own
// missing check also means every later field is missing -- no field
// needs to know about its neighbors), and -- regardless of how many
// fields it actually understood -- jump *offset to that computed end.
// That unconditional jump is what makes this work in both directions: an
// old reader skips a newer writer's trailing fields it doesn't know
// about; a new reader reading an old writer's shorter struct just leaves
// its newer fields at their already-value-initialized (or defaulted)
// state, per-field version checks having simply never fired.
void EmitStructType(std::ostream& os, const StructDecl& s,
                    const std::string& module_ns,
                    const GeneratorOptions& opt) {
  if (const std::string* mapped =
          LookupTypemap(opt, s.name, s.owner_container, module_ns)) {
    os << "using " << CppEmbedName(s) << " = " << *mapped << ";\n\n";
    return;
  }
  os << "struct " << CppEmbedName(s) << " {\n";
  for (const EnumDecl& e : s.enums) {
    os << "  using " << e.name << " = " << MangledNestedName(s.name, e.name)
       << ";\n";
  }
  for (const StructField& f : s.fields) {
    os << "  " << CppValueType(f.type) << " " << f.name
       << CppFieldInitializer(f) << ";\n";
  }
  EmitTypeInternMembers(os, NamedKey(module_ns, CppEmbedName(s), "struct"),
                        CppEmbedName(s));
  os << "};\n\n";
}

void EmitWriteReadForward(std::ostream& os, const std::string& name,
                          bool needs_ctx) {
  os << "void Write" << name << "(mojo::Message& message, "
     << (needs_ctx ? "" : "const ") << name << "& value);\n";
  os << "bool Read" << name << "(const mojo::Message& message, size_t* offset, "
     << name << "* out";
  if (needs_ctx) {
    os << ",\n                   std::vector<mojo::ScopedHandle>* wvc_handles,\n"
          "                   size_t* wvc_handle_idx,\n"
          "                   mojo::MultiplexRouter* router";
  }
  os << ");\n";
}

void EmitStructIO(std::ostream& os, const StructDecl& s,
                  const EnumExtensibleMap& enum_extensible,
                  const HandleCtxSet& ctx, const GeneratorOptions& opt,
                  const std::string& module_ns) {
  const std::string n = CppEmbedName(s);
  if (LookupTypemap(opt, s.name, s.owner_container, module_ns)) {
    os << "inline void Write" << n << "(mojo::Message& message, const " << n
       << "& value) {\n";
    os << "  mojo::NativeTraits<" << n << ">::Write(message, value);\n";
    os << "}\n\n";
    os << "inline bool Read" << n
       << "(const mojo::Message& message, size_t* offset, " << n
       << "* out) {\n";
    os << "  return mojo::NativeTraits<" << n
       << ">::Read(message, offset, out);\n";
    os << "}\n\n";
    return;
  }
  const bool needs_ctx = ctx.count(n) != 0;
  os << "inline void Write" << n
     << "(mojo::Message& message, " << (needs_ctx ? "" : "const ")
     << n << "& value) {\n";
  os << "  size_t wvc_header_offset = message.payload_num_bytes();\n";
  os << "  mojo::internal::WriteStructHeader(&message, 0, 0);\n";
  {
    int uid = 0;
    for (size_t i : WireFieldOrder(s)) {
      const StructField& f = s.fields[i];
      EmitWriteValue(os, "  ", "message", f.type, "value." + f.name, &uid,
                     ctx);
    }
  }
  os << "  mojo::internal::PatchStructHeader(\n";
  os << "      message.mutable_payload() + wvc_header_offset,\n";
  os << "      static_cast<uint32_t>(message.payload_num_bytes() - "
        "wvc_header_offset),\n";
  os << "      " << s.version << "u);\n";
  os << "}\n\n";

  os << "inline bool Read" << n
     << "(const mojo::Message& message, size_t* offset, " << n
     << "* out";
  if (needs_ctx) {
    os << ",\n                    std::vector<mojo::ScopedHandle>* wvc_handles,\n"
          "                    size_t* wvc_handle_idx,\n"
          "                    mojo::MultiplexRouter* router";
  }
  os << ") {\n";
  if (needs_ctx) {
    os << "  (void)wvc_handles;\n";
    os << "  (void)wvc_handle_idx;\n";
    os << "  (void)router;\n";
  }
  os << "  uint32_t wvc_num_bytes = 0;\n";
  os << "  uint32_t wvc_version = 0;\n";
  os << "  if (!mojo::internal::ReadStructHeader(message, offset, "
        "&wvc_num_bytes, &wvc_version)) {\n";
  os << "    return false;\n";
  os << "  }\n";
  os << "  if (wvc_num_bytes < 8u) return false;\n";
  os << "  size_t wvc_struct_end =\n";
  os << "      *offset - 8u + wvc_num_bytes;\n";
  os << "  if (wvc_struct_end > message.payload_num_bytes()) return "
        "false;\n";
  {
    int uid = 0;
    for (size_t i : WireFieldOrder(s)) {
      const StructField& f = s.fields[i];
      if (f.min_version == 0) {
        EmitReadInto(os, "  ", "message", "offset", f.type,
                     "out->" + f.name, &uid, enum_extensible, ctx,
                     "wvc_version");
        continue;
      }
      os << "  if (wvc_version >= " << f.min_version << "u) {\n";
      EmitReadInto(os, "    ", "message", "offset", f.type,
                   "out->" + f.name, &uid, enum_extensible, ctx,
                   "wvc_version");
      os << "  }\n";
    }
  }
  os << "  *offset = wvc_struct_end;\n";
  os << "  return true;\n";
  os << "}\n\n";
}

// Generated as a plain class with one storage member per field (not a real
// C++ union) -- same reasoning as EmitStruct's flat, no-reflection style:
// this keeps the field's own WriteX/ReadX (or scalar Read/WriteScalar) code
// path identical whether it's writing a top-level param, a struct field, or
// a union field, with no packed-storage/placement-new bookkeeping. `Tag`
// enumerator names are the field names verbatim -- no case transform, same
// as EmitEnum's value names.
//
// Every union is wire-framed with a leading uint32_t size (same
// backpatch-then-fix-up approach as EmitStruct's StructHeader, just one
// field instead of two -- a union only ever has ONE active variant on the
// wire, so there's no per-field version gating the way a struct has,
// nothing to have `MinVersion` gate at read time) -- this is what lets an
// old reader safely skip a union carrying a tag it doesn't recognize
// instead of desyncing whatever comes after it in the message. What
// happens on an unrecognized tag depends on `is_extensible`: a
// non-extensible union still just fails the read (`return false;`, same
// as before this had a size prefix at all) -- the size buys nothing for
// it directly, but keeps the wire format identical to an extensible
// union of the same fields, so a schema can safely flip `[Extensible]`
// on later without a wire-incompatible change. An extensible union
// instead reports a synthetic `Tag::kUnknown` (which() returns it, no
// value is recoverable) and skips forward -- the same
// receiver-tolerance-only scope as Interface::is_extensible, not full
// mojom union extensibility (no designated default-field convention).
void EmitUnionType(std::ostream& os, const UnionDecl& u,
                   const std::string& module_ns) {
  const std::string n = CppEmbedName(u);
  os << "class " << n << " {\n";
  os << " public:\n";
  os << "  enum class Tag : int32_t {\n";
  for (const UnionField& f : u.fields) {
    os << "    " << f.name << " = " << f.tag << ",\n";
  }
  if (u.is_extensible) {
    os << "    kUnknown = -1,\n";
  }
  os << "  };\n\n";
  os << "  static constexpr uint32_t kVersion = " << u.version << "u;\n\n";
  EmitTypeInternMembers(os, NamedKey(module_ns, n, "union"), n);
  os << "  " << n << "() = default;\n\n";
  os << "  Tag which() const { return tag_; }\n\n";
  for (const UnionField& f : u.fields) {
    os << "  void set_" << f.name << "(" << CppValueType(f.type)
       << " value) {\n";
    os << "    tag_ = Tag::" << f.name << ";\n";
    os << "    " << f.name << "_ = std::move(value);\n";
    os << "  }\n";
    os << "  " << CppValueType(f.type) << "& " << f.name
       << "() { return " << f.name << "_; }\n";
    os << "  const " << CppValueType(f.type) << "& " << f.name
       << "() const { return " << f.name << "_; }\n\n";
  }
  if (u.is_extensible) {
    os << "  // Set by ReadUnionName when the wire tag doesn't match any\n";
    os << "  // field this union declares -- see this function's own "
          "comment.\n";
    os << "  void set_unknown() { tag_ = Tag::kUnknown; }\n\n";
  }
  os << " private:\n";
  os << "  Tag tag_ = Tag::" << u.fields.front().name << ";\n";
  for (const UnionField& f : u.fields) {
    os << "  " << CppValueType(f.type) << " " << f.name << "_{};\n";
  }
  os << "};\n\n";
}

void EmitUnionIO(std::ostream& os, const UnionDecl& u,
                 const EnumExtensibleMap& enum_extensible,
                 const HandleCtxSet& ctx) {
  const std::string n = CppEmbedName(u);
  const bool needs_ctx = ctx.count(n) != 0;
  os << "inline void Write" << n << "(mojo::Message& message, "
     << (needs_ctx ? "" : "const ") << n << "& value) {\n";
  os << "  size_t wvc_header_offset = message.payload_num_bytes();\n";
  os << "  mojo::internal::WriteScalar(&message, static_cast<uint32_t>(0));\n";
  os << "  mojo::internal::WriteScalar(&message, "
        "static_cast<int32_t>(value.which()));\n";
  os << "  switch (value.which()) {\n";
  for (const UnionField& f : u.fields) {
    int uid = 0;
    os << "    case " << n << "::Tag::" << f.name << ":\n";
    os << "      {\n";
    EmitWriteValue(os, "        ", "message", f.type, "value." + f.name + "()",
                    &uid, ctx);
    os << "      }\n";
    os << "      break;\n";
  }
  os << "    default:\n";
  os << "      break;\n";
  os << "  }\n";
  os << "  uint32_t wvc_union_size = static_cast<uint32_t>(\n";
  os << "      message.payload_num_bytes() - wvc_header_offset);\n";
  os << "  mojo::internal::PatchUint32(message.mutable_payload() + "
        "wvc_header_offset, wvc_union_size);\n";
  os << "}\n\n";

  os << "inline bool Read" << n << "(const mojo::Message& message, "
        "size_t* offset, "
     << n << "* out";
  if (needs_ctx) {
    os << ",\n                   std::vector<mojo::ScopedHandle>* wvc_handles,\n"
          "                   size_t* wvc_handle_idx,\n"
          "                   mojo::MultiplexRouter* router";
  }
  os << ") {\n";
  if (needs_ctx) {
    os << "  (void)wvc_handles;\n";
    os << "  (void)wvc_handle_idx;\n";
    os << "  (void)router;\n";
  }
  os << "  uint32_t wvc_union_size = 0;\n";
  os << "  if (!mojo::internal::ReadScalar(message, offset, "
        "&wvc_union_size)) {\n";
  os << "    return false;\n";
  os << "  }\n";
  os << "  if (wvc_union_size < sizeof(wvc_union_size)) return false;\n";
  os << "  size_t wvc_union_end =\n";
  os << "      *offset - sizeof(wvc_union_size) + wvc_union_size;\n";
  os << "  if (wvc_union_end > message.payload_num_bytes()) return "
        "false;\n";
  os << "  int32_t wvc_tag = 0;\n";
  os << "  if (!mojo::internal::ReadScalar(message, offset, &wvc_tag)) "
        "return false;\n";
  os << "  switch (static_cast<" << n << "::Tag>(wvc_tag)) {\n";
  for (const UnionField& f : u.fields) {
    int uid = 0;
    os << "    case " << n << "::Tag::" << f.name << ": {\n";
    os << "      " << CppValueType(f.type) << " wvc_v{};\n";
    EmitReadInto(os, "      ", "message", "offset", f.type, "wvc_v", &uid,
                 enum_extensible, ctx, nullptr);
    os << "      out->set_" << f.name << "(std::move(wvc_v));\n";
    os << "      break;\n";
    os << "    }\n";
  }
  if (u.is_extensible) {
    const UnionField* def = nullptr;
    for (const UnionField& f : u.fields) {
      if (f.is_default) def = &f;
    }
    os << "    default:\n";
    if (def) {
      os << "      out->set_" << def->name << "(" << CppValueType(def->type)
         << "{});\n";
    } else {
      os << "      out->set_unknown();\n";
    }
    os << "      break;\n";
  } else {
    os << "    default:\n";
    os << "      return false;\n";
  }
  os << "  }\n";
  os << "  *offset = wvc_union_end;\n";
  os << "  return true;\n";
  os << "}\n\n";
}

// Splits a dotted (`a.b.c`, straight from a `module a.b.c;` statement) or
// "::"-separated (a `--namespace=a::b::c` CLI override, spelled the C++
// way) namespace string into its segments, so GenerateCppHeader can emit
// proper nested `namespace a { namespace b { ... } }` C++ instead of one
// invalid `namespace a.b.c {`. A plain single-segment name (the common
// case, and the only case before dotted module names existed) round-trips
// through this unchanged.
std::vector<std::string> SplitNamespace(const std::string& ns) {
  std::string normalized;
  normalized.reserve(ns.size());
  for (size_t i = 0; i < ns.size(); ++i) {
    if (ns[i] == ':' && i + 1 < ns.size() && ns[i + 1] == ':') {
      normalized.push_back('.');
      ++i;
    } else {
      normalized.push_back(ns[i]);
    }
  }
  std::vector<std::string> segments;
  size_t start = 0;
  while (start <= normalized.size()) {
    size_t dot = normalized.find('.', start);
    size_t end = dot == std::string::npos ? normalized.size() : dot;
    if (end > start) segments.push_back(normalized.substr(start, end - start));
    if (dot == std::string::npos) break;
    start = dot + 1;
  }
  return segments;
}

// The extra blocking overload a `[Sync]` method gets on Proxy_, alongside
// (never instead of) its normal callback-taking one -- same shape real
// mojom's C++ generator gives a sync method: same name, response values
// come back through trailing out-pointers and a bool return (false means
// "didn't get a response" -- a send failure or a SyncWaitFor error, never
// distinguished further, matching how the async path already discards
// SendMessage's own result). Built entirely on
// WASMCadidumBindings::Connector::SyncWaitFor -- see that header's comment
// for why blocking here means cooperatively pumping whp::Executor, not a
// real OS-level wait.
//
// This overload only exists on Proxy_, not on the pure-virtual Interface
// itself (Stub_/impl_ never needs to make a sync call to itself, so it was
// never a good fit for Interface's pure-virtual list) -- which means a
// caller reaches it through `remote.proxy()->Method(...)` /
// `associated_remote.proxy()->Method(...)`, not the ordinary
// `remote->Method(...)` (that operator-> only ever returns Interface*).
// See remote.h's/associated_remote.h's proxy() accessor, added to
// WASMCadidumBindings alongside Connector::SyncWaitFor specifically to
// make this reachable.
void EmitSyncProxyMethod(std::ostream& os, const std::string& iface_name,
                          const Method& m,
                          const EnumExtensibleMap& enum_extensible,
                          const HandleCtxSet& ctx) {
  os << "  // [Sync] blocking overload -- see WASMCadidumBindings'\n";
  os << "  // Connector::SyncWaitFor for what \"blocking\" means here.\n";
  os << "  [[nodiscard]] bool " << m.name << "(";
  {
    bool first = true;
    for (const Param& p : m.params) {
      if (!first) os << ", ";
      first = false;
      os << CppParamDecl(p, ctx);
    }
    for (const Param& rp : m.response_params) {
      if (!first) os << ", ";
      first = false;
      os << ResponseValueType(m, rp) << "* " << rp.name;
    }
  }
  os << ") {\n";
  os << "    bool wvc_sync_done = false;\n";
  for (const Param& rp : m.response_params) {
    os << "    " << ResponseValueType(m, rp) << " wvc_sync_" << rp.name
       << "{};\n";
  }
  os << "    uint64_t request_id = responses_.RegisterPendingResponse(\n";
  os << "        [&](mojo::Message* response) {\n";
  os << "          size_t offset = 0;\n";
  {
    bool handles_emitted = false;
    int resp_uid = 0;
    bool resp_needs = false;
    for (const Param& rp : m.response_params) {
      if (TypeNeedsHandleCtx(rp.type, ctx)) resp_needs = true;
    }
    if (resp_needs) {
      EmitHandleContextDecls(os, "          ", "response");
    }
    for (const Param& rp : m.response_params) {
      EmitTopReadParam(os, "          ", "response", "*response", rp,
                        &handles_emitted, &resp_uid, enum_extensible, ctx);
    }
  }
  for (const Param& rp : m.response_params) {
    if (m.is_result_response) {
      os << "          wvc_sync_" << rp.name << " = " << rp.name
         << ".which() == " << CppValueType(rp.type)
         << "::Tag::value\n";
      os << "              ? " << ExpectedCppType(m) << "(std::move(" << rp.name
         << ".value()))\n";
      os << "              : " << ExpectedCppType(m)
         << "(base::unexpected(std::move(" << rp.name << ".error())));\n";
    } else {
      os << "          wvc_sync_" << rp.name << " = std::move(" << rp.name
         << ");\n";
    }
  }
  os << "          wvc_sync_done = true;\n";
  os << "          return true;\n";
  os << "        });\n";
  int uid = 0;
  for (const Param& p : m.params) EmitTopWritePrelude(os, "    ", p);
  os << "    mojo::Message message(" << iface_name << "::k" << m.name
     << "Name, mojo::Message::kFlagExpectsResponse, id_);\n";
  os << "    message.set_request_id(request_id);\n";
  for (const Param& p : m.params) {
    EmitTopWritePayload(os, "    ", "message", p, &uid, ctx);
  }
  os << "    if (!router_->SendMessage(&message)) return false;\n";
  os << "    if (!router_->connector().SyncWaitFor(\n";
  os << "            [&] { return wvc_sync_done; })) {\n";
  os << "      return false;\n";
  os << "    }\n";
  for (const Param& rp : m.response_params) {
    os << "    *" << rp.name << " = std::move(wvc_sync_" << rp.name << ");\n";
  }
  os << "    return true;\n";
  os << "  }\n\n";
}

void EmitInterface(std::ostream& os, const Interface& iface,
                    const EnumExtensibleMap& enum_extensible,
                    const HandleCtxSet& ctx, const std::string& module_ns) {
  // ---- Interface (pure virtual) + nested Stub_ ----
  // (v16) iface.enums' real definitions are emitted elsewhere (namespace-
  // scope, early -- see EmitNestedEnumDefinition/MangledNestedEnumName);
  // nothing about them appears inside this class's own body.
  os << "class " << iface.name << " {\n";
  os << " public:\n";
  for (const EnumDecl& e : iface.enums) {
    os << "  using " << e.name << " = "
       << MangledNestedName(iface.name, e.name) << ";\n";
  }
  for (const StructDecl& s : iface.structs) {
    os << "  using " << s.name << " = " << CppEmbedName(s) << ";\n";
  }
  for (const UnionDecl& u : iface.unions) {
    os << "  using " << u.name << " = " << CppEmbedName(u) << ";\n";
  }
  os << "  virtual ~" << iface.name << "() = default;\n";
  for (const Method& m : iface.methods) {
    os << "  virtual void " << m.name << "(";
    bool first = true;
    for (const Param& p : m.params) {
      if (!first) os << ", ";
      first = false;
      os << CppParamDecl(p, ctx);
    }
    if (m.has_response) {
      if (!first) os << ", ";
      os << ResponseCallbackType(m) << " callback";
    }
    os << ") = 0;\n";
  }
  os << "\n";
  for (const Method& m : iface.methods) {
    os << "  static constexpr uint32_t k" << m.name << "Name = " << m.ordinal
       << ";\n";
  }
  os << "  static constexpr uint32_t kVersion = " << iface.version << "u;\n";
  os << "\n";
  EmitTypeInternMembers(os, NamedKey(module_ns, iface.name, "interface"),
                        iface.name);
  os << "  class Proxy_;  // defined below, once " << iface.name
     << " is a complete type\n\n";
  os << "  class Stub_ : public mojo::MessageReceiver {\n";
  os << "   public:\n";
  os << "    Stub_(" << iface.name << "* impl,\n";
  os << "          mojo::MultiplexRouter* router,\n";
  os << "          mojo::InterfaceId id = mojo::kPrimaryInterfaceId)\n";
  os << "        : impl_(impl), router_(router), id_(id) {}\n\n";
  os << "    [[nodiscard]] bool Accept(mojo::Message* message) override {\n";
  os << "      if (message->name() == mojo::internal::kRunMessageId) {\n";
  os << "        return mojo::internal::HandleQueryVersionMessage(\n";
  os << "            message, router_, id_, kVersion);\n";
  os << "      }\n";
  os << "      if (message->name() == "
        "mojo::internal::kRunOrClosePipeMessageId) {\n";
  os << "        return mojo::internal::HandleRequireVersionMessage(\n";
  os << "            *message, kVersion);\n";
  os << "      }\n";
  for (const Method& m : iface.methods) {
    os << "      if (message->name() == k" << m.name
       << "Name) return Accept" << m.name << "(message);\n";
  }
  if (iface.is_extensible) {
    os << "      // [Extensible]: an unrecognized message ordinal is\n";
    os << "      // tolerated, not a protocol error -- silently drop it\n";
    os << "      // rather than raising a connection error over it.\n";
    os << "      return true;\n";
  } else {
    os << "      return false;\n";
  }
  os << "    }\n\n";
  os << "   private:\n";
  for (const Method& m : iface.methods) {
    int uid = 0;
    bool handles_emitted = false;
    os << "    bool Accept" << m.name << "(mojo::Message* message) {\n";
    os << "      size_t offset = 0;\n";
    if (m.params.empty() && !m.has_response) {
      os << "      (void)message;\n";
      os << "      (void)offset;\n";
    } else if (m.params.empty()) {
      os << "      (void)offset;\n";
    }
    bool req_needs_ctx = false;
    for (const Param& p : m.params) {
      if (TypeNeedsHandleCtx(p.type, ctx)) req_needs_ctx = true;
    }
    if (req_needs_ctx) {
      EmitHandleContextDecls(os, "      ", "message");
    }
    for (const Param& p : m.params) {
      EmitTopReadParam(os, "      ", "message", "*message", p,
                        &handles_emitted, &uid, enum_extensible, ctx);
    }
    if (m.has_response) {
      os << "      const uint64_t request_id = message->request_id();\n";
      if (!req_needs_ctx) {
        os << "      mojo::MultiplexRouter* router = router_;\n";
      }
      os << "      const mojo::InterfaceId id = id_;\n";
      os << "      impl_->" << m.name << "(";
      for (const Param& p : m.params) os << ParamPassExpr(p, ctx) << ", ";
      os << "[router, id, request_id](";
      {
        bool rfirst = true;
        for (const Param& rp : m.response_params) {
          if (!rfirst) os << ", ";
          rfirst = false;
          os << ResponseValueType(m, rp) << " " << rp.name;
        }
      }
      os << ") {\n";
      if (m.is_result_response) {
        const Param& rp = m.response_params[0];
        os << "        " << CppValueType(rp.type) << " wvc_wire;\n";
        os << "        if (" << rp.name << ".has_value()) {\n";
        os << "          wvc_wire.set_value(std::move(" << rp.name
           << ".value()));\n";
        os << "        } else {\n";
        os << "          wvc_wire.set_error(std::move(" << rp.name
           << ".error()));\n";
        os << "        }\n";
      }
      os << "        mojo::Message response(" << iface.name << "::k"
         << m.name << "Name, mojo::Message::kFlagIsResponse, id);\n";
      os << "        response.set_request_id(request_id);\n";
      int response_uid = 0;
      for (const Param& rp : m.response_params) {
        Param write_p = rp;
        if (m.is_result_response) write_p.name = "wvc_wire";
        EmitTopWritePrelude(os, "        ", write_p);
      }
      for (const Param& rp : m.response_params) {
        Param write_p = rp;
        if (m.is_result_response) write_p.name = "wvc_wire";
        EmitTopWritePayload(os, "        ", "response", write_p, &response_uid,
                            ctx);
      }
      os << "        (void)router->SendMessage(&response);\n";
      os << "      });\n";
    } else {
      os << "      impl_->" << m.name << "(";
      bool first2 = true;
      for (const Param& p : m.params) {
        if (!first2) os << ", ";
        first2 = false;
        os << ParamPassExpr(p, ctx);
      }
      os << ");\n";
    }
    os << "      return true;\n";
    os << "    }\n\n";
  }
  os << "    " << iface.name << "* impl_;\n";
  os << "    mojo::MultiplexRouter* router_;\n";
  os << "    mojo::InterfaceId id_;\n";
  os << "  };\n";
  os << "};\n\n";

  // ---- Proxy_ (defined out-of-line, mirrors examples/echo) ----
  os << "class " << iface.name << "::Proxy_ : public " << iface.name
     << ", public mojo::MessageReceiver {\n";
  os << " public:\n";
  os << "  explicit Proxy_(mojo::MultiplexRouter* router,\n";
  os << "                   mojo::InterfaceId id = "
        "mojo::kPrimaryInterfaceId)\n";
  os << "      : router_(router), id_(id) {}\n\n";

  for (const Method& m : iface.methods) {
    os << "  void " << m.name << "(";
    {
      bool first = true;
      for (const Param& p : m.params) {
        if (!first) os << ", ";
        first = false;
        os << CppParamDecl(p, ctx);
      }
      if (m.has_response) {
        if (!first) os << ", ";
        os << ResponseCallbackType(m) << " callback";
      }
    }
    os << ") override {\n";
    int uid = 0;
    if (m.has_response) {
      os << "    uint64_t request_id = "
            "responses_.RegisterPendingResponse(\n";
      bool resp_needs = false;
      for (const Param& rp : m.response_params) {
        if (TypeNeedsHandleCtx(rp.type, ctx)) resp_needs = true;
      }
      if (resp_needs) {
        os << "        [callback = std::move(callback), router = router_]("
              "mojo::Message* response) mutable {\n";
      } else {
        os << "        [callback = std::move(callback)](mojo::Message* "
              "response) mutable {\n";
      }
      os << "          size_t offset = 0;\n";
      if (m.response_params.empty()) {
        os << "          (void)response;\n";
        os << "          (void)offset;\n";
      }
      bool handles_emitted = false;
      int resp_uid = 0;
      if (resp_needs) {
        os << "          std::vector<mojo::ScopedHandle> wvc_handles_store = "
              "response->TakeHandles();\n";
        os << "          std::vector<mojo::ScopedHandle>* wvc_handles = "
              "&wvc_handles_store;\n";
        os << "          size_t wvc_handle_idx_store = 0;\n";
        os << "          size_t* wvc_handle_idx = &wvc_handle_idx_store;\n";
        os << "          (void)router;\n";
      }
      for (const Param& rp : m.response_params) {
        EmitTopReadParam(os, "          ", "response", "*response", rp,
                          &handles_emitted, &resp_uid, enum_extensible, ctx);
      }
      if (m.is_result_response) {
        const Param& rp = m.response_params[0];
        os << "          std::move(callback).Run(" << rp.name << ".which() == "
           << CppValueType(rp.type) << "::Tag::value\n";
        os << "              ? " << ExpectedCppType(m) << "(std::move("
           << rp.name << ".value()))\n";
        os << "              : " << ExpectedCppType(m)
           << "(base::unexpected(std::move(" << rp.name << ".error()))));\n";
      } else {
        os << "          std::move(callback).Run(";
        {
          bool rfirst = true;
          for (const Param& rp : m.response_params) {
            if (!rfirst) os << ", ";
            rfirst = false;
            os << ParamPassExpr(rp, ctx);
          }
        }
        os << ");\n";
      }
      os << "          return true;\n";
      os << "        });\n";
      for (const Param& p : m.params) EmitTopWritePrelude(os, "    ", p);
      os << "    mojo::Message message(k" << m.name
         << "Name, mojo::Message::kFlagExpectsResponse, id_);\n";
      os << "    message.set_request_id(request_id);\n";
      for (const Param& p : m.params) {
        EmitTopWritePayload(os, "    ", "message", p, &uid, ctx);
      }
      os << "    (void)router_->SendMessage(&message);\n";
    } else {
      for (const Param& p : m.params) EmitTopWritePrelude(os, "    ", p);
      os << "    mojo::Message message(k" << m.name << "Name, 0, id_);\n";
      for (const Param& p : m.params) {
        EmitTopWritePayload(os, "    ", "message", p, &uid, ctx);
      }
      os << "    (void)router_->SendMessage(&message);\n";
    }
    os << "  }\n\n";

    if (m.is_sync) {
      EmitSyncProxyMethod(os, iface.name, m, enum_extensible, ctx);
    }
  }

  // QueryVersion/RequireVersion are emitted unconditionally, on every
  // interface -- not behind any .voodoom-level attribute, since they're
  // protocol-level (see README's "Interface versioning" and
  // WASMCadidumBindings' interface_control_messages.h). Because
  // QueryVersion always expects a response, responses_/Accept()'s
  // response dispatch below are also always declared now, not gated on
  // whether the interface happens to have any response-bearing
  // *application* method (any_response, before this feature existed).
  os << "  void QueryVersion(base::OnceCallback<void(uint32_t)> callback) {\n";
  os << "    uint64_t request_id = "
        "responses_.RegisterPendingResponse(\n";
  os << "        [callback = std::move(callback)](mojo::Message* response) "
        "mutable {\n";
  os << "          size_t offset = 0;\n";
  os << "          uint32_t tag = 0;\n";
  os << "          uint32_t version = 0;\n";
  os << "          if (!mojo::internal::ReadScalar(*response, &offset, "
        "&tag) ||\n";
  os << "              tag != mojo::internal::kRunQueryVersionResult ||\n";
  os << "              !mojo::internal::ReadScalar(*response, &offset, "
        "&version)) {\n";
  os << "            return false;\n";
  os << "          }\n";
  os << "          std::move(callback).Run(version);\n";
  os << "          return true;\n";
  os << "        });\n";
  os << "    mojo::Message message(mojo::internal::kRunMessageId,\n";
  os << "                          mojo::Message::kFlagExpectsResponse, "
        "id_);\n";
  os << "    message.set_request_id(request_id);\n";
  os << "    mojo::internal::WriteScalar(&message, "
        "mojo::internal::kRunQueryVersion);\n";
  os << "    (void)router_->SendMessage(&message);\n";
  os << "  }\n\n";
  os << "  void RequireVersion(uint32_t version) {\n";
  os << "    mojo::Message message(mojo::internal::kRunOrClosePipeMessageId, "
        "0, id_);\n";
  os << "    mojo::internal::WriteScalar(&message, "
        "mojo::internal::kRunRequireVersion);\n";
  os << "    mojo::internal::WriteScalar(&message, version);\n";
  os << "    (void)router_->SendMessage(&message);\n";
  os << "  }\n\n";

  os << "  [[nodiscard]] bool Accept(mojo::Message* message) override {\n";
  os << "    if (!message->has_flag(mojo::Message::kFlagIsResponse)) {\n";
  os << "      return false;\n";
  os << "    }\n";
  os << "    return responses_.DispatchResponse(message);\n";
  os << "  }\n\n";
  os << " private:\n";
  os << "  mojo::MultiplexRouter* router_;\n";
  os << "  mojo::InterfaceId id_;\n";
  os << "  mojo::internal::ResponseDispatcher responses_;\n";
  os << "};\n\n";
}

}  // namespace

namespace {

struct Embed {
  int decl_index;
  const StructDecl* s;  // exactly one of s/u is non-null
  const UnionDecl* u;
};

}  // namespace

// Complete-type topological sort: B depends on A if B has a by-value /
// optional / array<T,N> field of type A. array<T>/map<K,T> do not create
// an edge (CageVector/CageMap of incomplete T). Independent types keep
// file order via decl_index. Parser already rejected true by-value cycles.
std::vector<Embed> SortEmbeds(std::vector<Embed> embeds) {
  const int n = static_cast<int>(embeds.size());
  std::unordered_map<std::string, int> index;
  for (int i = 0; i < n; ++i) {
    const std::string name =
        embeds[i].s ? EmbedDeclKey(embeds[i].s->owner_container, embeds[i].s->name)
                    : EmbedDeclKey(embeds[i].u->owner_container, embeds[i].u->name);
    index[name] = i;
  }
  std::vector<std::vector<int>> outgoing(static_cast<size_t>(n));
  std::vector<int> indeg(static_cast<size_t>(n), 0);
  for (int i = 0; i < n; ++i) {
    std::vector<std::string> deps;
    if (embeds[i].s) {
      for (const StructField& f : embeds[i].s->fields) {
        AppendCompleteTypeDeps(f.type, &deps);
      }
    } else {
      for (const UnionField& f : embeds[i].u->fields) {
        AppendCompleteTypeDeps(f.type, &deps);
      }
    }
    std::unordered_set<std::string> seen;
    for (const std::string& d : deps) {
      if (!seen.insert(d).second) continue;
      auto it = index.find(d);
      if (it == index.end()) continue;
      outgoing[static_cast<size_t>(it->second)].push_back(i);
      indeg[static_cast<size_t>(i)]++;
    }
  }
  std::set<std::pair<int, int>> ready;
  for (int i = 0; i < n; ++i) {
    if (indeg[static_cast<size_t>(i)] == 0) {
      ready.insert({embeds[static_cast<size_t>(i)].decl_index, i});
    }
  }
  std::vector<Embed> out;
  out.reserve(static_cast<size_t>(n));
  while (!ready.empty()) {
    int i = ready.begin()->second;
    ready.erase(ready.begin());
    out.push_back(embeds[static_cast<size_t>(i)]);
    for (int j : outgoing[static_cast<size_t>(i)]) {
      if (--indeg[static_cast<size_t>(j)] == 0) {
        ready.insert({embeds[static_cast<size_t>(j)].decl_index, j});
      }
    }
  }
  if (out.size() != static_cast<size_t>(n)) {
    throw std::logic_error(
        "struct/union complete-type cycle in codegen (parser should have "
        "rejected this)");
  }
  return out;
}

std::string GenerateCppHeader(const Module& module,
                               const std::string& header_guard,
                               const std::string& cpp_namespace,
                               const std::string& source_filename,
                               const std::vector<std::string>& imported_headers,
                               const GeneratorOptions& options) {
  std::ostringstream os;
  os << "// Generated by WASMVoodooCompile (voodoomc) from '"
     << source_filename
     << "'. Do not edit by hand -- edit the .voodoom source and "
        "regenerate.\n";
  os << "#ifndef " << header_guard << "\n";
  os << "#define " << header_guard << "\n\n";
  os << "#include \"mojo/public/cpp/bindings/lib/interface_control_messages.h\"\n";
  os << "#include \"mojo/public/cpp/bindings/lib/multiplex_router.h\"\n";
  os << "#include \"mojo/public/cpp/bindings/lib/response_dispatcher.h\"\n";
  os << "#include \"mojo/public/cpp/bindings/lib/wire_primitives.h\"\n";
  os << "#include \"mojo/public/cpp/bindings/message.h\"\n";
  os << "#include \"src/sandbox/cage-allocator.h\"\n";
  os << "#include "
        "\"mojo/public/cpp/bindings/pending_associated_receiver.h\"\n";
  os << "#include "
        "\"mojo/public/cpp/bindings/pending_associated_remote.h\"\n";
  os << "#include \"mojo/public/cpp/bindings/pending_receiver.h\"\n";
  os << "#include \"mojo/public/cpp/bindings/pending_remote.h\"\n";
  os << "#include \"base/callback.h\"\n";
  os << "#include \"base/expected.h\"\n";
  os << "#include \"mojo/public/cpp/bindings/lib/type_intern.h\"\n";
  os << "#include \"mojo/public/cpp/system/handle.h\"\n";
  os << "#include \"mojo/public/cpp/system/platform_handle.h\"\n";
  os << "#include \"mojo/public/cpp/system/message_pipe.h\"\n";
  os << "#include \"mojo/public/cpp/system/data_pipe.h\"\n";
  os << "#include \"mojo/public/cpp/system/buffer.h\"\n\n";
  os << "#include <array>\n";
  os << "#include <cstdint>\n";
  os << "#include <cstring>\n";
  os << "#include <limits>\n";
  os << "#include <functional>\n";
  os << "#include <optional>\n";
  os << "#include <string>\n";
  os << "#include <unordered_map>\n";
  os << "#include <utility>\n";
  os << "#include <vector>\n";
  // (v15) One #include per direct import's own generated header -- every
  // cross-file type this header references (TypeSpec::owner_namespace) was
  // declared in one of these, now that import no longer flattens the whole
  // graph into this one file.
  if (!imported_headers.empty()) {
    os << "\n";
    for (const std::string& h : imported_headers) {
      os << "#include \"" << h << "\"\n";
    }
  }
  os << "\n";
  os << "#ifndef MOJO_NATIVE_TRAITS_DEFINED\n";
  os << "#define MOJO_NATIVE_TRAITS_DEFINED\n";
  os << "namespace mojo {\n";
  os << "template <typename T>\n";
  os << "struct NativeTraits {\n";
  os << "  static void Write(Message&, const T&) = delete;\n";
  os << "  static bool Read(const Message&, size_t*, T*) = delete;\n";
  os << "};\n";
  os << "}  // namespace mojo\n";
  os << "#endif\n";
  for (const std::string& inc : options.extra_includes) {
    os << "#include \"" << inc << "\"\n";
  }
  os << "\n";
  std::vector<std::string> namespaces = SplitNamespace(cpp_namespace);
  for (const std::string& ns : namespaces) {
    os << "namespace " << ns << " {\n";
  }
  os << "\n";

  // Interfaces are always forward-declared up front, regardless of file
  // order: pending_(associated_)remote/receiver<T> only ever need T to be
  // an incomplete type (they store a handle/id, never a T member), so
  // nothing below -- enums, structs, or other interfaces -- has to wait
  // for an interface's full definition.
  for (const Interface& iface : module.interfaces) {
    os << "class " << iface.name << ";\n";
  }
  if (!module.interfaces.empty()) os << "\n";

  // Structs and unions are also forward-declared so CageVector/CageMap
  // fields can name an incomplete T (values.mojom DictionaryValue /
  // ListValue holding Value). By-value members still need the complete
  // type; SortEmbeds below emits those owners after their dependencies.
  for (const StructDecl* s : AllStructs(module)) {
    if (LookupTypemap(options, s->name, s->owner_container, module.name)) {
      continue;
    }
    os << "struct " << CppEmbedName(*s) << ";\n";
  }
  for (const UnionDecl* u : AllUnions(module)) {
    os << "class " << CppEmbedName(*u) << ";\n";
  }
  if (!AllStructs(module).empty() || !AllUnions(module).empty()) os << "\n";

  EnumExtensibleMap enum_extensible;
  HandleCtxSet handle_ctx = BuildHandleCtx(module);
  for (const EnumDecl& e : module.enums) {
    enum_extensible[e.name] = e.is_extensible;
  }
  // (v16) Nested enums need the same treatment -- EmitReadInto's
  // IsEnumExtensible check is keyed by bare name regardless of nesting
  // (same flat-namespace caveat as everywhere else in this design; see
  // README's "Known simplifications").
  for (const StructDecl* s : AllStructs(module)) {
    for (const EnumDecl& e : s->enums) enum_extensible[e.name] = e.is_extensible;
  }
  for (const Interface& iface : module.interfaces) {
    for (const EnumDecl& e : iface.enums) {
      enum_extensible[e.name] = e.is_extensible;
    }
  }
  for (const EnumDecl& e : module.enums) {
    EmitEnum(os, e, options, module.name);
  }
  // (v16) Nested enums' *real* definitions go here too -- same early,
  // ordering-free pass as top-level enums, under their mangled name (see
  // EmitNestedEnumDefinition for why: emitting them textually inside their
  // declaring interface/struct's own body isn't safe in general once a
  // struct can reference an interface's nested enum, or vice versa).
  for (const StructDecl* s : AllStructs(module)) {
    const std::string container =
        s->owner_container.empty() ? s->name : CppEmbedName(*s);
    for (const EnumDecl& e : s->enums) {
      EmitNestedEnumDefinition(os, container, e, options, module.name);
    }
  }
  for (const Interface& iface : module.interfaces) {
    for (const EnumDecl& e : iface.enums) {
      EmitNestedEnumDefinition(os, iface.name, e, options, module.name);
    }
  }
  // (real-mojom-parity phase 2) A const's value can reference an earlier-
  // declared const regardless of whether either is top-level or nested in
  // a struct/interface (see ConstDecl::decl_index's own comment) -- so
  // these can't be three separate "all top-level, then all of each
  // struct's, then all of each interface's" loops the way
  // EmitNestedEnumDefinition's loops above safely are (enums have no such
  // cross-referencing) -- combine and sort by decl_index first, mirroring
  // the Embed struct/sort just below for struct/union interleaving.
  struct ConstEmit {
    int decl_index;
    std::string container;  // "" for a top-level const
    const ConstDecl* c;
  };
  std::vector<ConstEmit> const_emits;
  for (const ConstDecl& c : module.consts) {
    const_emits.push_back({c.decl_index, "", &c});
  }
  for (const StructDecl* s : AllStructs(module)) {
    const std::string container =
        s->owner_container.empty() ? s->name : CppEmbedName(*s);
    for (const ConstDecl& c : s->consts) {
      const_emits.push_back({c.decl_index, container, &c});
    }
  }
  for (const Interface& iface : module.interfaces) {
    for (const ConstDecl& c : iface.consts) {
      const_emits.push_back({c.decl_index, iface.name, &c});
    }
  }
  // (real-mojom-parity phase 7) A feature's own consts get the exact same
  // mangled-namespace-scope treatment a struct's or interface's nested
  // consts already do -- a `feature` block itself never generates
  // anything else (see FeatureDecl's own comment in ast.h).
  for (const FeatureDecl& ft : module.features) {
    for (const ConstDecl& c : ft.consts) {
      const_emits.push_back({c.decl_index, ft.name, &c});
    }
  }
  std::sort(const_emits.begin(), const_emits.end(),
            [](const ConstEmit& a, const ConstEmit& b) {
              return a.decl_index < b.decl_index;
            });
  for (const ConstEmit& ce : const_emits) {
    if (ce.container.empty()) {
      EmitConst(os, *ce.c);
    } else {
      EmitNestedConstDefinition(os, ce.container, *ce.c);
    }
  }
  if (!const_emits.empty()) os << "\n";

  // Structs and unions can embed each other by value, so they must be
  // emitted in complete-type dependency order (not structs-then-unions,
  // and not always file order): a by-value field needs the complete class
  // already emitted; a CageVector/CageMap field does not. Parser rejected
  // true by-value cycles. WriteX/ReadX are a second pass -- they iterate
  // container elements and call WriteT/ReadT, which needs T complete
  // (values.mojom: DictionaryValue's map of Value). Write/Read also call
  // each other across the recursive cycle, so they're forward-declared
  // first.
  std::vector<Embed> embeds;
  for (const StructDecl* s : AllStructs(module)) {
    embeds.push_back({s->decl_index, s, nullptr});
  }
  for (const UnionDecl* u : AllUnions(module)) {
    embeds.push_back({u->decl_index, nullptr, u});
  }
  embeds = SortEmbeds(std::move(embeds));
  for (const Embed& e : embeds) {
    if (e.s) {
      EmitStructType(os, *e.s, module.name, options);
    } else {
      EmitUnionType(os, *e.u, module.name);
    }
  }
  if (!embeds.empty()) {
    for (const Embed& e : embeds) {
      if (e.s) {
        const bool mapped =
            LookupTypemap(options, e.s->name, e.s->owner_container,
                          module.name) != nullptr;
        EmitWriteReadForward(os, CppEmbedName(*e.s),
                             !mapped &&
                                 handle_ctx.count(CppEmbedName(*e.s)) != 0);
      } else {
        EmitWriteReadForward(os, CppEmbedName(*e.u),
                             handle_ctx.count(CppEmbedName(*e.u)) != 0);
      }
    }
    os << "\n";
    for (const Embed& e : embeds) {
      if (e.s) {
        EmitStructIO(os, *e.s, enum_extensible, handle_ctx, options,
                     module.name);
      } else {
        EmitUnionIO(os, *e.u, enum_extensible, handle_ctx);
      }
    }
  }
  for (const Interface& iface : module.interfaces) {
    EmitInterface(os, iface, enum_extensible, handle_ctx, module.name);
  }

  for (auto it = namespaces.rbegin(); it != namespaces.rend(); ++it) {
    os << "}  // namespace " << *it << "\n";
  }
  os << "\n";
  os << "#endif  // " << header_guard << "\n";
  return os.str();
}

std::string GenerateJsHeader(const Module& module,
                             const std::string& cpp_namespace,
                             const std::string& source_filename) {
  std::ostringstream os;
  os << "// Generated JS method table for WASMExtWrench wew.mojo.Remote.\n";
  os << "// Source: " << source_filename << "\n";
  os << "// Install via wew.mojo.Remote; each entry is {mojom name, JS name}.\n";
  os << "#pragma once\n\n";
  auto nss = SplitNamespace(cpp_namespace);
  for (const std::string& ns : nss) {
    os << "namespace " << ns << " {\n";
  }
  os << "\n";
  os << "struct JsMethod { const char* mojom; const char* js; };\n\n";
  os << "inline constexpr const char* kJsInterfaceNames[] = {\n";
  for (const Interface& iface : module.interfaces) {
    os << "    \"" << iface.name << "\",\n";
  }
  os << "};\n\n";
  for (const Interface& iface : module.interfaces) {
    os << "inline constexpr const char* k" << iface.name << "JsName = \""
       << iface.name << "\";\n";
    os << "inline constexpr JsMethod k" << iface.name << "JsMethods[] = {\n";
    for (const Method& m : iface.methods) {
      std::string js = m.name;
      if (!js.empty() && js[0] >= 'A' && js[0] <= 'Z') {
        js[0] = static_cast<char>(js[0] - 'A' + 'a');
      }
      os << "    {\"" << m.name << "\", \"" << js << "\"},\n";
    }
    os << "};\n";
    os << "inline constexpr const char* k" << iface.name
       << "JsInternKey = \"" << NamedKey(cpp_namespace, iface.name, "interface")
       << "\";\n\n";
  }
  for (auto it = nss.rbegin(); it != nss.rend(); ++it) {
    os << "}  // namespace " << *it << "\n";
  }
  return os.str();
}

std::string GenerateGnBuild(const std::string& target_name,
                            const std::string& generated_header) {
  std::ostringstream os;
  os << "# Generated by voodoomc. Chromium-style source_set.\n";
  os << "source_set(\"" << target_name << "\") {\n";
  os << "  sources = [ \"" << generated_header << "\" ]\n";
  os << "}\n";
  return os.str();
}

}  // namespace voodoom
