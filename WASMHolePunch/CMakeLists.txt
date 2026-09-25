cmake_minimum_required(VERSION 3.16)
project(WASMHolePunch LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# WASM sockets are go++ wasigocvm sysroot POSIX sockets — same tcp/udp
# sources as native. Never Bytecode Alliance wasi:sockets / wasi-sdk stub.
set(WHP_NET_SOCKETS src/net/udp_socket.cc src/net/tcp_socket.cc)

# WASMSafeSpace sibling: wasmsafe_sandbox's Sandbox/CageAllocator<T> is
# what src/system/core.cc's MessageObj::bytes (the per-message wire-byte
# buffer) now allocates through, instead of a plain std::vector living in
# ordinary process heap -- see cage-allocator.h's file comment for why a
# handle-table-protected object whose backing bytes are still plain heap
# is only half-protected. Same guarded sibling add_subdirectory
# convention every other repo in this family uses.
set(WHP_WSS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../WASMSafeSpace")
if(NOT TARGET wasmsafe_sandbox AND EXISTS "${WHP_WSS_DIR}/CMakeLists.txt")
  add_subdirectory("${WHP_WSS_DIR}" "${CMAKE_BINARY_DIR}/wasmsafe" EXCLUDE_FROM_ALL)
endif()
if(NOT TARGET wasmsafe_sandbox)
  message(FATAL_ERROR
    "WASMHolePunch requires the sibling WASMSafeSpace checkout at "
    "${WHP_WSS_DIR} (Sandbox/CageAllocator<T> for MessageObj::bytes).")
endif()

add_library(whp
  src/base/executor.cc
  src/base/time.cc
  src/net/sock_internal.cc
  ${WHP_NET_SOCKETS}
  src/net/poll.cc
  src/net/c_net.cc
  src/system/core.cc
  src/punch/punch.cc
  src/platform/invitation.cc
  src/bindings/message.cc
  src/bindings/message_header_validator.cc
)

target_include_directories(whp
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/net
)
target_link_libraries(whp PUBLIC wasmsafe_sandbox)

if(WIN32)
  target_link_libraries(whp PUBLIC ws2_32)
  target_compile_definitions(whp PUBLIC _WIN32_WINNT=0x0601 WIN32_LEAN_AND_MEAN)
endif()

# wasigocvm: mmap is cage linear memory, sysroot minimal-mmap emulation
# (see go++ docs/architecture.md "mmap / getpid").
if(CMAKE_SYSTEM_NAME STREQUAL "WASI")
  target_compile_definitions(whp PUBLIC _WASI_EMULATED_MMAN)
  target_link_libraries(whp PUBLIC wasi-emulated-mman)
endif()

if(MSVC)
  target_compile_options(whp PRIVATE /W4)
else()
  target_compile_options(whp PRIVATE -Wall -Wextra -Wpedantic)
endif()

enable_testing()

add_executable(whp_tests
  tests/test_main.cc
  tests/base/once_callback_and_expected.cc
  tests/net/udp_loopback.cc
  tests/punch/loopback.cc
  tests/system/pipe_ping.cc
  tests/system/platform_handle.cc
  tests/system/shared_memory_region.cc
  tests/system/fuse_and_quota.cc
  tests/system/message_context.cc
  tests/codec/header_sizes.cc
  tests/codec/validator.cc
  tests/bindings/message_roundtrip.cc
  tests/platform/invite_loopback.cc
)
target_include_directories(whp_tests PRIVATE tests)
target_link_libraries(whp_tests PRIVATE whp)
add_test(NAME whp_tests COMMAND whp_tests)

add_executable(whp_example_sockets examples/sockets/main.cc)
target_link_libraries(whp_example_sockets PRIVATE whp)

add_executable(whp_cadmium examples/cadmium/main.cc)
target_link_libraries(whp_cadmium PRIVATE whp)

# WASMv8bindings sibling: the V8 embedder-API facade (Isolate/Context/
# FunctionTemplate, cppgc-backed, JS execution on quickjs-ng via
# WASMSafeSpace) that src/js_bindings/ exposes Whp* to. Guarded sibling
# add_subdirectory, same convention WASMThunker uses.
set(WHP_WV8_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../WASMv8Bindings")
if(NOT EXISTS "${WHP_WV8_DIR}/CMakeLists.txt")
  set(WHP_WV8_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../WASMv8bindings")
endif()
if(NOT TARGET wasmv8_facade AND EXISTS "${WHP_WV8_DIR}/CMakeLists.txt")
  add_subdirectory("${WHP_WV8_DIR}" "${CMAKE_BINARY_DIR}/wasmv8" EXCLUDE_FROM_ALL)
endif()
if(NOT TARGET wasmv8_facade)
  message(FATAL_ERROR
    "WASMv8bindings sibling checkout required at ${WHP_WV8_DIR} "
    "(src/js_bindings/ exposes Whp* to JS through its facade)")
endif()

# mojovm.Call parses the named .mojom. The portfolio is the 9062-row catalog.
set(WVC_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
if(NOT TARGET voodoomc_lib)
  add_library(voodoomc_lib STATIC
    ${WVC_ROOT}/src/lexer.cc
    ${WVC_ROOT}/src/parser.cc
    ${WVC_ROOT}/src/module_loader.cc
    ${WVC_ROOT}/src/cpp_generator.cc
    ${WVC_ROOT}/src/type_intern.cc
    ${WVC_ROOT}/src/mojovm.cc
  )
  target_include_directories(voodoomc_lib PUBLIC ${WVC_ROOT}/src)
endif()

# JS bindings under js_bindings/ (Mojo C++ codec stays in src/bindings/).
add_library(whp_bindings STATIC
  src/js_bindings/runtime.cc
  src/js_bindings/system_bindings.cc
  src/js_bindings/chrome_bindings.cc
  src/js_bindings/mojo_js_bindings.cc
  src/js_bindings/chrome_devtools_bindings.cc
  src/js_bindings/mojo_vm_bindings.cc
  src/js_bindings/chrome_guest.cc
)
target_include_directories(whp_bindings PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${WVC_ROOT}/src
)
target_link_libraries(whp_bindings PUBLIC whp wasmv8_facade voodoomc_lib)
if(MSVC)
  target_compile_options(whp_bindings PRIVATE /W4)
else()
  target_compile_options(whp_bindings PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(whp_bindings_system_test tests/js_bindings/system_test.cc)
target_link_libraries(whp_bindings_system_test PRIVATE whp_bindings)
add_test(NAME whp_bindings_system_test COMMAND whp_bindings_system_test)

add_executable(whp_bindings_chrome_test tests/js_bindings/chrome_test.cc)
target_link_libraries(whp_bindings_chrome_test PRIVATE whp_bindings)
add_test(NAME whp_bindings_chrome_test COMMAND whp_bindings_chrome_test)

add_executable(whp_bindings_chrome_guest_test tests/js_bindings/chrome_guest_test.cc)
target_link_libraries(whp_bindings_chrome_guest_test PRIVATE whp_bindings)
add_test(NAME whp_bindings_chrome_guest_test COMMAND whp_bindings_chrome_guest_test)

set(CES_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../ChromeErrStates")
add_executable(whp_chrome_script
  src/js_bindings/chrome_script.cc
  ${CES_ROOT}/src/catalog/catalog.cc
  ${CES_ROOT}/src/loader/loader.cc
  ${CES_ROOT}/src/deputy/deputy.cc
  ${CES_ROOT}/src/harness/harness.cc
)
target_include_directories(whp_chrome_script PRIVATE "${CES_ROOT}/include")
target_link_libraries(whp_chrome_script PRIVATE whp_bindings)
add_test(NAME whp_chrome_script
  COMMAND whp_chrome_script "${CMAKE_CURRENT_SOURCE_DIR}/tests/js_bindings/guest.script")

add_custom_target(whp_bindings_everything DEPENDS
  whp_bindings
  whp_bindings_system_test
  whp_bindings_chrome_test
  whp_bindings_chrome_guest_test
  whp_chrome_script
)

# holepunch_wasi_reactor — WASI reactor named whp.wasm
if(CMAKE_SYSTEM_NAME STREQUAL "WASI")
  set(CMAKE_EXECUTABLE_SUFFIX ".wasm")
  if(NOT TARGET whp_wasm)
    add_executable(whp_wasm "${CMAKE_CURRENT_SOURCE_DIR}/src/c/wasi_start.cc")
    target_link_libraries(whp_wasm PRIVATE "-Wl,--whole-archive" whp "-Wl,--no-whole-archive")
    target_link_options(whp_wasm PRIVATE "LINKER:--export-all" "LINKER:--no-entry" "LINKER:--allow-undefined")
    set_target_properties(whp_wasm PROPERTIES OUTPUT_NAME "whp")
  endif()
endif()
