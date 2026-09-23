# WASI toolchain for WASMSafeSpace, targeting wasm32-wasip1 -- the actual
# target this whole repo exists for (see the top-level README). Same file
# as ../WASMv8bindings/cmake/wasi-sdk.cmake (that repo's own comment
# explains the wasi-sdk 34 noeh/libc++ fix this reuses verbatim), trimmed
# of the quickjs-ng-specific thread-probing hint since this repo has no
# FetchContent dependency at all.
#
#   cmake -B build-wasi -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/wasi-sdk.cmake [-DWASI_SDK_PATH=...]
#   cmake --build build-wasi
#   wasmtime run build-wasi/sandbox_test.wasm
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
# CMAKE_SYSTEM_NAME WASI isn't a platform CMake ships a Platform/WASI.cmake
# module for, so CMAKE_EXECUTABLE_SUFFIX never gets set automatically --
# set all three forms so whichever one CMake/Ninja actually consults takes.
set(CMAKE_EXECUTABLE_SUFFIX_INIT ".wasm")
set(CMAKE_EXECUTABLE_SUFFIX ".wasm")
set(CMAKE_EXECUTABLE_SUFFIX_C ".wasm")
set(CMAKE_EXECUTABLE_SUFFIX_CXX ".wasm")

# `clang++ --target=wasm32-wasip1` alone resolves <string>/<atomic>/etc.
# against a broken mix of wasi-sdk 34 (LLVM 23)'s no-exceptions ("noeh") and
# full libc++ include directories and defaults to `-fexceptions` even
# though noeh libc++abi has no `__cxa_throw`. Fix (already diagnosed by
# ~/go++ and reused verbatim by ../WASMv8bindings for the same issue): no
# exceptions, wipe the default C++ include path, then add back only noeh
# libc++ followed by wasi-libc, in that order.
set(WASI_SYSROOT "${WASI_SDK_PATH}/share/wasi-sysroot")
set(CMAKE_CXX_FLAGS_INIT
  "-fno-exceptions -nostdinc++ \
-isystem \"${WASI_SYSROOT}/include/wasm32-wasip1/noeh/c++/v1\" \
-isystem \"${WASI_SYSROOT}/include/wasm32-wasip1\" \
-isystem \"${WASI_SYSROOT}/include\""
)

set(WASMSAFESPACE_WASI ON)
