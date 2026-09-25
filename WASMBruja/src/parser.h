#ifndef BRUJA_PARSER_H_
#define BRUJA_PARSER_H_

#include <stdexcept>
#include <string>
#include <vector>

#include "ast.h"
#include "lexer.h"

namespace bruja {

class ParseError : public std::runtime_error {
 public:
  ParseError(const std::string& msg, int line)
      : std::runtime_error(msg + " (line " + std::to_string(line) + ")"),
        line(line) {}
  int line;
};

// Parses a full .bruja source (already tokenized) into a Module. v1 has no
// multi-file import graph, so unlike voodoomc's Parser class this is a
// single free function with no header/body split.
Module Parse(const std::vector<Token>& tokens);

}  // namespace bruja

#endif  // BRUJA_PARSER_H_
