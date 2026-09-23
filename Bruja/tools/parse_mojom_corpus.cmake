# Host-parse every Chromium .mojom under CORPUS with voodoomc.
# VOODOOMC, CORPUS, OUT are -D flags from CMakeLists.txt.
if(NOT VOODOOMC OR NOT CORPUS OR NOT OUT)
  message(FATAL_ERROR "parse_mojom_corpus.cmake needs VOODOOMC, CORPUS, OUT")
endif()
file(MAKE_DIRECTORY "${OUT}")
file(GLOB_RECURSE mojoms "${CORPUS}/*.mojom")
list(LENGTH mojoms n)
if(n EQUAL 0)
  message(FATAL_ERROR "no .mojom under ${CORPUS}")
endif()
message(STATUS "voodoomc corpus: ${n} files")
set(ok 0)
set(fail 0)
foreach(f IN LISTS mojoms)
  file(RELATIVE_PATH rel "${CORPUS}" "${f}")
  string(REPLACE "\\" "/" rel "${rel}")
  string(REPLACE "/" "_" stamp "${rel}")
  execute_process(
    COMMAND "${VOODOOMC}" "${f}"
            "--import-dir=${CORPUS}"
            "--out-dir=${OUT}"
    RESULT_VARIABLE rc
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr
  )
  if(rc EQUAL 0)
    math(EXPR ok "${ok}+1")
  else()
    math(EXPR fail "${fail}+1")
    message(STATUS "FAIL ${rel}: ${stderr}")
  endif()
endforeach()
message(STATUS "mojom corpus ok=${ok} fail=${fail} total=${n}")
if(ok EQUAL 0)
  message(FATAL_ERROR "voodoomc parsed none of ${n} Chromium .mojom files")
endif()
