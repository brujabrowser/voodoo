// brujac: `.bruja` -> C++ pure-virtual interface + quickjs binding header,
// the Blink-bindings-generator equivalent of WASMVoodooCompile's voodoomc
// (which targets Mojo IPC stubs instead of in-process JS bindings). Usage:
//
//   brujac input.bruja -o output.h [--namespace=ns] [--guard=GUARD_H_]
//            [--backend=quickjs|v8|goxx]
//   brujac input.ts -o output.goxx
//
// --namespace defaults to "bruja_generated".
// --guard defaults to an all-caps mangling of the output path.
// --backend defaults to "quickjs" (cpp_generator.h, the original backend,
// full Web IDL feature coverage). "v8" (cpp_generator_v8.h) targets the
// WASMv8bindings V8 embedder-API facade instead -- see that file's header
// comment for exactly which subset of the grammar it covers today.
// "goxx" emits Go++ (wasigoc) source; TypeScript input always takes this
// path, and the `.goxx` extension marks converted-from-TS (handwritten
// Go++ stays `.go`).
#include "cpp_generator.h"
#include "cpp_generator_v8.h"
#include "frontends.h"
#include "goxx_generator.h"
#include "lexer.h"
#include "parser.h"
#include "resolver.h"

#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::string ReadFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in.is_open()) {
    throw std::runtime_error("cannot open '" + path + "' for reading");
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

void WriteFile(const std::string& path, const std::string& content) {
  std::ofstream out(path, std::ios::binary);
  if (!out.is_open()) {
    throw std::runtime_error("cannot open '" + path + "' for writing");
  }
  out << content;
}

std::string DefaultGuardFrom(const std::string& output_path) {
  std::string base = output_path;
  size_t slash = base.find_last_of("/\\");
  if (slash != std::string::npos) base = base.substr(slash + 1);
  std::string guard;
  guard.reserve(base.size() + 1);
  for (char c : base) {
    if (std::isalnum(static_cast<unsigned char>(c))) {
      guard.push_back(static_cast<char>(std::toupper(c)));
    } else {
      guard.push_back('_');
    }
  }
  guard += "_";
  return guard;
}

void PrintUsage(const char* argv0) {
  std::fprintf(stderr,
               "usage: %s <input.bruja|.messages.in|.mm|.ts> -o <output.h|.goxx> "
               "[--namespace=NAME] [--guard=NAME] [--backend=quickjs|v8|goxx]\n",
               argv0);
}

bool EndsWithPath(const std::string& s, const std::string& suffix) {
  return s.size() >= suffix.size() &&
         s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool IsTypeScriptPath(const std::string& path) {
  return EndsWithPath(path, ".ts") || EndsWithPath(path, ".tsx");
}

std::string PackageFromNamespace(const std::string& ns) {
  if (ns.empty()) return "";
  std::string pkg;
  for (char c : ns) {
    if (std::isalnum(static_cast<unsigned char>(c)))
      pkg.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    else if (!pkg.empty() && pkg.back() != '_')
      pkg.push_back('_');
  }
  return pkg;
}

}  // namespace

int main(int argc, char** argv) {
  std::string input_path;
  std::string output_path;
  std::string ns_override;
  std::string guard_override;
  std::string backend = "quickjs";
  bool backend_set = false;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "-o" && i + 1 < argc) {
      output_path = argv[++i];
    } else if (arg.rfind("--namespace=", 0) == 0) {
      ns_override = arg.substr(12);
    } else if (arg.rfind("--guard=", 0) == 0) {
      guard_override = arg.substr(8);
    } else if (arg.rfind("--backend=", 0) == 0) {
      backend = arg.substr(10);
      backend_set = true;
    } else if (arg == "-h" || arg == "--help") {
      PrintUsage(argv[0]);
      return 0;
    } else if (!arg.empty() && arg[0] == '-') {
      std::fprintf(stderr, "unknown flag '%s'\n", arg.c_str());
      PrintUsage(argv[0]);
      return 2;
    } else if (input_path.empty()) {
      input_path = arg;
    } else {
      std::fprintf(stderr, "unexpected extra argument '%s'\n", arg.c_str());
      PrintUsage(argv[0]);
      return 2;
    }
  }

  if (input_path.empty() || output_path.empty()) {
    PrintUsage(argv[0]);
    return 2;
  }
  if (!backend_set) {
    if (IsTypeScriptPath(input_path) || EndsWithPath(output_path, ".goxx"))
      backend = "goxx";
  }
  if (IsTypeScriptPath(input_path) && backend != "goxx") {
    std::fprintf(stderr,
                 "TypeScript input requires --backend=goxx (or omit --backend)\n");
    return 2;
  }
  if (backend != "quickjs" && backend != "v8" && backend != "goxx") {
    std::fprintf(stderr,
                 "unknown --backend '%s' (want 'quickjs', 'v8', or 'goxx')\n",
                 backend.c_str());
    return 2;
  }

  try {
    std::string source = ReadFile(input_path);
    if (IsTypeScriptPath(input_path)) {
      std::string goxx = bruja::GenerateGoxx(source, input_path);
      WriteFile(output_path, goxx);
      return 0;
    }

    bruja::InputKind kind = bruja::InferInputKind(input_path, source);
    std::string bruja_source = bruja::ToBrujaSource(kind, source);
    std::vector<bruja::Token> tokens = bruja::Tokenize(bruja_source);
    bruja::Module module = bruja::Parse(tokens);
    bruja::Resolve(module);

    if (backend == "goxx") {
      std::string pkg = PackageFromNamespace(ns_override);
      std::string goxx = bruja::GenerateGoxxFromModule(module, input_path, pkg);
      WriteFile(output_path, goxx);
      return 0;
    }

    std::string ns = !ns_override.empty() ? ns_override : "bruja_generated";
    std::string guard =
        !guard_override.empty() ? guard_override : DefaultGuardFrom(output_path);

    std::string header = (backend == "v8")
                             ? bruja::GenerateV8CppHeader(module, guard, ns, input_path)
                             : bruja::GenerateCppHeader(module, guard, ns, input_path);
    WriteFile(output_path, header);
  } catch (const bruja::LexError& e) {
    std::fprintf(stderr, "%s: lex error: %s\n", input_path.c_str(), e.what());
    return 1;
  } catch (const bruja::ParseError& e) {
    std::fprintf(stderr, "%s: parse error: %s\n", input_path.c_str(),
                 e.what());
    return 1;
  } catch (const bruja::ResolveError& e) {
    std::fprintf(stderr, "%s: resolve error: %s\n", input_path.c_str(),
                 e.what());
    return 1;
  } catch (const std::exception& e) {
    std::fprintf(stderr, "%s: %s\n", input_path.c_str(), e.what());
    return 1;
  }

  return 0;
}
