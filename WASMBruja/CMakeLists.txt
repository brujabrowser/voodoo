cmake_minimum_required(VERSION 3.16)
project(WASMBruja LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# brujac is a host-side dev tool (like voodoomc / protoc), not a WASM
# module -- it never runs inside the sandboxed viewer, so it has no WASI
# build and no runtime dependency on any sibling WASM* repo. Only its
# golden test (below) needs quickjs-ng, to prove generated bindings
# actually work against a real JS engine, not just resemble working code.

add_library(brujac_lib
  src/lexer.cc
  src/parser.cc
  src/resolver.cc
  src/cpp_generator.cc
  src/cpp_generator_v8.cc
  src/frontends.cc
  src/goxx_generator.cc
)
target_include_directories(brujac_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

if(MSVC)
  target_compile_options(brujac_lib PRIVATE /W4)
else()
  target_compile_options(brujac_lib PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(brujac src/main.cc)
target_link_libraries(brujac PRIVATE brujac_lib)

# brujac is a host tool (like voodoomc / protoc) -- when the whole tree is
# cross-compiled to WASI (a single global toolchain file, no per-target
# native/host split in CMake), the `brujac` target above becomes a .wasm
# module the host CMake/ninja process cannot execute. Same WASI/host split
# WASMBlinker's/WASMLime's own CMakeLists use for voodoomc, for the same
# reason -- despite the "no WASI build" comment above, add_subdirectory
# from a WASI-targeted parent (WASMv16, WASMLime) still compiles this
# target with the parent's cross toolchain unless we route codegen
# elsewhere.
set(WB_BRUJAC brujac)
set(WB_BRUJAC_DEP brujac)
if(CMAKE_SYSTEM_NAME STREQUAL "WASI")
  if(NOT BRUJAC_HOST)
    find_program(BRUJAC_HOST NAMES brujac brujac.exe
      HINTS
        "${CMAKE_CURRENT_SOURCE_DIR}/build"
        "${CMAKE_CURRENT_SOURCE_DIR}/../WASMv16/brujac"
        "${CMAKE_CURRENT_SOURCE_DIR}/../WASMv16/build/brujac"
    )
  endif()
  if(NOT BRUJAC_HOST)
    message(FATAL_ERROR
      "WASI build needs a native brujac. Build ${CMAKE_CURRENT_SOURCE_DIR} "
      "natively, then reconfigure with -DBRUJAC_HOST=/path/to/brujac.exe")
  endif()
  set(WB_BRUJAC "${BRUJAC_HOST}")
  set(WB_BRUJAC_DEP "${BRUJAC_HOST}")
  message(STATUS "WASMBruja WASI: host brujac = ${BRUJAC_HOST}")
endif()

enable_testing()

add_executable(objc_frontend_test tests/golden/objc_frontend_test.cc)
target_link_libraries(objc_frontend_test PRIVATE brujac_lib)
add_test(NAME objc_frontend_test
         COMMAND objc_frontend_test
                 "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/WKWebView.slice.h")

add_executable(messages_in_frontend_test tests/golden/messages_in_frontend_test.cc)
target_link_libraries(messages_in_frontend_test PRIVATE brujac_lib)
add_test(NAME messages_in_frontend_test
         COMMAND messages_in_frontend_test
                 "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/WebPage.slice.messages.in")

add_executable(ts_goxx_frontend_test tests/golden/ts_goxx_frontend_test.cc)
target_link_libraries(ts_goxx_frontend_test PRIVATE brujac_lib)
add_test(NAME ts_goxx_frontend_test
         COMMAND ts_goxx_frontend_test
                 "${CMAKE_CURRENT_SOURCE_DIR}/examples/goxx/hello.ts"
                 "${CMAKE_CURRENT_SOURCE_DIR}/examples/console/console.bruja")

# Golden roundtrips fetch quickjs-ng. brujac itself does not link it.
# -DBRUJA_FETCH_QUICKJS=OFF is a compiler-only configure (no network).
option(BRUJA_FETCH_QUICKJS
       "Fetch quickjs-ng and build the quickjs golden tests" ON)
if(BRUJA_FETCH_QUICKJS)
  include(FetchContent)
  set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
  set(QJS_BUILD_LIBC ON CACHE BOOL "" FORCE)
  FetchContent_Declare(quickjs
    GIT_REPOSITORY https://github.com/quickjs-ng/quickjs
    GIT_TAG v0.16.2
  )
  FetchContent_MakeAvailable(quickjs)
endif()

# ---- WASMv8bindings sibling: the V8 embedder-API facade the --backend=v8
# golden tests build their generated headers against (same guarded sibling
# add_subdirectory convention every other repo in this family uses). Only
# needed for those tests -- brujac itself has no facade dependency, and
# --backend=quickjs (the default) doesn't need this at all.
set(BRUJA_WV8_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../WASMv8bindings")
if(EXISTS "${BRUJA_WV8_DIR}/CMakeLists.txt" AND NOT TARGET wasmv8_facade)
  add_subdirectory("${BRUJA_WV8_DIR}" "${CMAKE_BINARY_DIR}/wasmv8" EXCLUDE_FROM_ALL)
endif()

# CMAKE_CURRENT_BINARY_DIR (not CMAKE_BINARY_DIR): this is WASMBruja's own
# example/test fixture dir. The top-level CMAKE_BINARY_DIR collided with a
# parent project's own "generated/console_gen.h" (e.g. WASMv16's) when this
# repo is pulled in as a sibling add_subdirectory instead of built
# standalone -- see WASMVoodooCompile's CMakeLists.txt for the same fix and
# fuller explanation.
set(BRUJA_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${BRUJA_GEN_DIR}")
set(BRUJA_GEN_CONSOLE_H "${BRUJA_GEN_DIR}/console_gen.h")
add_custom_command(
  OUTPUT "${BRUJA_GEN_CONSOLE_H}"
  COMMAND ${WB_BRUJAC}
          "${CMAKE_CURRENT_SOURCE_DIR}/examples/console/console.bruja"
          -o "${BRUJA_GEN_CONSOLE_H}"
  DEPENDS ${WB_BRUJAC_DEP} "${CMAKE_CURRENT_SOURCE_DIR}/examples/console/console.bruja"
  COMMENT "brujac: generating console_gen.h"
  VERBATIM
)
add_custom_target(bruja_generate_console DEPENDS "${BRUJA_GEN_CONSOLE_H}")

if(TARGET qjs)
  add_executable(console_roundtrip tests/golden/console_roundtrip.cc)
  add_dependencies(console_roundtrip bruja_generate_console)
  target_include_directories(console_roundtrip PRIVATE "${BRUJA_GEN_DIR}")
  target_link_libraries(console_roundtrip PRIVATE qjs)
  add_test(NAME console_roundtrip COMMAND console_roundtrip)
endif()

# ---- Same interface, --backend=v8: proves brujac's V8 backend generates a
# real, callable WASMv8bindings binding, not just a quickjs one.
if(TARGET wasmv8_facade)
  set(BRUJA_GEN_CONSOLE_V8_H "${BRUJA_GEN_DIR}/console_v8_gen.h")
  add_custom_command(
    OUTPUT "${BRUJA_GEN_CONSOLE_V8_H}"
    COMMAND ${WB_BRUJAC}
            "${CMAKE_CURRENT_SOURCE_DIR}/examples/console/console.bruja"
            -o "${BRUJA_GEN_CONSOLE_V8_H}"
            --backend=v8
    DEPENDS ${WB_BRUJAC_DEP} "${CMAKE_CURRENT_SOURCE_DIR}/examples/console/console.bruja"
    COMMENT "brujac: generating console_v8_gen.h"
    VERBATIM
  )
  add_custom_target(bruja_generate_console_v8 DEPENDS "${BRUJA_GEN_CONSOLE_V8_H}")

  add_executable(console_v8_roundtrip tests/golden/console_v8_roundtrip.cc)
  add_dependencies(console_v8_roundtrip bruja_generate_console_v8)
  target_include_directories(console_v8_roundtrip PRIVATE "${BRUJA_GEN_DIR}")
  target_link_libraries(console_v8_roundtrip PRIVATE wasmv8_facade)
  add_test(NAME console_v8_roundtrip COMMAND console_v8_roundtrip)

  # ---- Regression test: two independent, concurrently-live v8::Isolates
  # each with their own Console binding -- see the test file's header
  # comment for the real per-isolate ObjectTemplate bug this caught.
  add_executable(console_v8_multi_isolate_roundtrip
                 tests/golden/console_v8_multi_isolate_roundtrip.cc)
  add_dependencies(console_v8_multi_isolate_roundtrip bruja_generate_console_v8)
  target_include_directories(console_v8_multi_isolate_roundtrip PRIVATE "${BRUJA_GEN_DIR}")
  target_link_libraries(console_v8_multi_isolate_roundtrip PRIVATE wasmv8_facade)
  add_test(NAME console_v8_multi_isolate_roundtrip COMMAND console_v8_multi_isolate_roundtrip)
endif()

# ---- Golden test: same idea, for v2's attributes (readonly getter-only,
# and a read-write getter+setter pair).
set(BRUJA_GEN_NAVIGATOR_H "${BRUJA_GEN_DIR}/navigator_gen.h")
add_custom_command(
  OUTPUT "${BRUJA_GEN_NAVIGATOR_H}"
  COMMAND ${WB_BRUJAC}
          "${CMAKE_CURRENT_SOURCE_DIR}/examples/navigator/navigator.bruja"
          -o "${BRUJA_GEN_NAVIGATOR_H}"
  DEPENDS ${WB_BRUJAC_DEP} "${CMAKE_CURRENT_SOURCE_DIR}/examples/navigator/navigator.bruja"
  COMMENT "brujac: generating navigator_gen.h"
  VERBATIM
)
add_custom_target(bruja_generate_navigator DEPENDS "${BRUJA_GEN_NAVIGATOR_H}")

if(TARGET qjs)
  add_executable(navigator_roundtrip tests/golden/navigator_roundtrip.cc)
  add_dependencies(navigator_roundtrip bruja_generate_navigator)
  target_include_directories(navigator_roundtrip PRIVATE "${BRUJA_GEN_DIR}")
  target_link_libraries(navigator_roundtrip PRIVATE qjs)
  add_test(NAME navigator_roundtrip COMMAND navigator_roundtrip)
endif()

# ---- Same interface, --backend=v8 -- see console_v8_roundtrip above.
if(TARGET wasmv8_facade)
  set(BRUJA_GEN_NAVIGATOR_V8_H "${BRUJA_GEN_DIR}/navigator_v8_gen.h")
  add_custom_command(
    OUTPUT "${BRUJA_GEN_NAVIGATOR_V8_H}"
    COMMAND ${WB_BRUJAC}
            "${CMAKE_CURRENT_SOURCE_DIR}/examples/navigator/navigator.bruja"
            -o "${BRUJA_GEN_NAVIGATOR_V8_H}"
            --backend=v8
    DEPENDS ${WB_BRUJAC_DEP} "${CMAKE_CURRENT_SOURCE_DIR}/examples/navigator/navigator.bruja"
    COMMENT "brujac: generating navigator_v8_gen.h"
    VERBATIM
  )
  add_custom_target(bruja_generate_navigator_v8 DEPENDS "${BRUJA_GEN_NAVIGATOR_V8_H}")

  add_executable(navigator_v8_roundtrip tests/golden/navigator_v8_roundtrip.cc)
  add_dependencies(navigator_v8_roundtrip bruja_generate_navigator_v8)
  target_include_directories(navigator_v8_roundtrip PRIVATE "${BRUJA_GEN_DIR}")
  target_link_libraries(navigator_v8_roundtrip PRIVATE wasmv8_facade)
  add_test(NAME navigator_v8_roundtrip COMMAND navigator_v8_roundtrip)
endif()

# ---- Golden test: v3, methods with long/boolean params and returns (not
# just DOMString) -- see processes.bruja's header comment.
set(BRUJA_GEN_PROCESSES_H "${BRUJA_GEN_DIR}/processes_gen.h")
add_custom_command(
  OUTPUT "${BRUJA_GEN_PROCESSES_H}"
  COMMAND ${WB_BRUJAC}
          "${CMAKE_CURRENT_SOURCE_DIR}/examples/processes/processes.bruja"
          -o "${BRUJA_GEN_PROCESSES_H}"
  DEPENDS ${WB_BRUJAC_DEP} "${CMAKE_CURRENT_SOURCE_DIR}/examples/processes/processes.bruja"
  COMMENT "brujac: generating processes_gen.h"
  VERBATIM
)
add_custom_target(bruja_generate_processes DEPENDS "${BRUJA_GEN_PROCESSES_H}")

if(TARGET qjs)
  add_executable(processes_roundtrip tests/golden/processes_roundtrip.cc)
  add_dependencies(processes_roundtrip bruja_generate_processes)
  target_include_directories(processes_roundtrip PRIVATE "${BRUJA_GEN_DIR}")
  target_link_libraries(processes_roundtrip PRIVATE qjs)
  add_test(NAME processes_roundtrip COMMAND processes_roundtrip)
endif()

# ---- Same interface, --backend=v8 -- proves the long/boolean marshaling
# (not just DOMString) works against the V8 facade too. See
# console_v8_roundtrip above.
if(TARGET wasmv8_facade)
  set(BRUJA_GEN_PROCESSES_V8_H "${BRUJA_GEN_DIR}/processes_v8_gen.h")
  add_custom_command(
    OUTPUT "${BRUJA_GEN_PROCESSES_V8_H}"
    COMMAND ${WB_BRUJAC}
            "${CMAKE_CURRENT_SOURCE_DIR}/examples/processes/processes.bruja"
            -o "${BRUJA_GEN_PROCESSES_V8_H}"
            --backend=v8
    DEPENDS ${WB_BRUJAC_DEP} "${CMAKE_CURRENT_SOURCE_DIR}/examples/processes/processes.bruja"
    COMMENT "brujac: generating processes_v8_gen.h"
    VERBATIM
  )
  add_custom_target(bruja_generate_processes_v8 DEPENDS "${BRUJA_GEN_PROCESSES_V8_H}")

  add_executable(processes_v8_roundtrip tests/golden/processes_v8_roundtrip.cc)
  add_dependencies(processes_v8_roundtrip bruja_generate_processes_v8)
  target_include_directories(processes_v8_roundtrip PRIVATE "${BRUJA_GEN_DIR}")
  target_link_libraries(processes_v8_roundtrip PRIVATE wasmv8_facade)
  add_test(NAME processes_v8_roundtrip COMMAND processes_v8_roundtrip)
endif()

# ---- Golden test: the HTML5-parity DOM slice -- interface inheritance,
# nullable/interface/sequence/enum/dictionary/callback types, and
# optional/variadic params, all exercised against real quickjs-ng. See
# examples/dom/dom.bruja's header comment.
set(BRUJA_GEN_DOM_H "${BRUJA_GEN_DIR}/dom_gen.h")
add_custom_command(
  OUTPUT "${BRUJA_GEN_DOM_H}"
  COMMAND ${WB_BRUJAC}
          "${CMAKE_CURRENT_SOURCE_DIR}/examples/dom/dom.bruja"
          -o "${BRUJA_GEN_DOM_H}"
          --namespace=bruja_dom_generated
  DEPENDS ${WB_BRUJAC_DEP} "${CMAKE_CURRENT_SOURCE_DIR}/examples/dom/dom.bruja"
  COMMENT "brujac: generating dom_gen.h"
  VERBATIM
)
add_custom_target(bruja_generate_dom DEPENDS "${BRUJA_GEN_DOM_H}")

if(TARGET qjs)
  add_executable(dom_roundtrip tests/golden/dom_roundtrip.cc)
  add_dependencies(dom_roundtrip bruja_generate_dom)
  target_include_directories(dom_roundtrip PRIVATE "${BRUJA_GEN_DIR}")
  target_link_libraries(dom_roundtrip PRIVATE qjs)
  add_test(NAME dom_roundtrip COMMAND dom_roundtrip)

  # Header-only DOM implementation (include/bruja_dom/dom_impl.h) for an
  # embedder that wants document/window against the quickjs-generated header.
  add_library(bruja_dom INTERFACE)
  add_dependencies(bruja_dom bruja_generate_dom)
  target_include_directories(bruja_dom INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${BRUJA_GEN_DIR}"
  )
  target_link_libraries(bruja_dom INTERFACE qjs)
endif()

# ---- Same DOM slice, --backend=v8 -- the real stress test for interface
# inheritance/interface-typed values/callbacks/dictionaries/sequences/
# unions/Promise<T>/constructors/mixins all at once, against a real
# v8::Isolate. See console_v8_roundtrip's own comment for why this is
# guarded on wasmv8_facade existing.
if(TARGET wasmv8_facade)
  set(BRUJA_GEN_DOM_V8_H "${BRUJA_GEN_DIR}/dom_v8_gen.h")
  add_custom_command(
    OUTPUT "${BRUJA_GEN_DOM_V8_H}"
    COMMAND ${WB_BRUJAC}
            "${CMAKE_CURRENT_SOURCE_DIR}/examples/dom/dom.bruja"
            -o "${BRUJA_GEN_DOM_V8_H}"
            --namespace=bruja_dom_generated
            --backend=v8
    DEPENDS ${WB_BRUJAC_DEP} "${CMAKE_CURRENT_SOURCE_DIR}/examples/dom/dom.bruja"
    COMMENT "brujac: generating dom_v8_gen.h"
    VERBATIM
  )
  add_custom_target(bruja_generate_dom_v8 DEPENDS "${BRUJA_GEN_DOM_V8_H}")

  add_executable(dom_v8_roundtrip tests/golden/dom_v8_roundtrip.cc)
  add_dependencies(dom_v8_roundtrip bruja_generate_dom_v8)
  target_include_directories(dom_v8_roundtrip PRIVATE
    "${BRUJA_GEN_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
  )
  target_link_libraries(dom_v8_roundtrip PRIVATE wasmv8_facade)
  add_test(NAME dom_v8_roundtrip COMMAND dom_v8_roundtrip)

  # ---- bruja_dom_v8: V8-backend counterpart to bruja_dom, for an embedder
  # (WASMBlinker's LocalFrameImpl, once migrated) needing dom_v8_impl.h's
  # real implementation classes against the V8-facade generated header
  # instead of the quickjs one.
  add_library(bruja_dom_v8 INTERFACE)
  add_dependencies(bruja_dom_v8 bruja_generate_dom_v8)
  target_include_directories(bruja_dom_v8 INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${BRUJA_GEN_DIR}"
  )
  target_link_libraries(bruja_dom_v8 INTERFACE wasmv8_facade)
endif()

# ---- Golden test: Cauldron, the greenfield RmlUi IDL (RCSS RegisterProperty
# + Rml::Element SetProperty). See examples/cauldron/cauldron.bruja.
set(BRUJA_GEN_CAULDRON_H "${BRUJA_GEN_DIR}/cauldron_gen.h")
add_custom_command(
  OUTPUT "${BRUJA_GEN_CAULDRON_H}"
  COMMAND ${WB_BRUJAC}
          "${CMAKE_CURRENT_SOURCE_DIR}/examples/cauldron/cauldron.bruja"
          -o "${BRUJA_GEN_CAULDRON_H}"
          --namespace=cauldron_generated
  DEPENDS ${WB_BRUJAC_DEP} "${CMAKE_CURRENT_SOURCE_DIR}/examples/cauldron/cauldron.bruja"
  COMMENT "brujac: generating cauldron_gen.h"
  VERBATIM
)
add_custom_target(bruja_generate_cauldron DEPENDS "${BRUJA_GEN_CAULDRON_H}")

if(TARGET qjs)
  add_executable(cauldron_roundtrip tests/golden/cauldron_roundtrip.cc)
  add_dependencies(cauldron_roundtrip bruja_generate_cauldron)
  target_include_directories(cauldron_roundtrip PRIVATE "${BRUJA_GEN_DIR}")
  target_link_libraries(cauldron_roundtrip PRIVATE qjs)
  add_test(NAME cauldron_roundtrip COMMAND cauldron_roundtrip)
endif()

# ---- Golden test: RmlUi JIT (live document/element + registerElement).
# See examples/rml/rml.bruja.
set(BRUJA_GEN_RML_H "${BRUJA_GEN_DIR}/rml_gen.h")
add_custom_command(
  OUTPUT "${BRUJA_GEN_RML_H}"
  COMMAND ${WB_BRUJAC}
          "${CMAKE_CURRENT_SOURCE_DIR}/examples/rml/rml.bruja"
          -o "${BRUJA_GEN_RML_H}"
          --namespace=rml_generated
  DEPENDS ${WB_BRUJAC_DEP} "${CMAKE_CURRENT_SOURCE_DIR}/examples/rml/rml.bruja"
  COMMENT "brujac: generating rml_gen.h"
  VERBATIM
)
add_custom_target(bruja_generate_rml DEPENDS "${BRUJA_GEN_RML_H}")

if(TARGET qjs)
  add_executable(rml_roundtrip tests/golden/rml_roundtrip.cc)
  add_dependencies(rml_roundtrip bruja_generate_rml)
  target_include_directories(rml_roundtrip PRIVATE "${BRUJA_GEN_DIR}")
  target_link_libraries(rml_roundtrip PRIVATE qjs)
  add_test(NAME rml_roundtrip COMMAND rml_roundtrip)
endif()

# ---- cocoa .mm and WebKit *.messages.in frontends. brujac translates
# those into the existing .bruja IR and reuses --backend=v8 codegen.
# Portable C++ for the extras lives in WASMYetiKernel; these goldens prove
# the JS surface.
if(TARGET wasmv8_facade)
  set(BRUJA_GEN_COCOA_V8_H "${BRUJA_GEN_DIR}/cocoa_v8_gen.h")
  add_custom_command(
    OUTPUT "${BRUJA_GEN_COCOA_V8_H}"
    COMMAND ${WB_BRUJAC}
            "${CMAKE_CURRENT_SOURCE_DIR}/examples/cocoa/cocoa.mm"
            -o "${BRUJA_GEN_COCOA_V8_H}"
            --backend=v8
    DEPENDS ${WB_BRUJAC_DEP} "${CMAKE_CURRENT_SOURCE_DIR}/examples/cocoa/cocoa.mm"
    COMMENT "brujac: generating cocoa_v8_gen.h from cocoa.mm"
    VERBATIM
  )
  add_custom_target(bruja_generate_cocoa_v8 DEPENDS "${BRUJA_GEN_COCOA_V8_H}")

  add_executable(cocoa_v8_roundtrip tests/golden/cocoa_v8_roundtrip.cc)
  add_dependencies(cocoa_v8_roundtrip bruja_generate_cocoa_v8)
  target_include_directories(cocoa_v8_roundtrip PRIVATE
    "${BRUJA_GEN_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
  )
  target_link_libraries(cocoa_v8_roundtrip PRIVATE wasmv8_facade)
  add_test(NAME cocoa_v8_roundtrip COMMAND cocoa_v8_roundtrip)

  set(BRUJA_GEN_ECHO_MESSAGES_V8_H "${BRUJA_GEN_DIR}/echo_messages_v8_gen.h")
  add_custom_command(
    OUTPUT "${BRUJA_GEN_ECHO_MESSAGES_V8_H}"
    COMMAND ${WB_BRUJAC}
            "${CMAKE_CURRENT_SOURCE_DIR}/examples/messages/echo.messages.in"
            -o "${BRUJA_GEN_ECHO_MESSAGES_V8_H}"
            --backend=v8
    DEPENDS ${WB_BRUJAC_DEP} "${CMAKE_CURRENT_SOURCE_DIR}/examples/messages/echo.messages.in"
    COMMENT "brujac: generating echo_messages_v8_gen.h from echo.messages.in"
    VERBATIM
  )
  add_custom_target(bruja_generate_messages_in_v8 DEPENDS
    "${BRUJA_GEN_ECHO_MESSAGES_V8_H}"
  )

  add_executable(messages_in_v8_roundtrip tests/golden/messages_in_v8_roundtrip.cc)
  add_dependencies(messages_in_v8_roundtrip bruja_generate_messages_in_v8)
  target_include_directories(messages_in_v8_roundtrip PRIVATE "${BRUJA_GEN_DIR}")
  target_link_libraries(messages_in_v8_roundtrip PRIVATE wasmv8_facade)
  add_test(NAME messages_in_v8_roundtrip COMMAND messages_in_v8_roundtrip)
endif()

# ---- TypeScript -> Go++ (.goxx). The extension marks converted source;
# handwritten Go++ stays .go. CLI proof that brujac writes the file.
set(BRUJA_GEN_HELLO_GOXX "${BRUJA_GEN_DIR}/hello.goxx")
add_custom_command(
  OUTPUT "${BRUJA_GEN_HELLO_GOXX}"
  COMMAND ${WB_BRUJAC}
          "${CMAKE_CURRENT_SOURCE_DIR}/examples/goxx/hello.ts"
          -o "${BRUJA_GEN_HELLO_GOXX}"
  DEPENDS ${WB_BRUJAC_DEP} "${CMAKE_CURRENT_SOURCE_DIR}/examples/goxx/hello.ts"
  COMMENT "brujac: generating hello.goxx from hello.ts"
  VERBATIM
)
add_custom_target(bruja_generate_goxx DEPENDS "${BRUJA_GEN_HELLO_GOXX}")
add_dependencies(ts_goxx_frontend_test bruja_generate_goxx)
