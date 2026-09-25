#ifndef BRUJA_GOXX_GENERATOR_H_
#define BRUJA_GOXX_GENERATOR_H_

#include <string>

#include "ast.h"

namespace bruja {

// TypeScript -> Go++. The `.goxx` extension is the converted-from-TS
// marker: handwritten Go++ stays `.go`. wasigoc accepts both.
//
// v1 covers a practical subset: functions, classes, interfaces (fields
// become structs; methods become interfaces), type aliases, let/const/var,
// if/else, for/for-of, while, return, console.log -> fmt.Println, new C()
// -> NewC(), and the usual expression operators. See examples/goxx/hello.ts.
std::string GenerateGoxx(const std::string& ts_source,
                         const std::string& source_filename);

// Web IDL Module -> Go++ interfaces/structs, same `.goxx` marker. Used
// when `--backend=goxx` is pointed at a `.bruja` (or cocoa/messages.in)
// input rather than TypeScript.
std::string GenerateGoxxFromModule(const Module& module,
                                   const std::string& source_filename,
                                   const std::string& package_name);

}  // namespace bruja

#endif  // BRUJA_GOXX_GENERATOR_H_
