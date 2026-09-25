#include "frontends.h"

#include <cctype>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace bruja {
namespace {

bool EndsWith(const std::string& s, const std::string& suffix) {
  return s.size() >= suffix.size() &&
         s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

void SkipSpaceAndComments(const std::string& s, size_t& i) {
  while (i < s.size()) {
    if (std::isspace(static_cast<unsigned char>(s[i]))) {
      ++i;
      continue;
    }
    if (s[i] == '/' && i + 1 < s.size() && s[i + 1] == '/') {
      i += 2;
      while (i < s.size() && s[i] != '\n') ++i;
      continue;
    }
    if (s[i] == '/' && i + 1 < s.size() && s[i + 1] == '*') {
      i += 2;
      while (i + 1 < s.size() && !(s[i] == '*' && s[i + 1] == '/')) ++i;
      if (i + 1 < s.size()) i += 2;
      continue;
    }
    break;
  }
}

std::string TakeIdent(const std::string& s, size_t& i) {
  SkipSpaceAndComments(s, i);
  size_t start = i;
  if (i < s.size() && (std::isalpha(static_cast<unsigned char>(s[i])) ||
                       s[i] == '_')) {
    ++i;
    while (i < s.size() && (std::isalnum(static_cast<unsigned char>(s[i])) ||
                            s[i] == '_' || s[i] == ':')) {
      ++i;
    }
  }
  return s.substr(start, i - start);
}

// ObjC selectors use ':' as an argument marker, not a C++ `::` nest.
std::string TakeObjCIdent(const std::string& s, size_t& i) {
  SkipSpaceAndComments(s, i);
  size_t start = i;
  if (i < s.size() && (std::isalpha(static_cast<unsigned char>(s[i])) ||
                       s[i] == '_')) {
    ++i;
    while (i < s.size() && (std::isalnum(static_cast<unsigned char>(s[i])) ||
                            s[i] == '_')) {
      ++i;
    }
  }
  return s.substr(start, i - start);
}

bool Consume(const std::string& s, size_t& i, const char* word) {
  SkipSpaceAndComments(s, i);
  size_t n = std::char_traits<char>::length(word);
  if (s.compare(i, n, word) != 0) return false;
  i += n;
  return true;
}

void SkipBalanced(const std::string& s, size_t& i, char open, char close) {
  if (i >= s.size() || s[i] != open) return;
  int depth = 0;
  do {
    if (s[i] == open) ++depth;
    else if (s[i] == close) --depth;
    ++i;
  } while (i < s.size() && depth > 0);
}

void SkipAttributeLists(const std::string& s, size_t& i) {
  SkipSpaceAndComments(s, i);
  while (i < s.size() && s[i] == '[') {
    SkipBalanced(s, i, '[', ']');
    SkipSpaceAndComments(s, i);
  }
}

std::string LastComponent(std::string name) {
  auto pos = name.rfind("::");
  if (pos != std::string::npos) name = name.substr(pos + 2);
  return name;
}

std::string ToCamel(std::string name) {
  name = LastComponent(std::move(name));
  if (!name.empty()) {
    name[0] = static_cast<char>(
        std::tolower(static_cast<unsigned char>(name[0])));
  }
  return name;
}

std::string MapType(std::string type) {
  while (!type.empty() && std::isspace(static_cast<unsigned char>(type.front())))
    type.erase(type.begin());
  while (!type.empty() && std::isspace(static_cast<unsigned char>(type.back())))
    type.pop_back();

  if (type.rfind("enum:", 0) == 0) return "octet";
  if (type.rfind("struct ", 0) == 0) return "DOMString";
  if (type.rfind("Vector<", 0) == 0 && type.back() == '>') {
    std::string inner = type.substr(7, type.size() - 8);
    return "sequence<" + MapType(inner) + ">";
  }
  if (type == "String" || type == "WTF::String" || type == "DOMString")
    return "DOMString";
  if (type == "bool" || type == "boolean") return "boolean";
  if (type == "uint8_t" || type == "octet") return "octet";
  if (type == "uint16_t") return "unsigned short";
  if (type == "int32_t" || type == "int" || type == "long") return "long";
  if (type == "uint32_t" || type == "unsigned") return "unsigned long";
  if (type == "uint64_t") return "unsigned long long";
  if (type == "int64_t") return "long long";
  if (type == "double") return "double";
  if (type == "float") return "float";
  if (type == "void") return "void";
  return "DOMString";
}

std::string ParseType(const std::string& s, size_t& i) {
  SkipSpaceAndComments(s, i);
  if (Consume(s, i, "enum:")) {
    TakeIdent(s, i);  // underlying
    TakeIdent(s, i);  // name
    SkipSpaceAndComments(s, i);
    if (i < s.size() && s[i] == '<') SkipBalanced(s, i, '<', '>');
    return "octet";
  }
  if (Consume(s, i, "struct")) {
    TakeIdent(s, i);
    SkipSpaceAndComments(s, i);
    if (i < s.size() && s[i] == '<') SkipBalanced(s, i, '<', '>');
    return "DOMString";
  }
  std::string name = TakeIdent(s, i);
  SkipSpaceAndComments(s, i);
  if (i < s.size() && s[i] == '<') {
    SkipBalanced(s, i, '<', '>');
    if (name == "Vector" || name == "WTF::Vector") return "sequence<DOMString>";
    return MapType(name);
  }
  return MapType(name);
}

// ---- preprocessor for WebKit headers / *.messages.in ----
// TARGET_OS_IPHONE / PLATFORM(COCOA) / ENABLE(*) default off so a Windows
// / wasigocvm port sees the portable messages and the AppKit-shaped API.

bool EvalPpExpr(std::string expr) {
  auto strip = [](std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
      if (std::isspace(static_cast<unsigned char>(s[i]))) {
        ++i;
        continue;
      }
      out.push_back(s[i++]);
    }
    s.swap(out);
  };
  strip(expr);

  auto replaceIdent = [&](const char* name, const char* value) {
    std::string n = name;
    size_t pos = 0;
    while ((pos = expr.find(n, pos)) != std::string::npos) {
      bool left = pos == 0 || !(std::isalnum(static_cast<unsigned char>(expr[pos - 1])) ||
                                expr[pos - 1] == '_');
      bool right = pos + n.size() >= expr.size() ||
                   !(std::isalnum(static_cast<unsigned char>(expr[pos + n.size()])) ||
                     expr[pos + n.size()] == '_');
      if (left && right) {
        expr.replace(pos, n.size(), value);
        pos += std::char_traits<char>::length(value);
      } else {
        ++pos;
      }
    }
  };

  // Function-like macros WebKit uses in messages.in / cocoa headers.
  auto zeroCall = [&](const char* name) {
    std::string n = name;
    size_t pos = 0;
    while ((pos = expr.find(n + "(", pos)) != std::string::npos) {
      size_t open = pos + n.size();
      int depth = 1;
      size_t j = open + 1;
      while (j < expr.size() && depth) {
        if (expr[j] == '(') ++depth;
        else if (expr[j] == ')') --depth;
        ++j;
      }
      expr.replace(pos, j - pos, "0");
      pos += 1;
    }
  };
  zeroCall("PLATFORM");
  zeroCall("ENABLE");
  zeroCall("HAVE");
  zeroCall("USE");
  zeroCall("defined");
  zeroCall("TARGET_OS_IPHONE");
  zeroCall("TARGET_OS_IOS");
  zeroCall("TARGET_OS_VISION");

  replaceIdent("TARGET_OS_IPHONE", "0");
  replaceIdent("TARGET_OS_IOS", "0");
  replaceIdent("TARGET_OS_VISION", "0");
  replaceIdent("TARGET_OS_OSX", "1");
  replaceIdent("TARGET_OS_MAC", "1");
  replaceIdent("__APPLE__", "0");

  auto parseOr = [&](auto& self, size_t& i) -> int {
    auto skip = [&]() {
      while (i < expr.size() && std::isspace(static_cast<unsigned char>(expr[i])))
        ++i;
    };
    auto parsePrimary = [&](auto& rec, size_t& k) -> int {
      skip();
      if (k < expr.size() && expr[k] == '!') {
        ++k;
        return !rec(rec, k);
      }
      if (k < expr.size() && expr[k] == '(') {
        ++k;
        int v = self(self, k);
        skip();
        if (k < expr.size() && expr[k] == ')') ++k;
        return v;
      }
      if (k < expr.size() && std::isdigit(static_cast<unsigned char>(expr[k]))) {
        int v = 0;
        while (k < expr.size() && std::isdigit(static_cast<unsigned char>(expr[k]))) {
          v = v * 10 + (expr[k] - '0');
          ++k;
        }
        return v != 0;
      }
      while (k < expr.size() && (std::isalnum(static_cast<unsigned char>(expr[k])) ||
                                 expr[k] == '_'))
        ++k;
      return 0;
    };
    auto parseAnd = [&](auto& rec, size_t& k) -> int {
      int v = parsePrimary(rec, k);
      for (;;) {
        skip();
        if (k + 1 < expr.size() && expr[k] == '&' && expr[k + 1] == '&') {
          k += 2;
          v = v && parsePrimary(rec, k);
          continue;
        }
        break;
      }
      return v;
    };
    int v = parseAnd(self, i);
    for (;;) {
      skip();
      if (i + 1 < expr.size() && expr[i] == '|' && expr[i + 1] == '|') {
        i += 2;
        v = v || parseAnd(self, i);
        continue;
      }
      break;
    }
    return v;
  };
  size_t i = 0;
  return parseOr(parseOr, i) != 0;
}

std::string Preprocess(const std::string& source) {
  std::string out;
  out.reserve(source.size());
  struct Frame {
    bool parent = true;
    bool taking = true;
    bool else_seen = false;
  };
  std::vector<Frame> stack;
  bool taking = true;

  size_t i = 0;
  auto atLineStart = [&]() {
    return i == 0 || source[i - 1] == '\n' || source[i - 1] == '\r';
  };
  while (i < source.size()) {
    if (atLineStart()) {
      size_t j = i;
      while (j < source.size() && (source[j] == ' ' || source[j] == '\t')) ++j;
      if (j < source.size() && source[j] == '#') {
        size_t line = j + 1;
        while (line < source.size() && source[line] != '\n') ++line;
        std::string dir = source.substr(j + 1, (line - j - 1));
        size_t d = 0;
        while (d < dir.size() && std::isspace(static_cast<unsigned char>(dir[d])))
          ++d;
        auto starts = [&](const char* w) {
          size_t n = std::char_traits<char>::length(w);
          return dir.compare(d, n, w) == 0;
        };
        if (starts("if") || starts("ifdef") || starts("ifndef")) {
          bool cond = false;
          if (starts("ifdef")) {
            cond = false;
          } else if (starts("ifndef")) {
            cond = true;
          } else {
            size_t exprStart = d + 2;
            while (exprStart < dir.size() &&
                   std::isspace(static_cast<unsigned char>(dir[exprStart])))
              ++exprStart;
            cond = EvalPpExpr(dir.substr(exprStart));
          }
          stack.push_back({taking, taking && cond, false});
          taking = stack.back().taking;
        } else if (starts("else")) {
          if (!stack.empty() && !stack.back().else_seen) {
            stack.back().else_seen = true;
            stack.back().taking = stack.back().parent && !stack.back().taking;
            taking = stack.back().taking;
          }
        } else if (starts("elif")) {
          if (!stack.empty() && !stack.back().else_seen) {
            size_t exprStart = d + 4;
            while (exprStart < dir.size() &&
                   std::isspace(static_cast<unsigned char>(dir[exprStart])))
              ++exprStart;
            bool cond = EvalPpExpr(dir.substr(exprStart));
            stack.back().taking = stack.back().parent && !stack.back().taking && cond;
            taking = stack.back().taking;
          }
        } else if (starts("endif")) {
          if (!stack.empty()) stack.pop_back();
          taking = stack.empty() ? true : stack.back().taking;
        }
        i = line;
        if (i < source.size() && source[i] == '\n') {
          if (taking) out.push_back('\n');
          ++i;
        }
        continue;
      }
    }
    if (taking) out.push_back(source[i]);
    ++i;
  }
  return out;
}

std::string MessagesInToBruja(const std::string& raw) {
  const std::string source = Preprocess(raw);
  size_t i = 0;
  std::string out;
  bool any = false;
  while (i < source.size()) {
    SkipSpaceAndComments(source, i);
    if (i >= source.size()) break;
    SkipAttributeLists(source, i);
    if (i >= source.size()) break;
    if (!Consume(source, i, "messages")) {
      ++i;
      continue;
    }
    if (!Consume(source, i, "->")) {
      throw std::runtime_error("messages.in: expected '->' after messages");
    }
    std::string receiver = LastComponent(TakeIdent(source, i));
    if (receiver.empty()) {
      throw std::runtime_error("messages.in: missing receiver name");
    }
    SkipSpaceAndComments(source, i);
    while (i < source.size() && source[i] != '{') {
      SkipAttributeLists(source, i);
      if (i >= source.size() || source[i] == '{') break;
      std::string extra = TakeIdent(source, i);
      if (extra.empty()) break;
      SkipSpaceAndComments(source, i);
    }
    if (!Consume(source, i, "{")) {
      throw std::runtime_error("messages.in: expected '{' after receiver");
    }
    out += "interface " + receiver + " {\n";
    any = true;
    while (i < source.size()) {
      SkipSpaceAndComments(source, i);
      if (i >= source.size()) break;
      if (source[i] == '}') {
        ++i;
        break;
      }
      SkipAttributeLists(source, i);
      if (i >= source.size()) break;
      if (source[i] == '}') {
        ++i;
        break;
      }
      std::string msg = TakeIdent(source, i);
      if (msg.empty()) {
        ++i;
        continue;
      }
      if (!Consume(source, i, "(")) {
        throw std::runtime_error("messages.in: expected '(' after " + msg);
      }
      std::vector<std::pair<std::string, std::string>> params;
      SkipSpaceAndComments(source, i);
      if (!(i < source.size() && source[i] == ')')) {
        while (true) {
          std::string type = ParseType(source, i);
          std::string name = TakeIdent(source, i);
          if (name.empty()) name = "arg" + std::to_string(params.size());
          params.push_back({type, name});
          SkipSpaceAndComments(source, i);
          if (i < source.size() && source[i] == ',') {
            ++i;
            continue;
          }
          break;
        }
      }
      if (!Consume(source, i, ")")) {
        throw std::runtime_error("messages.in: expected ')' after params");
      }
      std::string ret = "void";
      SkipSpaceAndComments(source, i);
      if (Consume(source, i, "->")) {
        if (!Consume(source, i, "(")) {
          throw std::runtime_error("messages.in: expected '(' after ->");
        }
        std::vector<std::string> replies;
        SkipSpaceAndComments(source, i);
        if (!(i < source.size() && source[i] == ')')) {
          while (true) {
            std::string type = ParseType(source, i);
            TakeIdent(source, i);  // reply name
            replies.push_back(type);
            SkipSpaceAndComments(source, i);
            if (i < source.size() && source[i] == ',') {
              ++i;
              continue;
            }
            break;
          }
        }
        if (!Consume(source, i, ")")) {
          throw std::runtime_error("messages.in: expected ')' after replies");
        }
        SkipSpaceAndComments(source, i);
        bool sync = Consume(source, i, "Synchronous");
        if (replies.empty()) {
          ret = "void";
        } else if (sync || replies.size() == 1) {
          ret = replies[0];
        } else {
          ret = "Promise<" + replies[0] + ">";
        }
        if (!sync && replies.size() == 1) ret = "Promise<" + replies[0] + ">";
      }
      for (;;) {
        SkipSpaceAndComments(source, i);
        if (i >= source.size()) break;
        if (source[i] == ';') {
          ++i;
          break;
        }
        if (source[i] == '}' || source[i] == '[') break;
        size_t save = i;
        std::string flag = TakeIdent(source, i);
        if (flag.empty()) break;
        SkipSpaceAndComments(source, i);
        if (i < source.size() && source[i] == '(') {
          i = save;
          break;
        }
      }
      out += "  " + ret + " " + ToCamel(msg) + "(";
      for (size_t p = 0; p < params.size(); ++p) {
        if (p) out += ", ";
        out += params[p].first + " " + params[p].second;
      }
      out += ");\n";
    }
    out += "};\n";
  }
  if (!any) {
    throw std::runtime_error("messages.in: no messages -> receiver block");
  }
  return out;
}

std::string ExtractBrujaFromMm(const std::string& source) {
  auto isLineStart = [&](size_t pos) {
    return pos == 0 || source[pos - 1] == '\n' || source[pos - 1] == '\r';
  };
  size_t marker = std::string::npos;
  for (size_t pos = 0; (pos = source.find("// --- cpp ---", pos)) !=
                       std::string::npos;
       ++pos) {
    if (isLineStart(pos)) {
      marker = pos;
      break;
    }
  }
  if (marker != std::string::npos) return source.substr(0, marker);
  auto includePos = source.find("\n#include");
  if (includePos != std::string::npos &&
      source.find("interface") < includePos) {
    return source.substr(0, includePos);
  }
  return source;
}

bool TypeLooksAppleOnly(const std::string& t) {
  static const char* kSkip[] = {
      "NSCoder",  "NSImage",     "NSView",  "UIView",  "NSEvent",
      "NSMenu",   "NSPrintInfo", "NSPrint", "CGImage", "SecTrust",
      "IBAction", "NSGesture",   "NSColor", "UIColor", "NSWindow",
      "NSItemProvider", "NSMatch", "WKSnapshot", "PDF", "NSDataDetector"};
  for (const char* s : kSkip) {
    if (t.find(s) != std::string::npos) return true;
  }
  return false;
}

std::string MapObjCType(std::string raw, bool nullable) {
  auto trim = [](std::string& s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
      s.erase(s.begin());
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
      s.pop_back();
  };
  // Drop protocol qualifications: id<Foo> / Type<Bar>
  auto lt = raw.find('<');
  auto star = raw.find('*');
  if (lt != std::string::npos && (star == std::string::npos || lt < star)) {
    raw = raw.substr(0, lt);
  }
  while (!raw.empty() && (raw.back() == '*' ||
                          std::isspace(static_cast<unsigned char>(raw.back()))))
    raw.pop_back();
  trim(raw);
  // Drop leading attributes like __nullable
  while (!raw.empty()) {
    if (raw.rfind("nullable ", 0) == 0) {
      raw.erase(0, 9);
      nullable = true;
      trim(raw);
      continue;
    }
    if (raw.rfind("_Nullable ", 0) == 0) {
      raw.erase(0, 10);
      nullable = true;
      trim(raw);
      continue;
    }
    break;
  }

  std::string mapped;
  if (raw.empty() || raw == "void") mapped = "void";
  else if (raw == "BOOL" || raw == "bool" || raw == "Boolean") mapped = "boolean";
  else if (raw == "NSString" || raw == "NSURL" || raw == "NSURLRequest" ||
           raw == "NSData" || raw == "NSError" || raw == "NSNumber" ||
           raw == "NSDate" || raw == "NSDictionary" || raw == "NSArray")
    mapped = (raw == "NSArray") ? "sequence<any>" : "DOMString";
  else if (raw == "id" || raw == "Class" || raw == "instancetype") mapped = "any";
  else if (raw == "double" || raw == "float" || raw == "CGFloat") mapped = "double";
  else if (raw == "NSInteger" || raw == "NSTimeInterval" || raw == "int" ||
           raw == "long")
    mapped = "long";
  else if (raw == "NSUInteger" || raw == "unsigned" || raw == "uint32_t")
    mapped = "unsigned long";
  else if (raw == "CGRect" || raw == "CGSize" || raw == "CGPoint" ||
           raw == "NSRect" || raw == "NSSize")
    mapped = "DOMString";
  else if (raw.rfind("NSArray", 0) == 0)
    mapped = "sequence<any>";
  else if (raw.rfind("WK", 0) == 0)
    mapped = raw;
  else
    mapped = "any";

  if (nullable && mapped != "void" && mapped != "any") mapped += "?";
  return mapped;
}

std::string TakeObjCType(const std::string& s, size_t& i) {
  SkipSpaceAndComments(s, i);
  bool nullable = Consume(s, i, "nullable") || Consume(s, i, "_Nullable");
  SkipSpaceAndComments(s, i);
  size_t start = i;
  int angles = 0;
  int parens = 0;
  while (i < s.size()) {
    char c = s[i];
    if (c == '<') ++angles;
    if (c == '>') --angles;
    if (c == '(') ++parens;
    if (c == ')') {
      if (parens == 0 && angles == 0) break;
      --parens;
    }
    if (angles == 0 && parens == 0 &&
        (c == ')' || c == ';' || c == ',' || c == ':'))
      break;
    ++i;
  }
  std::string raw = s.substr(start, i - start);
  return MapObjCType(raw, nullable);
}

bool SkipIfAppleOnlyType(const std::string& mapped, const std::string& raw) {
  return TypeLooksAppleOnly(mapped) || TypeLooksAppleOnly(raw);
}

std::string ObjCToBruja(const std::string& raw) {
  const std::string source = Preprocess(raw);
  std::string out;
  std::unordered_set<std::string> defined;
  std::unordered_set<std::string> referenced;
  size_t i = 0;

  auto skipToSemicolon = [&]() {
    while (i < source.size() && source[i] != ';' && source[i] != '\n') ++i;
    if (i < source.size()) ++i;
  };

  while (i < source.size()) {
    SkipSpaceAndComments(source, i);
    if (i >= source.size()) break;

    if (Consume(source, i, "@class")) {
      while (i < source.size() && source[i] != ';') {
        std::string name = TakeObjCIdent(source, i);
        if (!name.empty() && name.rfind("WK", 0) == 0) referenced.insert(name);
        SkipSpaceAndComments(source, i);
        if (i < source.size() && source[i] == ',') ++i;
      }
      if (i < source.size() && source[i] == ';') ++i;
      continue;
    }
    if (Consume(source, i, "@protocol")) {
      TakeObjCIdent(source, i);
      while (i < source.size()) {
        SkipSpaceAndComments(source, i);
        if (Consume(source, i, "@end")) break;
        ++i;
      }
      continue;
    }
    if (Consume(source, i, "typedef")) {
      SkipSpaceAndComments(source, i);
      if (Consume(source, i, "NS_ENUM") || Consume(source, i, "NS_OPTIONS")) {
        SkipSpaceAndComments(source, i);
        if (i < source.size() && source[i] == '(') {
          ++i;
          while (i < source.size() && source[i] != ')') ++i;
          if (i < source.size()) ++i;
        }
        std::string ename = TakeObjCIdent(source, i);
        SkipSpaceAndComments(source, i);
        if (i < source.size() && source[i] == '{') {
          ++i;
          out += "enum " + ename + " { ";
          bool first = true;
          while (i < source.size() && source[i] != '}') {
            SkipSpaceAndComments(source, i);
            if (i < source.size() && source[i] == '}') break;
            std::string v = TakeObjCIdent(source, i);
            if (!v.empty()) {
              if (!first) out += ", ";
              first = false;
              out += "\"" + v + "\"";
            }
            while (i < source.size() && source[i] != ',' && source[i] != '}') ++i;
            if (i < source.size() && source[i] == ',') ++i;
          }
          out += " };\n";
          if (i < source.size() && source[i] == '}') ++i;
          skipToSemicolon();
          continue;
        }
      }
      skipToSemicolon();
      continue;
    }

    if (!Consume(source, i, "@interface")) {
      ++i;
      continue;
    }
    std::string name = TakeObjCIdent(source, i);
    if (name.empty()) continue;
    SkipSpaceAndComments(source, i);
    if (i < source.size() && source[i] == '(') {
      // Category: skip to @end. Methods would duplicate the primary.
      while (i < source.size()) {
        SkipSpaceAndComments(source, i);
        if (Consume(source, i, "@end")) break;
        ++i;
      }
      continue;
    }
    std::string base;
    if (i < source.size() && source[i] == ':') {
      ++i;
      base = TakeObjCIdent(source, i);
    }
    // Skip protocol list <NSSecureCoding, NSCopying>
    SkipSpaceAndComments(source, i);
    if (i < source.size() && source[i] == '<') {
      int depth = 1;
      ++i;
      while (i < source.size() && depth) {
        if (source[i] == '<') ++depth;
        else if (source[i] == '>') --depth;
        ++i;
      }
    }
    SkipSpaceAndComments(source, i);
    if (i < source.size() && source[i] == '{') {
      int depth = 1;
      ++i;
      while (i < source.size() && depth) {
        if (source[i] == '{') ++depth;
        else if (source[i] == '}') --depth;
        ++i;
      }
    }

    defined.insert(name);
    out += "interface " + name;
    if (!base.empty() && base.rfind("WK", 0) == 0 && defined.count(base)) {
      out += " : " + base;
    }
    out += " {\n";

    while (i < source.size()) {
      SkipSpaceAndComments(source, i);
      if (i >= source.size()) break;
      if (Consume(source, i, "@end")) break;
      if (Consume(source, i, "@property")) {
        SkipSpaceAndComments(source, i);
        bool readonly = false;
        bool nullable = false;
        if (i < source.size() && source[i] == '(') {
          ++i;
          std::string attrs;
          while (i < source.size() && source[i] != ')') {
            attrs.push_back(source[i++]);
          }
          if (i < source.size()) ++i;
          if (attrs.find("readonly") != std::string::npos) readonly = true;
          if (attrs.find("nullable") != std::string::npos) nullable = true;
        }
        size_t typeStart = i;
        SkipSpaceAndComments(source, i);
        std::string propName;
        // Walk back from ';' to the property name.
        size_t semi = source.find(';', i);
        if (semi == std::string::npos) break;
        size_t nameEnd = semi;
        while (nameEnd > i &&
               std::isspace(static_cast<unsigned char>(source[nameEnd - 1])))
          --nameEnd;
        size_t nameStart = nameEnd;
        while (nameStart > i &&
               (std::isalnum(static_cast<unsigned char>(source[nameStart - 1])) ||
                source[nameStart - 1] == '_'))
          --nameStart;
        propName = source.substr(nameStart, nameEnd - nameStart);
        std::string rawType = source.substr(typeStart, nameStart - typeStart);
        std::string mapped = MapObjCType(rawType, nullable);
        i = semi + 1;
        if (propName.empty() || SkipIfAppleOnlyType(mapped, rawType)) continue;
        if (mapped.rfind("WK", 0) == 0) {
          auto cut = mapped.find('?');
          referenced.insert(cut == std::string::npos ? mapped
                                                     : mapped.substr(0, cut));
        }
        out += "  ";
        if (readonly) out += "readonly ";
        out += "attribute " + mapped + " " + propName + ";\n";
        continue;
      }
      if (source[i] == '+' || source[i] == '-') {
        bool isInit = false;
        ++i;  // + / -
        SkipSpaceAndComments(source, i);
        if (i >= source.size() || source[i] != '(') {
          skipToSemicolon();
          continue;
        }
        ++i;
        size_t retStart = i;
        int parens = 1;
        int angles = 0;
        while (i < source.size() && parens) {
          if (source[i] == '<') ++angles;
          if (source[i] == '>') --angles;
          if (angles == 0 && source[i] == '(') ++parens;
          if (angles == 0 && source[i] == ')') --parens;
          ++i;
        }
        std::string rawRet = source.substr(retStart, i - retStart - 1);
        bool retNullable = rawRet.find("nullable") != std::string::npos;
        std::string ret = MapObjCType(rawRet, retNullable);

        std::string firstSel = TakeObjCIdent(source, i);
        if (firstSel.empty()) {
          skipToSemicolon();
          continue;
        }
        if (firstSel.rfind("init", 0) == 0) isInit = true;
        if (firstSel == "initWithCoder") {
          skipToSemicolon();
          continue;
        }

        struct P {
          std::string type;
          std::string name;
        };
        std::vector<P> params;
        bool skipMethod = SkipIfAppleOnlyType(ret, rawRet);
        bool promise = false;
        SkipSpaceAndComments(source, i);
        if (i < source.size() && source[i] == ':') {
          ++i;
          for (;;) {
            SkipSpaceAndComments(source, i);
            std::string pty;
            std::string pn;
            if (i < source.size() && source[i] == '(') {
              ++i;
              size_t ts = i;
              int dp = 1;
              int da = 0;
              while (i < source.size() && dp) {
                if (source[i] == '<') ++da;
                if (source[i] == '>') --da;
                if (da == 0 && source[i] == '(') ++dp;
                if (da == 0 && source[i] == ')') --dp;
                ++i;
              }
              std::string rawP = source.substr(ts, i - ts - 1);
              bool n = rawP.find("nullable") != std::string::npos;
              if (rawP.find('^') != std::string::npos) {
                pn = TakeObjCIdent(source, i);
                if (pn == "completionHandler" || pn == "callback" ||
                    pn == "function") {
                  promise = true;
                  SkipSpaceAndComments(source, i);
                  if (i < source.size() &&
                      (std::isalpha(static_cast<unsigned char>(source[i])) ||
                       source[i] == '_'))
                    TakeObjCIdent(source, i);
                  SkipSpaceAndComments(source, i);
                  if (i < source.size() && source[i] == ':') {
                    ++i;
                    continue;
                  }
                  break;
                }
                pty = "any";
              } else {
                pty = MapObjCType(rawP, n);
                if (SkipIfAppleOnlyType(pty, rawP)) skipMethod = true;
              }
            }
            pn = TakeObjCIdent(source, i);
            if (pn.empty()) pn = "arg" + std::to_string(params.size());
            if (!pty.empty()) params.push_back({pty, pn});
            if (pty.rfind("WK", 0) == 0) {
              auto cut = pty.find('?');
              referenced.insert(cut == std::string::npos ? pty
                                                         : pty.substr(0, cut));
            }
            SkipSpaceAndComments(source, i);
            if (i < source.size() &&
                (std::isalpha(static_cast<unsigned char>(source[i])) ||
                 source[i] == '_')) {
              TakeObjCIdent(source, i);  // next selector piece
              SkipSpaceAndComments(source, i);
              if (i < source.size() && source[i] == ':') {
                ++i;
                continue;
              }
            }
            break;
          }
        }
        skipToSemicolon();
        if (skipMethod) continue;
        if (isInit) {
          out += "  constructor(";
          for (size_t p = 0; p < params.size(); ++p) {
            if (p) out += ", ";
            out += "optional " + params[p].type + " " + params[p].name;
          }
          out += ");\n";
          continue;
        }
        if (promise) ret = "Promise<any>";
        out += "  " + ret + " " + firstSel + "(";
        for (size_t p = 0; p < params.size(); ++p) {
          if (p) out += ", ";
          out += params[p].type + " " + params[p].name;
        }
        out += ");\n";
        continue;
      }
      ++i;
    }
    out += "};\n";
  }

  for (const auto& name : referenced) {
    if (!defined.count(name) && name.rfind("WK", 0) == 0) {
      out += "interface " + name + " {\n};\n";
    }
  }
  if (out.empty()) {
    throw std::runtime_error("objc: no @interface found");
  }
  return out;
}

}  // namespace

InputKind InferInputKind(const std::string& path, const std::string& source) {
  if (EndsWith(path, ".messages.in")) return InputKind::kMessagesIn;
  if (EndsWith(path, ".mm") || EndsWith(path, ".h") || EndsWith(path, ".m")) {
    if (source.find("@interface") != std::string::npos)
      return InputKind::kCocoaMm;
    if (EndsWith(path, ".mm")) return InputKind::kCocoaMm;
  }
  if (source.find("messages ->") != std::string::npos)
    return InputKind::kMessagesIn;
  if (source.find("@interface") != std::string::npos) return InputKind::kCocoaMm;
  return InputKind::kBruja;
}

std::string ToBrujaSource(InputKind kind, const std::string& source) {
  switch (kind) {
    case InputKind::kMessagesIn:
      return MessagesInToBruja(source);
    case InputKind::kCocoaMm:
      if (source.find("@interface") != std::string::npos)
        return ObjCToBruja(source);
      return ExtractBrujaFromMm(source);
    case InputKind::kBruja:
      return source;
  }
  return source;
}

}  // namespace bruja
