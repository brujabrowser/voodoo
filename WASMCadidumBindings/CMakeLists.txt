cmake_minimum_required(VERSION 3.16)
project(WASMCadidumBindings LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(WCB_WCK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../WASMCadidumKernel")
if(EXISTS "${WCB_WCK_DIR}/CMakeLists.txt" AND NOT TARGET wck)
  add_subdirectory("${WCB_WCK_DIR}" "${CMAKE_BINARY_DIR}/wck" EXCLUDE_FROM_ALL)
endif()
if(NOT TARGET wck)
  message(FATAL_ERROR
    "WASMCadidumBindings requires the sibling WASMCadidumKernel checkout at "
    "${WCB_WCK_DIR} (it implements mojo/public/cpp/system, which this "
    "repo's mojo/public/cpp/bindings rung is built on).")
endif()

add_library(wcb
  src/message.cc
  src/connector.cc
  src/associated_group.cc
  src/lib/multiplex_router.cc
  src/lib/type_intern.cc
  src/report_bad_message.cc
)

target_include_directories(wcb
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(wcb PUBLIC wck)

if(MSVC)
  target_compile_options(wcb PRIVATE /W4)
else()
  target_compile_options(wcb PRIVATE -Wall -Wextra -Wpedantic)
endif()

enable_testing()

add_executable(wcb_tests
  tests/test_main.cc
  tests/bindings/message_roundtrip.cc
  tests/bindings/connector_roundtrip.cc
  tests/bindings/remote_receiver_roundtrip.cc
  tests/bindings/associated_roundtrip.cc
  tests/bindings/interface_control_messages_test.cc
  tests/bindings/pipe_control_message_test.cc
  tests/bindings/type_intern_test.cc
  tests/bindings/hosting_test.cc
)
target_include_directories(wcb_tests PRIVATE tests examples/echo)
target_link_libraries(wcb_tests PRIVATE wcb)
add_test(NAME wcb_tests COMMAND wcb_tests)
file(WRITE "${CMAKE_BINARY_DIR}/CTestCustom.cmake"
     "set(CTEST_CUSTOM_TESTS_IGNORE whp_tests;wst_tests;wck_tests)\n")

add_executable(wcb_example examples/echo/main.cc)
target_include_directories(wcb_example PRIVATE examples/echo)
target_link_libraries(wcb_example PRIVATE wcb)

# WASMv8bindings sibling: the V8 embedder-API facade that src/bindings/
# exposes Echo (and later Remote/Receiver surfaces) to. wck already pulls
# it in; require the target so a mis-ordered tree fails loudly.
if(NOT TARGET wasmv8_facade)
  set(WCB_WV8_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../WASMv8bindings")
  if(EXISTS "${WCB_WV8_DIR}/CMakeLists.txt")
    add_subdirectory("${WCB_WV8_DIR}" "${CMAKE_BINARY_DIR}/wasmv8" EXCLUDE_FROM_ALL)
  endif()
endif()
if(NOT TARGET wasmv8_facade)
  message(FATAL_ERROR
    "WASMv8bindings sibling checkout required (src/bindings/ exposes "
    "mojo::Remote/Receiver Echo to JS through its facade)")
endif()
if(NOT TARGET wasmv8_sandbox)
  message(FATAL_ERROR
    "WASMv8bindings wasmv8_sandbox required (CHPT type intern for generated "
    ".voodoom types -- Bindings is the base:: rung, intern tags live on CHPT)")
endif()
target_link_libraries(wcb PUBLIC wasmv8_sandbox wasmv8_cppgc)

add_library(wcb_bindings STATIC
  src/bindings/runtime.cc
  src/bindings/echo_bindings.cc
)
target_include_directories(wcb_bindings PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/examples/echo
)
target_link_libraries(wcb_bindings PUBLIC wcb wasmv8_facade)
if(MSVC)
  target_compile_options(wcb_bindings PRIVATE /W4)
else()
  target_compile_options(wcb_bindings PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(wcb_bindings_echo_test tests/bindings/echo_test.cc)
target_link_libraries(wcb_bindings_echo_test PRIVATE wcb_bindings)
add_test(NAME wcb_bindings_echo_test COMMAND wcb_bindings_echo_test)

add_custom_target(wcb_bindings_everything DEPENDS
  wcb_bindings
  wcb_bindings_echo_test
)

# WASI reactor named wcb.wasm
if(CMAKE_SYSTEM_NAME STREQUAL "WASI")
  set(CMAKE_EXECUTABLE_SUFFIX ".wasm")
  if(NOT TARGET wcb_wasm)
    add_executable(wcb_wasm "${CMAKE_CURRENT_SOURCE_DIR}/src/wasi_start.cc")
    target_link_libraries(wcb_wasm PRIVATE "-Wl,--whole-archive" wcb "-Wl,--no-whole-archive")
    target_link_options(wcb_wasm PRIVATE "LINKER:--export-all" "LINKER:--no-entry" "LINKER:--allow-undefined")
    set_target_properties(wcb_wasm PROPERTIES OUTPUT_NAME "wcb")
  endif()
endif()
