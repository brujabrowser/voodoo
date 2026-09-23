#include "module_loader.h"

#include "lexer.h"
#include "parser.h"

#include <cctype>
#include <unordered_set>

namespace voodoom {

namespace {

std::string DirName(const std::string& path) {
  size_t slash = path.find_last_of("/\\");
  if (slash == std::string::npos) return "";
  return path.substr(0, slash);
}

std::string JoinPath(const std::string& dir, const std::string& rel) {
  if (dir.empty()) return rel;
  char last = dir.back();
  if (last == '/' || last == '\\') return dir + rel;
  return dir + "/" + rel;
}

// Collapses "." and ".." segments and normalizes '\\' to '/' -- pure string
// manipulation, no filesystem access (no symlink resolution, no case
// folding, no existence check). This is what lets `import "../common/x";`
// resolve predictably from any importing file's directory instead of
// depending on how (or whether) the OS's own path resolution happens to
// handle a literal "a/../b" -- and it's what makes two different spellings
// of the same import (e.g. "a/../b/c.voodoom" and "b/c.voodoom") dedupe as
// the same file for cycle/diamond-dependency tracking.
std::string NormalizePath(const std::string& path) {
  std::string unified = path;
  for (char& c : unified) {
    if (c == '\\') c = '/';
  }
  std::string prefix;  // "" (relative), "/" (absolute), or "C:/" (drive)
  size_t start = 0;
  if (unified.size() >= 3 &&
      std::isalpha(static_cast<unsigned char>(unified[0])) &&
      unified[1] == ':' && unified[2] == '/') {
    prefix = unified.substr(0, 3);
    start = 3;
  } else if (!unified.empty() && unified[0] == '/') {
    prefix = "/";
    start = 1;
  }

  std::vector<std::string> stack;
  size_t i = start;
  while (i <= unified.size()) {
    size_t next = unified.find('/', i);
    std::string seg = unified.substr(
        i, next == std::string::npos ? std::string::npos : next - i);
    if (!seg.empty() && seg != ".") {
      if (seg == "..") {
        if (!stack.empty() && stack.back() != "..") {
          stack.pop_back();
        } else if (prefix.empty()) {
          stack.push_back("..");  // relative path climbing above its start
        }
        // ".." above an absolute/drive root has nowhere to go -- dropped.
      } else {
        stack.push_back(seg);
      }
    }
    if (next == std::string::npos) break;
    i = next + 1;
  }

  std::string result = prefix;
  for (size_t k = 0; k < stack.size(); ++k) {
    if (k) result += "/";
    result += stack[k];
  }
  return result.empty() ? "." : result;
}

struct ResolvedFile {
  std::string path;
  std::string content;
};

// Tries `import_path` relative to the importing file's own directory first
// (so a self-contained pair of files works with no extra flags), then each
// of `import_dirs` in order -- first readable candidate wins. Returns
// nullopt if none of them can be opened.
std::optional<ResolvedFile> ResolveImport(
    const std::string& import_path, const std::string& importer_dir,
    const std::vector<std::string>& import_dirs, const FileReader& read_file) {
  std::vector<std::string> candidates;
  candidates.push_back(NormalizePath(JoinPath(importer_dir, import_path)));
  for (const std::string& dir : import_dirs) {
    candidates.push_back(NormalizePath(JoinPath(dir, import_path)));
  }
  for (std::string& candidate : candidates) {
    if (std::optional<std::string> content = read_file(candidate)) {
      return ResolvedFile{std::move(candidate), std::move(*content)};
    }
  }
  return std::nullopt;
}

// Merges `src`'s own declarations into the running `accumulated` Prelude --
// both its flat decl accumulator (still exactly what SeedFromPrelude in
// parser.cc needs for cross-file name resolution/embedding-order, unchanged
// since before v15) and, new in v15, `owner_by_name`: every name `src`
// declares gets `owner_namespace` (src's own raw `module` statement) so a
// later file's parser can stamp TypeSpec::owner_namespace/
// DefaultValue::named_expr_owner_namespace correctly when it references
// something from `src`.
void MergeIntoAccumulated(Prelude& accumulated, const Module& src,
                           const std::string& owner_namespace) {
  for (const EnumDecl& e : src.enums) {
    accumulated.owner_by_name[e.name] = owner_namespace;
  }
  for (const StructDecl& s : src.structs) {
    accumulated.owner_by_name[s.name] = owner_namespace;
    // (v16) A nested enum's cross-file owner is the same file as its
    // declaring struct -- parser.cc's SeedFromPrelude derives *which*
    // struct it's nested inside directly from s.enums itself (no separate
    // map needed here for that half), but the cross-file owner_namespace
    // half still has to be registered here, same as any other name.
    for (const EnumDecl& e : s.enums) accumulated.owner_by_name[e.name] = owner_namespace;
    // (real-mojom-parity phase 2) Same treatment for this struct's own
    // nested consts.
    for (const ConstDecl& c : s.consts) accumulated.owner_by_name[c.name] = owner_namespace;
  }
  for (const UnionDecl& u : src.unions) {
    accumulated.owner_by_name[u.name] = owner_namespace;
  }
  for (const Interface& iface : src.interfaces) {
    accumulated.owner_by_name[iface.name] = owner_namespace;
    for (const EnumDecl& e : iface.enums) accumulated.owner_by_name[e.name] = owner_namespace;
    for (const ConstDecl& c : iface.consts) accumulated.owner_by_name[c.name] = owner_namespace;
    for (const StructDecl& s : iface.structs) {
      accumulated.owner_by_name[s.name] = owner_namespace;
      for (const EnumDecl& e : s.enums) accumulated.owner_by_name[e.name] = owner_namespace;
      for (const ConstDecl& c : s.consts) accumulated.owner_by_name[c.name] = owner_namespace;
    }
    for (const UnionDecl& u : iface.unions) {
      accumulated.owner_by_name[u.name] = owner_namespace;
    }
  }
  for (const ConstDecl& c : src.consts) {
    accumulated.owner_by_name[c.name] = owner_namespace;
  }
  // (real-mojom-parity phase 7) Same treatment for a feature's own consts
  // -- a feature declares no name of its own that's ever referenced
  // (unlike a struct/interface/union/enum), only its nested consts are.
  for (const FeatureDecl& ft : src.features) {
    for (const ConstDecl& c : ft.consts) {
      accumulated.owner_by_name[c.name] = owner_namespace;
    }
  }

  Module& dst = accumulated.module;
  dst.enums.insert(dst.enums.end(), src.enums.begin(), src.enums.end());
  dst.structs.insert(dst.structs.end(), src.structs.begin(), src.structs.end());
  dst.unions.insert(dst.unions.end(), src.unions.begin(), src.unions.end());
  dst.interfaces.insert(dst.interfaces.end(), src.interfaces.begin(),
                         src.interfaces.end());
  dst.consts.insert(dst.consts.end(), src.consts.begin(), src.consts.end());
  dst.features.insert(dst.features.end(), src.features.begin(),
                       src.features.end());
}

struct LoadState {
  const std::vector<std::string>& import_dirs;
  const FileReader& read_file;
  // Files fully parsed and merged already -- re-importing one (a diamond
  // dependency) is a no-op, not a re-parse: re-parsing would insert
  // duplicate struct/union/enum names and duplicate generated C++ classes.
  std::unordered_set<std::string> merged;
  // Resolved paths currently being loaded, in recursion order -- lets a
  // cycle (a imports b imports a) be reported as a chain instead of
  // recursing until the stack overflows.
  std::vector<std::string> loading;
  std::unordered_set<std::string> enabled_features;
  std::vector<std::string>* skipped = nullptr;
  std::unordered_set<std::string> failed;
  bool per_file = false;
};

std::string CycleChainMessage(const std::vector<std::string>& loading,
                               const std::string& closing_path) {
  std::string msg = "import cycle: ";
  for (const std::string& p : loading) {
    msg += p + " -> ";
  }
  msg += closing_path;
  return msg;
}

// Parses `content` (already read from `path`) -- after first recursively
// resolving and loading its own imports -- and appends the resulting
// LoadedFile to `graph`. `accumulated` is the running cross-file
// name-resolution accumulator (see MergeIntoAccumulated); every file's own
// declarations get merged into it, in dependency order, right after that
// file itself is parsed -- so `graph` ends up in the same dependency order
// (imports before importers, entry last) for free, exactly where
// `accumulated` would already have every name a later sibling/importer
// needs visible.
void LoadFileInto(const std::string& path, const std::string& content,
                   LoadState& state, Prelude& accumulated, LoadedGraph& graph) {
  if (state.merged.count(path)) return;  // diamond dependency, already in
  for (const std::string& loading_path : state.loading) {
    if (loading_path == path) {
      throw LoadError(CycleChainMessage(state.loading, path));
    }
  }
  state.loading.push_back(path);

  auto GiveUp = [&](const std::string& msg) {
    state.loading.pop_back();
    state.merged.insert(path);
    state.failed.insert(path);
    if (state.per_file) {
      if (state.skipped) state.skipped->push_back(msg);
      return;
    }
    throw LoadError(msg);
  };

  std::vector<Token> tokens;
  try {
    tokens = Tokenize(content);
  } catch (const LexError& e) {
    GiveUp("in '" + path + "': " + e.what());
    return;
  }

  Parser parser(tokens);
  FileHeader header;
  try {
    header = parser.ParseHeader();
  } catch (const ParseError& e) {
    GiveUp("in '" + path + "': " + e.what());
    return;
  }

  std::string importer_dir = DirName(path);
  for (ImportDecl& imp : header.imports) {
    std::optional<ResolvedFile> resolved =
        ResolveImport(imp.path, importer_dir, state.import_dirs,
                      state.read_file);
    if (!resolved) {
      if (!state.per_file) {
        throw LoadError("cannot resolve import '" + imp.path + "'", path,
                         imp.line);
      }
      if (state.skipped) {
        state.skipped->push_back(
            "cannot resolve import '" + imp.path + "' (imported from '" +
            path + "')");
      }
      imp.resolved_path.clear();
      continue;
    }
    imp.resolved_path = resolved->path;
    LoadFileInto(resolved->path, resolved->content, state, accumulated, graph);
    if (state.failed.count(resolved->path)) imp.resolved_path.clear();
  }

  Module file_module;
  try {
    file_module = parser.ParseBody(&accumulated);
  } catch (const ParseError& e) {
    GiveUp("in '" + path + "': " + e.what());
    return;
  }
  file_module.name = header.module_name;
  file_module.imports = std::move(header.imports);  // now resolved_path-complete
  ApplyEnableIf(file_module, state.enabled_features);

  MergeIntoAccumulated(accumulated, file_module, header.module_name);
  graph.push_back(LoadedFile{path, std::move(file_module)});

  state.loading.pop_back();
  state.merged.insert(path);
}

}  // namespace

LoadedGraph LoadModuleGraph(const std::string& entry_path,
                             const std::vector<std::string>& import_dirs,
                             const FileReader& read_file,
                             const std::unordered_set<std::string>&
                                 enabled_features,
                             std::vector<std::string>* skipped,
                             bool* entry_loaded) {
  std::string normalized_entry = NormalizePath(entry_path);
  std::optional<std::string> entry_content = read_file(normalized_entry);
  if (!entry_content) {
    throw LoadError("cannot open '" + entry_path + "' for reading");
  }
  Prelude accumulated;
  LoadedGraph graph;
  LoadState state{import_dirs, read_file, {}, {}, enabled_features,
                  skipped, {}, skipped != nullptr};
  LoadFileInto(normalized_entry, *entry_content, state, accumulated, graph);
  if (entry_loaded) {
    *entry_loaded =
        !graph.empty() && graph.back().resolved_path == normalized_entry;
  }
  return graph;
}

}  // namespace voodoom
