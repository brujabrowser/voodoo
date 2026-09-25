#include "parser.h"

namespace bruja {
namespace {

std::string EscapeCppStringLiteral(const std::string& value) {
  std::string out = "\"";
  for (char c : value) {
    if (c == '"' || c == '\\') out.push_back('\\');
    out.push_back(c);
  }
  out.push_back('"');
  return out;
}

class ParserImpl {
 public:
  explicit ParserImpl(const std::vector<Token>& tokens) : tokens_(tokens) {}

  Module ParseModule() {
    Module module;
    while (Peek().type != TokenType::kEnd) {
      if (AtKeyword("interface") && AtKeyword("mixin", 1)) {
        module.mixins.push_back(ParseMixin());
      } else if (AtKeyword("interface")) {
        module.interfaces.push_back(ParseInterface());
      } else if (AtKeyword("enum")) {
        module.enums.push_back(ParseEnum());
      } else if (AtKeyword("dictionary")) {
        module.dictionaries.push_back(ParseDictionary());
      } else if (AtKeyword("callback")) {
        module.callbacks.push_back(ParseCallback());
      } else if (Peek().type == TokenType::kName && AtKeyword("includes", 1)) {
        module.includes_decls.push_back(ParseIncludes());
      } else {
        throw ParseError(
            "expected 'interface', 'enum', 'dictionary', 'callback', or "
            "'Target includes Mixin;'",
            Peek().line);
      }
    }
    return module;
  }

 private:
  const Token& Peek(size_t ahead = 0) const {
    size_t idx = pos_ + ahead;
    return idx < tokens_.size() ? tokens_[idx] : tokens_.back();
  }

  const Token& Advance() { return tokens_[pos_++]; }

  const Token& Expect(TokenType type, const char* what) {
    if (Peek().type != type) {
      throw ParseError(std::string("expected ") + what, Peek().line);
    }
    return Advance();
  }

  std::string ExpectName(const char* what) {
    if (Peek().type != TokenType::kName) {
      throw ParseError(std::string("expected ") + what, Peek().line);
    }
    return Advance().text;
  }

  void ExpectKeyword(const char* keyword) {
    if (Peek().type != TokenType::kName || Peek().text != keyword) {
      throw ParseError(std::string("expected '") + keyword + "'",
                        Peek().line);
    }
    Advance();
  }

  bool AtKeyword(const char* keyword, size_t ahead = 0) const {
    return Peek(ahead).type == TokenType::kName && Peek(ahead).text == keyword;
  }

  Interface ParseInterface() {
    ExpectKeyword("interface");
    Interface iface;
    iface.name = ExpectName("interface name");
    if (Peek().type == TokenType::kColon) {
      Advance();
      iface.base_name = ExpectName("base interface name");
    }
    Expect(TokenType::kLBrace, "'{'");
    while (Peek().type != TokenType::kRBrace) {
      if (AtKeyword("const")) {
        iface.consts.push_back(ParseConst());
      } else if (AtKeyword("readonly") || AtKeyword("attribute")) {
        iface.attributes.push_back(ParseAttribute());
      } else if (AtKeyword("constructor")) {
        if (iface.has_constructor) {
          throw ParseError(
              "interface '" + iface.name +
                  "' declares more than one constructor (overloads aren't "
                  "supported)",
              Peek().line);
        }
        iface.has_constructor = true;
        iface.constructor_params = ParseConstructorDecl();
      } else {
        iface.methods.push_back(ParseMethod());
      }
    }
    Expect(TokenType::kRBrace, "'}'");
    Expect(TokenType::kSemi, "';' after interface body");
    return iface;
  }

  // `interface mixin Name { ... };` -- same body grammar as an interface
  // minus base/const... no, minus constructor/base (mixins have neither in
  // real Web IDL); `const` is legal on a mixin so kept here.
  MixinDecl ParseMixin() {
    ExpectKeyword("interface");
    ExpectKeyword("mixin");
    MixinDecl decl;
    decl.name = ExpectName("mixin name");
    Expect(TokenType::kLBrace, "'{'");
    while (Peek().type != TokenType::kRBrace) {
      if (AtKeyword("const")) {
        decl.consts.push_back(ParseConst());
      } else if (AtKeyword("readonly") || AtKeyword("attribute")) {
        decl.attributes.push_back(ParseAttribute());
      } else {
        decl.methods.push_back(ParseMethod());
      }
    }
    Expect(TokenType::kRBrace, "'}'");
    Expect(TokenType::kSemi, "';' after mixin body");
    return decl;
  }

