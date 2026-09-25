#include "lexer.h"

namespace bruja {
namespace {

bool IsNameStart(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

bool IsNameCont(char c) {
  return IsNameStart(c) || (c >= '0' && c <= '9');
}

bool IsDigit(char c) { return c >= '0' && c <= '9'; }

}  // namespace

std::vector<Token> Tokenize(const std::string& source) {
  std::vector<Token> tokens;
  int line = 1;
  size_t i = 0;
  const size_t n = source.size();

  auto push = [&](TokenType type, std::string text = "") {
    tokens.push_back(Token{type, std::move(text), line});
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
      int start_line = line;
      i += 2;
      while (i + 1 < n && !(source[i] == '*' && source[i + 1] == '/')) {
        if (source[i] == '\n') ++line;
        ++i;
      }
      if (i + 1 >= n) {
        throw LexError("unterminated block comment", start_line);
      }
      i += 2;
      continue;
    }
    if (IsNameStart(c)) {
      size_t start = i;
      while (i < n && IsNameCont(source[i])) ++i;
      push(TokenType::kName, source.substr(start, i - start));
      continue;
    }
    if (c == '"') {
      int start_line = line;
      ++i;
      std::string value;
      while (i < n && source[i] != '"') {
        if (source[i] == '\n') {
          throw LexError("unterminated string literal", start_line);
        }
        if (source[i] == '\\' && i + 1 < n &&
            (source[i + 1] == '"' || source[i + 1] == '\\')) {
          value.push_back(source[i + 1]);
          i += 2;
          continue;
        }
        value.push_back(source[i]);
        ++i;
      }
      if (i >= n) {
        throw LexError("unterminated string literal", start_line);
      }
      ++i;  // closing '"'
      push(TokenType::kString, value);
      continue;
    }
    if (IsDigit(c) || (c == '-' && i + 1 < n && IsDigit(source[i + 1]))) {
      size_t start = i;
      if (c == '-') ++i;
      while (i < n && IsDigit(source[i])) ++i;
      if (i < n && source[i] == '.' && i + 1 < n && IsDigit(source[i + 1])) {
        ++i;
        while (i < n && IsDigit(source[i])) ++i;
      }
      push(TokenType::kNumber, source.substr(start, i - start));
      continue;
    }
    if (c == '.' && i + 2 < n && source[i + 1] == '.' && source[i + 2] == '.') {
      push(TokenType::kEllipsis);
      i += 3;
      continue;
    }
    switch (c) {
      case '(':
        push(TokenType::kLParen);
        ++i;
        continue;
      case ')':
        push(TokenType::kRParen);
        ++i;
        continue;
      case '{':
        push(TokenType::kLBrace);
        ++i;
        continue;
      case '}':
        push(TokenType::kRBrace);
        ++i;
        continue;
      case ',':
        push(TokenType::kComma);
        ++i;
        continue;
      case ';':
        push(TokenType::kSemi);
        ++i;
        continue;
      case ':':
        push(TokenType::kColon);
        ++i;
        continue;
      case '?':
        push(TokenType::kQuestion);
        ++i;
        continue;
      case '<':
        push(TokenType::kLt);
        ++i;
        continue;
      case '>':
        push(TokenType::kGt);
        ++i;
        continue;
      case '=':
        push(TokenType::kEquals);
        ++i;
        continue;
      default:
        throw LexError(std::string("unexpected character '") + c + "'", line);
    }
  }

  push(TokenType::kEnd);
  return tokens;
}

}  // namespace bruja
