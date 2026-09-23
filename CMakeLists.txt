cmake_minimum_required(VERSION 3.16)
project(WASMVoodooCompile LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# voodoomc is a host-side dev tool (like protoc / mojom_bindings_generator),
# not a WASM module -- it never runs inside the sandboxed viewer, so unlike
# the sibling WASM* repos it has no WASI build and no runtime dependency on
# any of them. Only its golden test (below) needs the sibling Cadidum
# checkouts, to prove generated code actually works, not just resembles.

add_library(voodoomc_lib
  src/lexer.cc
  src/parser.cc
  src/module_loader.cc
  src/cpp_generator.cc
  src/type_intern.cc
)
target_include_directories(voodoomc_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

if(MSVC)
  target_compile_options(voodoomc_lib PRIVATE /W4)
else()
  target_compile_options(voodoomc_lib PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(voodoomc src/main.cc)
target_link_libraries(voodoomc PRIVATE voodoomc_lib)

enable_testing()

add_executable(wvc_tests
  tests/test_main.cc
  tests/lexer_test.cc
  tests/parser_test.cc
  tests/generator_test.cc
  tests/module_loader_test.cc
  tests/real_mojom_test.cc
  tests/type_intern_test.cc
)
target_link_libraries(wvc_tests PRIVATE voodoomc_lib)
# (real-mojom-parity phase 8) Absolute path to the vendored real-.mojom-file
# corpus, so tests/real_mojom_test.cc can read the files regardless of the
# test binary's current working directory.
target_compile_definitions(wvc_tests PRIVATE
  WVC_REAL_MOJOM_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/real_mojom")
add_test(NAME wvc_tests COMMAND wvc_tests)

# mojovm.Call(api, arg) parses the named .mojom on that call.
# It is not linked into gocvm.
add_executable(mojovm src/mojovm_main.cc src/mojovm.cc)
target_link_libraries(mojovm PRIVATE voodoomc_lib)
target_include_directories(mojovm PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_executable(mojovm_test tests/mojovm_test.cc src/mojovm.cc)
target_link_libraries(mojovm_test PRIVATE voodoomc_lib)
target_include_directories(mojovm_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
add_test(NAME mojovm_test COMMAND mojovm_test)

# One module. mojovm names methods. Fire takes any key, including SECRPC.
set(NG_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Bruja/ng")
add_executable(ng ${NG_DIR}/src/main.cc ${NG_DIR}/src/module.cc src/mojovm.cc)
target_link_libraries(ng PRIVATE voodoomc_lib)
target_include_directories(ng PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${NG_DIR}/include)

add_executable(ng_test ${NG_DIR}/tests/ng_test.cc ${NG_DIR}/src/module.cc src/mojovm.cc)
target_link_libraries(ng_test PRIVATE voodoomc_lib)
target_include_directories(ng_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${NG_DIR}/include)
add_test(NAME ng_test COMMAND ng_test)

# ---- Golden test: generate the echo interface and build it against real
# WASMCadidumBindings/WASMCadidumKernel, proving voodoomc's output isn't
# just structurally similar to examples/echo/echo_interface.h but actually
# compiles and passes the same kind of roundtrip that hand-written file's
# own tests do (see WASMCadidumBindings/tests/bindings/associated_roundtrip.cc).
set(WVC_WCB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/WASMCadidumBindings")
if(EXISTS "${WVC_WCB_DIR}/CMakeLists.txt" AND NOT TARGET wcb)
  add_subdirectory("${WVC_WCB_DIR}" "${CMAKE_BINARY_DIR}/wcb" EXCLUDE_FROM_ALL)
endif()

if(TARGET wcb)
  # CMAKE_CURRENT_BINARY_DIR (not CMAKE_BINARY_DIR): this golden-test
  # fixture dir is WASMVoodooCompile's own internal scratch space. Using
  # the top-level CMAKE_BINARY_DIR collided with a parent project's own
  # "generated/echo_interface_gen.h" (e.g. WASMv16's) when this repo is
  # pulled in as a sibling add_subdirectory rather than built standalone --
  # two differently-named custom targets (wvc_generate_echo vs.
  # wv16_generate_echo) both writing the same literal output path, which
  # Ninja rejects outright ("multiple rules generate ...").
  set(WVC_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
  file(MAKE_DIRECTORY "${WVC_GEN_DIR}")
  set(WVC_GEN_ECHO_H "${WVC_GEN_DIR}/echo_interface_gen.h")
  set(WVC_GEN_REGISTRY_H "${WVC_GEN_DIR}/registry_interface_gen.h")
  set(WVC_GEN_SETTINGS_H "${WVC_GEN_DIR}/settings_interface_gen.h")
  set(WVC_GEN_LOGGING_H "${WVC_GEN_DIR}/logging_interface_gen.h")
  # Deliberately NOT the DeriveGenFilename default ("types_gen.h") -- named
  # via --import-out below, both to demonstrate the flag and so
  # wvc_generate_logging's build (which only compiles if
  # logging_interface_gen.h's #include matches this exact filename) proves
  # the override actually took effect.
  set(WVC_GEN_TYPES_H "${WVC_GEN_DIR}/common_types_gen.h")
  set(WVC_GEN_PROFILE_H "${WVC_GEN_DIR}/profile_interface_gen.h")
  set(WVC_GEN_TELEMETRY_H "${WVC_GEN_DIR}/telemetry_interface_gen.h")
  set(WVC_GEN_CALCULATOR_H "${WVC_GEN_DIR}/calculator_interface_gen.h")
  set(WVC_GEN_WIDGET_V1_H "${WVC_GEN_DIR}/widget_v1_interface_gen.h")
  set(WVC_GEN_WIDGET_V2_H "${WVC_GEN_DIR}/widget_v2_interface_gen.h")
  set(WVC_GEN_GREETER_V1_EXT_H "${WVC_GEN_DIR}/greeter_v1_extensible_interface_gen.h")
  set(WVC_GEN_GREETER_V1_CLOSED_H "${WVC_GEN_DIR}/greeter_v1_closed_interface_gen.h")
  set(WVC_GEN_GREETER_V2_H "${WVC_GEN_DIR}/greeter_v2_interface_gen.h")
  set(WVC_GEN_STATUS_V1_CLOSED_H "${WVC_GEN_DIR}/status_v1_closed_interface_gen.h")
  set(WVC_GEN_STATUS_V1_EXT_H "${WVC_GEN_DIR}/status_v1_extensible_interface_gen.h")
  set(WVC_GEN_STATUS_V2_H "${WVC_GEN_DIR}/status_v2_interface_gen.h")
  set(WVC_GEN_COLOR_V1_CLOSED_H "${WVC_GEN_DIR}/color_v1_closed_interface_gen.h")
  set(WVC_GEN_COLOR_V1_EXT_H "${WVC_GEN_DIR}/color_v1_extensible_interface_gen.h")
  set(WVC_GEN_COLOR_V2_H "${WVC_GEN_DIR}/color_v2_interface_gen.h")
  set(WVC_GEN_OPTIONAL_HANDLES_H "${WVC_GEN_DIR}/optional_handles_interface_gen.h")
  set(WVC_GEN_CADMIUM_VOODOO_H "${WVC_GEN_DIR}/cadmium_voodoo_interface_gen.h")
  set(WVC_GEN_NESTED_ENUM_H "${WVC_GEN_DIR}/nested_enum_gen.h")
  set(WVC_GEN_CONST_PARITY_H "${WVC_GEN_DIR}/const_parity_gen.h")
  set(WVC_GEN_HANDLE_TYPES_H "${WVC_GEN_DIR}/handle_types_gen.h")
  set(WVC_GEN_FIXED_ARRAY_H "${WVC_GEN_DIR}/fixed_array_gen.h")
  set(WVC_GEN_RESULT_TYPE_H "${WVC_GEN_DIR}/result_type_gen.h")
  set(WVC_GEN_FEATURE_DECL_H "${WVC_GEN_DIR}/feature_decl_gen.h")
  set(WVC_GEN_HANDLE_FIELDS_H "${WVC_GEN_DIR}/handle_fields_gen.h")

  add_custom_command(
    OUTPUT "${WVC_GEN_ECHO_H}"
    COMMAND voodoomc
            "${CMAKE_CURRENT_SOURCE_DIR}/examples/echo/echo.voodoom"
            -o "${WVC_GEN_ECHO_H}"
    DEPENDS voodoomc "${CMAKE_CURRENT_SOURCE_DIR}/examples/echo/echo.voodoom"
    COMMENT "voodoomc: generating echo_interface_gen.h"
    VERBATIM
  )
  add_custom_target(wvc_generate_echo DEPENDS "${WVC_GEN_ECHO_H}")

  add_custom_command(
    OUTPUT "${WVC_GEN_REGISTRY_H}"
    COMMAND voodoomc
            "${CMAKE_CURRENT_SOURCE_DIR}/examples/registry/registry.voodoom"
            -o "${WVC_GEN_REGISTRY_H}"
    DEPENDS voodoomc "${CMAKE_CURRENT_SOURCE_DIR}/examples/registry/registry.voodoom"
    COMMENT "voodoomc: generating registry_interface_gen.h"
    VERBATIM
  )
  add_custom_target(wvc_generate_registry DEPENDS "${WVC_GEN_REGISTRY_H}")

  add_custom_command(
    OUTPUT "${WVC_GEN_SETTINGS_H}"
    COMMAND voodoomc
            "${CMAKE_CURRENT_SOURCE_DIR}/examples/settings/settings.voodoom"
            -o "${WVC_GEN_SETTINGS_H}"
    DEPENDS voodoomc "${CMAKE_CURRENT_SOURCE_DIR}/examples/settings/settings.voodoom"
    COMMENT "voodoomc: generating settings_interface_gen.h"
    VERBATIM
  )
  add_custom_target(wvc_generate_settings DEPENDS "${WVC_GEN_SETTINGS_H}")

  # logging.voodoom imports ../common/types.voodoom by a path relative to
  # its own directory -- no --import-dir needed, proving that resolution
  # order works with zero extra flags for the common case. Since v15,
  # voodoomc generates one header per file in the import graph, each in its
  # own namespace -- one invocation with --out-dir produces BOTH
  # logging_interface_gen.h (via -o, logging's own namespace) and
  # common_types_gen.h (custom name via --import-out, common's own
  # namespace, #included from logging_interface_gen.h), so both are
  # declared as this one command's OUTPUTs. --import-out's PATH must match
  # exactly what module_loader.cc resolves the import to -- here that's
  # simply types.voodoom's own absolute path, since logging.voodoom imports
  # it with nothing but a relative "../common/types.voodoom" (no
  # --import-dir indirection to normalize away).
  add_custom_command(
    OUTPUT "${WVC_GEN_LOGGING_H}" "${WVC_GEN_TYPES_H}"
    COMMAND voodoomc
            "${CMAKE_CURRENT_SOURCE_DIR}/examples/logging/logging.voodoom"
            -o "${WVC_GEN_LOGGING_H}"
            "--out-dir=${WVC_GEN_DIR}"
            "--import-out=${CMAKE_CURRENT_SOURCE_DIR}/examples/common/types.voodoom=common_types_gen.h"
    DEPENDS voodoomc
            "${CMAKE_CURRENT_SOURCE_DIR}/examples/logging/logging.voodoom"
            "${CMAKE_CURRENT_SOURCE_DIR}/examples/common/types.voodoom"
    COMMENT "voodoomc: generating logging_interface_gen.h + common_types_gen.h"
    VERBATIM
  )
  add_custom_target(wvc_generate_logging DEPENDS "${WVC_GEN_LOGGING_H}" "${WVC_GEN_TYPES_H}")

  add_custom_command(
    OUTPUT "${WVC_GEN_PROFILE_H}"
    COMMAND voodoomc
            "${CMAKE_CURRENT_SOURCE_DIR}/examples/profile/profile.voodoom"
            -o "${WVC_GEN_PROFILE_H}"
    DEPENDS voodoomc "${CMAKE_CURRENT_SOURCE_DIR}/examples/profile/profile.voodoom"
    COMMENT "voodoomc: generating profile_interface_gen.h"
    VERBATIM
  )
  add_custom_target(wvc_generate_profile DEPENDS "${WVC_GEN_PROFILE_H}")

  add_custom_command(
    OUTPUT "${WVC_GEN_TELEMETRY_H}"
    COMMAND voodoomc
            "${CMAKE_CURRENT_SOURCE_DIR}/examples/telemetry/telemetry.voodoom"
            -o "${WVC_GEN_TELEMETRY_H}"
    DEPENDS voodoomc "${CMAKE_CURRENT_SOURCE_DIR}/examples/telemetry/telemetry.voodoom"
    COMMENT "voodoomc: generating telemetry_interface_gen.h"
    VERBATIM
  )
  add_custom_target(wvc_generate_telemetry DEPENDS "${WVC_GEN_TELEMETRY_H}")

  add_custom_command(
    OUTPUT "${WVC_GEN_CALCULATOR_H}"
    COMMAND voodoomc
            "${CMAKE_CURRENT_SOURCE_DIR}/examples/calculator/calculator.voodoom"
            -o "${WVC_GEN_CALCULATOR_H}"
    DEPENDS voodoomc "${CMAKE_CURRENT_SOURCE_DIR}/examples/calculator/calculator.voodoom"
    COMMENT "voodoomc: generating calculator_interface_gen.h"
    VERBATIM
  )
  add_custom_target(wvc_generate_calculator DEPENDS "${WVC_GEN_CALCULATOR_H}")

  add_custom_command(
    OUTPUT "${WVC_GEN_WIDGET_V1_H}"
    COMMAND voodoomc
            "${CMAKE_CURRENT_SOURCE_DIR}/examples/versioning/widget_v1.voodoom"
            -o "${WVC_GEN_WIDGET_V1_H}"
    DEPENDS voodoomc "${CMAKE_CURRENT_SOURCE_DIR}/examples/versioning/widget_v1.voodoom"
    COMMENT "voodoomc: generating widget_v1_interface_gen.h"
    VERBATIM
  )
  add_custom_target(wvc_generate_widget_v1 DEPENDS "${WVC_GEN_WIDGET_V1_H}")

  add_custom_command(
    OUTPUT "${WVC_GEN_WIDGET_V2_H}"
    COMMAND voodoomc
            "${CMAKE_CURRENT_SOURCE_DIR}/examples/versioning/widget_v2.voodoom"
            -o "${WVC_GEN_WIDGET_V2_H}"
    DEPENDS voodoomc "${CMAKE_CURRENT_SOURCE_DIR}/examples/versioning/widget_v2.voodoom"
    COMMENT "voodoomc: generating widget_v2_interface_gen.h"
    VERBATIM
  )
  add_custom_target(wvc_generate_widget_v2 DEPENDS "${WVC_GEN_WIDGET_V2_H}")

  add_custom_command(
    OUTPUT "${WVC_GEN_GREETER_V1_EXT_H}"
    COMMAND voodoomc
            "${CMAKE_CURRENT_SOURCE_DIR}/examples/extensibility/greeter_v1_extensible.voodoom"
            -o "${WVC_GEN_GREETER_V1_EXT_H}"
    DEPENDS voodoomc "${CMAKE_CURRENT_SOURCE_DIR}/examples/extensibility/greeter_v1_extensible.voodoom"
    COMMENT "voodoomc: generating greeter_v1_extensible_interface_gen.h"
    VERBATIM
  )
  add_custom_target(wvc_generate_greeter_v1_ext DEPENDS "${WVC_GEN_GREETER_V1_EXT_H}")

  add_custom_command(
    OUTPUT "${WVC_GEN_GREETER_V1_CLOSED_H}"
    COMMAND voodoomc
            "${CMAKE_CURRENT_SOURCE_DIR}/examples/extensibility/greeter_v1_closed.voodoom"
            -o "${WVC_GEN_GREETER_V1_CLOSED_H}"
    DEPENDS voodoomc "${CMAKE_CURRENT_SOURCE_DIR}/examples/extensibility/greeter_v1_closed.voodoom"
    COMMENT "voodoomc: generating greeter_v1_closed_interface_gen.h"
    VERBATIM
  )
  add_custom_target(wvc_generate_greeter_v1_closed DEPENDS "${WVC_GEN_GREETER_V1_CLOSED_H}")

  add_custom_command(
    OUTPUT "${WVC_GEN_GREETER_V2_H}"
    COMMAND voodoomc
            "${CMAKE_CURRENT_SOURCE_DIR}/examples/extensibility/greeter_v2.voodoom"
            -o "${WVC_GEN_GREETER_V2_H}"
    DEPENDS voodoomc "${CMAKE_CURRENT_SOURCE_DIR}/examples/extensibility/greeter_v2.voodoom"
    COMMENT "voodoomc: generating greeter_v2_interface_gen.h"
    VERBATIM
  )
  add_custom_target(wvc_generate_greeter_v2 DEPENDS "${WVC_GEN_GREETER_V2_H}")

  add_custom_command(
    OUTPUT "${WVC_GEN_STATUS_V1_CLOSED_H}"
    COMMAND voodoomc
            "${CMAKE_CURRENT_SOURCE_DIR}/examples/union_versioning/status_v1_closed.voodoom"
            -o "${WVC_GEN_STATUS_V1_CLOSED_H}"
    DEPENDS voodoomc "${CMAKE_CURRENT_SOURCE_DIR}/examples/union_versioning/status_v1_closed.voodoom"
    COMMENT "voodoomc: generating status_v1_closed_interface_gen.h"
    VERBATIM
  )
  add_custom_target(wvc_generate_status_v1_closed DEPENDS "${WVC_GEN_STATUS_V1_CLOSED_H}")

  add_custom_command(
    OUTPUT "${WVC_GEN_STATUS_V1_EXT_H}"
    COMMAND voodoomc
            "${CMAKE_CURRENT_SOURCE_DIR}/examples/union_versioning/status_v1_extensible.voodoom"
            -o "${WVC_GEN_STATUS_V1_EXT_H}"
    DEPENDS voodoomc "${CMAKE_CURRENT_SOURCE_DIR}/examples/union_versioning/status_v1_extensible.voodoom"
    COMMENT "voodoomc: generating status_v1_extensible_interface_gen.h"
    VERBATIM
  )
  add_custom_target(wvc_generate_status_v1_ext DEPENDS "${WVC_GEN_STATUS_V1_EXT_H}")

  add_custom_command(
    OUTPUT "${WVC_GEN_STATUS_V2_H}"
    COMMAND voodoomc
            "${CMAKE_CURRENT_SOURCE_DIR}/examples/union_versioning/status_v2.voodoom"
            -o "${WVC_GEN_STATUS_V2_H}"
    DEPENDS voodoomc "${CMAKE_CURRENT_SOURCE_DIR}/examples/union_versioning/status_v2.voodoom"
    COMMENT "voodoomc: generating status_v2_interface_gen.h"
    VERBATIM
  )
  add_custom_target(wvc_generate_status_v2 DEPENDS "${WVC_GEN_STATUS_V2_H}")

  add_custom_command(
    OUTPUT "${WVC_GEN_COLOR_V1_CLOSED_H}"
    COMMAND voodoomc
            "${CMAKE_CURRENT_SOURCE_DIR}/examples/enum_versioning/color_v1_closed.voodoom"
            -o "${WVC_GEN_COLOR_V1_CLOSED_H}"
    DEPENDS voodoomc "${CMAKE_CURRENT_SOURCE_DIR}/examples/enum_versioning/color_v1_closed.voodoom"
    COMMENT "voodoomc: generating color_v1_closed_interface_gen.h"
    VERBATIM
  )
  add_custom_target(wvc_generate_color_v1_closed DEPENDS "${WVC_GEN_COLOR_V1_CLOSED_H}")

  add_custom_command(
    OUTPUT "${WVC_GEN_COLOR_V1_EXT_H}"
    COMMAND voodoomc
            "${CMAKE_CURRENT_SOURCE_DIR}/examples/enum_versioning/color_v1_extensible.voodoom"
            -o "${WVC_GEN_COLOR_V1_EXT_H}"
    DEPENDS voodoomc "${CMAKE_CURRENT_SOURCE_DIR}/examples/enum_versioning/color_v1_extensible.voodoom"
    COMMENT "voodoomc: generating color_v1_extensible_interface_gen.h"
    VERBATIM
  )
  add_custom_target(wvc_generate_color_v1_ext DEPENDS "${WVC_GEN_COLOR_V1_EXT_H}")

  add_custom_command(
    OUTPUT "${WVC_GEN_COLOR_V2_H}"
    COMMAND voodoomc
            "${CMAKE_CURRENT_SOURCE_DIR}/examples/enum_versioning/color_v2.voodoom"
            -o "${WVC_GEN_COLOR_V2_H}"
    DEPENDS voodoomc "${CMAKE_CURRENT_SOURCE_DIR}/examples/enum_versioning/color_v2.voodoom"
    COMMENT "voodoomc: generating color_v2_interface_gen.h"
    VERBATIM
  )
  add_custom_target(wvc_generate_color_v2 DEPENDS "${WVC_GEN_COLOR_V2_H}")

  add_custom_command(
    OUTPUT "${WVC_GEN_OPTIONAL_HANDLES_H}"
    COMMAND voodoomc
            "${CMAKE_CURRENT_SOURCE_DIR}/examples/optional_handles/optional_handles.voodoom"
            -o "${WVC_GEN_OPTIONAL_HANDLES_H}"
    DEPENDS voodoomc "${CMAKE_CURRENT_SOURCE_DIR}/examples/optional_handles/optional_handles.voodoom"
    COMMENT "voodoomc: generating optional_handles_interface_gen.h"
    VERBATIM
  )
  add_custom_target(wvc_generate_optional_handles DEPENDS "${WVC_GEN_OPTIONAL_HANDLES_H}")

  # cadmium_voodoo.voodoom is the real Mojo-shaped control channel between
  # loki-closure/cmd/cadmium and WASMHolePunch's whp_cadmium bridge -- an
  # associated-listener push interface (Attach/OnFire) plus a plain report
  # call (Report), replacing the /api/rbi/ipc polling loop bridge_ipc.go
  # hand-rolls there today.
  add_custom_command(
    OUTPUT "${WVC_GEN_CADMIUM_VOODOO_H}"
    COMMAND voodoomc
            "${CMAKE_CURRENT_SOURCE_DIR}/examples/cadmium_voodoo/cadmium_voodoo.voodoom"
            -o "${WVC_GEN_CADMIUM_VOODOO_H}"
    DEPENDS voodoomc "${CMAKE_CURRENT_SOURCE_DIR}/examples/cadmium_voodoo/cadmium_voodoo.voodoom"
    COMMENT "voodoomc: generating cadmium_voodoo_interface_gen.h"
    VERBATIM
  )
  add_custom_target(wvc_generate_cadmium_voodoo DEPENDS "${WVC_GEN_CADMIUM_VOODOO_H}")

  # nested_enum.voodoom exercises v16's nested enum -- an interface's own
  # nested enum (Worker.Status) referenced from a struct (Job), and a
  # struct's own nested enum (Job.Priority) referenced from a *different*
  # interface (JobQueue) -- both cross-container directions in one file,
  # proving the generator's nested-enum codegen doesn't depend on emission
  # order between structs and interfaces.
  add_custom_command(
    OUTPUT "${WVC_GEN_NESTED_ENUM_H}"
    COMMAND voodoomc
            "${CMAKE_CURRENT_SOURCE_DIR}/examples/nested_enum/nested_enum.voodoom"
            -o "${WVC_GEN_NESTED_ENUM_H}"
    DEPENDS voodoomc "${CMAKE_CURRENT_SOURCE_DIR}/examples/nested_enum/nested_enum.voodoom"
    COMMENT "voodoomc: generating nested_enum_gen.h"
    VERBATIM
  )
  add_custom_target(wvc_generate_nested_enum DEPENDS "${WVC_GEN_NESTED_ENUM_H}")

  # const_parity.voodoom exercises real-mojom-parity phase 2's nested
  # `const` and widened const/default value grammar (float/double/bool/
  # string, hex literals) -- Config's own nested const references
  # Worker.kMaxJobs (a *different* container's nested const), the same
  # cross-container-ordering proof nested_enum.voodoom gives for enums.
  add_custom_command(
    OUTPUT "${WVC_GEN_CONST_PARITY_H}"
    COMMAND voodoomc
            "${CMAKE_CURRENT_SOURCE_DIR}/examples/const_parity/const_parity.voodoom"
            -o "${WVC_GEN_CONST_PARITY_H}"
    DEPENDS voodoomc "${CMAKE_CURRENT_SOURCE_DIR}/examples/const_parity/const_parity.voodoom"
    COMMENT "voodoomc: generating const_parity_gen.h"
    VERBATIM
  )
  add_custom_target(wvc_generate_const_parity DEPENDS "${WVC_GEN_CONST_PARITY_H}")

  # handle_types.voodoom exercises real-mojom-parity phase 4's `handle`
  # type -- bare `handle` (opaque, any kind), `handle<message_pipe>`, and
  # `handle<shared_buffer>`, each in both non-nullable and nullable form,
  # proving a real handle actually transits the wire in every combination
  # and that an absent nullable handle transits cleanly.
  add_custom_command(
    OUTPUT "${WVC_GEN_HANDLE_TYPES_H}"
    COMMAND voodoomc
            "${CMAKE_CURRENT_SOURCE_DIR}/examples/handle_types/handle_types.voodoom"
            -o "${WVC_GEN_HANDLE_TYPES_H}"
    DEPENDS voodoomc "${CMAKE_CURRENT_SOURCE_DIR}/examples/handle_types/handle_types.voodoom"
    COMMENT "voodoomc: generating handle_types_gen.h"
    VERBATIM
  )
  add_custom_target(wvc_generate_handle_types DEPENDS "${WVC_GEN_HANDLE_TYPES_H}")

  # fixed_array.voodoom exercises real-mojom-parity phase 5's fixed-size
  # `array<T, N>` -- a fixed array of scalars and a fixed array of a
  # struct (both as Grid struct fields), a nullable fixed array, and a
  # fixed array as a bare top-level method parameter.
  add_custom_command(
    OUTPUT "${WVC_GEN_FIXED_ARRAY_H}"
    COMMAND voodoomc
            "${CMAKE_CURRENT_SOURCE_DIR}/examples/fixed_array/fixed_array.voodoom"
            -o "${WVC_GEN_FIXED_ARRAY_H}"
    DEPENDS voodoomc "${CMAKE_CURRENT_SOURCE_DIR}/examples/fixed_array/fixed_array.voodoom"
    COMMENT "voodoomc: generating fixed_array_gen.h"
    VERBATIM
  )
  add_custom_target(wvc_generate_fixed_array DEPENDS "${WVC_GEN_FIXED_ARRAY_H}")

  # result_type.voodoom exercises real-mojom-parity phase 6's `result<T,
  # E>` response type -- Inventory.Get exercises a struct success type
  # and a struct error type, and the [Sync] Divide exercises result<T, E>
  # alongside a real blocking call.
  add_custom_command(
    OUTPUT "${WVC_GEN_RESULT_TYPE_H}"
    COMMAND voodoomc
            "${CMAKE_CURRENT_SOURCE_DIR}/examples/result_type/result_type.voodoom"
            -o "${WVC_GEN_RESULT_TYPE_H}"
    DEPENDS voodoomc "${CMAKE_CURRENT_SOURCE_DIR}/examples/result_type/result_type.voodoom"
    COMMENT "voodoomc: generating result_type_gen.h"
    VERBATIM
  )
  add_custom_target(wvc_generate_result_type DEPENDS "${WVC_GEN_RESULT_TYPE_H}")

  # feature_decl.voodoom exercises real-mojom-parity phase 7's `feature
  # NAME { ... };` declarations -- RetryPolicy's nested consts are used
  # both as a struct field default and as an ordinary method argument.
  add_custom_command(
    OUTPUT "${WVC_GEN_FEATURE_DECL_H}"
    COMMAND voodoomc
            "${CMAKE_CURRENT_SOURCE_DIR}/examples/feature_decl/feature_decl.voodoom"
            -o "${WVC_GEN_FEATURE_DECL_H}"
    DEPENDS voodoomc "${CMAKE_CURRENT_SOURCE_DIR}/examples/feature_decl/feature_decl.voodoom"
    COMMENT "voodoomc: generating feature_decl_gen.h"
    VERBATIM
  )
  add_custom_target(wvc_generate_feature_decl DEPENDS "${WVC_GEN_FEATURE_DECL_H}")

  # handle_fields.voodoom exercises real-mojom-parity phase 9's
  # handle-bearing struct/union/array fields and Chromium Name@N() ordinals.
  add_custom_command(
    OUTPUT "${WVC_GEN_HANDLE_FIELDS_H}"
    COMMAND voodoomc
            "${CMAKE_CURRENT_SOURCE_DIR}/examples/handle_fields/handle_fields.voodoom"
            -o "${WVC_GEN_HANDLE_FIELDS_H}"
    DEPENDS voodoomc "${CMAKE_CURRENT_SOURCE_DIR}/examples/handle_fields/handle_fields.voodoom"
    COMMENT "voodoomc: generating handle_fields_gen.h"
    VERBATIM
  )
  add_custom_target(wvc_generate_handle_fields DEPENDS "${WVC_GEN_HANDLE_FIELDS_H}")

  set(WVC_GEN_TIME_MOJOM_H "${WVC_GEN_DIR}/time_mojom_gen.h")
  add_custom_command(
    OUTPUT "${WVC_GEN_TIME_MOJOM_H}"
    COMMAND voodoomc
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/real_mojom/mojo/public/mojom/base/time.mojom"
            -o "${WVC_GEN_TIME_MOJOM_H}"
    DEPENDS voodoomc
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/real_mojom/mojo/public/mojom/base/time.mojom"
    COMMENT "voodoomc: generating time_mojom_gen.h from real Chromium time.mojom"
    VERBATIM
  )
  add_custom_target(wvc_generate_time_mojom DEPENDS "${WVC_GEN_TIME_MOJOM_H}")

  set(WVC_GEN_VALUES_MOJOM_H "${WVC_GEN_DIR}/values_mojom_gen.h")
  add_custom_command(
    OUTPUT "${WVC_GEN_VALUES_MOJOM_H}"
    COMMAND voodoomc
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/real_mojom/mojo/public/mojom/base/values.mojom"
            -o "${WVC_GEN_VALUES_MOJOM_H}"
    DEPENDS voodoomc
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/real_mojom/mojo/public/mojom/base/values.mojom"
    COMMENT "voodoomc: generating values_mojom_gen.h from real Chromium values.mojom"
    VERBATIM
  )
  add_custom_target(wvc_generate_values_mojom DEPENDS "${WVC_GEN_VALUES_MOJOM_H}")

  set(WVC_GEN_TEXT_DIRECTION_H "${WVC_GEN_DIR}/text_direction_mojom_gen.h")
  add_custom_command(
    OUTPUT "${WVC_GEN_TEXT_DIRECTION_H}"
    COMMAND voodoomc
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/real_mojom/mojo/public/mojom/base/text_direction.mojom"
            -o "${WVC_GEN_TEXT_DIRECTION_H}"
    DEPENDS voodoomc
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/real_mojom/mojo/public/mojom/base/text_direction.mojom"
    COMMENT "voodoomc: generating text_direction_mojom_gen.h"
    VERBATIM
  )
  add_custom_target(wvc_generate_text_direction_mojom DEPENDS "${WVC_GEN_TEXT_DIRECTION_H}")

  set(WVC_GEN_GENERIC_PENDING_H "${WVC_GEN_DIR}/generic_pending_receiver_mojom_gen.h")
  add_custom_command(
    OUTPUT "${WVC_GEN_GENERIC_PENDING_H}"
    COMMAND voodoomc
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/real_mojom/mojo/public/mojom/base/generic_pending_receiver.mojom"
            -o "${WVC_GEN_GENERIC_PENDING_H}"
    DEPENDS voodoomc
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/real_mojom/mojo/public/mojom/base/generic_pending_receiver.mojom"
    COMMENT "voodoomc: generating generic_pending_receiver_mojom_gen.h"
    VERBATIM
  )
  add_custom_target(wvc_generate_generic_pending_mojom DEPENDS "${WVC_GEN_GENERIC_PENDING_H}")

  set(WVC_GEN_BATTERY_STATUS_H "${WVC_GEN_DIR}/battery_status_mojom_gen.h")
  add_custom_command(
    OUTPUT "${WVC_GEN_BATTERY_STATUS_H}"
    COMMAND voodoomc
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/real_mojom/services/device/public/mojom/battery_status.mojom"
            -o "${WVC_GEN_BATTERY_STATUS_H}"
    DEPENDS voodoomc
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/real_mojom/services/device/public/mojom/battery_status.mojom"
    COMMENT "voodoomc: generating battery_status_mojom_gen.h"
    VERBATIM
  )
  add_custom_target(wvc_generate_battery_status_mojom DEPENDS "${WVC_GEN_BATTERY_STATUS_H}")

  set(WVC_GEN_GEOPOSITION_H "${WVC_GEN_DIR}/geoposition_mojom_gen.h")
  set(WVC_GEN_TIME_IMPORT_H "${WVC_GEN_DIR}/time_gen.h")
  add_custom_command(
    OUTPUT "${WVC_GEN_GEOPOSITION_H}" "${WVC_GEN_TIME_IMPORT_H}"
    COMMAND voodoomc
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/real_mojom/services/device/public/mojom/geoposition.mojom"
            -o "${WVC_GEN_GEOPOSITION_H}"
            "--out-dir=${WVC_GEN_DIR}"
            "--import-dir=${CMAKE_CURRENT_SOURCE_DIR}/tests/real_mojom"
    DEPENDS voodoomc
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/real_mojom/services/device/public/mojom/geoposition.mojom"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/real_mojom/mojo/public/mojom/base/time.mojom"
    COMMENT "voodoomc: generating geoposition_mojom_gen.h"
    VERBATIM
  )
  add_custom_target(wvc_generate_geoposition_mojom DEPENDS "${WVC_GEN_GEOPOSITION_H}")

  set(WVC_GEN_BATTERY_MONITOR_H "${WVC_GEN_DIR}/battery_monitor_mojom_gen.h")
  set(WVC_GEN_BATTERY_STATUS_IMPORT_H "${WVC_GEN_DIR}/battery_status_gen.h")
  add_custom_command(
    OUTPUT "${WVC_GEN_BATTERY_MONITOR_H}" "${WVC_GEN_BATTERY_STATUS_IMPORT_H}"
    COMMAND voodoomc
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/real_mojom/services/device/public/mojom/battery_monitor.mojom"
            -o "${WVC_GEN_BATTERY_MONITOR_H}"
            "--out-dir=${WVC_GEN_DIR}"
            "--import-dir=${CMAKE_CURRENT_SOURCE_DIR}/tests/real_mojom"
    DEPENDS voodoomc
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/real_mojom/services/device/public/mojom/battery_monitor.mojom"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/real_mojom/services/device/public/mojom/battery_status.mojom"
    COMMENT "voodoomc: generating battery_monitor_mojom_gen.h"
    VERBATIM
  )
  add_custom_target(wvc_generate_battery_monitor_mojom DEPENDS "${WVC_GEN_BATTERY_MONITOR_H}")

  set(WVC_GEN_APPLICATION_STATE_H "${WVC_GEN_DIR}/application_state_mojom_gen.h")
  add_custom_command(
    OUTPUT "${WVC_GEN_APPLICATION_STATE_H}"
    COMMAND voodoomc
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/real_mojom/mojo/public/mojom/base/application_state.mojom"
            -o "${WVC_GEN_APPLICATION_STATE_H}"
    DEPENDS voodoomc
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/real_mojom/mojo/public/mojom/base/application_state.mojom"
    COMMENT "voodoomc: generating application_state_mojom_gen.h"
    VERBATIM
  )
  add_custom_target(wvc_generate_application_state_mojom DEPENDS "${WVC_GEN_APPLICATION_STATE_H}")

  set(WVC_GEN_PROCESS_ID_H "${WVC_GEN_DIR}/process_id_mojom_gen.h")
  add_custom_command(
    OUTPUT "${WVC_GEN_PROCESS_ID_H}"
    COMMAND voodoomc
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/real_mojom/mojo/public/mojom/base/process_id.mojom"
            -o "${WVC_GEN_PROCESS_ID_H}"
    DEPENDS voodoomc
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/real_mojom/mojo/public/mojom/base/process_id.mojom"
    COMMENT "voodoomc: generating process_id_mojom_gen.h"
    VERBATIM
  )
  add_custom_target(wvc_generate_process_id_mojom DEPENDS "${WVC_GEN_PROCESS_ID_H}")

  set(WVC_GEN_READ_ONLY_BUFFER_H "${WVC_GEN_DIR}/read_only_buffer_mojom_gen.h")
  add_custom_command(
    OUTPUT "${WVC_GEN_READ_ONLY_BUFFER_H}"
    COMMAND voodoomc
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/real_mojom/mojo/public/mojom/base/read_only_buffer.mojom"
            -o "${WVC_GEN_READ_ONLY_BUFFER_H}"
    DEPENDS voodoomc
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/real_mojom/mojo/public/mojom/base/read_only_buffer.mojom"
    COMMENT "voodoomc: generating read_only_buffer_mojom_gen.h"
    VERBATIM
  )
  add_custom_target(wvc_generate_read_only_buffer_mojom DEPENDS "${WVC_GEN_READ_ONLY_BUFFER_H}")

  set(WVC_GEN_FILE_PATH_H "${WVC_GEN_DIR}/file_path_mojom_gen.h")
  add_custom_command(
    OUTPUT "${WVC_GEN_FILE_PATH_H}"
    COMMAND voodoomc
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/real_mojom/mojo/public/mojom/base/file_path.mojom"
            -o "${WVC_GEN_FILE_PATH_H}"
            --enable-if=file_path_is_string
    DEPENDS voodoomc
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/real_mojom/mojo/public/mojom/base/file_path.mojom"
    COMMENT "voodoomc: generating file_path_mojom_gen.h"
    VERBATIM
  )
  add_custom_target(wvc_generate_file_path_mojom DEPENDS "${WVC_GEN_FILE_PATH_H}")

  set(WVC_GEN_GENERIC_PENDING_ASSOC_H "${WVC_GEN_DIR}/generic_pending_associated_receiver_mojom_gen.h")
  add_custom_command(
    OUTPUT "${WVC_GEN_GENERIC_PENDING_ASSOC_H}"
    COMMAND voodoomc
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/real_mojom/mojo/public/mojom/base/generic_pending_associated_receiver.mojom"
            -o "${WVC_GEN_GENERIC_PENDING_ASSOC_H}"
    DEPENDS voodoomc
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/real_mojom/mojo/public/mojom/base/generic_pending_associated_receiver.mojom"
    COMMENT "voodoomc: generating generic_pending_associated_receiver_mojom_gen.h"
    VERBATIM
  )
  add_custom_target(wvc_generate_generic_pending_assoc_mojom DEPENDS "${WVC_GEN_GENERIC_PENDING_ASSOC_H}")

  add_executable(wvc_golden_tests
    tests/golden/test_main.cc
    tests/golden/echo_roundtrip.cc
    tests/golden/registry_roundtrip.cc
    tests/golden/settings_roundtrip.cc
    tests/golden/logging_roundtrip.cc
    tests/golden/profile_roundtrip.cc
    tests/golden/telemetry_roundtrip.cc
    tests/golden/calculator_roundtrip.cc
    tests/golden/versioning_roundtrip.cc
    tests/golden/extensibility_roundtrip.cc
    tests/golden/union_versioning_roundtrip.cc
    tests/golden/interface_control_messages_roundtrip.cc
    tests/golden/enum_versioning_roundtrip.cc
    tests/golden/optional_handles_roundtrip.cc
    tests/golden/cadmium_voodoo_roundtrip.cc
    tests/golden/nested_enum_roundtrip.cc
    tests/golden/const_parity_roundtrip.cc
    tests/golden/handle_types_roundtrip.cc
    tests/golden/fixed_array_roundtrip.cc
    tests/golden/result_type_roundtrip.cc
    tests/golden/feature_decl_roundtrip.cc
    tests/golden/handle_fields_roundtrip.cc
    tests/golden/real_mojom_time_compile.cc
    tests/golden/real_mojom_values_compile.cc
    tests/golden/real_mojom_corpus_compile.cc
  )
  add_dependencies(wvc_golden_tests wvc_generate_echo wvc_generate_registry wvc_generate_settings wvc_generate_logging wvc_generate_profile wvc_generate_telemetry wvc_generate_calculator wvc_generate_widget_v1 wvc_generate_widget_v2 wvc_generate_greeter_v1_ext wvc_generate_greeter_v1_closed wvc_generate_greeter_v2 wvc_generate_status_v1_closed wvc_generate_status_v1_ext wvc_generate_status_v2 wvc_generate_color_v1_closed wvc_generate_color_v1_ext wvc_generate_color_v2 wvc_generate_optional_handles wvc_generate_cadmium_voodoo wvc_generate_nested_enum wvc_generate_const_parity wvc_generate_handle_types wvc_generate_fixed_array wvc_generate_result_type wvc_generate_feature_decl wvc_generate_handle_fields wvc_generate_time_mojom wvc_generate_values_mojom wvc_generate_text_direction_mojom wvc_generate_generic_pending_mojom wvc_generate_battery_status_mojom wvc_generate_geoposition_mojom wvc_generate_battery_monitor_mojom wvc_generate_application_state_mojom wvc_generate_process_id_mojom wvc_generate_read_only_buffer_mojom wvc_generate_file_path_mojom wvc_generate_generic_pending_assoc_mojom)
  target_include_directories(wvc_golden_tests PRIVATE
    tests
    tests/golden
    "${WVC_GEN_DIR}"
  )
  target_link_libraries(wvc_golden_tests PRIVATE wcb)
  add_test(NAME wvc_golden_tests COMMAND wvc_golden_tests)
else()
  message(STATUS
    "WASMVoodooCompile: WASMCadidumBindings not found -- "
    "skipping the golden codegen-roundtrip test (voodoomc itself and "
    "wvc_tests still build).")
endif()

# Every sibling pulled in EXCLUDE_FROM_ALL above (wcb -> wck -> wst -> whp,
# wasmsafe, wasmv8bindings) still calls enable_testing()/add_test() in its
# own CMakeLists.txt -- EXCLUDE_FROM_ALL keeps their executables out of the
# default build, but CTest still registers the tests and reports each one
# "Not Run" (could not find executable). These are those siblings' own test
# suites, not voodoomc's; ignore them so a plain `ctest` here reports
# voodoomc's actual two tests (wvc_tests, wvc_golden_tests), not a wall of
# unrelated "Not Run" noise from repos this one only needs as libraries.
# This list is names, not the whp_tests/wst_tests/wck_tests/wcb_tests
# aggregate targets those repos also define -- CTEST_CUSTOM_TESTS_IGNORE
# matches registered *test* names exactly, and those aggregates were never
# what got registered.
file(WRITE "${CMAKE_BINARY_DIR}/CTestCustom.cmake"
     "set(CTEST_CUSTOM_TESTS_IGNORE\n"
     "  wcb_bindings_echo_test\n"
     "  wck_bindings_message_pipe_test\n"
     "  wck_bindings_data_pipe_test\n"
     "  wck_bindings_buffer_test\n"
     "  wck_bindings_invitation_test\n"
     "  wck_bindings_simple_watcher_test\n"
     "  wst_bindings_message_pipe_test\n"
     "  whp_bindings_system_test\n"
     "  cppgc_gc_test\n"
     "  cppgc_allocator_test\n"
     "  cppheap_pointer_table_test\n"
     "  cppgc_sandbox_test\n"
     "  cppgc_jobs_test\n"
     "  v8_facade_console_test\n"
     "  v8_facade_accessor_test\n"
     "  v8_facade_constructor_test\n"
     "  v8_facade_trycatch_test\n"
     "  v8_facade_global_test\n"
     "  v8_facade_inherit_test\n"
     "  sandbox_test\n"
     "  sandboxed_pointer_test\n"
     "  trusted_pointer_table_test\n"
     "  code_pointer_table_test\n"
     "  external_pointer_table_test\n"
     "  sandbox_integration_test\n"
     "  cage_allocator_test\n"
     ")\n")
