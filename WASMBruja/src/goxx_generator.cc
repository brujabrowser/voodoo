// TypeScript -> Go++ (.goxx) and Web IDL Module -> Go++. See goxx_generator.h.
#include "goxx_generator.h"

#include <cctype>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace bruja {
namespace {

bool EndsWith(const std::string& s, const std::string& suffix) {
  return s.size() >= suffix.size() &&
         s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string BaseName(const std::string& path) {
  std::string base = path;
  size_t slash = base.find_last_of("/\\");
  if (slash != std::string::npos) base = base.substr(slash + 1);
  for (const char* ext : {".tsx", ".ts", ".bruja", ".mm", ".h", ".m"}) {
    std::string e = ext;
    if (EndsWith(base, e)) {
      base = base.substr(0, base.size() - e.size());
      break;
    }
  }
  if (EndsWith(base, ".messages.in")) {
    base = base.substr(0, base.size() - 12);
  }
  return base;
}

std::string PascalCase(const std::string& name) {
  if (name.empty()) return name;
  std::string out = name;
  out[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(out[0])));
  return out;
}

// ---- TypeScript lexer -----------------------------------------------------

enum class TsTok {
  Ident,
  Number,
  String,
  End,
  LParen,
  RParen,
  LBrace,
  RBrace,
  LBrack,
  RBrack,
  Comma,
  Semi,
  Colon,
  Dot,
  Question,
  Bang,
  Plus,
  Minus,
  Star,
  Slash,
  Percent,
  Eq,
  EqEq,
  EqEqEq,
  NotEq,
  NotEqEq,
  Lt,
  Gt,
  Le,
  Ge,
  AndAnd,
  OrOr,
  PlusPlus,
  MinusMinus,
  PlusEq,
  MinusEq,
  StarEq,
  SlashEq,
  Arrow,
  Ellipsis,
  Amp,
  Pipe,
};

struct TsToken {
  TsTok type = TsTok::End;
  std::string text;
  int line = 1;
};

class TsLexError : public std::runtime_error {
 public:
  TsLexError(const std::string& msg, int line)
      : std::runtime_error(msg + " (line " + std::to_string(line) + ")"),
        line(line) {}
  int line;
};

class TsParseError : public std::runtime_error {
 public:
  TsParseError(const std::string& msg, int line)
      : std::runtime_error(msg + " (line " + std::to_string(line) + ")"),
        line(line) {}
  int line;
};

bool IsIdentStart(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '$';
}
bool IsIdentCont(char c) { return IsIdentStart(c) || (c >= '0' && c <= '9'); }
bool IsDigit(char c) { return c >= '0' && c <= '9'; }

std::vector<TsToken> TokenizeTs(const std::string& source) {
  std::vector<TsToken> tokens;
  int line = 1;
  size_t i = 0;
  const size_t n = source.size();
  auto push = [&](TsTok type, std::string text = "") {
    tokens.push_back(TsToken{type, std::move(text), line});
  };

  while (i < n) {
    char c = source[i];
    if (c == '\n') {
      ++line;
      ++i;
      continue;
    }
    if (c == ' ' || c == '\t' || c == '\r') {
      ++i;
      continue;
    }
    if (c == '/' && i + 1 < n && source[i + 1] == '/') {
      while (i < n && source[i] != '\n') ++i;
      continue;
    }
    if (c == '/' && i + 1 < n && source[i + 1] == '*') {
      int start = line;
      i += 2;
      while (i + 1 < n && !(source[i] == '*' && source[i + 1] == '/')) {
        if (source[i] == '\n') ++line;
        ++i;
      }
      if (i + 1 >= n) throw TsLexError("unterminated block comment", start);
      i += 2;
      continue;
    }
    if (IsIdentStart(c)) {
      size_t start = i;
      while (i < n && IsIdentCont(source[i])) ++i;
      push(TsTok::Ident, source.substr(start, i - start));
      continue;
    }
    if (c == '"' || c == '\'') {
      char quote = c;
      int start = line;
      ++i;
      std::string value;
      while (i < n && source[i] != quote) {
        if (source[i] == '\n') throw TsLexError("unterminated string literal", start);
        if (source[i] == '\\' && i + 1 < n) {
          char e = source[i + 1];
          if (e == 'n')
            value.push_back('\n');
          else if (e == 't')
            value.push_back('\t');
          else if (e == 'r')
            value.push_back('\r');
          else
            value.push_back(e);
          i += 2;
          continue;
        }
        value.push_back(source[i]);
        ++i;
      }
      if (i >= n) throw TsLexError("unterminated string literal", start);
      ++i;
      push(TsTok::String, value);
      continue;
    }
    if (IsDigit(c)) {
      size_t start = i;
      while (i < n && IsDigit(source[i])) ++i;
      if (i < n && source[i] == '.' && i + 1 < n && IsDigit(source[i + 1])) {
        ++i;
        while (i < n && IsDigit(source[i])) ++i;
      }
      push(TsTok::Number, source.substr(start, i - start));
      continue;
    }
    if (c == '.' && i + 2 < n && source[i + 1] == '.' && source[i + 2] == '.') {
      push(TsTok::Ellipsis);
      i += 3;
      continue;
    }
    if (c == '=' && i + 1 < n && source[i + 1] == '>') {
      push(TsTok::Arrow);
      i += 2;
      continue;
    }
    if (c == '=' && i + 2 < n && source[i + 1] == '=' && source[i + 2] == '=') {
      push(TsTok::EqEqEq);
      i += 3;
      continue;
    }
    if (c == '!' && i + 2 < n && source[i + 1] == '=' && source[i + 2] == '=') {
      push(TsTok::NotEqEq);
      i += 3;
      continue;
    }
    if (c == '=' && i + 1 < n && source[i + 1] == '=') {
      push(TsTok::EqEq);
      i += 2;
      continue;
    }
    if (c == '!' && i + 1 < n && source[i + 1] == '=') {
      push(TsTok::NotEq);
      i += 2;
      continue;
    }
    if (c == '<' && i + 1 < n && source[i + 1] == '=') {
      push(TsTok::Le);
      i += 2;
      continue;
    }
    if (c == '>' && i + 1 < n && source[i + 1] == '=') {
      push(TsTok::Ge);
      i += 2;
      continue;
    }
    if (c == '&' && i + 1 < n && source[i + 1] == '&') {
      push(TsTok::AndAnd);
      i += 2;
      continue;
    }
    if (c == '|' && i + 1 < n && source[i + 1] == '|') {
      push(TsTok::OrOr);
      i += 2;
      continue;
    }
    if (c == '+' && i + 1 < n && source[i + 1] == '+') {
      push(TsTok::PlusPlus);
      i += 2;
      continue;
    }
    if (c == '-' && i + 1 < n && source[i + 1] == '-') {
      push(TsTok::MinusMinus);
      i += 2;
      continue;
    }
    if (c == '+' && i + 1 < n && source[i + 1] == '=') {
      push(TsTok::PlusEq);
      i += 2;
      continue;
    }
    if (c == '-' && i + 1 < n && source[i + 1] == '=') {
      push(TsTok::MinusEq);
      i += 2;
      continue;
    }
    if (c == '*' && i + 1 < n && source[i + 1] == '=') {
      push(TsTok::StarEq);
      i += 2;
      continue;
    }
    if (c == '/' && i + 1 < n && source[i + 1] == '=') {
      push(TsTok::SlashEq);
      i += 2;
      continue;
    }

    switch (c) {
      case '(':
        push(TsTok::LParen);
        break;
      case ')':
        push(TsTok::RParen);
        break;
      case '{':
        push(TsTok::LBrace);
        break;
      case '}':
        push(TsTok::RBrace);
        break;
      case '[':
        push(TsTok::LBrack);
        break;
      case ']':
        push(TsTok::RBrack);
        break;
      case ',':
        push(TsTok::Comma);
        break;
      case ';':
        push(TsTok::Semi);
        break;
      case ':':
        push(TsTok::Colon);
        break;
      case '.':
        push(TsTok::Dot);
        break;
      case '?':
        push(TsTok::Question);
        break;
      case '!':
        push(TsTok::Bang);
        break;
      case '+':
        push(TsTok::Plus);
        break;
      case '-':
        push(TsTok::Minus);
        break;
      case '*':
        push(TsTok::Star);
        break;
      case '/':
        push(TsTok::Slash);
        break;
      case '%':
        push(TsTok::Percent);
        break;
      case '=':
        push(TsTok::Eq);
        break;
      case '<':
        push(TsTok::Lt);
        break;
      case '>':
        push(TsTok::Gt);
        break;
      case '&':
        push(TsTok::Amp);
        break;
      case '|':
        push(TsTok::Pipe);
        break;
      default:
        throw TsLexError(std::string("unexpected character '") + c + "'", line);
    }
    ++i;
  }
  tokens.push_back(TsToken{TsTok::End, "", line});
  return tokens;
}

// ---- TypeScript AST -------------------------------------------------------

struct TsType {
  std::string name;  // Go++ spelling after MapTsType, or raw ident
  bool array = false;
};

struct Expr {
  enum class Kind {
    Ident,
    Number,
    String,
    Bool,
    Null,
    This,
    Call,
    Member,
    Index,
    Binary,
    Unary,
    PrefixUpdate,
    PostfixUpdate,
    Assign,
    New,
    Array,
    Object,
    Func,
  } kind = Kind::Ident;
  std::string text;
  std::string op;
  std::unique_ptr<Expr> left;
  std::unique_ptr<Expr> right;
  std::vector<std::unique_ptr<Expr>> args;
  std::vector<std::pair<std::string, std::unique_ptr<Expr>>> fields;
  TsType func_return;
  std::vector<std::pair<std::string, TsType>> func_params;
  std::vector<std::unique_ptr<struct Stmt>> func_body;
};

struct TsParam {
  std::string name;
  TsType type;
};

struct Stmt {
  enum class Kind {
    Block,
    Var,
    If,
    For,
    ForOf,
    While,
    Return,
    Expr,
    Func,
    Class,
    Interface,
    TypeAlias,
    Break,
    Continue,
  } kind = Kind::Expr;
  std::string name;
  TsType type;
  std::unique_ptr<Expr> expr;
  std::unique_ptr<Expr> expr2;
  std::unique_ptr<Stmt> init;
  std::unique_ptr<Stmt> then_s;
  std::unique_ptr<Stmt> else_s;
  std::vector<std::unique_ptr<Stmt>> body;
  std::vector<TsParam> params;
  std::vector<std::pair<std::string, TsType>> fields;
  std::vector<std::unique_ptr<Stmt>> methods;  // class methods as Func stmts
  std::unique_ptr<Stmt> ctor;
  std::vector<std::pair<std::string, std::unique_ptr<Expr>>> field_inits;
  bool exported = false;
};

struct Program {
  std::vector<std::string> imports;
  std::vector<std::unique_ptr<Stmt>> decls;
  bool needs_fmt = false;
  bool has_main = false;
};

// ---- Parser ---------------------------------------------------------------

class TsParser {
 public:
  explicit TsParser(std::vector<TsToken> toks) : toks_(std::move(toks)) {}

