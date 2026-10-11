# Plugin-private request layer. No Source package is advertised until its v2
# session and authentication UI are implemented and validated.
add_library(quemusic_kugou_client STATIC KugouApiClient.cpp KugouApiClient.h)
target_compile_features(quemusic_kugou_client PUBLIC cxx_std_17)
set_target_properties(quemusic_kugou_client PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(quemusic_kugou_client PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(quemusic_kugou_client PUBLIC quemusic_source_sdk Qt6::Network)

if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    qt_add_executable(quemusic_kugou_client_test ../../tests/tst_KugouApiClient.cpp)
    target_link_libraries(quemusic_kugou_client_test PRIVATE quemusic_kugou_client Qt6::Test)
    add_test(NAME quemusic_kugou_client_test COMMAND quemusic_kugou_client_test)
endif()