  IncludesDecl ParseIncludes() {
    IncludesDecl decl;
    decl.target = ExpectName("includes target interface name");
    ExpectKeyword("includes");
    decl.mixin_name = ExpectName("mixin name");
    Expect(TokenType::kSemi, "';' after includes statement");
    return decl;
  }

  std::vector<Param> ParseConstructorDecl() {
    ExpectKeyword("constructor");
    Expect(TokenType::kLParen, "'('");
    std::vector<Param> params;
    if (Peek().type != TokenType::kRParen) {
      params.push_back(ParseParam());
      while (Peek().type == TokenType::kComma) {
        Advance();
        params.push_back(ParseParam());
      }
    }
    Expect(TokenType::kRParen, "')'");
    Expect(TokenType::kSemi, "';' after constructor declaration");
    return params;
  }

  Const ParseConst() {
    ExpectKeyword("const");
    Const c;
    c.type = ParseType();
    c.name = ExpectName("const name");
    Expect(TokenType::kEquals, "'=' in const declaration");
    c.value_literal = ParseLiteral();
    Expect(TokenType::kSemi, "';' after const declaration");
    return c;
  }

  Attribute ParseAttribute() {
    Attribute attr;
    if (AtKeyword("readonly")) {
      Advance();
      attr.readonly = true;
    }
    ExpectKeyword("attribute");
    attr.type = ParseType();
    attr.name = ExpectName("attribute name");
    Expect(TokenType::kSemi, "';' after attribute declaration");
    return attr;
  }

  Method ParseMethod() {
    Method method;
    method.return_type = ParseType();
    method.name = ExpectName("method name");
    Expect(TokenType::kLParen, "'('");
    if (Peek().type != TokenType::kRParen) {
      method.params.push_back(ParseParam());
      while (Peek().type == TokenType::kComma) {
        Advance();
        method.params.push_back(ParseParam());
      }
    }
    Expect(TokenType::kRParen, "')'");
    Expect(TokenType::kSemi, "';' after method declaration");
    return method;
  }

  Param ParseParam() {
    Param param;
    if (AtKeyword("optional")) {
      Advance();
      param.optional = true;
    }
    param.type = ParseType();
    if (Peek().type == TokenType::kEllipsis) {
      Advance();
      param.variadic = true;
    }
    param.name = ExpectName("parameter name");
    if (Peek().type == TokenType::kEquals) {
      Advance();
      param.has_default = true;
      param.default_literal = ParseLiteral();
    }
    return param;
  }

  EnumDecl ParseEnum() {
    ExpectKeyword("enum");
    EnumDecl decl;
    decl.name = ExpectName("enum name");
    Expect(TokenType::kLBrace, "'{'");
    decl.values.push_back(Expect(TokenType::kString, "an enum value string").text);
    while (Peek().type == TokenType::kComma) {
      Advance();
      decl.values.push_back(
          Expect(TokenType::kString, "an enum value string").text);
    }
    Expect(TokenType::kRBrace, "'}'");
    Expect(TokenType::kSemi, "';' after enum body");
    return decl;
  }

  Dictionary ParseDictionary() {
    ExpectKeyword("dictionary");
    Dictionary dict;
    dict.name = ExpectName("dictionary name");
    if (Peek().type == TokenType::kColon) {
      Advance();
      dict.base_name = ExpectName("base dictionary name");
    }
    Expect(TokenType::kLBrace, "'{'");
    while (Peek().type != TokenType::kRBrace) {
      DictField field;
      field.type = ParseType();
      field.name = ExpectName("dictionary field name");
      if (Peek().type == TokenType::kEquals) {
        Advance();
        field.has_default = true;
        field.default_literal = ParseLiteral();
      }
      Expect(TokenType::kSemi, "';' after dictionary field");
      dict.fields.push_back(std::move(field));
    }
    Expect(TokenType::kRBrace, "'}'");
    Expect(TokenType::kSemi, "';' after dictionary body");
    return dict;
  }

  CallbackDecl ParseCallback() {
    ExpectKeyword("callback");
    CallbackDecl decl;
    decl.name = ExpectName("callback name");
    Expect(TokenType::kEquals, "'=' in callback declaration");
    decl.return_type = ParseType();
    Expect(TokenType::kLParen, "'('");
    if (Peek().type != TokenType::kRParen) {
      decl.params.push_back(ParseParam());
      while (Peek().type == TokenType::kComma) {
        Advance();
        decl.params.push_back(ParseParam());
      }
    }
    Expect(TokenType::kRParen, "')'");
    Expect(TokenType::kSemi, "';' after callback declaration");
    return decl;
  }