  Program Parse() {
    Program p;
    while (!Check(TsTok::End)) {
      if (CheckIdent("import")) {
        ParseImport(p);
        continue;
      }
      p.decls.push_back(ParseStatement());
    }
    return p;
  }

 private:
  std::vector<TsToken> toks_;
  size_t i_ = 0;

  const TsToken& Peek() const { return toks_[i_]; }
  const TsToken& PeekN(size_t n) const {
    size_t j = i_ + n;
    if (j >= toks_.size()) return toks_.back();
    return toks_[j];
  }
  bool Check(TsTok t) const { return Peek().type == t; }
  bool CheckIdent(const char* w) const {
    return Check(TsTok::Ident) && Peek().text == w;
  }
  TsToken Advance() { return toks_[i_++]; }
  bool Match(TsTok t) {
    if (Check(t)) {
      Advance();
      return true;
    }
    return false;
  }
  TsToken Expect(TsTok t, const char* what) {
    if (!Check(t)) {
      throw TsParseError(std::string("expected ") + what, Peek().line);
    }
    return Advance();
  }
  void OptionalSemi() { Match(TsTok::Semi); }
  void SkipModifiers() {
    while (CheckIdent("export") || CheckIdent("default") || CheckIdent("public") ||
           CheckIdent("private") || CheckIdent("protected") || CheckIdent("readonly") ||
           CheckIdent("static") || CheckIdent("declare") || CheckIdent("abstract")) {
      if (CheckIdent("async")) {
        throw TsParseError("async is not supported in the Go++ converter", Peek().line);
      }
      Advance();
    }
    if (CheckIdent("async")) {
      throw TsParseError("async is not supported in the Go++ converter", Peek().line);
    }
  }

  void ParseImport(Program& p) {
    Advance();  // import
    if (Match(TsTok::String)) {
      p.imports.push_back(StripTsExt(toks_[i_ - 1].text));
      OptionalSemi();
      return;
    }
    if (Match(TsTok::Star)) {
      if (CheckIdent("as")) Advance();
      if (Check(TsTok::Ident)) Advance();
    } else if (Match(TsTok::LBrace)) {
      while (!Check(TsTok::RBrace) && !Check(TsTok::End)) {
        if (Check(TsTok::Ident)) Advance();
        Match(TsTok::Comma);
        if (CheckIdent("as") && PeekN(1).type == TsTok::Ident) {
          Advance();
          Advance();
        }
      }
      Expect(TsTok::RBrace, "'}'");
    } else if (Check(TsTok::Ident)) {
      Advance();
      if (Match(TsTok::Comma) && Match(TsTok::LBrace)) {
        while (!Check(TsTok::RBrace) && !Check(TsTok::End)) {
          if (Check(TsTok::Ident)) Advance();
          Match(TsTok::Comma);
        }
        Expect(TsTok::RBrace, "'}'");
      }
    }
    if (CheckIdent("from")) Advance();
    if (Check(TsTok::String)) {
      p.imports.push_back(StripTsExt(Advance().text));
    }
    OptionalSemi();
  }

  static std::string StripTsExt(std::string path) {
    if (EndsWith(path, ".tsx")) path = path.substr(0, path.size() - 4);
    else if (EndsWith(path, ".ts")) path = path.substr(0, path.size() - 3);
    return path;
  }

