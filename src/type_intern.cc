#include "type_intern.h"

#include <sstream>

namespace voodoom {
namespace {

std::string QualName(const std::string& ns, const std::string& name) {
  if (ns.empty()) return name;
  return ns + "." + name;
}

const char* HandleKey(TypeKind k) {
  switch (k) {
    case TypeKind::kHandle:
      return "handle";
    case TypeKind::kHandleMessagePipe:
      return "handle<message_pipe>";
    case TypeKind::kHandleDataPipeConsumer:
      return "handle<data_pipe_consumer>";
    case TypeKind::kHandleDataPipeProducer:
      return "handle<data_pipe_producer>";
    case TypeKind::kHandleSharedBuffer:
      return "handle<shared_buffer>";
    case TypeKind::kHandlePlatform:
      return "handle<platform>";
    default:
      return "handle";
  }
}

std::string JoinParams(const std::vector<Param>& ps, const std::string& ns) {
  std::ostringstream os;
  for (size_t i = 0; i < ps.size(); ++i) {
    if (i) os << ",";
    os << TypeKey(ps[i].type, ns);
  }
  return os.str();
}

}  // namespace

std::string TypeKey(const TypeSpec& t, const std::string& current_ns) {
  const std::string ns =
      t.owner_namespace.empty() ? current_ns : t.owner_namespace;
  std::string base;
  switch (t.kind) {
    case TypeKind::kBool:
      base = "bool";
      break;
    case TypeKind::kInt8:
      base = "int8";
      break;
    case TypeKind::kUint8:
      base = "uint8";
      break;
    case TypeKind::kInt16:
      base = "int16";
      break;
    case TypeKind::kUint16:
      base = "uint16";
      break;
    case TypeKind::kInt32:
      base = "int32";
      break;
    case TypeKind::kUint32:
      base = "uint32";
      break;
    case TypeKind::kInt64:
      base = "int64";
      break;
    case TypeKind::kUint64:
      base = "uint64";
      break;
    case TypeKind::kFloat:
      base = "float";
      break;
    case TypeKind::kDouble:
      base = "double";
      break;
    case TypeKind::kString:
      base = "string";
      break;
    case TypeKind::kEnumRef: {
      std::string n = t.name;
      if (!t.owner_container.empty()) n = t.owner_container + "." + n;
      base = "enum:" + QualName(ns, n);
      break;
    }
    case TypeKind::kStructRef:
      base = "struct:" + QualName(ns, t.name);
      break;
    case TypeKind::kUnionRef:
      base = "union:" + QualName(ns, t.name);
      break;
    case TypeKind::kArray: {
      std::string e = TypeKey(*t.element, current_ns);
      if (t.fixed_array_size > 0) {
        base = "array<" + e + "," + std::to_string(t.fixed_array_size) + ">";
      } else {
        base = "array<" + e + ">";
      }
      break;
    }
    case TypeKind::kMap:
      base = "map<" + TypeKey(*t.key, current_ns) + "," +
             TypeKey(*t.element, current_ns) + ">";
      break;
    case TypeKind::kPendingRemote:
      base = "pending_remote<" + QualName(ns, t.name) + ">";
      break;
    case TypeKind::kPendingReceiver:
      base = "pending_receiver<" + QualName(ns, t.name) + ">";
      break;
    case TypeKind::kPendingAssociatedRemote:
      base = "pending_associated_remote<" + QualName(ns, t.name) + ">";
      break;
    case TypeKind::kPendingAssociatedReceiver:
      base = "pending_associated_receiver<" + QualName(ns, t.name) + ">";
      break;
    case TypeKind::kHandle:
    case TypeKind::kHandleMessagePipe:
    case TypeKind::kHandleDataPipeConsumer:
    case TypeKind::kHandleDataPipeProducer:
    case TypeKind::kHandleSharedBuffer:
    case TypeKind::kHandlePlatform:
      base = HandleKey(t.kind);
      break;
  }
  if (t.nullable) base += "?";
  return base;
}

std::string NamedKey(const std::string& ns, const std::string& name,
                     const char* kind) {
  return std::string(kind) + ":" + QualName(ns, name);
}

std::string MethodKey(const std::string& ns, const std::string& iface,
                      const Method& m) {
  std::ostringstream os;
  os << "func " << QualName(ns, iface) << "." << m.name << "("
     << JoinParams(m.params, ns) << ")";
  if (m.is_result_response) {
    os << "->result<" << TypeKey(m.result_success, ns) << ","
       << TypeKey(m.result_error, ns) << ">";
  } else if (m.has_response) {
    os << "->(" << JoinParams(m.response_params, ns) << ")";
  }
  return os.str();
}

uint32_t TypeIntern::InternKey(const std::string& key) {
  auto it = by_key_.find(key);
  if (it != by_key_.end()) return it->second;
  uint32_t id = static_cast<uint32_t>(keys_.size() + 1);
  by_key_.emplace(key, id);
  keys_.push_back(key);
  return id;
}

uint32_t TypeIntern::Intern(const TypeSpec& t, const std::string& current_ns) {
  return InternKey(TypeKey(t, current_ns));
}

uint32_t TypeIntern::InternNamed(const std::string& ns, const std::string& name,
                                 const char* kind) {
  return InternKey(NamedKey(ns, name, kind));
}

uint32_t TypeIntern::InternMethod(const std::string& ns, const std::string& iface,
                                  const Method& m) {
  return InternKey(MethodKey(ns, iface, m));
}

const std::string& TypeIntern::Key(uint32_t id) const {
  static const std::string empty;
  if (id == 0 || id > keys_.size()) return empty;
  return keys_[id - 1];
}

}  // namespace voodoom