  // A literal used in a const value or a default value: a string, a
  // number, or `true`/`false`. Returns text ready to splice directly into
  // generated C++ (strings are re-quoted/escaped; numbers and booleans are
  // already valid C++ literal spelling).
  std::string ParseLiteral() {
    if (Peek().type == TokenType::kString) {
      return EscapeCppStringLiteral(Advance().text);
    }
    if (Peek().type == TokenType::kNumber) {
      return Advance().text;
    }
    if (AtKeyword("true") || AtKeyword("false")) {
      return Advance().text;
    }
    if (AtKeyword("null")) {
      // Only meaningful as a default for an `any`-typed field/param --
      // spliced directly as the C++ constant from <quickjs.h>, not a
      // quoted string (unlike every other literal kind here).
      Advance();
      return "JS_NULL";
    }
    throw ParseError("expected a literal (string, number, boolean, or null)",
                      Peek().line);
  }

  TypeSpec ParseType() {
    TypeSpec type;
    if (Peek().type == TokenType::kLParen) {
      Advance();
      type.kind = TypeKind::kUnion;
      type.union_members = std::make_shared<std::vector<TypeSpec>>();
      type.union_members->push_back(ParseType());
      while (AtKeyword("or")) {
        Advance();
        type.union_members->push_back(ParseType());
      }
      Expect(TokenType::kRParen, "')' to close union type");
      if (Peek().type == TokenType::kQuestion) {
        Advance();
        type.nullable = true;
      }
      return type;
    }
    std::string name = ExpectName("a type");
    if (name == "void") {
      type.kind = TypeKind::kVoid;
    } else if (name == "boolean") {
      type.kind = TypeKind::kBoolean;
    } else if (name == "byte") {
      type.kind = TypeKind::kByte;
    } else if (name == "octet") {
      type.kind = TypeKind::kOctet;
    } else if (name == "short") {
      type.kind = TypeKind::kShort;
    } else if (name == "unsigned") {
      if (AtKeyword("short")) {
        Advance();
        type.kind = TypeKind::kUnsignedShort;
      } else {
        ExpectKeyword("long");
        if (AtKeyword("long")) {
          Advance();
          type.kind = TypeKind::kUnsignedLongLong;
        } else {
          type.kind = TypeKind::kUnsignedLong;
        }
      }
    } else if (name == "long") {
      if (AtKeyword("long")) {
        Advance();
        type.kind = TypeKind::kLongLong;
      } else {
        type.kind = TypeKind::kLong;
      }
    } else if (name == "float") {
      type.kind = TypeKind::kFloat;
    } else if (name == "double") {
      type.kind = TypeKind::kDouble;
    } else if (name == "unrestricted") {
      if (AtKeyword("float")) {
        Advance();
        type.kind = TypeKind::kFloat;
      } else {
        ExpectKeyword("double");
        type.kind = TypeKind::kDouble;
      }
    } else if (name == "DOMString") {
      type.kind = TypeKind::kDOMString;
    } else if (name == "USVString") {
      type.kind = TypeKind::kUSVString;
    } else if (name == "any" || name == "object") {
      type.kind = TypeKind::kAny;
    } else if (name == "sequence") {
      Expect(TokenType::kLt, "'<' after 'sequence'");
      auto element = std::make_shared<TypeSpec>(ParseType());
      Expect(TokenType::kGt, "'>' after sequence element type");
      type.kind = TypeKind::kSequence;
      type.element_type = std::move(element);
    } else if (name == "Promise") {
      Expect(TokenType::kLt, "'<' after 'Promise'");
      auto inner = std::make_shared<TypeSpec>(ParseType());
      Expect(TokenType::kGt, "'>' after Promise element type");
      type.kind = TypeKind::kPromise;
      type.element_type = std::move(inner);
    } else {
      type.kind = TypeKind::kUnresolvedRef;
      type.ref_name = name;
    }
    if (Peek().type == TokenType::kQuestion) {
      Advance();
      type.nullable = true;
    }
    return type;
  }

  const std::vector<Token>& tokens_;
  size_t pos_ = 0;
};

}  // namespace

Module Parse(const std::vector<Token>& tokens) {
  return ParserImpl(tokens).ParseModule();
}

}  // namespace bruja