  TsType ParseType() {
    TsType t;
    if (Match(TsTok::LBrace)) {
      // Inline object type: treat as a struct-shaped `any` for v1 values;
      // field lists in `type X = { ... }` are parsed separately.
      int depth = 1;
      while (depth > 0 && !Check(TsTok::End)) {
        if (Match(TsTok::LBrace))
          ++depth;
        else if (Match(TsTok::RBrace))
          --depth;
        else
          Advance();
      }
      t.name = "any";
      return t;
    }
    if (Check(TsTok::Ident)) {
      t.name = Advance().text;
    } else {
      t.name = "any";
    }
    if (t.name == "Array" && Match(TsTok::Lt)) {
      TsType inner = ParseType();
      Expect(TsTok::Gt, "'>'");
      t = inner;
      t.array = true;
    } else if (t.name == "Promise" && Match(TsTok::Lt)) {
      TsType inner = ParseType();
      Expect(TsTok::Gt, "'>'");
      t = inner;
    } else if (Match(TsTok::Lt)) {
      int depth = 1;
      while (depth > 0 && !Check(TsTok::End)) {
        if (Match(TsTok::Lt))
          ++depth;
        else if (Match(TsTok::Gt))
          --depth;
        else
          Advance();
      }
    }
    while (Match(TsTok::LBrack)) {
      Expect(TsTok::RBrack, "']'");
      t.array = true;
    }
    while (Match(TsTok::Pipe)) {
      ParseType();  // T | null | undefined -- keep the first
    }
    return t;
  }

  std::unique_ptr<Stmt> ParseStatement() {
    SkipModifiers();
    if (CheckIdent("async")) {
      throw TsParseError("async is not supported in the Go++ converter", Peek().line);
    }
    if (Check(TsTok::LBrace)) return ParseBlock();
    if (CheckIdent("function")) return ParseFunction();
    if (CheckIdent("class")) return ParseClass();
    if (CheckIdent("interface")) return ParseInterface();
    if (CheckIdent("type")) return ParseTypeAlias();
    if (CheckIdent("let") || CheckIdent("const") || CheckIdent("var"))
      return ParseVar();
    if (CheckIdent("if")) return ParseIf();
    if (CheckIdent("for")) return ParseFor();
    if (CheckIdent("while")) return ParseWhile();
    if (CheckIdent("return")) {
      auto s = std::make_unique<Stmt>();
      s->kind = Stmt::Kind::Return;
      int line = Advance().line;
      if (!Check(TsTok::Semi) && !Check(TsTok::RBrace) && Peek().line == line) {
        s->expr = ParseExpression();
      }
      OptionalSemi();
      return s;
    }
    if (CheckIdent("break")) {
      auto s = std::make_unique<Stmt>();
      s->kind = Stmt::Kind::Break;
      Advance();
      OptionalSemi();
      return s;
    }
    if (CheckIdent("continue")) {
      auto s = std::make_unique<Stmt>();
      s->kind = Stmt::Kind::Continue;
      Advance();
      OptionalSemi();
      return s;
    }
    auto s = std::make_unique<Stmt>();
    s->kind = Stmt::Kind::Expr;
    s->expr = ParseExpression();
    OptionalSemi();
    return s;
  }

  std::unique_ptr<Stmt> ParseBlock() {
    auto s = std::make_unique<Stmt>();
    s->kind = Stmt::Kind::Block;
    Expect(TsTok::LBrace, "'{'");
    while (!Check(TsTok::RBrace) && !Check(TsTok::End)) {
      s->body.push_back(ParseStatement());
    }
    Expect(TsTok::RBrace, "'}'");
    return s;
  }

  std::unique_ptr<Stmt> ParseFunction() {
    Advance();  // function
    auto s = std::make_unique<Stmt>();
    s->kind = Stmt::Kind::Func;
    if (Check(TsTok::Ident)) s->name = Advance().text;
    Expect(TsTok::LParen, "'('");
    s->params = ParseParamList();
    Expect(TsTok::RParen, "')'");
    if (Match(TsTok::Colon)) s->type = ParseType();
    auto body = ParseBlock();
    s->body = std::move(body->body);
    return s;
  }

  std::vector<TsParam> ParseParamList() {
    std::vector<TsParam> params;
    while (!Check(TsTok::RParen) && !Check(TsTok::End)) {
      Match(TsTok::Ellipsis);
      TsParam p;
      if (Check(TsTok::Ident)) p.name = Advance().text;
      Match(TsTok::Question);
      if (Match(TsTok::Colon)) p.type = ParseType();
      params.push_back(std::move(p));
      if (!Match(TsTok::Comma)) break;
    }
    return params;
  }

  std::unique_ptr<Stmt> ParseClass() {
    Advance();  // class
    auto s = std::make_unique<Stmt>();
    s->kind = Stmt::Kind::Class;
    s->name = Expect(TsTok::Ident, "class name").text;
    if (CheckIdent("extends") || CheckIdent("implements")) {
      Advance();
      if (Check(TsTok::Ident)) Advance();
    }
    Expect(TsTok::LBrace, "'{'");
    while (!Check(TsTok::RBrace) && !Check(TsTok::End)) {
      SkipModifiers();
      if (CheckIdent("constructor")) {
        Advance();
        auto ctor = std::make_unique<Stmt>();
        ctor->kind = Stmt::Kind::Func;
        ctor->name = "constructor";
        Expect(TsTok::LParen, "'('");
        ctor->params = ParseParamList();
        Expect(TsTok::RParen, "')'");
        auto body = ParseBlock();
        ctor->body = std::move(body->body);
        s->ctor = std::move(ctor);
        continue;
      }
      if (!Check(TsTok::Ident)) {
        Advance();
        continue;
      }
      std::string name = Advance().text;
      if (Match(TsTok::LParen)) {
        auto m = std::make_unique<Stmt>();
        m->kind = Stmt::Kind::Func;
        m->name = name;
        m->params = ParseParamList();
        Expect(TsTok::RParen, "')'");
        if (Match(TsTok::Colon)) m->type = ParseType();
        auto body = ParseBlock();
        m->body = std::move(body->body);
        s->methods.push_back(std::move(m));
      } else {
        Match(TsTok::Question);
        TsType ty;
        if (Match(TsTok::Colon)) ty = ParseType();
        std::unique_ptr<Expr> init;
        if (Match(TsTok::Eq)) init = ParseExpression();
        s->fields.push_back({name, ty});
        if (init) s->field_inits.push_back({name, std::move(init)});
        OptionalSemi();
      }
    }
    Expect(TsTok::RBrace, "'}'");
    return s;
  }

  std::unique_ptr<Stmt> ParseInterface() {
    Advance();  // interface
    auto s = std::make_unique<Stmt>();
    s->kind = Stmt::Kind::Interface;
    s->name = Expect(TsTok::Ident, "interface name").text;
    if (CheckIdent("extends")) {
      Advance();
      if (Check(TsTok::Ident)) Advance();
    }
    Expect(TsTok::LBrace, "'{'");
    while (!Check(TsTok::RBrace) && !Check(TsTok::End)) {
      if (!Check(TsTok::Ident)) {
        Advance();
        continue;
      }
      std::string name = Advance().text;
      Match(TsTok::Question);
      if (Match(TsTok::LParen)) {
        auto m = std::make_unique<Stmt>();
        m->kind = Stmt::Kind::Func;
        m->name = name;
        m->params = ParseParamList();
        Expect(TsTok::RParen, "')'");
        if (Match(TsTok::Colon)) m->type = ParseType();
        s->methods.push_back(std::move(m));
        OptionalSemi();
      } else {
        Expect(TsTok::Colon, "':'");
        TsType ty = ParseType();
        s->fields.push_back({name, ty});
        OptionalSemi();
      }
    }
    Expect(TsTok::RBrace, "'}'");
    return s;
  }

