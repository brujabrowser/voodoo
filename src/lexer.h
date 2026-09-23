// Hand-rolled tokenizer for .voodoom, covering the subset of Chromium's
// mojom lexer (mojo/public/tools/mojom/mojom/parse/lexer.py) this compiler
// understands: identifiers/keywords, '@'-ordinals, double-quoted string
// literals (for `import "path";` and string field defaults), a trailing
// '?' for nullable types, '.'-separated segments for a dotted
// `module a.b.c;` name, a '[' ']'-bracketed attribute list (only `[Sync]`
// on a method_decl is actually recognized -- see parser.h), and the
// punctuation a module/interface/method/struct/union/enum/const/import
// declaration needs. Keywords ('struct', 'union', 'map', 'import', 'true',
// 'false', 'Sync', ...) aren't special-cased here -- they tokenize as
// plain kName, and the parser is what recognizes them by spelling.
//
// (real-mojom-parity phase 1) kHexNumber/kFloatNumber and kAmpersand/kPipe
// exist so a real .mojom file's hex/float literals and pipe/amp-delimited
// attribute values lex correctly and distinctly from a plain decimal
// kNumber. As of phase 2, parser.cc consumes all four: kHexNumber/
// kFloatNumber in ParseSignedNumber/ParseFloatValue (int and float/double
// literal positions), kAmpersand/kPipe in the attribute-value-list
// production (see "(v14+)" attribute parsing below). A .voodoom/.mojom
// file that uses one where the grammar position doesn't accept it still
// gets today's ordinary "expected number, got ..." parse error, not a
// silent misparse -- kept a distinct token type from kNumber rather than
// folding hex into it, since std::stoll(digits) (base 10) on a
// "0x1F"-shaped string would otherwise silently compute the wrong value
// instead of failing loudly.
#ifndef WVC_SRC_LEXER_H_
#define WVC_SRC_LEXER_H_

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace voodoom {

enum class TokenType {
  kName,          // identifier or keyword; Parser checks the spelling
  kNumber,        // decimal digits (ordinal value, or a decimal int literal)
  kHexNumber,     // 0[xX][0-9a-fA-F]+, text includes the "0x"/"0X" prefix
  kFloatNumber,   // a float/double literal (fractional and/or exponent form)
  kStringLiteral, // "..."; text holds the unescaped contents (an import path)
  kArrow,   // =>
  kLParen,
  kRParen,
  kLBrace,
  kRBrace,
  kLAngle,
  kRAngle,
  kComma,
  kSemi,
  kAt,
  kEquals,
  kMinus,
  kQuestion,  // trailing '?' marking a nullable type
  kDot,       // '.' separating segments of a dotted `module a.b.c;` name
  kLBracket,  // '[' opening an attribute list, e.g. `[Sync]`
  kRBracket,
  kAmpersand, // '&' -- ampersand-delimited attribute value list
  kPipe,      // '|' -- pipe-delimited attribute value list
  kEnd,
};

struct Token {
  TokenType type;
  std::string text;  // spelling for kName/kNumber/kStringLiteral; else empty
  int line;
};

class LexError : public std::runtime_error {
 public:
  LexError(const std::string& msg, int line)
      : std::runtime_error(msg + " (line " + std::to_string(line) + ")"),
        line(line) {}
  int line;
};

// Tokenizes the whole input up front (files are small; no need to stream).
// Strips // line comments and /* block */ comments, same as mojom's lexer.
// A leading-zero multi-digit decimal run (e.g. "0123") is a LexError
// ("octal values are not allowed"), matching mojom's own explicit
// rejection -- not silently reinterpreted as decimal or octal.
std::vector<Token> Tokenize(const std::string& source);

}  // namespace voodoom

#endif  // WVC_SRC_LEXER_H_
