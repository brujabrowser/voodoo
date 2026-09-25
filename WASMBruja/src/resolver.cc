#include "resolver.h"

#include <unordered_map>
#include <unordered_set>

namespace bruja {
namespace {

struct Context {
  std::unordered_set<std::string> interfaces;
  std::unordered_set<std::string> enums;
  std::unordered_set<std::string> dictionaries;
  std::unordered_set<std::string> callbacks;
};

void ResolveType(TypeSpec& type, const Context& ctx, bool allow_promise) {
  if (type.kind == TypeKind::kSequence) {
    ResolveType(*type.element_type, ctx, /*allow_promise=*/false);
    return;
  }
  if (type.kind == TypeKind::kPromise) {
    if (!allow_promise) {
      throw ResolveError(
          "Promise<T> may only be used as a method's return type");
    }
    ResolveType(*type.element_type, ctx, /*allow_promise=*/false);
    return;
  }
  if (type.kind == TypeKind::kUnion) {
    for (TypeSpec& member : *type.union_members) {
      ResolveType(member, ctx, /*allow_promise=*/false);
    }
    return;
  }
  if (type.kind != TypeKind::kUnresolvedRef) return;

  const std::string& name = type.ref_name;
  if (ctx.interfaces.count(name)) {
    type.kind = TypeKind::kInterfaceRef;
  } else if (ctx.enums.count(name)) {
    type.kind = TypeKind::kEnumRef;
  } else if (ctx.dictionaries.count(name)) {
    type.kind = TypeKind::kDictionaryRef;
  } else if (ctx.callbacks.count(name)) {
    type.kind = TypeKind::kCallbackRef;
  } else {
    throw ResolveError("unknown type '" + name + "'");
  }
}

void ResolveParams(std::vector<Param>& params, const Context& ctx) {
  for (Param& param : params) ResolveType(param.type, ctx, /*allow_promise=*/false);
}

void CheckBaseChain(const Interface& iface,
                     const std::unordered_map<std::string, const Interface*>&
                         by_name) {
  std::unordered_set<std::string> visited;
  const Interface* current = &iface;
  visited.insert(current->name);
  while (!current->base_name.empty()) {
    auto it = by_name.find(current->base_name);
    if (it == by_name.end()) {
      throw ResolveError("interface '" + current->name +
                          "' has unknown base interface '" +
                          current->base_name + "'");
    }
    if (visited.count(current->base_name)) {
      throw ResolveError("interface '" + iface.name +
                          "' has a cyclic base-interface chain (via '" +
                          current->base_name + "')");
    }
    visited.insert(current->base_name);
    current = it->second;
  }
}

void CheckDictBaseChain(const Dictionary& dict,
                         const std::unordered_map<std::string, const Dictionary*>&
                             by_name) {
  std::unordered_set<std::string> visited;
  const Dictionary* current = &dict;
  visited.insert(current->name);
  while (!current->base_name.empty()) {
    auto it = by_name.find(current->base_name);
    if (it == by_name.end()) {
      throw ResolveError("dictionary '" + current->name +
                          "' has unknown base dictionary '" +
                          current->base_name + "'");
    }
    if (visited.count(current->base_name)) {
      throw ResolveError("dictionary '" + dict.name +
                          "' has a cyclic base-dictionary chain (via '" +
                          current->base_name + "')");
    }
    visited.insert(current->base_name);
    current = it->second;
  }
}

}  // namespace

void Resolve(Module& module) {
  Context ctx;
  for (const Interface& iface : module.interfaces) ctx.interfaces.insert(iface.name);
  for (const EnumDecl& e : module.enums) ctx.enums.insert(e.name);
  for (const Dictionary& d : module.dictionaries) ctx.dictionaries.insert(d.name);
  for (const CallbackDecl& c : module.callbacks) ctx.callbacks.insert(c.name);

  std::unordered_map<std::string, const Interface*> by_name;
  for (const Interface& iface : module.interfaces) by_name[iface.name] = &iface;
  std::unordered_map<std::string, MixinDecl*> mixins_by_name;
  for (MixinDecl& m : module.mixins) mixins_by_name[m.name] = &m;

  for (Interface& iface : module.interfaces) {
    if (!iface.base_name.empty()) CheckBaseChain(iface, by_name);
    for (Method& method : iface.methods) {
      ResolveType(method.return_type, ctx, /*allow_promise=*/true);
      ResolveParams(method.params, ctx);
    }
    for (Attribute& attr : iface.attributes) {
      ResolveType(attr.type, ctx, /*allow_promise=*/false);
    }
    for (Const& c : iface.consts) ResolveType(c.type, ctx, /*allow_promise=*/false);
    if (iface.has_constructor) ResolveParams(iface.constructor_params, ctx);
  }
  std::unordered_map<std::string, const Dictionary*> dict_by_name;
  for (const Dictionary& d : module.dictionaries) dict_by_name[d.name] = &d;
  for (Dictionary& dict : module.dictionaries) {
    if (!dict.base_name.empty()) CheckDictBaseChain(dict, dict_by_name);
    for (DictField& field : dict.fields) {
      ResolveType(field.type, ctx, /*allow_promise=*/false);
    }
  }
  for (CallbackDecl& cb : module.callbacks) {
    ResolveType(cb.return_type, ctx, /*allow_promise=*/false);
    ResolveParams(cb.params, ctx);
  }

  // Mixin bodies resolve exactly like an interface body's members (no base,
  // no constructor -- neither is legal Web IDL for a mixin).
  for (MixinDecl& mixin : module.mixins) {
    for (Method& method : mixin.methods) {
      ResolveType(method.return_type, ctx, /*allow_promise=*/true);
      ResolveParams(method.params, ctx);
    }
    for (Attribute& attr : mixin.attributes) {
      ResolveType(attr.type, ctx, /*allow_promise=*/false);
    }
    for (Const& c : mixin.consts) ResolveType(c.type, ctx, /*allow_promise=*/false);
  }

  // `Target includes Mixin;` -- literally copy the (already-resolved)
  // mixin's members into the target interface's own lists. Codegen never
  // sees MixinDecl/IncludesDecl: by this point the target interface looks
  // exactly as if those members had been hand-written on it directly.
  std::unordered_map<std::string, Interface*> mutable_by_name;
  for (Interface& iface : module.interfaces) mutable_by_name[iface.name] = &iface;
  for (const IncludesDecl& inc : module.includes_decls) {
    auto target_it = mutable_by_name.find(inc.target);
    if (target_it == mutable_by_name.end()) {
      throw ResolveError("'" + inc.target + " includes " + inc.mixin_name +
                          "': unknown target interface '" + inc.target + "'");
    }
    auto mixin_it = mixins_by_name.find(inc.mixin_name);
    if (mixin_it == mixins_by_name.end()) {
      throw ResolveError("'" + inc.target + " includes " + inc.mixin_name +
                          "': unknown mixin '" + inc.mixin_name + "'");
    }
    Interface& target = *target_it->second;
    const MixinDecl& mixin = *mixin_it->second;
    target.methods.insert(target.methods.end(), mixin.methods.begin(), mixin.methods.end());
    target.attributes.insert(target.attributes.end(), mixin.attributes.begin(),
                              mixin.attributes.end());
    target.consts.insert(target.consts.end(), mixin.consts.begin(), mixin.consts.end());
  }
}

}  // namespace bruja
