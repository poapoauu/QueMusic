if(NOT EXISTS "${package_dir}/manifest.json")
    message(FATAL_ERROR "Missing bundled plugin manifest: ${package_dir}/manifest.json")
endif()

if(NOT EXISTS "${package_dir}/${library_name}")
    message(FATAL_ERROR "Missing bundled plugin library: ${package_dir}/${library_name}")
endif()

file(GLOB package_entries LIST_DIRECTORIES true RELATIVE "${package_dir}"
    "${package_dir}/*" "${package_dir}/.*")
list(REMOVE_ITEM package_entries "." "..")
list(SORT package_entries)

set(expected_entries "${library_name}" "manifest.json")
list(SORT expected_entries)

if(NOT package_entries STREQUAL expected_entries)
    list(JOIN package_entries ", " package_entries_text)
    message(FATAL_ERROR
        "Unexpected bundled plugin package contents: ${package_entries_text}")
endif()