  std::unique_ptr<Stmt> ParseTypeAlias() {
    Advance();  // type
    auto s = std::make_unique<Stmt>();
    s->kind = Stmt::Kind::TypeAlias;
    s->name = Expect(TsTok::Ident, "type name").text;
    Expect(TsTok::Eq, "'='");
    if (Check(TsTok::LBrace)) {
      s->kind = Stmt::Kind::Interface;  // struct-shaped
      Expect(TsTok::LBrace, "'{'");
      while (!Check(TsTok::RBrace) && !Check(TsTok::End)) {
        if (!Check(TsTok::Ident)) {
          Advance();
          continue;
        }
        std::string name = Advance().text;
        Match(TsTok::Question);
        Expect(TsTok::Colon, "':'");
        TsType ty = ParseType();
        s->fields.push_back({name, ty});
        OptionalSemi();
        Match(TsTok::Comma);
      }
      Expect(TsTok::RBrace, "'}'");
    } else {
      s->type = ParseType();
    }
    OptionalSemi();
    return s;
  }

  std::unique_ptr<Stmt> ParseVar() {
    Advance();  // let/const/var
    auto s = std::make_unique<Stmt>();
    s->kind = Stmt::Kind::Var;
    s->name = Expect(TsTok::Ident, "variable name").text;
    if (Match(TsTok::Colon)) s->type = ParseType();
    if (Match(TsTok::Eq)) s->expr = ParseExpression();
    OptionalSemi();
    return s;
  }

  std::unique_ptr<Stmt> ParseIf() {
    Advance();
    auto s = std::make_unique<Stmt>();
    s->kind = Stmt::Kind::If;
    Expect(TsTok::LParen, "'('");
    s->expr = ParseExpression();
    Expect(TsTok::RParen, "')'");
    s->then_s = ParseStatement();
    if (CheckIdent("else")) {
      Advance();
      s->else_s = ParseStatement();
    }
    return s;
  }

  std::unique_ptr<Stmt> ParseWhile() {
    Advance();
    auto s = std::make_unique<Stmt>();
    s->kind = Stmt::Kind::While;
    Expect(TsTok::LParen, "'('");
    s->expr = ParseExpression();
    Expect(TsTok::RParen, "')'");
    s->then_s = ParseStatement();
    return s;
  }

  std::unique_ptr<Stmt> ParseFor() {
    Advance();
    auto s = std::make_unique<Stmt>();
    Expect(TsTok::LParen, "'('");
    // for (const x of arr)
    if ((CheckIdent("let") || CheckIdent("const") || CheckIdent("var")) &&
        PeekN(1).type == TsTok::Ident && PeekN(2).type == TsTok::Ident &&
        PeekN(2).text == "of") {
      s->kind = Stmt::Kind::ForOf;
      Advance();
      s->name = Advance().text;
      Advance();  // of
      s->expr = ParseExpression();
      Expect(TsTok::RParen, "')'");
      s->then_s = ParseStatement();
      return s;
    }
    s->kind = Stmt::Kind::For;
    if (!Check(TsTok::Semi)) {
      if (CheckIdent("let") || CheckIdent("const") || CheckIdent("var"))
        s->init = ParseVar();
      else {
        auto e = std::make_unique<Stmt>();
        e->kind = Stmt::Kind::Expr;
        e->expr = ParseExpression();
        s->init = std::move(e);
        OptionalSemi();
      }
    } else {
      Advance();
    }
    if (!Check(TsTok::Semi)) s->expr = ParseExpression();
    Expect(TsTok::Semi, "';'");
    if (!Check(TsTok::RParen)) s->expr2 = ParseExpression();
    Expect(TsTok::RParen, "')'");
    s->then_s = ParseStatement();
    return s;
  }

  std::unique_ptr<Expr> ParseExpression() { return ParseAssign(); }

  std::unique_ptr<Expr> ParseAssign() {
    auto left = ParseOr();
    if (Check(TsTok::Eq) || Check(TsTok::PlusEq) || Check(TsTok::MinusEq) ||
        Check(TsTok::StarEq) || Check(TsTok::SlashEq)) {
      auto e = std::make_unique<Expr>();
      e->kind = Expr::Kind::Assign;
      e->op = OpSpelling(Advance().type);
      e->left = std::move(left);
      e->right = ParseAssign();
      return e;
    }
    return left;
  }

  std::unique_ptr<Expr> ParseOr() {
    auto e = ParseAnd();
    while (Match(TsTok::OrOr)) {
      auto n = std::make_unique<Expr>();
      n->kind = Expr::Kind::Binary;
      n->op = "||";
      n->left = std::move(e);
      n->right = ParseAnd();
      e = std::move(n);
    }
    return e;
  }

  std::unique_ptr<Expr> ParseAnd() {
    auto e = ParseEq();
    while (Match(TsTok::AndAnd)) {
      auto n = std::make_unique<Expr>();
      n->kind = Expr::Kind::Binary;
      n->op = "&&";
      n->left = std::move(e);
      n->right = ParseEq();
      e = std::move(n);
    }
    return e;
  }

  std::unique_ptr<Expr> ParseEq() {
    auto e = ParseCmp();
    while (Check(TsTok::EqEq) || Check(TsTok::EqEqEq) || Check(TsTok::NotEq) ||
           Check(TsTok::NotEqEq)) {
      TsTok t = Advance().type;
      auto n = std::make_unique<Expr>();
      n->kind = Expr::Kind::Binary;
      n->op = (t == TsTok::EqEq || t == TsTok::EqEqEq) ? "==" : "!=";
      n->left = std::move(e);
      n->right = ParseCmp();
      e = std::move(n);
    }
    return e;
  }

  std::unique_ptr<Expr> ParseCmp() {
    auto e = ParseAdd();
    while (Check(TsTok::Lt) || Check(TsTok::Gt) || Check(TsTok::Le) ||
           Check(TsTok::Ge)) {
      auto n = std::make_unique<Expr>();
      n->kind = Expr::Kind::Binary;
      n->op = OpSpelling(Advance().type);
      n->left = std::move(e);
      n->right = ParseAdd();
      e = std::move(n);
    }
    return e;
  }

  std::unique_ptr<Expr> ParseAdd() {
    auto e = ParseMul();
    while (Check(TsTok::Plus) || Check(TsTok::Minus)) {
      auto n = std::make_unique<Expr>();
      n->kind = Expr::Kind::Binary;
      n->op = OpSpelling(Advance().type);
      n->left = std::move(e);
      n->right = ParseMul();
      e = std::move(n);
    }
    return e;
  }

  std::unique_ptr<Expr> ParseMul() {
    auto e = ParseUnary();
    while (Check(TsTok::Star) || Check(TsTok::Slash) || Check(TsTok::Percent)) {
      auto n = std::make_unique<Expr>();
      n->kind = Expr::Kind::Binary;
      n->op = OpSpelling(Advance().type);
      n->left = std::move(e);
      n->right = ParseUnary();
      e = std::move(n);
    }
    return e;
  }

