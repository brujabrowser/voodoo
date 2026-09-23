#include "lexer.h"

#include <cctype>

namespace voodoom {

namespace {

bool IsNameStart(char c) {
  return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
}

bool IsNameCont(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

}  // namespace

std::vector<Token> Tokenize(const std::string& src) {
  std::vector<Token> out;
  size_t i = 0;
  int line = 1;
  const size_t n = src.size();

  auto push = [&](TokenType type, std::string text = "") {
    out.push_back(Token{type, std::move(text), line});
  };

  while (i < n) {
    char c = src[i];

    if (c == '\n') {
      ++line;
      ++i;
      continue;
    }
    if (std::isspace(static_cast<unsigned char>(c))) {
      ++i;
      continue;
    }
    if (c == '/' && i + 1 < n && src[i + 1] == '/') {
      while (i < n && src[i] != '\n') ++i;
      continue;
    }
    if (c == '/' && i + 1 < n && src[i + 1] == '*') {
      i += 2;
      while (i + 1 < n && !(src[i] == '*' && src[i + 1] == '/')) {
        if (src[i] == '\n') ++line;
        ++i;
      }
      if (i + 1 >= n) throw LexError("unterminated block comment", line);
      i += 2;
      continue;
    }
    if (IsNameStart(c)) {
      size_t start = i;
      while (i < n && IsNameCont(src[i])) ++i;
      push(TokenType::kName, src.substr(start, i - start));
      continue;
    }
    if (std::isdigit(static_cast<unsigned char>(c))) {
      size_t start = i;
      // Hex: 0[xX][0-9a-fA-F]+ -- checked first, since it also starts with
      // a digit and would otherwise be swallowed by the plain-decimal scan
      // below.
      if (c == '0' && i + 1 < n && (src[i + 1] == 'x' || src[i + 1] == 'X')) {
        i += 2;
        size_t hex_start = i;
        while (i < n && std::isxdigit(static_cast<unsigned char>(src[i]))) {
          ++i;
        }
        if (i == hex_start) {
          throw LexError("invalid hex literal (no digits after '0x')", line);
        }
        push(TokenType::kHexNumber, src.substr(start, i - start));
        continue;
      }
      while (i < n && std::isdigit(static_cast<unsigned char>(src[i]))) ++i;
      bool is_float = false;
      // Fractional part: DIGITS '.' DIGITS -- only consumed if a digit
      // actually follows the '.' (a bare trailing '.', e.g. after "1", is
      // module-dotted-name punctuation instead, unrelated to this number).
      if (i < n && src[i] == '.' && i + 1 < n &&
          std::isdigit(static_cast<unsigned char>(src[i + 1]))) {
        is_float = true;
        ++i;
        while (i < n && std::isdigit(static_cast<unsigned char>(src[i]))) {
          ++i;
        }
      }
      // Exponent part: [eE] [+-]? DIGITS.
      if (i < n && (src[i] == 'e' || src[i] == 'E')) {
        size_t j = i + 1;
        if (j < n && (src[j] == '+' || src[j] == '-')) ++j;
        if (j < n && std::isdigit(static_cast<unsigned char>(src[j]))) {
          is_float = true;
          ++j;
          while (j < n && std::isdigit(static_cast<unsigned char>(src[j]))) {
            ++j;
          }
          i = j;
        }
      }
      if (is_float) {
        push(TokenType::kFloatNumber, src.substr(start, i - start));
        continue;
      }
      // Plain decimal: a leading '0' followed by more digits is explicitly
      // rejected (matching mojom's own lexer), not silently reinterpreted.
      std::string digits = src.substr(start, i - start);
      if (digits.size() > 1 && digits[0] == '0') {
        throw LexError("octal values are not allowed", line);
      }
      push(TokenType::kNumber, digits);
      continue;
    }
    // A leading-dot float, e.g. ".5" -- '.' alone (module-dotted-name
    // punctuation) is handled below in the switch; this only fires when a
    // digit actually follows.
    if (c == '.' && i + 1 < n &&
        std::isdigit(static_cast<unsigned char>(src[i + 1]))) {
      size_t start = i;
      ++i;
      while (i < n && std::isdigit(static_cast<unsigned char>(src[i]))) ++i;
      if (i < n && (src[i] == 'e' || src[i] == 'E')) {
        size_t j = i + 1;
        if (j < n && (src[j] == '+' || src[j] == '-')) ++j;
        if (j < n && std::isdigit(static_cast<unsigned char>(src[j]))) {
          ++j;
          while (j < n && std::isdigit(static_cast<unsigned char>(src[j]))) {
            ++j;
          }
          i = j;
        }
      }
      push(TokenType::kFloatNumber, src.substr(start, i - start));
      continue;
    }
    if (c == '"') {
      int start_line = line;
      ++i;
      std::string value;
      while (true) {
        if (i >= n) throw LexError("unterminated string literal", start_line);
        char sc = src[i];
        if (sc == '"') {
          ++i;
          break;
        }
        if (sc == '\n') throw LexError("unterminated string literal", start_line);
        if (sc == '\\' && i + 1 < n && (src[i + 1] == '"' || src[i + 1] == '\\')) {
          value.push_back(src[i + 1]);
          i += 2;
          continue;
        }
        // (real-mojom-parity phase 1) \NNN (decimal char code) and \xHH
        // (hex char code) escapes, matching mojom's string-literal regex.
        // Other "simple character" escapes mojom's lexer also allows
        // aren't implemented here yet -- not confidently verified against
        // the real lexer.py source, so left as a documented gap rather
        // than guessed at.
        if (sc == '\\' && i + 1 < n &&
            std::isdigit(static_cast<unsigned char>(src[i + 1]))) {
          size_t num_start = i + 1;
          size_t j = num_start;
          while (j < n && std::isdigit(static_cast<unsigned char>(src[j]))) {
            ++j;
          }
          long code = std::stol(src.substr(num_start, j - num_start));
          value.push_back(static_cast<char>(code));
          i = j;
          continue;
        }
        if (sc == '\\' && i + 1 < n && src[i + 1] == 'x' && i + 2 < n &&
            std::isxdigit(static_cast<unsigned char>(src[i + 2]))) {
          size_t hex_start = i + 2;
          size_t j = hex_start;
          while (j < n && std::isxdigit(static_cast<unsigned char>(src[j]))) {
            ++j;
          }
          long code = std::stol(src.substr(hex_start, j - hex_start), nullptr, 16);
          value.push_back(static_cast<char>(code));
          i = j;
          continue;
        }
        value.push_back(sc);
        ++i;
      }
      push(TokenType::kStringLiteral, std::move(value));
      continue;
    }
    if (c == '=' && i + 1 < n && src[i + 1] == '>') {
      push(TokenType::kArrow);
      i += 2;
      continue;
    }
    switch (c) {
      case '(': push(TokenType::kLParen); ++i; continue;
      case ')': push(TokenType::kRParen); ++i; continue;
      case '{': push(TokenType::kLBrace); ++i; continue;
      case '}': push(TokenType::kRBrace); ++i; continue;
      case '<': push(TokenType::kLAngle); ++i; continue;
      case '>': push(TokenType::kRAngle); ++i; continue;
      case ',': push(TokenType::kComma); ++i; continue;
      case ';': push(TokenType::kSemi); ++i; continue;
      case '@': push(TokenType::kAt); ++i; continue;
      case '=': push(TokenType::kEquals); ++i; continue;
      case '-': push(TokenType::kMinus); ++i; continue;
      case '?': push(TokenType::kQuestion); ++i; continue;
      case '.': push(TokenType::kDot); ++i; continue;
      case '[': push(TokenType::kLBracket); ++i; continue;
      case ']': push(TokenType::kRBracket); ++i; continue;
      case '&': push(TokenType::kAmpersand); ++i; continue;
      case '|': push(TokenType::kPipe); ++i; continue;
      default:
        throw LexError(std::string("unexpected character '") + c + "'", line);
    }
  }
  push(TokenType::kEnd);
  return out;
}

}  // namespace voodoom
