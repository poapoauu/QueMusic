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