  std::unique_ptr<Expr> ParseUnary() {
    if (Check(TsTok::Bang) || Check(TsTok::Minus) || Check(TsTok::Plus)) {
      auto e = std::make_unique<Expr>();
      e->kind = Expr::Kind::Unary;
      e->op = OpSpelling(Advance().type);
      e->left = ParseUnary();
      return e;
    }
    if (Check(TsTok::PlusPlus) || Check(TsTok::MinusMinus)) {
      auto e = std::make_unique<Expr>();
      e->kind = Expr::Kind::PrefixUpdate;
      e->op = OpSpelling(Advance().type);
      e->left = ParseUnary();
      return e;
    }
    return ParsePostfix();
  }

  std::unique_ptr<Expr> ParsePostfix() {
    auto e = ParsePrimary();
    for (;;) {
      if (Match(TsTok::LParen)) {
        auto n = std::make_unique<Expr>();
        n->kind = Expr::Kind::Call;
        n->left = std::move(e);
        if (!Check(TsTok::RParen)) {
          do {
            n->args.push_back(ParseExpression());
          } while (Match(TsTok::Comma));
        }
        Expect(TsTok::RParen, "')'");
        e = std::move(n);
        continue;
      }
      if (Match(TsTok::Dot) || (Check(TsTok::Question) && PeekN(1).type == TsTok::Dot)) {
        if (Check(TsTok::Question)) {
          Advance();
          Advance();
        }
        auto n = std::make_unique<Expr>();
        n->kind = Expr::Kind::Member;
        n->left = std::move(e);
        n->text = Expect(TsTok::Ident, "member name").text;
        e = std::move(n);
        continue;
      }
      if (Match(TsTok::LBrack)) {
        auto n = std::make_unique<Expr>();
        n->kind = Expr::Kind::Index;
        n->left = std::move(e);
        n->right = ParseExpression();
        Expect(TsTok::RBrack, "']'");
        e = std::move(n);
        continue;
      }
      if (Check(TsTok::PlusPlus) || Check(TsTok::MinusMinus)) {
        auto n = std::make_unique<Expr>();
        n->kind = Expr::Kind::PostfixUpdate;
        n->op = OpSpelling(Advance().type);
        n->left = std::move(e);
        e = std::move(n);
        continue;
      }
      if (CheckIdent("as")) {
        Advance();
        ParseType();  // drop the assertion
        continue;
      }
      if (Match(TsTok::Bang) && Peek().type != TsTok::Eq) {
        continue;  // non-null assertion
      }
      break;
    }
    return e;
  }

  std::unique_ptr<Expr> ParsePrimary() {
    if (CheckIdent("true") || CheckIdent("false")) {
      auto e = std::make_unique<Expr>();
      e->kind = Expr::Kind::Bool;
      e->text = Advance().text;
      return e;
    }
    if (CheckIdent("null") || CheckIdent("undefined")) {
      auto e = std::make_unique<Expr>();
      e->kind = Expr::Kind::Null;
      Advance();
      return e;
    }
    if (CheckIdent("this")) {
      auto e = std::make_unique<Expr>();
      e->kind = Expr::Kind::This;
      Advance();
      return e;
    }
    if (CheckIdent("new")) {
      Advance();
      auto e = std::make_unique<Expr>();
      e->kind = Expr::Kind::New;
      e->text = Expect(TsTok::Ident, "constructor name").text;
      if (Match(TsTok::LParen)) {
        if (!Check(TsTok::RParen)) {
          do {
            e->args.push_back(ParseExpression());
          } while (Match(TsTok::Comma));
        }
        Expect(TsTok::RParen, "')'");
      }
      return e;
    }
    if (CheckIdent("function")) {
      auto fn = ParseFunction();
      auto e = std::make_unique<Expr>();
      e->kind = Expr::Kind::Func;
      e->text = fn->name;
      e->func_return = fn->type;
      for (const TsParam& p : fn->params) e->func_params.push_back({p.name, p.type});
      e->func_body = std::move(fn->body);
      return e;
    }
    if (Check(TsTok::Ident)) {
      auto e = std::make_unique<Expr>();
      e->kind = Expr::Kind::Ident;
      e->text = Advance().text;
      // x => expr  /  x => { }
      if (Match(TsTok::Arrow)) {
        return FinishArrow(std::move(e), {});
      }
      return e;
    }
    if (Check(TsTok::Number)) {
      auto e = std::make_unique<Expr>();
      e->kind = Expr::Kind::Number;
      e->text = Advance().text;
      return e;
    }
    if (Check(TsTok::String)) {
      auto e = std::make_unique<Expr>();
      e->kind = Expr::Kind::String;
      e->text = Advance().text;
      return e;
    }
    if (Match(TsTok::LParen)) {
      // (params) =>  or  (expr)
      if (LooksLikeArrowParams()) {
        std::vector<std::pair<std::string, TsType>> params;
        if (!Check(TsTok::RParen)) {
          do {
            std::string name;
            if (Check(TsTok::Ident)) name = Advance().text;
            TsType ty;
            if (Match(TsTok::Colon)) ty = ParseType();
            params.push_back({name, ty});
          } while (Match(TsTok::Comma));
        }
        Expect(TsTok::RParen, "')'");
        TsType ret;
        if (Match(TsTok::Colon)) ret = ParseType();
        Expect(TsTok::Arrow, "'=>'");
        auto dummy = std::make_unique<Expr>();
        dummy->kind = Expr::Kind::Ident;
        auto arrow = FinishArrow(std::move(dummy), params);
        arrow->func_return = ret;
        return arrow;
      }
      auto e = ParseExpression();
      Expect(TsTok::RParen, "')'");
      return e;
    }
    if (Match(TsTok::LBrack)) {
      auto e = std::make_unique<Expr>();
      e->kind = Expr::Kind::Array;
      if (!Check(TsTok::RBrack)) {
        do {
          e->args.push_back(ParseExpression());
        } while (Match(TsTok::Comma));
      }
      Expect(TsTok::RBrack, "']'");
      return e;
    }
    if (Match(TsTok::LBrace)) {
      auto e = std::make_unique<Expr>();
      e->kind = Expr::Kind::Object;
      while (!Check(TsTok::RBrace) && !Check(TsTok::End)) {
        std::string key;
        if (Check(TsTok::Ident) || Check(TsTok::String))
          key = Advance().text;
        Expect(TsTok::Colon, "':'");
        e->fields.push_back({key, ParseExpression()});
        Match(TsTok::Comma);
      }
      Expect(TsTok::RBrace, "'}'");
      return e;
    }
    throw TsParseError("expected expression", Peek().line);
  }

  bool LooksLikeArrowParams() const {
    // Scan from current token to matching ')' then '=>' (optional ': type').
    size_t j = i_;
    int depth = 1;
    while (j < toks_.size() && depth > 0) {
      if (toks_[j].type == TsTok::LParen) ++depth;
      else if (toks_[j].type == TsTok::RParen) --depth;
      ++j;
    }
    if (j >= toks_.size()) return false;
    if (toks_[j].type == TsTok::Colon) {
      while (j < toks_.size() && toks_[j].type != TsTok::Arrow &&
             toks_[j].type != TsTok::Semi && toks_[j].type != TsTok::LBrace) {
        if (toks_[j].type == TsTok::Arrow) break;
        ++j;
      }
    }
    return j < toks_.size() && toks_[j].type == TsTok::Arrow;
  }

