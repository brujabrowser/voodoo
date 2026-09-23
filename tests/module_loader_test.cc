// Exercises LoadModuleGraph's multi-file resolution against an in-memory
// filesystem (no real files touched) -- import path resolution (relative
// to the importing file, then --import-dir-style search dirs), diamond-
// dependency dedup, cycle detection, cross-file struct/union ordering, and
// (v15) that each file in the graph keeps its own Module/namespace and
// that cross-file type references get TypeSpec::owner_namespace stamped.
#include "test.h"

#include "../src/module_loader.h"

#include <map>

using namespace voodoom;

namespace {

// A tiny in-memory "filesystem": exact-path lookup, nothing fancier (no
// '.'/'..' normalization) -- matches how ResolveImport in module_loader.cc
// just tries candidate path strings as-is.
FileReader MakeReader(const std::map<std::string, std::string>& files) {
  return [files](const std::string& path) -> std::optional<std::string> {
    auto it = files.find(path);
    if (it == files.end()) return std::nullopt;
    return it->second;
  };
}

}  // namespace

TEST(loader_single_import_merges_before_importer) {
  std::map<std::string, std::string> files = {
      {"b.voodoom", "module b;\nstruct Shared { int32 x; };"},
      {"a.voodoom",
       "module a;\nimport \"b.voodoom\";\nstruct Outer { Shared s; };"},
  };
  LoadedGraph graph = LoadModuleGraph("a.voodoom", {}, MakeReader(files));
  EXPECT_EQ(graph.size(), 2u);
  // Dependency order: b (the import) before a (the entry, always last).
  EXPECT_EQ(graph[0].resolved_path, "b.voodoom");
  EXPECT_EQ(graph[0].module.name, "b");
  EXPECT_EQ(graph[0].module.structs.size(), 1u);
  EXPECT_EQ(graph[0].module.structs[0].name, "Shared");
  EXPECT_EQ(graph[1].resolved_path, "a.voodoom");
  EXPECT_EQ(graph[1].module.name, "a");  // this file's own module name kept
  EXPECT_EQ(graph[1].module.structs.size(), 1u);  // Outer only -- not merged
  EXPECT_EQ(graph[1].module.structs[0].name, "Outer");
  const TypeSpec& field_type = graph[1].module.structs[0].fields[0].type;
  EXPECT(field_type.kind == TypeKind::kStructRef);
  EXPECT_EQ(field_type.name, "Shared");
  EXPECT_EQ(field_type.owner_namespace, "b");  // cross-file -- needs qualifying
  // The import statement records where it actually resolved to.
  EXPECT_EQ(graph[1].module.imports.size(), 1u);
  EXPECT_EQ(graph[1].module.imports[0].resolved_path, "b.voodoom");
}

TEST(loader_diamond_dependency_merged_once) {
  // a imports b and c; b and c both import d. d must appear exactly once,
  // strictly before both b and c.
  std::map<std::string, std::string> files = {
      {"d.voodoom", "module d;\nstruct D { int32 x; };"},
      {"b.voodoom", "module b;\nimport \"d.voodoom\";\nstruct B { D d; };"},
      {"c.voodoom", "module c;\nimport \"d.voodoom\";\nstruct C { D d; };"},
      {"a.voodoom",
       "module a;\nimport \"b.voodoom\";\nimport \"c.voodoom\";\n"
       "struct A { B b; C c; };"},
  };
  LoadedGraph graph = LoadModuleGraph("a.voodoom", {}, MakeReader(files));
  EXPECT_EQ(graph.size(), 4u);  // D, B, C, A -- each exactly once
  int d_count = 0;
  for (const LoadedFile& f : graph) {
    if (f.module.name == "d") ++d_count;
  }
  EXPECT_EQ(d_count, 1);
  EXPECT_EQ(graph[0].module.name, "d");
  EXPECT_EQ(graph[1].module.name, "b");
  EXPECT_EQ(graph[2].module.name, "c");
  EXPECT_EQ(graph[3].module.name, "a");  // entry is always graph.back()
}

TEST(loader_import_cycle_is_an_error) {
  std::map<std::string, std::string> files = {
      {"a.voodoom", "import \"b.voodoom\";\nstruct A { int32 x; };"},
      {"b.voodoom", "import \"a.voodoom\";\nstruct B { int32 x; };"},
  };
  bool threw = false;
  try {
    LoadModuleGraph("a.voodoom", {}, MakeReader(files));
  } catch (const LoadError&) {
    threw = true;
  }
  EXPECT(threw);
}

