cmake_minimum_required(VERSION 3.16)
project(WASMThunker LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(WST_WHP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../WASMHolePunch")
if(EXISTS "${WST_WHP_DIR}/CMakeLists.txt" AND NOT TARGET whp)
  add_subdirectory("${WST_WHP_DIR}" "${CMAKE_BINARY_DIR}/whp" EXCLUDE_FROM_ALL)
endif()
if(NOT TARGET whp)
  message(FATAL_ERROR
    "WASMThunker requires the sibling WASMHolePunch checkout at "
    "${WST_WHP_DIR} (it implements the Mojo C ABI over whp::c).")
endif()

add_library(wst
  src/thunk.cc
)

target_include_directories(wst
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    "${WST_WHP_DIR}/include"
)
target_link_libraries(wst PUBLIC whp)

if(MSVC)
  target_compile_options(wst PRIVATE /W4)
else()
  target_compile_options(wst PRIVATE -Wall -Wextra -Wpedantic)
endif()

enable_testing()

add_executable(wst_tests
  tests/test_main.cc
  tests/core/message_pipe_roundtrip.cc
  tests/core/invitation_loopback.cc
  tests/core/platform_handle.cc
)
target_include_directories(wst_tests PRIVATE tests)
target_link_libraries(wst_tests PRIVATE wst)
add_test(NAME wst_tests COMMAND wst_tests)
file(WRITE "${CMAKE_BINARY_DIR}/CTestCustom.cmake"
     "set(CTEST_CUSTOM_TESTS_IGNORE whp_tests)\n")

add_executable(wst_example examples/roundtrip/main.cc)
target_link_libraries(wst_example PRIVATE wst)

# WASMv8bindings sibling: the V8 embedder-API facade (Isolate/Context/
# FunctionTemplate, cppgc-backed, JS execution on quickjs-ng via
# WASMSafeSpace) that src/bindings/ exposes every Mojo* C API to. Guarded
# sibling add_subdirectory, same convention WASMCadidumKernel uses.
set(WST_WV8_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../WASMv8bindings")
if(NOT TARGET wasmv8_facade AND EXISTS "${WST_WV8_DIR}/CMakeLists.txt")
  add_subdirectory("${WST_WV8_DIR}" "${CMAKE_BINARY_DIR}/wasmv8" EXCLUDE_FROM_ALL)
endif()
if(NOT TARGET wasmv8_facade)
  message(FATAL_ERROR
    "WASMv8bindings sibling checkout required at ${WST_WV8_DIR} "
    "(src/bindings/ exposes Mojo* to JS through its facade)")
endif()

# JS bindings: Mojo C System surface exposed to JS running on the
# WASMv8bindings facade, following WASMCadidumKernel's wck_bindings idiom.
# Namespace is `wst` (not `mojo`) to avoid colliding with CadidumKernel.
add_library(wst_bindings STATIC
  src/bindings/runtime.cc
  src/bindings/message_pipe_bindings.cc
)
target_include_directories(wst_bindings PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(wst_bindings PUBLIC wst wasmv8_facade)
if(MSVC)
  target_compile_options(wst_bindings PRIVATE /W4)
else()
  target_compile_options(wst_bindings PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(wst_bindings_message_pipe_test tests/bindings/message_pipe_test.cc)
target_link_libraries(wst_bindings_message_pipe_test PRIVATE wst_bindings)
add_test(NAME wst_bindings_message_pipe_test COMMAND wst_bindings_message_pipe_test)

add_custom_target(wst_bindings_everything DEPENDS
  wst_bindings
  wst_bindings_message_pipe_test
)

# WASI reactor named wst.wasm
if(CMAKE_SYSTEM_NAME STREQUAL "WASI")
  set(CMAKE_EXECUTABLE_SUFFIX ".wasm")
  if(NOT TARGET wst_wasm)
    add_executable(wst_wasm "${CMAKE_CURRENT_SOURCE_DIR}/src/wasi_start.cc")
    target_link_libraries(wst_wasm PRIVATE "-Wl,--whole-archive" wst "-Wl,--no-whole-archive")
    target_link_options(wst_wasm PRIVATE "LINKER:--export-all" "LINKER:--no-entry" "LINKER:--allow-undefined")
    set_target_properties(wst_wasm PROPERTIES OUTPUT_NAME "wst")
  endif()
endif()
