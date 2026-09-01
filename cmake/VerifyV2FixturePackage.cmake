foreach(required_variable package_dir library_name expected_architecture)
    if(NOT DEFINED ${required_variable} OR "${${required_variable}}" STREQUAL "")
        message(FATAL_ERROR "${required_variable} is required")
    endif()
endforeach()

set(manifest_path "${package_dir}/manifest.json")
set(library_path "${package_dir}/${library_name}")

if(NOT EXISTS "${manifest_path}")
    message(FATAL_ERROR "Fixture manifest is not beside the module: ${manifest_path}")
endif()
if(NOT EXISTS "${library_path}")
    message(FATAL_ERROR "Fixture module is not beside the manifest: ${library_path}")
endif()

file(READ "${manifest_path}" manifest_contents)

function(read_manifest_string key output_variable)
    string(REGEX MATCH
        "\"${key}\"[ \t\r\n]*:[ \t\r\n]*\"([^\"]+)\""
        match
        "${manifest_contents}"
    )
    if(NOT match)
        message(FATAL_ERROR "Fixture manifest has no string value for ${key}")
    endif()
    set(${output_variable} "${CMAKE_MATCH_1}" PARENT_SCOPE)
endfunction()

read_manifest_string("library" manifest_library)
read_manifest_string("architecture" manifest_architecture)
read_manifest_string("buildKey" manifest_build_key)

if(NOT manifest_library STREQUAL library_name)
    message(FATAL_ERROR
        "Fixture manifest library '${manifest_library}' does not match '${library_name}'")
endif()
if(NOT manifest_architecture STREQUAL expected_architecture)
    message(FATAL_ERROR
        "Fixture manifest architecture '${manifest_architecture}' does not match target '${expected_architecture}'")
endif()

if(build_config STREQUAL "Debug")
    set(expected_build_key "Debug")
else()
    set(expected_build_key "Release")
endif()
if(NOT manifest_build_key STREQUAL expected_build_key)
    message(FATAL_ERROR
        "Fixture manifest buildKey '${manifest_build_key}' does not match configuration '${build_config}' (expected '${expected_build_key}')")
endif()