  std::unique_ptr<Expr> FinishArrow(std::unique_ptr<Expr> ident,
                                    std::vector<std::pair<std::string, TsType>> params) {
    auto e = std::make_unique<Expr>();
    e->kind = Expr::Kind::Func;
    if (params.empty() && ident && ident->kind == Expr::Kind::Ident &&
        !ident->text.empty()) {
      params.push_back({ident->text, TsType{}});
    }
    e->func_params = std::move(params);
    if (Check(TsTok::LBrace)) {
      auto body = ParseBlock();
      e->func_body = std::move(body->body);
    } else {
      auto ret = std::make_unique<Stmt>();
      ret->kind = Stmt::Kind::Return;
      ret->expr = ParseAssign();
      e->func_body.push_back(std::move(ret));
    }
    return e;
  }

  static const char* OpSpelling(TsTok t) {
    switch (t) {
      case TsTok::Plus:
        return "+";
      case TsTok::Minus:
        return "-";
      case TsTok::Star:
        return "*";
      case TsTok::Slash:
        return "/";
      case TsTok::Percent:
        return "%";
      case TsTok::Eq:
        return "=";
      case TsTok::PlusEq:
        return "+=";
      case TsTok::MinusEq:
        return "-=";
      case TsTok::StarEq:
        return "*=";
      case TsTok::SlashEq:
        return "/=";
      case TsTok::Lt:
        return "<";
      case TsTok::Gt:
        return ">";
      case TsTok::Le:
        return "<=";
      case TsTok::Ge:
        return ">=";
      case TsTok::PlusPlus:
        return "++";
      case TsTok::MinusMinus:
        return "--";
      case TsTok::Bang:
        return "!";
      default:
        return "?";
    }
  }
};

// ---- Emit Go++ ------------------------------------------------------------

std::string MapTsType(const TsType& t) {
  std::string n = t.name;
  if (n.empty() || n == "void") return t.array ? "[]any" : "";
  if (n == "number") n = "float64";
  else if (n == "string") n = "string";
  else if (n == "boolean" || n == "bool") n = "bool";
  else if (n == "any" || n == "unknown" || n == "object") n = "any";
  else if (n == "undefined" || n == "null" || n == "never") n = "any";
  if (t.array) return "[]" + n;
  return n;
}

std::string EscapeGoString(const std::string& s) {
  std::string out;
  out.push_back('"');
  for (unsigned char c : s) {
    switch (c) {
      case '\\':
        out += "\\\\";
        break;
      case '"':
        out += "\\\"";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\t':
        out += "\\t";
        break;
      case '\r':
        out += "\\r";
        break;
      default:
        out.push_back(static_cast<char>(c));
        break;
    }
  }
  out.push_back('"');
  return out;
}

struct Emitter {
  std::ostringstream out;
  int indent = 0;
  bool needs_fmt = false;
  std::unordered_set<std::string> classes;
  std::string ret_hint;

  void Ind() {
    for (int i = 0; i < indent; ++i) out << '\t';
  }
  void Nl() { out << '\n'; }

  void EmitProgram(Program& p, const std::string& source_filename) {
    ScanFmt(p);
    CollectClasses(p);
    out << "// Generated by brujac (--backend=goxx) from " << source_filename
        << ". DO NOT EDIT.\n";
    out << "// .goxx marks TypeScript converted to Go++; handwritten Go++ stays .go.\n";
    out << "package main\n\n";

    std::vector<std::string> imports = p.imports;
    if (needs_fmt) {
      bool has = false;
      for (const std::string& im : imports)
        if (im == "fmt") has = true;
      if (!has) imports.insert(imports.begin(), "fmt");
    }
    if (!imports.empty()) {
      if (imports.size() == 1) {
        out << "import \"" << imports[0] << "\"\n\n";
      } else {
        out << "import (\n";
        for (const std::string& im : imports) out << "\t\"" << im << "\"\n";
        out << ")\n\n";
      }
    }

    std::vector<Stmt*> top_stmts;
    bool saw_main = false;
    for (auto& d : p.decls) {
      if (d->kind == Stmt::Kind::Func && d->name == "main") saw_main = true;
      if (d->kind == Stmt::Kind::Func || d->kind == Stmt::Kind::Class ||
          d->kind == Stmt::Kind::Interface || d->kind == Stmt::Kind::TypeAlias) {
        EmitDecl(*d);
      } else {
        top_stmts.push_back(d.get());
      }
    }
    if (!top_stmts.empty() && !saw_main) {
      out << "func main() {\n";
      indent = 1;
      for (Stmt* s : top_stmts) EmitStmt(*s);
      indent = 0;
      out << "}\n";
    } else if (!top_stmts.empty() && saw_main) {
      throw TsParseError(
          "top-level statements plus function main() -- put the statements in main",
          1);
    }
  }

  void ScanFmt(const Program& p) {
    for (const auto& d : p.decls) ScanFmtStmt(*d);
  }
  void ScanFmtStmt(const Stmt& s) {
    if (s.expr) ScanFmtExpr(*s.expr);
    if (s.expr2) ScanFmtExpr(*s.expr2);
    if (s.init) ScanFmtStmt(*s.init);
    if (s.then_s) ScanFmtStmt(*s.then_s);
    if (s.else_s) ScanFmtStmt(*s.else_s);
    if (s.ctor) ScanFmtStmt(*s.ctor);
    for (const auto& b : s.body) ScanFmtStmt(*b);
    for (const auto& m : s.methods) ScanFmtStmt(*m);
    for (const auto& fi : s.field_inits)
      if (fi.second) ScanFmtExpr(*fi.second);
  }
  void ScanFmtExpr(const Expr& e) {
    if (e.kind == Expr::Kind::Call && e.left && e.left->kind == Expr::Kind::Member &&
        e.left->left && e.left->left->kind == Expr::Kind::Ident &&
        e.left->left->text == "console") {
      needs_fmt = true;
    }
    if (e.left) ScanFmtExpr(*e.left);
    if (e.right) ScanFmtExpr(*e.right);
    for (const auto& a : e.args) ScanFmtExpr(*a);
    for (const auto& f : e.fields)
      if (f.second) ScanFmtExpr(*f.second);
    for (const auto& b : e.func_body) ScanFmtStmt(*b);
  }

  void CollectClasses(const Program& p) {
    for (const auto& d : p.decls)
      if (d->kind == Stmt::Kind::Class) classes.insert(d->name);
  }

  void EmitDecl(const Stmt& s) {
    switch (s.kind) {
      case Stmt::Kind::Interface:
        EmitInterface(s);
        break;
      case Stmt::Kind::TypeAlias:
        Ind();
        out << "type " << s.name << " = " << MapTsType(s.type) << "\n\n";
        break;
      case Stmt::Kind::Class:
        EmitClass(s);
        break;
      case Stmt::Kind::Func:
        EmitFunc(s, /*recv=*/"");
        break;
      default:
        EmitStmt(s);
        break;
    }
  }

  void EmitInterface(const Stmt& s) {
    if (!s.methods.empty() && s.fields.empty()) {
      out << "type " << s.name << " interface {\n";
      for (const auto& m : s.methods) {
        out << "\t" << m->name << "(";
        EmitParams(m->params);
        out << ")";
        std::string ret = MapTsType(m->type);
        if (!ret.empty()) out << " " << ret;
        out << "\n";
      }
      out << "}\n\n";
      return;
    }
    out << "type " << s.name << " struct {\n";
    for (const auto& f : s.fields) {
      std::string ty = MapTsType(f.second);
      if (ty.empty()) ty = "any";
      out << "\t" << f.first << " " << ty << "\n";
    }
    out << "}\n\n";
    if (!s.methods.empty()) {
      out << "type " << s.name << "Methods interface {\n";
      for (const auto& m : s.methods) {
        out << "\t" << m->name << "(";
        EmitParams(m->params);
        out << ")";
        std::string ret = MapTsType(m->type);
        if (!ret.empty()) out << " " << ret;
        out << "\n";
      }
      out << "}\n\n";
    }
  }

