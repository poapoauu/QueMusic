add_library(quemusic_navidrome_source MODULE
    NavidromeApiClient.cpp
    NavidromeApiClient.h
    NavidromeMappers.cpp
    NavidromeMappers.h
    NavidromeSourcePlugin.cpp
    NavidromeSourcePlugin.h
    NavidromeSourceSession.cpp
    NavidromeSourceSession.h
    plugin.json
)
target_compile_features(quemusic_navidrome_source PRIVATE cxx_std_17)
target_link_libraries(quemusic_navidrome_source PRIVATE
    Qt6::Core
    Qt6::Network
    quemusic_source_sdk
)
# The root smoke executable compiles the concrete session directly. Add the
# Task 10 mapper implementation after that parent-directory target is declared
# without changing the protected root CMakeLists.txt.
cmake_language(DEFER DIRECTORY "${PROJECT_SOURCE_DIR}" CALL target_sources
    quemusic_navidrome_smoke PRIVATE
    "${PROJECT_SOURCE_DIR}/plugins/navidrome-source/NavidromeMappers.cpp")
set_target_properties(quemusic_navidrome_source PROPERTIES
    # Keep a package directory directly below <build>/plugins so it matches
    # SourceStartup's development search root for both single- and multi-config
    # generators.
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/navidrome"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/navidrome"
)
set(QUEMUSIC_NAVIDROME_ARCHITECTURE "${CMAKE_SYSTEM_PROCESSOR}")
if(APPLE AND CMAKE_OSX_ARCHITECTURES)
    list(LENGTH CMAKE_OSX_ARCHITECTURES navidrome_architecture_count)
    if(navidrome_architecture_count GREATER 1)
        set(QUEMUSIC_NAVIDROME_ARCHITECTURE "universal")
    else()
        list(GET CMAKE_OSX_ARCHITECTURES 0 QUEMUSIC_NAVIDROME_ARCHITECTURE)
    endif()
endif()
string(TOLOWER "${QUEMUSIC_NAVIDROME_ARCHITECTURE}" QUEMUSIC_NAVIDROME_ARCHITECTURE)
if(QUEMUSIC_NAVIDROME_ARCHITECTURE STREQUAL "amd64"
        OR QUEMUSIC_NAVIDROME_ARCHITECTURE STREQUAL "x64")
    set(QUEMUSIC_NAVIDROME_ARCHITECTURE "x86_64")
elseif(QUEMUSIC_NAVIDROME_ARCHITECTURE STREQUAL "aarch64")
    set(QUEMUSIC_NAVIDROME_ARCHITECTURE "arm64")
endif()
set(QUEMUSIC_NAVIDROME_BUILD_KEY "$<IF:$<CONFIG:Debug>,Debug,Release>")
configure_file(
    manifest.json.in
    "${CMAKE_CURRENT_BINARY_DIR}/manifest.json.in"
    @ONLY
)
set(QUEMUSIC_NAVIDROME_MANIFEST
    "${CMAKE_BINARY_DIR}/plugins/navidrome/manifest.json")
file(GENERATE
    OUTPUT "${QUEMUSIC_NAVIDROME_MANIFEST}"
    INPUT "${CMAKE_CURRENT_BINARY_DIR}/manifest.json.in")
set(QUEMUSIC_NAVIDROME_MANIFEST "${QUEMUSIC_NAVIDROME_MANIFEST}" PARENT_SCOPE)

if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)

    qt_add_executable(quemusic_navidrome_source_test
        ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/tst_NavidromeSource.cpp
        NavidromeApiClient.cpp
        NavidromeApiClient.h
        NavidromeMappers.cpp
        NavidromeMappers.h
        NavidromeSourcePlugin.cpp
        NavidromeSourcePlugin.h
        NavidromeSourceSession.cpp
        NavidromeSourceSession.h
        plugin.json
    )
    target_compile_features(quemusic_navidrome_source_test PRIVATE cxx_std_17)
    target_include_directories(quemusic_navidrome_source_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
    )
    target_link_libraries(quemusic_navidrome_source_test PRIVATE
        Qt6::Network
        Qt6::Test
        quemusic_source_sdk
    )
    add_dependencies(quemusic_navidrome_source_test quemusic_navidrome_source)
    add_test(NAME quemusic_navidrome_source_test COMMAND quemusic_navidrome_source_test)
endif()
