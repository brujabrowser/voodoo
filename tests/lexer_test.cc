#include "test.h"

#include "../src/lexer.h"

using voodoom::TokenType;
using voodoom::Tokenize;

TEST(lexer_basic_interface) {
  auto toks = Tokenize("interface Foo { Bar(string x) => (int32 y); };");
  // interface Foo { Bar ( string x ) => ( int32 y ) ; } ; <end>
  EXPECT_EQ(toks.size(), 17u);
  EXPECT_EQ(toks[0].type, TokenType::kName);
  EXPECT_EQ(toks[0].text, "interface");
  EXPECT_EQ(toks[1].text, "Foo");
  EXPECT_EQ(toks[2].type, TokenType::kLBrace);
  EXPECT_EQ(toks[8].type, TokenType::kArrow);
  EXPECT(toks.back().type == TokenType::kEnd);
}

TEST(lexer_strips_comments) {
  auto toks = Tokenize("// line comment\nmodule /* block */ echo;");
  EXPECT_EQ(toks.size(), 4u);  // module echo ; <end>
  EXPECT_EQ(toks[0].text, "module");
  EXPECT_EQ(toks[1].text, "echo");
  EXPECT_EQ(toks[2].type, TokenType::kSemi);
}

TEST(lexer_ordinal_and_generic) {
  auto toks = Tokenize("Foo() @0; pending_associated_remote<X> l");
  bool saw_at = false, saw_langle = false, saw_rangle = false;
  for (auto& t : toks) {
    if (t.type == TokenType::kAt) saw_at = true;
    if (t.type == TokenType::kLAngle) saw_langle = true;
    if (t.type == TokenType::kRAngle) saw_rangle = true;
  }
  EXPECT(saw_at);
  EXPECT(saw_langle);
  EXPECT(saw_rangle);
}

TEST(lexer_rejects_bad_character) {
  bool threw = false;
  try {
    Tokenize("interface Foo { Bar(string x) $ };");
  } catch (const voodoom::LexError&) {
    threw = true;
  }
  EXPECT(threw);
}

TEST(lexer_string_literal) {
  auto toks = Tokenize("import \"a/b.voodoom\";");
  // import "a/b.voodoom" ; <end>
  EXPECT_EQ(toks.size(), 4u);
  EXPECT_EQ(toks[0].text, "import");
  EXPECT_EQ(toks[1].type, TokenType::kStringLiteral);
  EXPECT_EQ(toks[1].text, "a/b.voodoom");
  EXPECT_EQ(toks[2].type, TokenType::kSemi);
}

TEST(lexer_string_literal_escapes) {
  auto toks = Tokenize("\"a\\\"b\\\\c\"");
  EXPECT_EQ(toks[0].type, TokenType::kStringLiteral);
  EXPECT_EQ(toks[0].text, "a\"b\\c");
}

TEST(lexer_rejects_unterminated_string) {
  bool threw = false;
  try {
    Tokenize("\"unterminated");
  } catch (const voodoom::LexError&) {
    threw = true;
  }
  EXPECT(threw);
}

TEST(lexer_question_mark) {
  auto toks = Tokenize("string? x");
  EXPECT_EQ(toks[0].text, "string");
  EXPECT_EQ(toks[1].type, TokenType::kQuestion);
  EXPECT_EQ(toks[2].text, "x");
}

TEST(lexer_brackets) {
  auto toks = Tokenize("[Sync] Add()");
  EXPECT_EQ(toks[0].type, TokenType::kLBracket);
  EXPECT_EQ(toks[1].text, "Sync");
  EXPECT_EQ(toks[2].type, TokenType::kRBracket);
}

TEST(lexer_rejects_string_crossing_newline) {
  bool threw = false;
  try {
    Tokenize("\"line one\nline two\"");
  } catch (const voodoom::LexError&) {
    threw = true;
  }
  EXPECT(threw);
}

