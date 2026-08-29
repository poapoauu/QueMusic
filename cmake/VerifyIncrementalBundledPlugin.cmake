foreach(required_variable build_dir build_target source_library bundled_library source_manifest bundled_manifest)
    if(NOT DEFINED ${required_variable} OR "${${required_variable}}" STREQUAL "")
        message(FATAL_ERROR "Missing required variable: ${required_variable}")
    endif()
endforeach()

if(NOT EXISTS "${source_library}")
    message(FATAL_ERROR "Missing development plugin library: ${source_library}")
endif()
if(NOT EXISTS "${bundled_library}")
    message(FATAL_ERROR "Missing bundled plugin library: ${bundled_library}")
endif()
if(NOT EXISTS "${source_manifest}")
    message(FATAL_ERROR "Missing development plugin manifest: ${source_manifest}")
endif()
if(NOT EXISTS "${bundled_manifest}")
    message(FATAL_ERROR "Missing bundled plugin manifest: ${bundled_manifest}")
endif()

file(WRITE "${bundled_library}" "stale bundled plugin")
file(REMOVE "${source_library}")

set(build_command
    "${CMAKE_COMMAND}" --build "${build_dir}" --target "${build_target}")
if(DEFINED build_config AND NOT "${build_config}" STREQUAL "")
    list(APPEND build_command --config "${build_config}")
endif()

execute_process(
    COMMAND ${build_command}
    RESULT_VARIABLE build_result
    OUTPUT_VARIABLE build_output
    ERROR_VARIABLE build_error)
if(NOT build_result EQUAL 0)
    message(FATAL_ERROR
        "Plugin-only incremental build failed (${build_result}):\n${build_output}\n${build_error}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E compare_files "${source_library}" "${bundled_library}"
    RESULT_VARIABLE compare_result)
if(NOT compare_result EQUAL 0)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${source_library}" "${bundled_library}")
    message(FATAL_ERROR
        "Plugin-only incremental build left a stale bundled module: ${bundled_library}")
endif()

file(READ "${source_manifest}" original_manifest)
file(WRITE "${source_manifest}" "${original_manifest}\n")

execute_process(
    COMMAND ${build_command}
    RESULT_VARIABLE build_result
    OUTPUT_VARIABLE build_output
    ERROR_VARIABLE build_error)
if(NOT build_result EQUAL 0)
    file(WRITE "${source_manifest}" "${original_manifest}")
    message(FATAL_ERROR
        "Manifest-only incremental build failed (${build_result}):\n${build_output}\n${build_error}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E compare_files "${source_manifest}" "${bundled_manifest}"
    RESULT_VARIABLE compare_result)
if(NOT compare_result EQUAL 0)
    file(WRITE "${source_manifest}" "${original_manifest}")
    message(FATAL_ERROR
        "Plugin-only incremental build left a stale bundled manifest: ${bundled_manifest}")
endif()

file(WRITE "${source_manifest}" "${original_manifest}")
execute_process(
    COMMAND ${build_command}
    RESULT_VARIABLE build_result
    OUTPUT_VARIABLE build_output
    ERROR_VARIABLE build_error)
if(NOT build_result EQUAL 0)
    message(FATAL_ERROR
        "Manifest restore build failed (${build_result}):\n${build_output}\n${build_error}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E compare_files "${source_manifest}" "${bundled_manifest}"
    RESULT_VARIABLE compare_result)
if(NOT compare_result EQUAL 0)
    message(FATAL_ERROR
        "Plugin-only incremental build did not restore the bundled manifest: ${bundled_manifest}")
endif()
