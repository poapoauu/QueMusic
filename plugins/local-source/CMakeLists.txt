add_library(quemusic_local_source MODULE
    LocalSourcePlugin.cpp LocalSourcePlugin.h
    LocalSourceSession.cpp LocalSourceSession.h
    plugin.json)
target_compile_features(quemusic_local_source PRIVATE cxx_std_17)
target_link_libraries(quemusic_local_source PRIVATE
    quemusic_local_library_index quemusic_source_content_events Qt6::Core)
set_target_properties(quemusic_local_source PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/local"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/local")
set(QUEMUSIC_LOCAL_ARCHITECTURE "${CMAKE_SYSTEM_PROCESSOR}")
if(APPLE AND CMAKE_OSX_ARCHITECTURES)
    list(LENGTH CMAKE_OSX_ARCHITECTURES local_architecture_count)
    if(local_architecture_count GREATER 1)
        set(QUEMUSIC_LOCAL_ARCHITECTURE "universal")
    else()
        list(GET CMAKE_OSX_ARCHITECTURES 0 QUEMUSIC_LOCAL_ARCHITECTURE)
    endif()
endif()
string(TOLOWER "${QUEMUSIC_LOCAL_ARCHITECTURE}" QUEMUSIC_LOCAL_ARCHITECTURE)
if(QUEMUSIC_LOCAL_ARCHITECTURE STREQUAL "amd64" OR QUEMUSIC_LOCAL_ARCHITECTURE STREQUAL "x64")
    set(QUEMUSIC_LOCAL_ARCHITECTURE "x86_64")
elseif(QUEMUSIC_LOCAL_ARCHITECTURE STREQUAL "aarch64")
    set(QUEMUSIC_LOCAL_ARCHITECTURE "arm64")
endif()
set(QUEMUSIC_LOCAL_BUILD_KEY "$<IF:$<CONFIG:Debug>,Debug,Release>")
configure_file(manifest.json.in "${CMAKE_CURRENT_BINARY_DIR}/manifest.json.in" @ONLY)
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/plugins/local/manifest.json"
    INPUT "${CMAKE_CURRENT_BINARY_DIR}/manifest.json.in")
