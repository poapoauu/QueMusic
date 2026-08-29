add_library(quemusic_test_source MODULE
    TestSourcePlugin.cpp
    TestSourcePlugin.h
    TestSourceSession.cpp
    TestSourceSession.h
    plugin.json
)
target_compile_features(quemusic_test_source PRIVATE cxx_std_17)
target_link_libraries(quemusic_test_source PRIVATE
    Qt6::Core
    Qt6::Network
    quemusic_source_sdk
)
set_target_properties(quemusic_test_source PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/test-source-plugins/primary"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/test-source-plugins/primary"
)

set(QUEMUSIC_TEST_SOURCE_PACKAGE_DIR "${CMAKE_BINARY_DIR}/test-plugin-packages/fixture")
file(MAKE_DIRECTORY "${QUEMUSIC_TEST_SOURCE_PACKAGE_DIR}")
configure_file(
    manifest.json.in
    "${QUEMUSIC_TEST_SOURCE_PACKAGE_DIR}/manifest.json"
    @ONLY
)
add_custom_command(TARGET quemusic_test_source POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:quemusic_test_source>"
        "${QUEMUSIC_TEST_SOURCE_PACKAGE_DIR}/$<TARGET_FILE_NAME:quemusic_test_source>"
)