  void EmitClass(const Stmt& s) {
    out << "type " << s.name << " struct {\n";
    for (const auto& f : s.fields) {
      std::string ty = MapTsType(f.second);
      if (ty.empty()) ty = "any";
      out << "\t" << f.first << " " << ty << "\n";
    }
    out << "}\n\n";

    out << "func New" << s.name << "(";
    if (s.ctor) EmitParams(s.ctor->params);
    out << ") *" << s.name << " {\n";
    out << "\tthis := new(" << s.name << ")\n";
    indent = 1;
    for (const auto& fi : s.field_inits) {
      Ind();
      out << "this." << fi.first << " = ";
      EmitExpr(*fi.second, "");
      Nl();
    }
    if (s.ctor) {
      for (const auto& b : s.ctor->body) EmitStmt(*b);
    }
    indent = 0;
    out << "\treturn this\n}\n\n";

    for (const auto& m : s.methods) EmitFunc(*m, s.name);
  }

  void EmitParams(const std::vector<TsParam>& params) {
    for (size_t i = 0; i < params.size(); ++i) {
      if (i) out << ", ";
      std::string ty = MapTsType(params[i].type);
      if (ty.empty()) ty = "any";
      out << params[i].name << " " << ty;
    }
  }

  void EmitFunc(const Stmt& s, const std::string& recv) {
    out << "func ";
    if (!recv.empty()) out << "(this *" << recv << ") ";
    out << (s.name.empty() ? "_" : s.name) << "(";
    EmitParams(s.params);
    out << ")";
    std::string ret = MapTsType(s.type);
    if (!ret.empty()) out << " " << ret;
    out << " {\n";
    std::string saved_hint = ret_hint;
    int saved_indent = indent;
    ret_hint = ret;
    indent = 1;
    for (const auto& b : s.body) EmitStmt(*b);
    indent = saved_indent;
    ret_hint = saved_hint;
    out << "}\n\n";
  }

  void EmitStmt(const Stmt& s) {
    switch (s.kind) {
      case Stmt::Kind::Block: {
        Ind();
        out << "{\n";
        ++indent;
        for (const auto& b : s.body) EmitStmt(*b);
        --indent;
        Ind();
        out << "}\n";
        break;
      }
      case Stmt::Kind::Var: {
        Ind();
        if (s.expr) {
          out << s.name << " := ";
          EmitExpr(*s.expr, MapTsType(s.type));
        } else {
          std::string ty = MapTsType(s.type);
          if (ty.empty()) ty = "any";
          out << "var " << s.name << " " << ty;
        }
        Nl();
        break;
      }
      case Stmt::Kind::If: {
        Ind();
        out << "if ";
        EmitExpr(*s.expr, "");
        out << " {\n";
        ++indent;
        EmitNested(*s.then_s);
        --indent;
        if (s.else_s) {
          Ind();
          out << "} else {\n";
          ++indent;
          EmitNested(*s.else_s);
          --indent;
        }
        Ind();
        out << "}\n";
        break;
      }
      case Stmt::Kind::While: {
        Ind();
        out << "for ";
        EmitExpr(*s.expr, "");
        out << " {\n";
        ++indent;
        EmitNested(*s.then_s);
        --indent;
        Ind();
        out << "}\n";
        break;
      }
      case Stmt::Kind::For: {
        Ind();
        out << "for ";
        if (s.init) {
          if (s.init->kind == Stmt::Kind::Var && s.init->expr) {
            out << s.init->name << " := ";
            EmitExpr(*s.init->expr, MapTsType(s.init->type));
          } else if (s.init->kind == Stmt::Kind::Expr && s.init->expr) {
            EmitExpr(*s.init->expr, "");
          }
        }
        out << "; ";
        if (s.expr) EmitExpr(*s.expr, "");
        out << "; ";
        if (s.expr2) EmitExpr(*s.expr2, "");
        out << " {\n";
        ++indent;
        EmitNested(*s.then_s);
        --indent;
        Ind();
        out << "}\n";
        break;
      }
      case Stmt::Kind::ForOf: {
        Ind();
        out << "for _, " << s.name << " := range ";
        EmitExpr(*s.expr, "");
        out << " {\n";
        ++indent;
        EmitNested(*s.then_s);
        --indent;
        Ind();
        out << "}\n";
        break;
      }
      case Stmt::Kind::Return: {
        Ind();
        out << "return";
        if (s.expr) {
          out << " ";
          std::string hint = MapTsType(s.type);
          if (hint.empty()) hint = ret_hint;
          EmitExpr(*s.expr, hint);
        }
        Nl();
        break;
      }
      case Stmt::Kind::Break:
        Ind();
        out << "break\n";
        break;
      case Stmt::Kind::Continue:
        Ind();
        out << "continue\n";
        break;
      case Stmt::Kind::Expr:
        Ind();
        if (s.expr) EmitExpr(*s.expr, "");
        Nl();
        break;
      default:
        EmitDecl(s);
        break;
    }
  }

  void EmitNested(const Stmt& s) {
    if (s.kind == Stmt::Kind::Block) {
      for (const auto& b : s.body) EmitStmt(*b);
    } else {
      EmitStmt(s);
    }
  }

