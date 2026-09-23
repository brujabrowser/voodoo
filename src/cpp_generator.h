// Emits a single self-contained C++ header from a parsed .voodoom Module,
// in exactly the Proxy_/Stub_ shape WASMCadidumBindings' generated-code
// contract documents (see that repo's remote.h/receiver.h header comments
// and examples/echo/echo_interface.h, which this generator's output is
// structurally identical to for the Echo/EchoListener interfaces).
#ifndef WVC_SRC_CPP_GENERATOR_H_
#define WVC_SRC_CPP_GENERATOR_H_

#include "ast.h"

#include <map>
#include <string>
#include <vector>

namespace voodoom {

// `--typemap=MojomName=CppType` / `--include=header`. Native structs/enums
// with a mapping emit `using Name = CppType` and Write/Read via
// `mojo::NativeTraits<CppType>` (user specializes in an --include header).
struct GeneratorOptions {
  std::map<std::string, std::string> typemaps;
  std::vector<std::string> extra_includes;
};

// `header_guard`: e.g. "ECHO_VOODOOM_GEN_H_" (no surrounding underscores
// added). `cpp_namespace`: the C++ namespace to wrap generated classes in.
// `source_filename`: recorded in the leading comment only. `imported_headers`
// (v15): bare filenames (e.g. "types_gen.h") of the generated headers for
// this file's own *direct* imports -- each gets a `#include "..."` right
// after the fixed WASMCadidumBindings/mojo includes. Every cross-file type
// reference `module` carries (TypeSpec::owner_namespace/
// DefaultValue::named_expr_owner_namespace) is qualified with that other
// file's namespace at the point of use, so this header never needs to
// `using` anything from an import -- see NamespacePrefix.
std::string GenerateCppHeader(
    const Module& module, const std::string& header_guard,
    const std::string& cpp_namespace, const std::string& source_filename,
    const std::vector<std::string>& imported_headers = {},
    const GeneratorOptions& options = {});

// Per-interface JS method table for WASMExtWrench `wew.mojo.Remote`.
std::string GenerateJsHeader(const Module& module,
                             const std::string& cpp_namespace,
                             const std::string& source_filename);

// Chromium-style BUILD.gn source_set fragment for this generated header.
std::string GenerateGnBuild(const std::string& target_name,
                            const std::string& generated_header);

}  // namespace voodoom

#endif  // WVC_SRC_CPP_GENERATOR_H_
