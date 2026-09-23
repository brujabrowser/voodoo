cmake_minimum_required(VERSION 3.16)
project(WASMv8bindings LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# cppgc (this library): a strip-port of real V8 src/heap/cppgc-internal +
# include/cppgc, atomic/precise/non-caged/single-heap only. See README.md
# for exactly what's real vs. simplified. No sibling checkouts required --
# host-buildable like every other WASM* repo in this family.

add_library(wasmv8_cppgc STATIC
  src/heap/internal/cppgc-logging.cc
  src/heap/internal/free-list.cc
  src/heap/internal/gc-info.cc
  src/heap/internal/gc-info-table.cc
  src/heap/internal/heap.cc
  src/heap/internal/platform.cc
)

# WASMSafeSpace sibling: Sandbox (the cage) + ExternalEntityTable<Entry> (the
# shared freelist/allocation base CppHeapPointerTable now derives from,
# instead of its own ad hoc std::vector+freelist) + TaggedPayload. Guarded
# sibling add_subdirectory, same convention as WASMBlinker/WASMSkia use for
# their own sibling deps.
set(WV8_WSS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../WASMSafeSpace")
if(NOT TARGET wasmsafe_sandbox AND EXISTS "${WV8_WSS_DIR}/CMakeLists.txt")
  add_subdirectory("${WV8_WSS_DIR}" "${CMAKE_BINARY_DIR}/wasmsafe" EXCLUDE_FROM_ALL)
endif()
if(NOT TARGET wasmsafe_sandbox)
  message(FATAL_ERROR "WASMSafeSpace sibling checkout required at ${WV8_WSS_DIR} (Object::Wrap's CppHeapPointerTable and cppgc's NormalPage allocator both now compose with its Sandbox/ExternalEntityTable)")
endif()

# CppHeapPointerTable: the indirect-handle/tag-range mechanism a future
# embedder-API wrapper layer will use to hand out safe cross-boundary
# handles to cppgc objects. Its own library target since it has no
# dependency on wasmv8_cppgc (or vice versa) yet -- see src/sandbox/
# cppheap-pointer-table.h's file comment. Now built on WASMSafeSpace's
# ExternalEntityTable<Entry>/Sandbox rather than a standalone freelist.
add_library(wasmv8_sandbox STATIC
  src/sandbox/cppheap-pointer-table.cc
)
target_include_directories(wasmv8_sandbox PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(wasmv8_sandbox PUBLIC wasmsafe_sandbox)
target_include_directories(wasmv8_cppgc PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}
)
# NormalPage::Create() now optionally allocates page payloads from a
# v8::internal::Sandbox (src/heap/internal/normal-page.h) so cppgc-managed
# objects genuinely live inside the cage when one is active.
target_link_libraries(wasmv8_cppgc PUBLIC wasmsafe_sandbox)

if(MSVC)
  target_compile_options(wasmv8_cppgc PRIVATE /W4)
else()
  target_compile_options(wasmv8_cppgc PRIVATE -Wall -Wextra)
endif()

# ---- V8 embedder-API facade: real V8-shaped types (Isolate, Context,
# HandleScope, Local<T>, FunctionTemplate, ObjectTemplate, Object::Wrap/
# Unwrap) whose actual JS execution runs on quickjs-ng underneath -- the
# same "API-compatible facade over what actually runs on this target"
# pattern WASMSkia/WASMBlinker already use, applied to V8's embedder API
# instead of Skia/blink.mojom. See include/v8.h and src/v8/*.cc file
# comments for the mapping (v8::Isolate ~ JSRuntime*, v8::Context ~
# JSContext*, v8::Local<Value> ~ a ref-counted JSValue). C++ wrapper
# objects exposed to JS are cppgc::GarbageCollected<T> instances (this
# repo's own GC, above), reached from their JS wrapper via the
# CppHeapPointerTable built above -- exactly Blink's real architecture.
include(FetchContent)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(QJS_BUILD_LIBC ON CACHE BOOL "" FORCE)
FetchContent_Declare(quickjs
  GIT_REPOSITORY https://github.com/quickjs-ng/quickjs
  GIT_TAG v0.16.2
)
FetchContent_MakeAvailable(quickjs)

# We only ever link the `qjs` library target. quickjs-ng's own CLI/dev
# targets (its REPL, its bytecode compiler, its own test262/API/regex test
# runners) are irrelevant to us and, for run-test262 specifically, don't
# even compile under wasm32-wasip1 (its multi-threaded worker-agent support
# has no WASI thread primitives to build against -- a real quickjs-ng gap,
# not this repo's). EXCLUDE_FROM_ALL keeps a plain `cmake --build` (no
# explicit target) from building, or on WASI failing on, any of them.
foreach(_qjs_extra_target qjsc qjs_exe run-test262 api-test lre-test function_source)
  if(TARGET ${_qjs_extra_target})
    set_target_properties(${_qjs_extra_target} PROPERTIES EXCLUDE_FROM_ALL TRUE)
  endif()
endforeach()

add_library(wasmv8_facade STATIC
  src/v8/isolate.cc
  src/v8/context.cc
  src/v8/exception.cc
  src/v8/object.cc
  src/v8/template.cc
  src/v8/function.cc
  src/v8/script.cc
  src/v8/primitive.cc
)
target_include_directories(wasmv8_facade PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(wasmv8_facade PUBLIC wasmv8_cppgc wasmv8_sandbox qjs)

enable_testing()

add_executable(cppgc_gc_test tests/cppgc_gc_test.cc)
target_link_libraries(cppgc_gc_test PRIVATE wasmv8_cppgc)
add_test(NAME cppgc_gc_test COMMAND cppgc_gc_test)

add_executable(cppgc_allocator_test tests/cppgc_allocator_test.cc)
target_link_libraries(cppgc_allocator_test PRIVATE wasmv8_cppgc)
add_test(NAME cppgc_allocator_test COMMAND cppgc_allocator_test)

add_executable(cppheap_pointer_table_test tests/cppheap_pointer_table_test.cc)
target_link_libraries(cppheap_pointer_table_test PRIVATE wasmv8_sandbox)
add_test(NAME cppheap_pointer_table_test COMMAND cppheap_pointer_table_test)

add_executable(cppgc_sandbox_test tests/cppgc_sandbox_test.cc)
target_link_libraries(cppgc_sandbox_test PRIVATE wasmv8_cppgc wasmsafe_sandbox)
add_test(NAME cppgc_sandbox_test COMMAND cppgc_sandbox_test)

add_executable(cppgc_jobs_test tests/cppgc_jobs_test.cc)
target_link_libraries(cppgc_jobs_test PRIVATE wasmv8_cppgc)
add_test(NAME cppgc_jobs_test COMMAND cppgc_jobs_test)

add_executable(v8_facade_console_test tests/v8_facade_console_test.cc)
target_link_libraries(v8_facade_console_test PRIVATE wasmv8_facade)
add_test(NAME v8_facade_console_test COMMAND v8_facade_console_test)

add_executable(v8_facade_accessor_test tests/v8_facade_accessor_test.cc)
target_link_libraries(v8_facade_accessor_test PRIVATE wasmv8_facade)
add_test(NAME v8_facade_accessor_test COMMAND v8_facade_accessor_test)

add_executable(v8_facade_constructor_test tests/v8_facade_constructor_test.cc)
target_link_libraries(v8_facade_constructor_test PRIVATE wasmv8_facade)
add_test(NAME v8_facade_constructor_test COMMAND v8_facade_constructor_test)

add_executable(v8_facade_trycatch_test tests/v8_facade_trycatch_test.cc)
target_link_libraries(v8_facade_trycatch_test PRIVATE wasmv8_facade)
add_test(NAME v8_facade_trycatch_test COMMAND v8_facade_trycatch_test)

add_executable(v8_facade_global_test tests/v8_facade_global_test.cc)
target_link_libraries(v8_facade_global_test PRIVATE wasmv8_facade)
add_test(NAME v8_facade_global_test COMMAND v8_facade_global_test)

add_executable(v8_facade_inherit_test tests/v8_facade_inherit_test.cc)
target_link_libraries(v8_facade_inherit_test PRIVATE wasmv8_facade)
add_test(NAME v8_facade_inherit_test COMMAND v8_facade_inherit_test)

add_executable(cppgc_example examples/cppgc_hello/main.cc)
target_link_libraries(cppgc_example PRIVATE wasmv8_cppgc)

add_executable(v8_example examples/v8_hello/main.cc)
target_link_libraries(v8_example PRIVATE wasmv8_facade)

# Convenience aggregate -- `cmake --build build-wasi --target wasmv8_everything`
# builds exactly this repo's own libraries/tests/examples, nothing from
# quickjs-ng beyond the `qjs` library it already needs.
add_custom_target(wasmv8_everything DEPENDS
  wasmv8_cppgc wasmv8_sandbox wasmv8_facade
  cppgc_gc_test cppgc_allocator_test cppheap_pointer_table_test cppgc_sandbox_test
  cppgc_jobs_test
  v8_facade_console_test v8_facade_accessor_test v8_facade_constructor_test
  v8_facade_trycatch_test v8_facade_global_test v8_facade_inherit_test
  cppgc_example v8_example
)
