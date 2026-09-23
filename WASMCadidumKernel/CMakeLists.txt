cmake_minimum_required(VERSION 3.16)
project(WASMCadidumKernel LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(WCK_WST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../WASMThunker")
if(EXISTS "${WCK_WST_DIR}/CMakeLists.txt" AND NOT TARGET wst)
  add_subdirectory("${WCK_WST_DIR}" "${CMAKE_BINARY_DIR}/wst" EXCLUDE_FROM_ALL)
endif()
if(NOT TARGET wst)
  message(FATAL_ERROR
    "WASMCadidumKernel requires the sibling WASMThunker checkout at "
    "${WCK_WST_DIR} (it implements mojo/public/cpp/system on top of wst's "
    "Mojo C ABI).")
endif()

# WASMv8bindings sibling, pulled in before add_library(wck ...) below (not
# just for wck_bindings further down): wasmv8_sandbox's CppHeapPointerTable
# (built on WASMSafeSpace's ExternalEntityTable/Sandbox) is what
# simple_watcher.cc now uses to hand MojoAddTrigger a safe, tag-checked,
# revocable handle instead of a raw `reinterpret_cast<uintptr_t>(this)` --
# a trap event firing for a SimpleWatcher whose Cancel()/destructor already
# ran resolves to a clean nullptr (Get() on a freed/out-of-range handle)
# instead of dereferencing freed memory. See simple_watcher.cc's own
# comment on WatcherHandleTable/WatcherTag for the full story -- this is
# the fix for the wcb_tests heap corruption in
# report_bad_message_closes_the_pipe (RaiseError -> CancelWatchers ->
# trap teardown racing an in-flight OnTrapEvent).
set(WCK_WV8_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../WASMv8bindings")
if(NOT TARGET wasmv8_sandbox AND EXISTS "${WCK_WV8_DIR}/CMakeLists.txt")
  add_subdirectory("${WCK_WV8_DIR}" "${CMAKE_BINARY_DIR}/wasmv8" EXCLUDE_FROM_ALL)
endif()
if(NOT TARGET wasmv8_sandbox)
  message(FATAL_ERROR
    "WASMCadidumKernel requires the sibling WASMv8bindings checkout at "
    "${WCK_WV8_DIR} (wasmv8_sandbox's CppHeapPointerTable -- see "
    "simple_watcher.cc)")
endif()

add_library(wck
  src/message_pipe.cc
  src/simple_watcher.cc
)

target_include_directories(wck
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(wck PUBLIC wst wasmv8_sandbox)

if(MSVC)
  target_compile_options(wck PRIVATE /W4)
else()
  target_compile_options(wck PRIVATE -Wall -Wextra -Wpedantic)
endif()

enable_testing()

add_executable(wck_tests
  tests/test_main.cc
  tests/system/message_pipe_roundtrip.cc
  tests/system/data_pipe_roundtrip.cc
  tests/system/buffer_roundtrip.cc
  tests/system/invitation_roundtrip.cc
  tests/system/simple_watcher_roundtrip.cc
  tests/system/platform_handle_roundtrip.cc
)
target_include_directories(wck_tests PRIVATE tests)
target_link_libraries(wck_tests PRIVATE wck)
add_test(NAME wck_tests COMMAND wck_tests)
file(WRITE "${CMAKE_BINARY_DIR}/CTestCustom.cmake"
     "set(CTEST_CUSTOM_TESTS_IGNORE whp_tests;wst_tests)\n")

add_executable(wck_example examples/roundtrip/main.cc)
target_link_libraries(wck_example PRIVATE wck)

# WASMv8bindings sibling was already add_subdirectory()'d above (for
# wasmv8_sandbox, before add_library(wck ...)) -- this just asserts the
# facade target it also declares is there for wck_bindings below (the V8
# embedder-API facade: Isolate/Context/FunctionTemplate, cppgc-backed, JS
# execution on quickjs-ng via WASMSafeSpace, that src/bindings/ exposes
# every mojo:: API to).
if(NOT TARGET wasmv8_facade)
  message(FATAL_ERROR
    "WASMv8bindings sibling checkout required at ${WCK_WV8_DIR} "
    "(src/bindings/ exposes mojo:: to JS through its facade)")
endif()

# JS bindings: every mojo:: public surface exposed to JS running on the
# WASMv8bindings facade, following WASMExtWrench's wew_bindings idiom.
add_library(wck_bindings STATIC
  src/bindings/runtime.cc
  src/bindings/message_pipe_bindings.cc
  src/bindings/data_pipe_bindings.cc
  src/bindings/buffer_bindings.cc
  src/bindings/invitation_bindings.cc
  src/bindings/simple_watcher_bindings.cc
)
target_include_directories(wck_bindings PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(wck_bindings PUBLIC wck wasmv8_facade)
if(MSVC)
  target_compile_options(wck_bindings PRIVATE /W4)
else()
  target_compile_options(wck_bindings PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(wck_bindings_message_pipe_test tests/bindings/message_pipe_test.cc)
target_link_libraries(wck_bindings_message_pipe_test PRIVATE wck_bindings)
add_test(NAME wck_bindings_message_pipe_test COMMAND wck_bindings_message_pipe_test)

add_executable(wck_bindings_data_pipe_test tests/bindings/data_pipe_test.cc)
target_link_libraries(wck_bindings_data_pipe_test PRIVATE wck_bindings)
add_test(NAME wck_bindings_data_pipe_test COMMAND wck_bindings_data_pipe_test)

add_executable(wck_bindings_buffer_test tests/bindings/buffer_test.cc)
target_link_libraries(wck_bindings_buffer_test PRIVATE wck_bindings)
add_test(NAME wck_bindings_buffer_test COMMAND wck_bindings_buffer_test)

add_executable(wck_bindings_invitation_test tests/bindings/invitation_test.cc)
target_link_libraries(wck_bindings_invitation_test PRIVATE wck_bindings)
add_test(NAME wck_bindings_invitation_test COMMAND wck_bindings_invitation_test)

add_executable(wck_bindings_simple_watcher_test tests/bindings/simple_watcher_test.cc)
target_link_libraries(wck_bindings_simple_watcher_test PRIVATE wck_bindings)
add_test(NAME wck_bindings_simple_watcher_test COMMAND wck_bindings_simple_watcher_test)

add_custom_target(wck_bindings_everything DEPENDS
  wck_bindings
  wck_bindings_message_pipe_test
  wck_bindings_data_pipe_test
  wck_bindings_buffer_test
  wck_bindings_invitation_test
  wck_bindings_simple_watcher_test
)

# WASI reactor named wck.wasm
if(CMAKE_SYSTEM_NAME STREQUAL "WASI")
  set(CMAKE_EXECUTABLE_SUFFIX ".wasm")
  if(NOT TARGET wck_wasm)
    add_executable(wck_wasm "${CMAKE_CURRENT_SOURCE_DIR}/src/wasi_start.cc")
    target_link_libraries(wck_wasm PRIVATE "-Wl,--whole-archive" wck "-Wl,--no-whole-archive")
    target_link_options(wck_wasm PRIVATE "LINKER:--export-all" "LINKER:--no-entry" "LINKER:--allow-undefined")
    set_target_properties(wck_wasm PROPERTIES OUTPUT_NAME "wck")
  endif()
endif()
