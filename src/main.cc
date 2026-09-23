// voodoomc: `.voodoom` -> C++ Proxy_/Stub_ header, the way mojom_bindgen
// targets real Mojo's bindings (see WASMCadidumBindings/README.md's
// "Generated-code contract"). Usage:
//
//   voodoomc input.voodoom -o output.h [--namespace=ns] [--guard=GUARD_H_]
//            [--import-dir=DIR ...] [--out-dir=DIR] [--import-out=PATH=NAME ...]
//            [--js-out=FILE] [--gn-out=FILE]
//
// --namespace defaults to the .voodoom file's `module` statement, if any.
// --guard defaults to an all-caps mangling of the output path.
// --import-dir may be repeated; each `import "path";` in input.voodoom (or
// transitively, in anything it imports) is resolved relative to the
// importing file's own directory first, then against each --import-dir in
// the order given -- see module_loader.h for the full semantics.
// --out-dir is required whenever input.voodoom transitively imports
// anything: every *imported* file gets its own generated header written
// there (see DeriveGenFilename), each in its own C++ namespace (from its
// own `module` statement -- no override for these), #included from whatever
// generated header(s) actually reference it. Real mojom parity: one header
// per .voodoom file, not one flattened header for the whole graph.
// --import-out=PATH=NAME may be repeated: overrides the generated filename
// for the file whose *resolved* import path is exactly PATH (the same
// string module_loader.cc resolves that import to -- for the common case of
// absolute paths passed throughout a build script, this is exactly the path
// you'd pass positionally if compiling that file directly). Without a
// matching --import-out, an imported file's generated filename is always
// DeriveGenFilename's fixed convention -- unlike the entry file, which
// always names its own output via -o.
// --js-out=FILE additionally writes a JS method-name table (mojom name ->
// camelCase JS name per method, for WASMExtWrench's `wew.mojo.Remote`) for
// the entry file's own interfaces -- see GenerateJsHeader. Entry-only, same
// as -o; imports don't get one.
// --gn-out=FILE additionally writes a minimal Chromium-style GN
// `source_set()` stanza wrapping -o's output path -- see GenerateGnBuild.
// Entry-only. A convenience snippet, not a build system of its own.

#include "cpp_generator.h"
#include "lexer.h"
#include "module_loader.h"
#include "parser.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