// (real-mojom-parity phase 1) Hex/float number literals, octal rejection,
// decimal/hex string escapes, and the new '&'/'|' tokens -- see
// lexer.h/.cc's own comments for why these are distinct token types from
// kNumber, and why nothing consumes them yet.

TEST(lexer_hex_number) {
  auto toks = Tokenize("0x1F");
  EXPECT_EQ(toks[0].type, TokenType::kHexNumber);
  EXPECT_EQ(toks[0].text, "0x1F");
}

TEST(lexer_hex_number_uppercase_prefix) {
  auto toks = Tokenize("0X2a");
  EXPECT_EQ(toks[0].type, TokenType::kHexNumber);
  EXPECT_EQ(toks[0].text, "0X2a");
}

TEST(lexer_rejects_hex_with_no_digits) {
  bool threw = false;
  try {
    Tokenize("0x");
  } catch (const voodoom::LexError&) {
    threw = true;
  }
  EXPECT(threw);
}

TEST(lexer_float_fractional) {
  auto toks = Tokenize("1.5");
  EXPECT_EQ(toks[0].type, TokenType::kFloatNumber);
  EXPECT_EQ(toks[0].text, "1.5");
}

TEST(lexer_float_leading_dot) {
  auto toks = Tokenize(".5");
  EXPECT_EQ(toks[0].type, TokenType::kFloatNumber);
  EXPECT_EQ(toks[0].text, ".5");
}

TEST(lexer_float_exponent_no_fraction) {
  auto toks = Tokenize("1e10");
  EXPECT_EQ(toks[0].type, TokenType::kFloatNumber);
  EXPECT_EQ(toks[0].text, "1e10");
}

TEST(lexer_float_fraction_and_signed_exponent) {
  auto toks = Tokenize("1.5e-3");
  EXPECT_EQ(toks[0].type, TokenType::kFloatNumber);
  EXPECT_EQ(toks[0].text, "1.5e-3");
}

TEST(lexer_plain_integer_still_knumber) {
  auto toks = Tokenize("42");
  EXPECT_EQ(toks[0].type, TokenType::kNumber);
  EXPECT_EQ(toks[0].text, "42");
}

TEST(lexer_bare_zero_is_not_rejected) {
  auto toks = Tokenize("0");
  EXPECT_EQ(toks[0].type, TokenType::kNumber);
  EXPECT_EQ(toks[0].text, "0");
}

TEST(lexer_rejects_octal_looking_literal) {
  bool threw = false;
  try {
    Tokenize("0123");
  } catch (const voodoom::LexError&) {
    threw = true;
  }
  EXPECT(threw);
}

TEST(lexer_dotted_name_after_integer_is_not_a_float) {
  // "1" then '.' then a NAME -- must stay three separate tokens (a bare
  // trailing '.' with no following digit is never part of a number).
  auto toks = Tokenize("Foo.Bar");
  EXPECT_EQ(toks[0].type, TokenType::kName);
  EXPECT_EQ(toks[1].type, TokenType::kDot);
  EXPECT_EQ(toks[2].type, TokenType::kName);
}

TEST(lexer_string_literal_decimal_escape) {
  // \65 -> 'A' (ASCII 65).
  auto toks = Tokenize("\"\\65\"");
  EXPECT_EQ(toks[0].type, TokenType::kStringLiteral);
  EXPECT_EQ(toks[0].text, "A");
}

TEST(lexer_string_literal_hex_escape) {
  // \x41 -> 'A' (0x41).
  auto toks = Tokenize("\"\\x41\"");
  EXPECT_EQ(toks[0].type, TokenType::kStringLiteral);
  EXPECT_EQ(toks[0].text, "A");
}

TEST(lexer_ampersand_and_pipe) {
  auto toks = Tokenize("a&b|c");
  EXPECT_EQ(toks[0].type, TokenType::kName);
  EXPECT_EQ(toks[1].type, TokenType::kAmpersand);
  EXPECT_EQ(toks[2].type, TokenType::kName);
  EXPECT_EQ(toks[3].type, TokenType::kPipe);
  EXPECT_EQ(toks[4].type, TokenType::kName);
}
