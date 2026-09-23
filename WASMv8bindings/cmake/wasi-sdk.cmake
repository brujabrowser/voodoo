# WASI toolchain for WASMv8bindings, targeting wasm32-wasip1 -- the actual
# target this whole repo exists for (see the top-level README's "Why
# cppgc, and why not compile V8"). Mirrors the generic toolchain file every
# other WASM* sibling repo uses (e.g. ../WASMExtWrench/cmake/wasi-sdk.cmake),
# trimmed to just this repo's own needs.
#
#   cmake -B build-wasi -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/wasi-sdk.cmake [-DWASI_SDK_PATH=...]
#   cmake --build build-wasi
#   wasmtime build-wasi/cppgc_gc_test.wasm
if(NOT WASI_SDK_PATH)
  if(DEFINED ENV{WASI_SDK_PATH})
    set(WASI_SDK_PATH "$ENV{WASI_SDK_PATH}")
  else()
    set(WASI_SDK_PATH "$ENV{USERPROFILE}/wasi-sdk")
  endif()
endif()
file(TO_CMAKE_PATH "${WASI_SDK_PATH}" WASI_SDK_PATH)
if(NOT EXISTS "${WASI_SDK_PATH}/bin/clang.exe" AND NOT EXISTS "${WASI_SDK_PATH}/bin/clang")
  message(FATAL_ERROR "Set WASI_SDK_PATH to a wasi-sdk root (missing clang)")
endif()

set(CMAKE_SYSTEM_NAME WASI)
set(CMAKE_SYSTEM_PROCESSOR wasm32)

if(EXISTS "${WASI_SDK_PATH}/bin/clang.exe")
  set(CMAKE_C_COMPILER "${WASI_SDK_PATH}/bin/clang.exe")
  set(CMAKE_CXX_COMPILER "${WASI_SDK_PATH}/bin/clang++.exe")
  set(CMAKE_AR "${WASI_SDK_PATH}/bin/llvm-ar.exe")
  set(CMAKE_RANLIB "${WASI_SDK_PATH}/bin/llvm-ranlib.exe")
else()
  set(CMAKE_C_COMPILER "${WASI_SDK_PATH}/bin/clang")
  set(CMAKE_CXX_COMPILER "${WASI_SDK_PATH}/bin/clang++")
  set(CMAKE_AR "${WASI_SDK_PATH}/bin/llvm-ar")
  set(CMAKE_RANLIB "${WASI_SDK_PATH}/bin/llvm-ranlib")
endif()

set(CMAKE_C_COMPILER_TARGET wasm32-wasip1)
set(CMAKE_CXX_COMPILER_TARGET wasm32-wasip1)
# CMAKE_SYSTEM_NAME WASI isn't a platform CMake ships a
# Platform/WASI.cmake module for, so the usual place that would set
# CMAKE_EXECUTABLE_SUFFIX from CMAKE_EXECUTABLE_SUFFIX_INIT never runs and
# outputs end up extension-less (still real, valid .wasm files --
# `wasmtime run <file>` doesn't care -- just not matching this repo's own
# documented `wasmtime build-wasi/x.wasm` convention). Set all three forms
# so whichever one CMake/Ninja actually consults on a given CMake version
# takes.
set(CMAKE_EXECUTABLE_SUFFIX_INIT ".wasm")
set(CMAKE_EXECUTABLE_SUFFIX ".wasm")
set(CMAKE_EXECUTABLE_SUFFIX_C ".wasm")
set(CMAKE_EXECUTABLE_SUFFIX_CXX ".wasm")

# wasi-sdk's sysroot has no threads; quickjs-ng's own CMakeLists probes
# find_package(Threads) unconditionally, which fails cross-compiling
# without this hint set to a no-op.
set(CMAKE_THREAD_LIBS_INIT "")
set(CMAKE_HAVE_THREADS_LIBRARY 0)
set(THREADS_PREFER_PTHREAD_FLAG OFF)

# `clang++ --target=wasm32-wasip1` alone resolves <string>/<atomic>/etc.
# against a *mix* of the no-exceptions ("noeh") and full libc++ include
# dirs plus libc++'s own conflicting <ctype.h>, and defaults to
# `-fexceptions` even though noeh libc++abi has no `__cxa_throw` --
# neither compiles. ~/go++ (docs/design-log.md, "Compile to wasm") already
# diagnosed this exact wasi-sdk 34 (LLVM 23) issue for the same reason (its
# own C++-to-wasm pipeline hits it too) and its fix is reused verbatim
# here: no exceptions, wipe the default C++ include path, then add back
# only noeh libc++ followed by wasi-libc, in that order.
set(WASI_SYSROOT "${WASI_SDK_PATH}/share/wasi-sysroot")
set(CMAKE_CXX_FLAGS_INIT
  "-fno-exceptions -nostdinc++ \
-isystem \"${WASI_SYSROOT}/include/wasm32-wasip1/noeh/c++/v1\" \
-isystem \"${WASI_SYSROOT}/include/wasm32-wasip1\" \
-isystem \"${WASI_SYSROOT}/include\""
)

set(WASMV8_WASI ON)