namespace {

std::optional<std::string> TryReadFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  // Deliberately checking is_open(), not just `!in`/in.fail(): at least one
  // libstdc++ build (MinGW-w64 x86_64-ucrt-posix-seh 16.1.0) leaves failbit
  // unset when the underlying open() fails for a nonexistent path, so `!in`
  // alone reports success for a file that was never actually opened.
  if (!in.is_open()) return std::nullopt;
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

void WriteFile(const std::string& path, const std::string& content) {
  std::ofstream out(path, std::ios::binary);
  if (!out.is_open()) {  // see TryReadFile's comment on is_open() vs `!in`
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

// e.g. "examples/common/types.voodoom" -> "types_gen.h", or (real-mojom-
// parity phase 8) "services/device/public/mojom/battery_status.mojom" ->
// "battery_status_gen.h" -- the naming convention every *imported* file's
// auto-generated header uses (the entry file keeps using its exact `-o`
// path, unchanged). Basename only (strip directory), ".voodoom" or
// ".mojom" stripped if present, "_gen.h" appended.
std::string DeriveGenFilename(const std::string& voodoom_path) {
  std::string base = voodoom_path;
  size_t slash = base.find_last_of("/\\");
  if (slash != std::string::npos) base = base.substr(slash + 1);
  for (const std::string& ext : {std::string(".voodoom"), std::string(".mojom")}) {
    if (base.size() > ext.size() &&
        base.compare(base.size() - ext.size(), ext.size(), ext) == 0) {
      base = base.substr(0, base.size() - ext.size());
      break;
    }
  }
  return base + "_gen.h";
}

std::string JoinDirFile(const std::string& dir, const std::string& filename) {
  if (dir.empty()) return filename;
  char last = dir.back();
  if (last == '/' || last == '\\') return dir + filename;
  return dir + "/" + filename;
}

// `resolved_path` -> caller-chosen generated filename, built from
// --import-out=PATH=NAME flags. GenFilenameFor consults this first,
// falling back to DeriveGenFilename's fixed convention when a resolved
// path has no override -- the one place that fallback decision is made, so
// every site choosing an imported file's generated filename (both when
// writing it out and when #include-ing it from elsewhere) agrees.
using ImportOutOverrides = std::map<std::string, std::string>;

std::string GenFilenameFor(const std::string& resolved_path,
                            const ImportOutOverrides& overrides) {
  auto it = overrides.find(resolved_path);
  return it != overrides.end() ? it->second : DeriveGenFilename(resolved_path);
}

// Bare filenames (via GenFilenameFor) of `file`'s own direct imports, in
// source order -- exactly what GenerateCppHeader's `imported_headers`
// parameter wants for this file's #include block.
std::vector<std::string> DirectImportHeaders(const voodoom::Module& file,
                                              const ImportOutOverrides& overrides) {
  std::vector<std::string> headers;
  headers.reserve(file.imports.size());
  for (const voodoom::ImportDecl& imp : file.imports) {
    if (imp.resolved_path.empty()) continue;
    headers.push_back(GenFilenameFor(imp.resolved_path, overrides));
  }
  return headers;
}

void PrintUsage(const char* argv0) {
  std::fprintf(stderr,
               "usage: %s <input.voodoom> -o <output.h> "
               "[--namespace=NAME] [--guard=NAME] [--import-dir=DIR ...] "
               "[--out-dir=DIR] [--import-out=PATH=NAME ...] "
               "[--js-out=FILE] [--gn-out=FILE] [--enable-if=FLAG ...] "
               "[--typemap=NAME=TYPE ...] [--include=HEADER ...]\n",
               argv0);
}

}  // namespace

int main(int argc, char** argv) {
  std::string input_path;
  std::string output_path;
  std::string ns_override;
  std::string guard_override;
  std::string out_dir;
  std::string js_out;
  std::string gn_out;
  std::vector<std::string> import_dirs;
  ImportOutOverrides import_out_overrides;
  std::unordered_set<std::string> enabled_features;
  voodoom::GeneratorOptions gen_options;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "-o" && i + 1 < argc) {
      output_path = argv[++i];
    } else if (arg.rfind("--namespace=", 0) == 0) {
      ns_override = arg.substr(12);
    } else if (arg.rfind("--guard=", 0) == 0) {
      guard_override = arg.substr(8);
    } else if (arg.rfind("--import-dir=", 0) == 0) {
      import_dirs.push_back(arg.substr(13));
    } else if (arg.rfind("--js-out=", 0) == 0) {
      js_out = arg.substr(9);
    } else if (arg.rfind("--gn-out=", 0) == 0) {
      gn_out = arg.substr(9);
    } else if (arg.rfind("--out-dir=", 0) == 0) {
      out_dir = arg.substr(10);
    } else if (arg.rfind("--import-out=", 0) == 0) {
      std::string rest = arg.substr(13);
      size_t eq = rest.find('=');
      if (eq == std::string::npos) {
        std::fprintf(stderr,
                      "--import-out expects PATH=NAME, got '%s'\n",
                      rest.c_str());
        PrintUsage(argv[0]);
        return 2;
      }
      import_out_overrides[rest.substr(0, eq)] = rest.substr(eq + 1);
    } else if (arg.rfind("--enable-if=", 0) == 0) {
      enabled_features.insert(arg.substr(12));
    } else if (arg.rfind("--typemap=", 0) == 0) {
      std::string rest = arg.substr(10);
      size_t eq = rest.find('=');
      if (eq == std::string::npos) {
        std::fprintf(stderr, "--typemap expects NAME=TYPE, got '%s'\n",
                      rest.c_str());
        PrintUsage(argv[0]);
        return 2;
      }
      gen_options.typemaps[rest.substr(0, eq)] = rest.substr(eq + 1);
    } else if (arg.rfind("--include=", 0) == 0) {
      gen_options.extra_includes.push_back(arg.substr(10));
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

  try {
    std::vector<std::string> skipped;
    bool entry_loaded = false;
    voodoom::LoadedGraph graph =
        voodoom::LoadModuleGraph(input_path, import_dirs, TryReadFile,
                                 enabled_features, &skipped, &entry_loaded);
    for (const std::string& msg : skipped) {
      std::fprintf(stderr, "skip: %s\n", msg.c_str());
    }

    // When the entry loaded, graph.back() is that file. Everything before
    // it is an import that parsed. When it did not, every file in the graph
    // is an import that parsed on its own.
    const size_t import_count =
        entry_loaded ? graph.size() - 1 : graph.size();
    if (import_count > 0 && out_dir.empty()) {
      std::fprintf(stderr,
                    "%s: this file transitively imports %zu other file(s); "
                    "pass --out-dir=DIR so their generated headers can be "
                    "written separately\n",
                    input_path.c_str(), import_count);
      return 1;
    }

    for (size_t i = 0; i < import_count; ++i) {
      const voodoom::LoadedFile& f = graph[i];
      if (f.module.name.empty()) {
        std::fprintf(stderr,
                      "%s: no 'module' statement -- every file in an import "
                      "graph now generates its own header and needs one "
                      "(imported from %s)\n",
                      f.resolved_path.c_str(), input_path.c_str());
        continue;
      }
      std::string out_path =
          JoinDirFile(out_dir, GenFilenameFor(f.resolved_path, import_out_overrides));
      std::string guard = DefaultGuardFrom(out_path);
      std::string header = voodoom::GenerateCppHeader(
          f.module, guard, f.module.name, f.resolved_path,
          DirectImportHeaders(f.module, import_out_overrides), gen_options);
      WriteFile(out_path, header);
    }

    if (!entry_loaded) return 1;

    const voodoom::LoadedFile& entry = graph.back();
    std::string ns = !ns_override.empty() ? ns_override : entry.module.name;
    if (ns.empty()) {
      std::fprintf(stderr,
                    "%s: no C++ namespace: pass --namespace=NAME or add a "
                    "'module NAME;' statement to the .voodoom file\n",
                    input_path.c_str());
      return 1;
    }
    std::string guard =
        !guard_override.empty() ? guard_override : DefaultGuardFrom(output_path);

    std::string header = voodoom::GenerateCppHeader(
        entry.module, guard, ns, input_path,
        DirectImportHeaders(entry.module, import_out_overrides), gen_options);
    WriteFile(output_path, header);
    if (!js_out.empty()) {
      WriteFile(js_out, voodoom::GenerateJsHeader(entry.module, ns, input_path));
    }
    if (!gn_out.empty()) {
      std::string base = output_path;
      size_t slash = base.find_last_of("/\\");
      if (slash != std::string::npos) base = base.substr(slash + 1);
      WriteFile(gn_out, voodoom::GenerateGnBuild(ns, base));
    }
  } catch (const voodoom::LexError& e) {
    std::fprintf(stderr, "%s: lex error: %s\n", input_path.c_str(), e.what());
    return 1;
  } catch (const voodoom::ParseError& e) {
    std::fprintf(stderr, "%s: parse error: %s\n", input_path.c_str(),
                 e.what());
    return 1;
  } catch (const voodoom::LoadError& e) {
    std::fprintf(stderr, "%s: %s\n", input_path.c_str(), e.what());
    return 1;
  } catch (const std::exception& e) {
    std::fprintf(stderr, "%s: %s\n", input_path.c_str(), e.what());
    return 1;
  }

  return 0;
}
