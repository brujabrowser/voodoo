#ifndef BRUJA_LEXER_H_
#define BRUJA_LEXER_H_

#include <stdexcept>
#include <string>
#include <vector>

namespace bruja {

enum class TokenType {
  kName,
  kLParen,
  kRParen,
  kLBrace,
  kRBrace,
  kComma,
  kSemi,
  kColon,
  kQuestion,
  kLt,
  kGt,
  kEquals,
  kEllipsis,
  kString,  // double-quoted literal; text holds the unescaped contents
  kNumber,  // numeric literal; text holds the raw spelling (e.g. "-1", "1.5")
  kEnd,
};

struct Token {
  TokenType type;
  std::string text;  // spelling for kName/kString/kNumber; empty otherwise
  int line;
};

class LexError : public std::runtime_error {
 public:
  LexError(const std::string& msg, int line)
      : std::runtime_error(msg + " (line " + std::to_string(line) + ")"),
        line(line) {}
  int line;
};

std::vector<Token> Tokenize(const std::string& source);

}  // namespace bruja

#endif  // BRUJA_LEXER_H_
