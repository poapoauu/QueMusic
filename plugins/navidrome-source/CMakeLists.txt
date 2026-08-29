add_library(quemusic_navidrome_source MODULE
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
set_target_properties(quemusic_navidrome_source PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/navidrome"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/navidrome"
)
configure_file(
    manifest.json.in
    "${CMAKE_BINARY_DIR}/plugins/navidrome/manifest.json"
    @ONLY
)

if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)

    qt_add_executable(quemusic_navidrome_source_test
        ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/tst_NavidromeSource.cpp
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
