if(NOT DEFINED smoke_app)
    message(FATAL_ERROR "smoke_app is required")
endif()
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env --unset=QUEMUSIC_PHASE6_SMOKE_ROOT
        "${smoke_app}" --phase6-ui-smoke
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 10)
if(NOT result EQUAL 64)
    message(FATAL_ERROR "Unisolated smoke must reject before application startup (result=${result})")
endif()