TEST(loader_missing_import_is_an_error) {
  std::map<std::string, std::string> files = {
      {"a.voodoom", "import \"missing.voodoom\";\nstruct A { int32 x; };"},
  };
  bool threw = false;
  try {
    LoadModuleGraph("a.voodoom", {}, MakeReader(files));
  } catch (const LoadError&) {
    threw = true;
  }
  EXPECT(threw);
}

TEST(loader_missing_entry_file_is_an_error) {
  std::map<std::string, std::string> files;
  bool threw = false;
  try {
    LoadModuleGraph("nope.voodoom", {}, MakeReader(files));
  } catch (const LoadError&) {
    threw = true;
  }
  EXPECT(threw);
}

TEST(loader_resolves_relative_to_importing_files_own_directory) {
  std::map<std::string, std::string> files = {
      {"common/types.voodoom", "module common;\nstruct Shared { int32 x; };"},
      {"pkg/importer.voodoom",
       "module pkg;\nimport \"../common/types.voodoom\";\n"
       "struct Outer { Shared s; };"},
  };
  LoadedGraph graph =
      LoadModuleGraph("pkg/importer.voodoom", {}, MakeReader(files));
  EXPECT_EQ(graph.size(), 2u);
  EXPECT_EQ(graph[0].resolved_path, "common/types.voodoom");
  EXPECT_EQ(graph[0].module.structs[0].name, "Shared");
}

TEST(loader_falls_back_to_import_dirs) {
  std::map<std::string, std::string> files = {
      {"vendor/shared/types.voodoom", "module shared;\nstruct Shared { int32 x; };"},
      {"importer.voodoom",
       "module importer;\nimport \"types.voodoom\";\n"
       "struct Outer { Shared s; };"},
  };
  // "types.voodoom" isn't next to importer.voodoom -- only resolvable via
  // the search path.
  LoadedGraph graph = LoadModuleGraph("importer.voodoom", {"vendor/shared"},
                                       MakeReader(files));
  EXPECT_EQ(graph.size(), 2u);
  EXPECT_EQ(graph[0].resolved_path, "vendor/shared/types.voodoom");
}

TEST(loader_prefers_importer_relative_over_import_dirs) {
  std::map<std::string, std::string> files = {
      {"local/types.voodoom",
       "module local;\nstruct Shared { int32 x; uint32 y; };"},
      {"other/types.voodoom", "module other;\nstruct Shared { int32 x; };"},
      {"local/importer.voodoom",
       "module importer;\nimport \"types.voodoom\";\n"
       "struct Outer { Shared s; };"},
  };
  LoadedGraph graph = LoadModuleGraph("local/importer.voodoom", {"other"},
                                       MakeReader(files));
  EXPECT_EQ(graph[0].resolved_path, "local/types.voodoom");
  EXPECT_EQ(graph[0].module.structs[0].fields.size(),
            2u);  // the "local" Shared, not "other"
}

TEST(loader_imported_files_own_module_statement_is_preserved) {
  // Before v15 this was discarded (only the entry's module name survived);
  // now every file in the graph generates its own header, so its own
  // module name has to be kept -- see module_loader.h's class comment.
  std::map<std::string, std::string> files = {
      {"b.voodoom",
       "module completely_different;\nstruct Shared { int32 x; };"},
      {"a.voodoom",
       "module a;\nimport \"b.voodoom\";\nstruct Outer { Shared s; };"},
  };
  LoadedGraph graph = LoadModuleGraph("a.voodoom", {}, MakeReader(files));
  EXPECT_EQ(graph[0].module.name, "completely_different");
  EXPECT_EQ(graph.back().module.name, "a");
}

TEST(loader_enum_and_const_from_import_are_visible) {
  std::map<std::string, std::string> files = {
      {"b.voodoom",
       "module b;\nenum Status { OK, ERROR };\nconst int32 kMax = 5;"},
      {"a.voodoom",
       "module a;\nimport \"b.voodoom\";\nstruct Outer { Status s; };"},
  };
  LoadedGraph graph = LoadModuleGraph("a.voodoom", {}, MakeReader(files));
  EXPECT_EQ(graph[0].module.enums.size(), 1u);
  EXPECT_EQ(graph[0].module.consts.size(), 1u);
  const TypeSpec& field_type = graph[1].module.structs[0].fields[0].type;
  EXPECT(field_type.kind == TypeKind::kEnumRef);
  EXPECT_EQ(field_type.owner_namespace, "b");
}
