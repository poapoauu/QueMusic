if(NOT EXISTS "${package_dir}/manifest.json")
    message(FATAL_ERROR "Missing bundled plugin manifest: ${package_dir}/manifest.json")
endif()

if(NOT EXISTS "${package_dir}/${library_name}")
    message(FATAL_ERROR "Missing bundled plugin library: ${package_dir}/${library_name}")
endif()