  void EmitExpr(const Expr& e, const std::string& type_hint) {
    switch (e.kind) {
      case Expr::Kind::Ident:
        out << e.text;
        break;
      case Expr::Kind::Number:
        out << e.text;
        break;
      case Expr::Kind::String:
        out << EscapeGoString(e.text);
        break;
      case Expr::Kind::Bool:
        out << e.text;
        break;
      case Expr::Kind::Null:
        out << "nil";
        break;
      case Expr::Kind::This:
        out << "this";
        break;
      case Expr::Kind::Member:
        EmitExpr(*e.left, "");
        out << "." << e.text;
        break;
      case Expr::Kind::Index:
        EmitExpr(*e.left, "");
        out << "[";
        EmitExpr(*e.right, "");
        out << "]";
        break;
      case Expr::Kind::Call:
        if (e.left && e.left->kind == Expr::Kind::Member && e.left->left &&
            e.left->left->kind == Expr::Kind::Ident && e.left->left->text == "console") {
          out << "fmt.Println(";
          for (size_t i = 0; i < e.args.size(); ++i) {
            if (i) out << ", ";
            EmitExpr(*e.args[i], "");
          }
          out << ")";
          break;
        }
        EmitExpr(*e.left, "");
        out << "(";
        for (size_t i = 0; i < e.args.size(); ++i) {
          if (i) out << ", ";
          EmitExpr(*e.args[i], "");
        }
        out << ")";
        break;
      case Expr::Kind::Binary:
        EmitExpr(*e.left, "");
        out << " " << e.op << " ";
        EmitExpr(*e.right, "");
        break;
      case Expr::Kind::Unary:
        out << e.op;
        EmitExpr(*e.left, "");
        break;
      case Expr::Kind::PrefixUpdate:
        out << e.op;
        EmitExpr(*e.left, "");
        break;
      case Expr::Kind::PostfixUpdate:
        EmitExpr(*e.left, "");
        out << e.op;
        break;
      case Expr::Kind::Assign:
        EmitExpr(*e.left, "");
        out << " " << e.op << " ";
        EmitExpr(*e.right, "");
        break;
      case Expr::Kind::New:
        if (classes.count(e.text)) {
          out << "New" << e.text << "(";
          for (size_t i = 0; i < e.args.size(); ++i) {
            if (i) out << ", ";
            EmitExpr(*e.args[i], "");
          }
          out << ")";
        } else {
          out << "new(" << e.text << ")";
        }
        break;
      case Expr::Kind::Array: {
        std::string inner = type_hint;
        if (inner.rfind("[]", 0) == 0) inner = inner.substr(2);
        if (inner.empty()) inner = "any";
        out << "[]" << inner << "{";
        for (size_t i = 0; i < e.args.size(); ++i) {
          if (i) out << ", ";
          EmitExpr(*e.args[i], inner);
        }
        out << "}";
        break;
      }
      case Expr::Kind::Object: {
        std::string ty = type_hint.empty() ? "struct{}" : type_hint;
        out << ty << "{";
        for (size_t i = 0; i < e.fields.size(); ++i) {
          if (i) out << ", ";
          out << e.fields[i].first << ": ";
          EmitExpr(*e.fields[i].second, "");
        }
        out << "}";
        break;
      }
      case Expr::Kind::Func: {
        out << "func(";
        for (size_t i = 0; i < e.func_params.size(); ++i) {
          if (i) out << ", ";
          std::string ty = MapTsType(e.func_params[i].second);
          if (ty.empty()) ty = "any";
          out << e.func_params[i].first << " " << ty;
        }
        out << ")";
        std::string ret = MapTsType(e.func_return);
        if (!ret.empty()) out << " " << ret;
        out << " {\n";
        std::string saved_hint = ret_hint;
        ret_hint = ret;
        ++indent;
        for (const auto& b : e.func_body) EmitStmt(*b);
        --indent;
        ret_hint = saved_hint;
        Ind();
        out << "}";
        break;
      }
    }
  }
};

// ---- Web IDL -> Go++ ------------------------------------------------------

std::string MapIdlType(const TypeSpec& t) {
  if (t.kind == TypeKind::kSequence) {
    std::string inner = t.element_type ? MapIdlType(*t.element_type) : "any";
    return "[]" + inner;
  }
  if (t.kind == TypeKind::kPromise) {
    return t.element_type ? MapIdlType(*t.element_type) : "any";
  }
  if (t.kind == TypeKind::kUnion) return "any";
  switch (t.kind) {
    case TypeKind::kVoid:
      return "";
    case TypeKind::kBoolean:
      return "bool";
    case TypeKind::kByte:
      return "int8";
    case TypeKind::kOctet:
      return "uint8";
    case TypeKind::kShort:
      return "int16";
    case TypeKind::kUnsignedShort:
      return "uint16";
    case TypeKind::kLong:
      return "int32";
    case TypeKind::kUnsignedLong:
      return "uint32";
    case TypeKind::kLongLong:
      return "int64";
    case TypeKind::kUnsignedLongLong:
      return "uint64";
    case TypeKind::kFloat:
      return "float32";
    case TypeKind::kDouble:
      return "float64";
    case TypeKind::kDOMString:
    case TypeKind::kUSVString:
    case TypeKind::kEnumRef:
      return "string";
    case TypeKind::kAny:
      return "any";
    case TypeKind::kInterfaceRef:
      return "*" + t.ref_name;
    case TypeKind::kDictionaryRef:
      return t.ref_name;
    case TypeKind::kCallbackRef:
      return t.ref_name;
    default:
      return t.ref_name.empty() ? "any" : t.ref_name;
  }
}

void EmitIdlParams(std::ostringstream& out, const std::vector<bruja::Param>& params) {
  for (size_t i = 0; i < params.size(); ++i) {
    if (i) out << ", ";
    std::string ty = MapIdlType(params[i].type);
    if (params[i].variadic) ty = "[]" + (ty.empty() ? std::string("any") : ty);
    if (ty.empty()) ty = "any";
    out << params[i].name << " " << ty;
  }
}

}  // namespace

std::string GenerateGoxx(const std::string& ts_source,
                         const std::string& source_filename) {
  std::vector<TsToken> toks = TokenizeTs(ts_source);
  Program p = TsParser(std::move(toks)).Parse();
  Emitter em;
  em.EmitProgram(p, source_filename);
  return em.out.str();
}

std::string GenerateGoxxFromModule(const Module& module,
                                   const std::string& source_filename,
                                   const std::string& package_name) {
  std::ostringstream out;
  out << "// Generated by brujac (--backend=goxx) from " << source_filename
      << ". DO NOT EDIT.\n";
  out << "// .goxx marks converted Go++; handwritten Go++ stays .go.\n";
  std::string pkg = package_name.empty() ? BaseName(source_filename) : package_name;
  if (pkg.empty()) pkg = "main";
  out << "package " << pkg << "\n\n";

  for (const EnumDecl& e : module.enums) {
    out << "type " << e.name << " string\n\n";
    if (!e.values.empty()) {
      out << "const (\n";
      for (const std::string& v : e.values) {
        std::string ident = e.name + "_" + v;
        for (char& c : ident)
          if (!std::isalnum(static_cast<unsigned char>(c))) c = '_';
        out << "\t" << ident << " " << e.name << " = \"" << v << "\"\n";
      }
      out << ")\n\n";
    }
  }

  for (const Dictionary& d : module.dictionaries) {
    out << "type " << d.name << " struct {\n";
    if (!d.base_name.empty()) out << "\t" << d.base_name << "\n";
    for (const DictField& f : d.fields) {
      std::string ty = MapIdlType(f.type);
      if (ty.empty()) ty = "any";
      out << "\t" << PascalCase(f.name) << " " << ty << "\n";
    }
    out << "}\n\n";
  }

  for (const CallbackDecl& c : module.callbacks) {
    out << "type " << c.name << " func(";
    EmitIdlParams(out, c.params);
    out << ")";
    std::string ret = MapIdlType(c.return_type);
    if (!ret.empty()) out << " " << ret;
    out << "\n\n";
  }

  for (const Interface& iface : module.interfaces) {
    out << "type " << iface.name << " interface {\n";
    if (!iface.base_name.empty()) out << "\t" << iface.base_name << "\n";
    for (const Const& c : iface.consts) {
      (void)c;
    }
    for (const Attribute& a : iface.attributes) {
      std::string ty = MapIdlType(a.type);
      if (ty.empty()) ty = "any";
      out << "\t" << PascalCase(a.name) << "() " << ty << "\n";
      if (!a.readonly) {
        out << "\tSet" << PascalCase(a.name) << "(value " << ty << ")\n";
      }
    }
    for (const Method& m : iface.methods) {
      out << "\t" << PascalCase(m.name) << "(";
      EmitIdlParams(out, m.params);
      out << ")";
      std::string ret = MapIdlType(m.return_type);
      if (!ret.empty()) out << " " << ret;
      out << "\n";
    }
    out << "}\n\n";
  }
  return out.str();
}

}  // namespace bruja
