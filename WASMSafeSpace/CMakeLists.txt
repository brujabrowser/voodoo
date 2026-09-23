cmake_minimum_required(VERSION 3.16)
project(WASMSafeSpace LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# wasmsafe_sandbox: a strip-port of real V8's src/sandbox -- the memory cage
# (Sandbox), cage-relative compressed pointers (SandboxedPointer), and the
# three tag-checked indirection tables sandboxed objects use to safely
# reference things outside the cage (TrustedPointerTable, CodePointerTable,
# ExternalPointerTable). See README.md for exactly what's real vs.
# simplified. No sibling checkouts required -- host-buildable like every
# other WASM* repo in this family.
add_library(wasmsafe_sandbox STATIC
  src/sandbox/sandbox.cc
  src/sandbox/trusted-pointer-table.cc
  src/sandbox/code-pointer-table.cc
  src/sandbox/external-pointer-table.cc
  src/sandbox/indirect-pointer-table.cc
)
target_include_directories(wasmsafe_sandbox PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}
)

if(MSVC)
  target_compile_options(wasmsafe_sandbox PRIVATE /W4)
else()
  target_compile_options(wasmsafe_sandbox PRIVATE -Wall -Wextra)
endif()

enable_testing()

add_executable(sandbox_test tests/sandbox_test.cc)
target_link_libraries(sandbox_test PRIVATE wasmsafe_sandbox)
add_test(NAME sandbox_test COMMAND sandbox_test)

add_executable(sandboxed_pointer_test tests/sandboxed_pointer_test.cc)
target_link_libraries(sandboxed_pointer_test PRIVATE wasmsafe_sandbox)
add_test(NAME sandboxed_pointer_test COMMAND sandboxed_pointer_test)

add_executable(trusted_pointer_table_test tests/trusted_pointer_table_test.cc)
target_link_libraries(trusted_pointer_table_test PRIVATE wasmsafe_sandbox)
add_test(NAME trusted_pointer_table_test COMMAND trusted_pointer_table_test)

add_executable(code_pointer_table_test tests/code_pointer_table_test.cc)
target_link_libraries(code_pointer_table_test PRIVATE wasmsafe_sandbox)
add_test(NAME code_pointer_table_test COMMAND code_pointer_table_test)

add_executable(external_pointer_table_test tests/external_pointer_table_test.cc)
target_link_libraries(external_pointer_table_test PRIVATE wasmsafe_sandbox)
add_test(NAME external_pointer_table_test COMMAND external_pointer_table_test)

add_executable(indirect_pointer_table_test tests/indirect_pointer_table_test.cc)
target_link_libraries(indirect_pointer_table_test PRIVATE wasmsafe_sandbox)
add_test(NAME indirect_pointer_table_test COMMAND indirect_pointer_table_test)

add_executable(sandbox_integration_test tests/sandbox_integration_test.cc)
target_link_libraries(sandbox_integration_test PRIVATE wasmsafe_sandbox)
add_test(NAME sandbox_integration_test COMMAND sandbox_integration_test)

add_executable(cage_allocator_test tests/cage_allocator_test.cc)
target_link_libraries(cage_allocator_test PRIVATE wasmsafe_sandbox)
add_test(NAME cage_allocator_test COMMAND cage_allocator_test)

add_executable(sandbox_example examples/sandbox_hello/main.cc)
target_link_libraries(sandbox_example PRIVATE wasmsafe_sandbox)

# Convenience aggregate -- `cmake --build build-wasi --target wasmsafe_everything`
# builds exactly this repo's own libraries/tests/examples.
add_custom_target(wasmsafe_everything DEPENDS
  wasmsafe_sandbox
  sandbox_test sandboxed_pointer_test trusted_pointer_table_test
  code_pointer_table_test external_pointer_table_test indirect_pointer_table_test
  sandbox_integration_test
  cage_allocator_test
  sandbox_example
)
