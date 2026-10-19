execute_process(COMMAND "${CMAKE_COMMAND}" -S "${FIXTURE}" -B "${BUILD_DIR}"
    -G "${GENERATOR}" "-DCMAKE_MAKE_PROGRAM=${MAKE_PROGRAM}" -DTEST_MISSING_AUTO_INIT=ON
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(result EQUAL 0 OR NOT "${output}${error}" MATCHES "Expected one ESP-Hosted auto-init source")
    message(FATAL_ERROR "Expected the deferred-init guard to reject a missing constructor: ${output}${error}")
endif()
