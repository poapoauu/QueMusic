cmake_minimum_required(VERSION 3.16)
project(QueMusicExternalPlugin LANGUAGES CXX)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Qml Quick)
find_package(QueMusicPluginSdk 1.0 REQUIRED)

add_library(external_plugin MODULE ExternalPlugin.cpp)
target_compile_features(external_plugin PRIVATE cxx_std_17)
target_link_libraries(external_plugin PRIVATE
    QueMusic::source_sdk QueMusic::plugin_ui_sdk Qt6::Core)
set_target_properties(external_plugin PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/package")

set(EXTERNAL_LIBRARY "$<TARGET_FILE_NAME:external_plugin>")
configure_file(manifest.json.in "${CMAKE_CURRENT_BINARY_DIR}/manifest.json.in" @ONLY)
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/package/manifest.json"
    INPUT "${CMAKE_CURRENT_BINARY_DIR}/manifest.json.in")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/package/qml")
configure_file(ManagementPage.qml
    "${CMAKE_CURRENT_BINARY_DIR}/package/qml/ManagementPage.qml" COPYONLY)
