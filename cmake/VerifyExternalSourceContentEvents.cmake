foreach(required_variable build_dir fixture_dir qt_dir)
    if(NOT DEFINED ${required_variable} OR "${${required_variable}}" STREQUAL "")
        message(FATAL_ERROR "Missing required variable: ${required_variable}")
    endif()
endforeach()
string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef nonce)
set(work_dir "${build_dir}/external-source-content-events-${nonce}")
set(prefix "${work_dir}/install")
set(external_build "${work_dir}/build")

set(install_command "${CMAKE_COMMAND}" --install "${build_dir}"
    --prefix "${prefix}" --component PluginSdk)
if(DEFINED build_config AND NOT "${build_config}" STREQUAL "")
    list(APPEND install_command --config "${build_config}")
endif()
execute_process(COMMAND ${install_command} RESULT_VARIABLE result
    OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Plugin SDK install failed:\n${output}\n${error}")
endif()

execute_process(COMMAND "${CMAKE_COMMAND}" -S "${fixture_dir}" -B "${external_build}"
    "-DCMAKE_PREFIX_PATH=${prefix}" "-DQt6_DIR=${qt_dir}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "External project configure failed:\n${output}\n${error}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" --build "${external_build}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "External project build failed:\n${output}\n${error}")
endif()
execute_process(COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${external_build}"
    --output-on-failure RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "External contract failed:\n${output}\n${error}")
endif()
