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
set(QUEMUSIC_LOCAL_MANIFEST "${CMAKE_BINARY_DIR}/plugins/local/manifest.json")
file(GENERATE OUTPUT "${QUEMUSIC_LOCAL_MANIFEST}"
    INPUT "${CMAKE_CURRENT_BINARY_DIR}/manifest.json.in")
set(QUEMUSIC_LOCAL_MANIFEST "${QUEMUSIC_LOCAL_MANIFEST}" PARENT_SCOPE)

set(QUEMUSIC_LOCAL_INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}/quemusic/plugins/local")
install(TARGETS quemusic_local_source
    LIBRARY DESTINATION "${QUEMUSIC_LOCAL_INSTALL_DIR}" COMPONENT LocalSource
    RUNTIME DESTINATION "${QUEMUSIC_LOCAL_INSTALL_DIR}" COMPONENT LocalSource)
install(FILES "${QUEMUSIC_LOCAL_MANIFEST}"
    DESTINATION "${QUEMUSIC_LOCAL_INSTALL_DIR}" COMPONENT LocalSource)
if(APPLE)
    set_target_properties(quemusic_local_source PROPERTIES
        BUILD_RPATH "@loader_path/../../../Frameworks"
        INSTALL_RPATH "@loader_path/../../..")
elseif(UNIX)
    set_target_properties(quemusic_local_source PROPERTIES
        INSTALL_RPATH "\$ORIGIN/../../..")
elseif(WIN32)
    install(TARGETS quemusic_source_content_events
        RUNTIME DESTINATION "${QUEMUSIC_LOCAL_INSTALL_DIR}" COMPONENT LocalSource)
endif()
