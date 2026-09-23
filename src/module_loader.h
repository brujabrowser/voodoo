// Resolves a .voodoom file's `import "path";` statements, transitively, and
// returns every file in the graph as its own (unmerged) voodoom::Module --
// see this file's own doc comment on LoadModuleGraph() for the exact
// semantics.
//
// (v15) This now matches real mojom_bindings_generator.py's shape: each
// file in the graph keeps its own declarations and its own `module`
// statement, so cpp_generator.h's GenerateCppHeader can emit one header per
// file, each in its own C++ namespace, wired together by #includes derived
// from ImportDecl::resolved_path -- see README's "Imports". Prior to v15,
// voodoomc instead merged the whole graph into one flat Module and emitted
// it as a single self-contained header in one namespace (the entry file's);
// that flattening is gone, but the *parser's* cross-file name-resolution
// mechanism (letting a file reference an imported struct/union/enum/
// interface/const unqualified; by-value struct/union cycles are still
// rejected, but file order is not -- see CheckCompleteTypeAcyclic) is unchanged -- see
// SeedFromPrelude in parser.cc and the Prelude/owner_by_name plumbing this
// file builds for it.
#ifndef WVC_SRC_MODULE_LOADER_H_
#define WVC_SRC_MODULE_LOADER_H_

#include "ast.h"

#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

namespace voodoom {

class LoadError : public std::runtime_error {
 public:
  LoadError(const std::string& msg, const std::string& in_file, int line)
      : std::runtime_error(msg + " (" + in_file +
                            (line > 0 ? ":" + std::to_string(line) : "") +
                            ")") {}
  explicit LoadError(const std::string& msg) : std::runtime_error(msg) {}
};

// Reads a resolved file path, returning nullopt if it can't be opened (so
// ResolveImportPath -- see module_loader.cc -- can try the next search
// directory instead of treating a missing file as fatal until every
// candidate has been tried). Real filesystem access lives in main.cc's
// implementation of this; tests supply an in-memory one instead.
using FileReader =
    std::function<std::optional<std::string>(const std::string& path)>;

// One file's own (unmerged) parsed declarations, plus the resolved path
// module_loader.cc found it at -- the latter is what cpp_generator.cc's
// GenerateCppHeader derives this file's own generated-header filename (and
// every direct importer's #include of it) from.
struct LoadedFile {
  std::string resolved_path;
  Module module;
};

// The whole import graph, in dependency order: every file a given file
// imports (transitively) appears strictly before it, so `back()` is
// always the entry file. A diamond-imported file (two files both importing
// a shared common.voodoom) appears exactly once.
using LoadedGraph = std::vector<LoadedFile>;

// Loads `entry_path` and every file it imports, transitively:
//
//   - Each import path is resolved relative to the importing file's own
//     directory first, then against each of `import_dirs` in order (first
//     match wins) -- like C/C++'s `#include "..."` search order, not
//     mojom's build-system-supplied roots (there's no build system here).
//     Resolved paths get lightweight, filesystem-free normalization ("."
//     and ".." segments collapsed -- see module_loader.cc's NormalizePath)
//     so `import "../common/x.voodoom";` behaves predictably regardless of
//     the OS, but nothing heavier: no symlink resolution, no case folding,
//     no existence-independent canonicalization. Two spellings of the same
//     import that don't collapse to the same string via that normalization
//     (e.g. one going through a symlink) are treated as different files.
//     Every ImportDecl in the returned graph's Modules has its
//     `resolved_path` filled in to whichever candidate actually matched.
//   - A file imported more than once (a "diamond" -- e.g. two files both
//     importing a shared common.voodoom) is only parsed once; it appears
//     once in the returned LoadedGraph, at the position dictated by
//     dependency order (before every file that (transitively) imports it).
//   - An import cycle (a imports b imports a) is a LoadError, not a hang --
//     you cannot topologically order a cycle, and this compiler has no
//     forward-declaration story across files the way it does for
//     interfaces within one file.
//   - Cross-file name resolution while parsing (an unqualified reference to
//     an imported struct/union/enum/interface/const, and the "declared
//     earlier" ordering rule struct/union embedding needs) still works
//     exactly as before -- every file gets to see everything loaded before
//     it in the whole graph, not just its own direct imports (a
//     deliberately preserved, permissive simplification -- see parser.cc's
//     SeedFromPrelude). What's new is that each returned Module keeps its
//     *own* declarations only (not merged with its imports') and its own
//     `module` statement -- LoadModuleGraph no longer discards any file's
//     module name the way the old, single-Module-returning LoadModule did.
//
// Throws LexError/ParseError (from the file that triggered them) or
// LoadError (unresolvable import, import cycle, or entry_path itself
// unreadable).
// `skipped`, when non-null, switches on per-file mode: a file that fails to
// lex, parse, or resolve an import is recorded there and left out of the
// graph, and the files that did parse are still returned. `entry_loaded`,
// when non-null, is set true only when the entry itself made it into the
// graph. With both null, a bad file still throws LoadError.
LoadedGraph LoadModuleGraph(const std::string& entry_path,
                             const std::vector<std::string>& import_dirs,
                             const FileReader& read_file,
                             const std::unordered_set<std::string>&
                                 enabled_features = {},
                             std::vector<std::string>* skipped = nullptr,
                             bool* entry_loaded = nullptr);

}  // namespace voodoom

#endif  // WVC_SRC_MODULE_LOADER_H_
