cmake_minimum_required(VERSION 3.16)
project(Bruja LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(BRUJA_CHROMIUM_SRC "${CMAKE_CURRENT_SOURCE_DIR}/third_party/chromium-src")
file(GLOB_RECURSE BRUJA_MOJOM CONFIGURE_DEPENDS
  "${BRUJA_CHROMIUM_SRC}/*.mojom")
list(LENGTH BRUJA_MOJOM BRUJA_MOJOM_COUNT)
message(STATUS "Bruja chromium mojom corpus: ${BRUJA_MOJOM_COUNT} files")
if(BRUJA_MOJOM_COUNT EQUAL 0)
  message(WARNING
    "No .mojom under third_party/chromium-src. "
    "Run: powershell -File tools/fetch-chromium-mojom.ps1 "
    "(published chromium/src, not a local leftover).")
endif()

set(BRUJA_VOODOOMC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../WASMVoodooCompile")
find_program(BRUJA_VOODOOMC NAMES voodoomc.exe voodoomc
  PATHS
    "${BRUJA_VOODOOMC_DIR}/build"
    "${BRUJA_VOODOOMC_DIR}/build/Release"
    "${BRUJA_VOODOOMC_DIR}/build/Debug")

set(BRUJA_BRUJAC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../WASMBruja")
find_program(BRUJA_BRUJAC NAMES brujac.exe brujac
  PATHS
    "${BRUJA_BRUJAC_DIR}/build"
    "${BRUJA_BRUJAC_DIR}/build/Release"
    "${BRUJA_BRUJAC_DIR}/build/Debug")

set(BRUJA_GOXX_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../go++")
set(BRUJA_COMPILE "${BRUJA_GOXX_DIR}/compile.bat")
set(BRUJA_WASITIME "${BRUJA_GOXX_DIR}/wasitime.bat")
find_program(BRUJA_WASITIME_EXE NAMES wasitime.exe
  PATHS "${BRUJA_GOXX_DIR}/examples/wasitime")

set(BRUJA_GEN "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${BRUJA_GEN}")

# Host IDL: voodoomc (Chromium .mojom + browser.voodoom) and brujac (.bruja).
# Occupancy is go++ wasigocvm (compile.bat → wasitime), not a native leftover.
if(BRUJA_VOODOOMC)
  add_custom_command(
    OUTPUT "${BRUJA_GEN}/browser_mojo.h"
    COMMAND "${BRUJA_VOODOOMC}"
            "${CMAKE_CURRENT_SOURCE_DIR}/idl/browser.voodoom"
            -o "${BRUJA_GEN}/browser_mojo.h"
            "--out-dir=${BRUJA_GEN}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/idl/browser.voodoom"
    COMMENT "voodoomc: idl/browser.voodoom"
    VERBATIM
  )
  set(BRUJA_VOODOO_OUTS "${BRUJA_GEN}/browser_mojo.h")
  if(BRUJA_MOJOM_COUNT GREATER 0)
    add_custom_command(
      OUTPUT "${BRUJA_GEN}/time_mojom.h"
      COMMAND "${BRUJA_VOODOOMC}"
              "${BRUJA_CHROMIUM_SRC}/mojo/public/mojom/base/time.mojom"
              "--import-dir=${BRUJA_CHROMIUM_SRC}"
              -o "${BRUJA_GEN}/time_mojom.h"
              "--out-dir=${BRUJA_GEN}"
      DEPENDS "${BRUJA_CHROMIUM_SRC}/mojo/public/mojom/base/time.mojom"
      COMMENT "voodoomc: published mojo_base.mojom.Time"
      VERBATIM
    )
    add_custom_command(
      OUTPUT "${BRUJA_GEN}/battery_monitor_mojom.h"
      COMMAND "${BRUJA_VOODOOMC}"
              "${BRUJA_CHROMIUM_SRC}/services/device/public/mojom/battery_monitor.mojom"
              "--import-dir=${BRUJA_CHROMIUM_SRC}"
              -o "${BRUJA_GEN}/battery_monitor_mojom.h"
              "--out-dir=${BRUJA_GEN}"
      DEPENDS
        "${BRUJA_CHROMIUM_SRC}/services/device/public/mojom/battery_monitor.mojom"
        "${BRUJA_CHROMIUM_SRC}/services/device/public/mojom/battery_status.mojom"
      COMMENT "voodoomc: published device.mojom.BatteryMonitor"
      VERBATIM
    )
    list(APPEND BRUJA_VOODOO_OUTS
      "${BRUJA_GEN}/time_mojom.h"
      "${BRUJA_GEN}/battery_monitor_mojom.h")
  endif()
  add_custom_target(bruja_voodoo ALL DEPENDS ${BRUJA_VOODOO_OUTS})
endif()

if(BRUJA_BRUJAC)
  add_custom_command(
    OUTPUT "${BRUJA_GEN}/console.goxx"
    COMMAND "${BRUJA_BRUJAC}"
            "${CMAKE_CURRENT_SOURCE_DIR}/idl/console.bruja"
            -o "${BRUJA_GEN}/console.goxx"
            --backend=goxx
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/idl/console.bruja"
    COMMENT "brujac goxx: idl/console.bruja"
    VERBATIM
  )
  add_custom_command(
    OUTPUT "${BRUJA_GEN}/navigator.goxx"
    COMMAND "${BRUJA_BRUJAC}"
            "${CMAKE_CURRENT_SOURCE_DIR}/idl/navigator.bruja"
            -o "${BRUJA_GEN}/navigator.goxx"
            --backend=goxx
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/idl/navigator.bruja"
    COMMENT "brujac goxx: idl/navigator.bruja"
    VERBATIM
  )
  add_custom_target(bruja_idl ALL
    DEPENDS "${BRUJA_GEN}/console.goxx" "${BRUJA_GEN}/navigator.goxx")
endif()

# Product occupancy: sibling go++ compile.bat (-DWASIGO_GOCVM=1) → wasitime.
# Not wasigoc→native exe. Target is opt-in (wasigocvm clang++ is slow).
if(EXISTS "${BRUJA_COMPILE}")
  add_custom_command(
    OUTPUT "${BRUJA_GEN}/bruja.wasm"
    COMMAND "${BRUJA_COMPILE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/occupancy/bruja.go"
            -o "${BRUJA_GEN}/bruja.wasm"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/occupancy/bruja.go"
    COMMENT "wasigocvm: occupancy/bruja.go"
    VERBATIM
  )
  add_custom_target(bruja_occupy DEPENDS "${BRUJA_GEN}/bruja.wasm")
  enable_testing()
  if(BRUJA_WASITIME_EXE)
    add_test(NAME bruja_occupancy
             COMMAND "${BRUJA_WASITIME_EXE}" "${BRUJA_GEN}/bruja.wasm")
  elseif(EXISTS "${BRUJA_WASITIME}")
    add_test(NAME bruja_occupancy
             COMMAND "${BRUJA_WASITIME}" "${BRUJA_GEN}/bruja.wasm")
  endif()
  if(TEST bruja_occupancy)
    set_tests_properties(bruja_occupancy PROPERTIES
      PASS_REGULAR_EXPRESSION "interface:bruja.Browser")
  endif()
endif()

# Occupancy headers from the published chromium/src .mojom corpus.
if(BRUJA_VOODOOMC AND BRUJA_MOJOM_COUNT GREATER 0)
  add_custom_target(bruja_mojom_source
    COMMAND "${CMAKE_COMMAND}"
            -DVOODOOMC=${BRUJA_VOODOOMC}
            -DCORPUS=${BRUJA_CHROMIUM_SRC}
            -DOUT=${BRUJA_GEN}/mojom
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tools/parse_mojom_corpus.cmake"
    COMMENT "voodoomc: all published chromium/src .mojom -> occupancy source"
    VERBATIM)
endif()

# Windowed occupancy: RendererImpl + LocalFrameImpl + FrameWindow, linked
# against sibling wlm. Not WASMv16 bruja_browser.
set(BRUJA_LIME_BUILD "${CMAKE_CURRENT_SOURCE_DIR}/../WASMLime/build")
if(EXISTS "${BRUJA_LIME_BUILD}/CMakeCache.txt")
  add_custom_target(bruja ALL
    COMMAND "${CMAKE_COMMAND}" --build "${BRUJA_LIME_BUILD}" --target bruja
    COMMENT "Bruja occupancy against RendererImpl/LocalFrameImpl/FrameWindow"
    VERBATIM)
endif()

# Parse every published Chromium .mojom through voodoomc (host). One
# --import-dir = chromium-src root so imports keep src-relative paths.
if(BRUJA_VOODOOMC AND BRUJA_MOJOM_COUNT GREATER 0)
  enable_testing()
  add_test(
    NAME bruja_mojom_corpus
    COMMAND ${CMAKE_COMMAND}
            -DVOODOOMC=${BRUJA_VOODOOMC}
            -DCORPUS=${BRUJA_CHROMIUM_SRC}
            -DOUT=${BRUJA_GEN}/mojom_corpus
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tools/parse_mojom_corpus.cmake"
  )
endif()
