cmake_minimum_required(VERSION 3.16)

project(QueMusic VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 设置所有配置的输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)  # 可执行文件和Windows DLL
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)  # Linux/macOS 动态库
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)  # 静态库

set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O2")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O2")

add_subdirectory(cmake)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick Network Multimedia Concurrent Sql ShaderTools)
#add_subdirectory(api)
#include_directories(api)

# 独立 Mesh Gradient 组件（AGPL-3.0，衍生自 AMLL）：以独立库 target 聚合
add_subdirectory(meshgradient)

add_library(quemusic_source_sdk STATIC
    sdk/source/IMusicSourceArtworkSession.h
    sdk/source/IMusicSourcePlugin.h
    sdk/source/IMusicSourceSession.h
    sdk/source/SourcePluginContext.h
    sdk/source/SourceTypes.cpp
    sdk/source/SourceTypes.h
)
target_compile_features(quemusic_source_sdk PUBLIC cxx_std_17)
set_target_properties(quemusic_source_sdk PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(quemusic_source_sdk PUBLIC Qt6::Core)
target_include_directories(quemusic_source_sdk PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/source
)

add_library(quemusic_source_manager STATIC
    core/source/SourceManager.cpp
    core/source/SourceManager.h
)
target_compile_features(quemusic_source_manager PUBLIC cxx_std_17)
target_link_libraries(quemusic_source_manager PUBLIC
    quemusic_source_sdk
    Qt6::Network
)
target_include_directories(quemusic_source_manager PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/core/source
)

add_library(quemusic_source_startup STATIC
    core/source/SourceStartup.cpp
    core/source/SourceStartup.h
)
target_compile_features(quemusic_source_startup PUBLIC cxx_std_17)
target_link_libraries(quemusic_source_startup PUBLIC
    quemusic_source_manager
    Qt6::Core
    Qt6::Qml
)
target_include_directories(quemusic_source_startup PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/core/source
)

if(BUILD_TESTING)
    enable_testing()
endif()

add_subdirectory(plugins/navidrome-source)

add_library(quemusic_playback_sdk STATIC
    sdk/playback/IPlaybackEngine.h
    sdk/playback/IPlaybackPlugin.h
    sdk/playback/PlaybackTypes.h
)
target_compile_features(quemusic_playback_sdk PUBLIC cxx_std_17)
set_target_properties(quemusic_playback_sdk PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(quemusic_playback_sdk PUBLIC
    quemusic_source_sdk
    Qt6::Core
)
target_include_directories(quemusic_playback_sdk PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk/playback
)

add_library(quemusic_playback_manager STATIC
    core/playback/PlaybackEngineManager.cpp
    core/playback/PlaybackEngineManager.h
)
target_compile_features(quemusic_playback_manager PUBLIC cxx_std_17)
target_link_libraries(quemusic_playback_manager PUBLIC quemusic_playback_sdk)
target_include_directories(quemusic_playback_manager PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/core/playback
)

if(BUILD_TESTING)
    enable_testing()
    find_package(Qt6 REQUIRED COMPONENTS Test)

    add_subdirectory(plugins/test-source)

    qt_add_executable(quemusic_source_types_test
        tests/tst_SourceTypes.cpp
    )
    target_link_libraries(quemusic_source_types_test PRIVATE
        quemusic_source_sdk
        Qt6::Test
    )
    add_test(NAME quemusic_source_types_test COMMAND quemusic_source_types_test)

    qt_add_executable(quemusic_source_plugin_contract_test
        tests/tst_SourcePluginContract.cpp
    )
    target_link_libraries(quemusic_source_plugin_contract_test PRIVATE
        quemusic_source_sdk
        Qt6::Network
        Qt6::Test
    )
    add_test(NAME quemusic_source_plugin_contract_test COMMAND quemusic_source_plugin_contract_test)

    qt_add_executable(quemusic_playback_plugin_contract_test
        tests/tst_PlaybackPluginContract.cpp
    )
    target_link_libraries(quemusic_playback_plugin_contract_test PRIVATE
        quemusic_playback_manager
        Qt6::Test
    )
    add_test(NAME quemusic_playback_plugin_contract_test COMMAND quemusic_playback_plugin_contract_test)

    function(add_source_plugin_fixture target directory)
        add_library(${target} MODULE tests/fixtures/FakeSourcePlugin.cpp)
        target_compile_features(${target} PRIVATE cxx_std_17)
        target_link_libraries(${target} PRIVATE quemusic_source_sdk)
        set_target_properties(${target} PROPERTIES
            LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/test-source-plugins/${directory}"
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/test-source-plugins/${directory}"
        )
    endfunction()

    add_library(quemusic_frozen_v1_source_sdk STATIC
        tests/fixtures/frozen-v1-sdk/FrozenV1Sdk.cpp
        tests/fixtures/frozen-v1-sdk/IMusicSourcePlugin.h
        tests/fixtures/frozen-v1-sdk/IMusicSourceSession.h
        tests/fixtures/frozen-v1-sdk/SourcePluginContext.h
        tests/fixtures/frozen-v1-sdk/SourceTypes.h
    )
    target_compile_features(quemusic_frozen_v1_source_sdk PUBLIC cxx_std_17)
    target_link_libraries(quemusic_frozen_v1_source_sdk PUBLIC Qt6::Core)
    target_include_directories(quemusic_frozen_v1_source_sdk PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/frozen-v1-sdk
    )
    set_target_properties(quemusic_frozen_v1_source_sdk PROPERTIES
        POSITION_INDEPENDENT_CODE ON
    )

    add_library(quemusic_frozen_v1_source_fixture MODULE
        tests/fixtures/FrozenV1SourcePlugin.cpp
    )
    target_compile_features(quemusic_frozen_v1_source_fixture PRIVATE cxx_std_17)
    target_link_libraries(quemusic_frozen_v1_source_fixture PRIVATE
        quemusic_frozen_v1_source_sdk
    )
    set_target_properties(quemusic_frozen_v1_source_fixture PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/test-source-plugins/frozen-v1"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/test-source-plugins/frozen-v1"
    )

    add_source_plugin_fixture(quemusic_duplicate_source_fixture_one duplicate)
    target_compile_definitions(quemusic_duplicate_source_fixture_one PRIVATE
        QUEMUSIC_FIXTURE_SOURCE_ID="fixture.duplicate"
        QUEMUSIC_FIXTURE_SOURCE_NAME="Duplicate Source"
        QUEMUSIC_FIXTURE_SDK_VERSION="1.0"
    )

    add_source_plugin_fixture(quemusic_duplicate_source_fixture_two duplicate)
    target_compile_definitions(quemusic_duplicate_source_fixture_two PRIVATE
        QUEMUSIC_FIXTURE_SOURCE_ID="fixture.duplicate"
        QUEMUSIC_FIXTURE_SOURCE_NAME="Duplicate Source"
        QUEMUSIC_FIXTURE_SDK_VERSION="1.0"
    )

    add_source_plugin_fixture(quemusic_invalid_valid_fixture invalid)
    target_compile_definitions(quemusic_invalid_valid_fixture PRIVATE
        QUEMUSIC_FIXTURE_SOURCE_ID="fixture.valid"
        QUEMUSIC_FIXTURE_SOURCE_NAME="Fixture Source"
        QUEMUSIC_FIXTURE_SDK_VERSION="1.0"
    )

    add_source_plugin_fixture(quemusic_empty_id_source_fixture invalid)
    target_compile_definitions(quemusic_empty_id_source_fixture PRIVATE
        QUEMUSIC_FIXTURE_SOURCE_ID=""
        QUEMUSIC_FIXTURE_SOURCE_NAME="Empty ID Source"
        QUEMUSIC_FIXTURE_SDK_VERSION="1.0"
    )

    add_source_plugin_fixture(quemusic_empty_sdk_source_fixture invalid)
    target_compile_definitions(quemusic_empty_sdk_source_fixture PRIVATE
        QUEMUSIC_FIXTURE_SOURCE_ID="fixture.empty-sdk"
        QUEMUSIC_FIXTURE_SOURCE_NAME="Empty SDK Source"
        QUEMUSIC_FIXTURE_SDK_VERSION=""
    )

    add_source_plugin_fixture(quemusic_missing_name_source_fixture invalid)
    target_compile_definitions(quemusic_missing_name_source_fixture PRIVATE
        QUEMUSIC_FIXTURE_SOURCE_ID="fixture.missing-name"
        QUEMUSIC_FIXTURE_SOURCE_NAME=""
        QUEMUSIC_FIXTURE_SDK_VERSION="1.0"
    )

    add_source_plugin_fixture(quemusic_initialization_failure_fixture invalid)
    target_compile_definitions(quemusic_initialization_failure_fixture PRIVATE
        QUEMUSIC_FIXTURE_SOURCE_ID="fixture.initialize-failure"
        QUEMUSIC_FIXTURE_SOURCE_NAME="Initialization Failure Source"
        QUEMUSIC_FIXTURE_SDK_VERSION="1.0"
        QUEMUSIC_FIXTURE_INITIALIZE_FAILS
    )

    add_source_plugin_fixture(quemusic_not_source_fixture invalid)
    target_compile_definitions(quemusic_not_source_fixture PRIVATE
        QUEMUSIC_FIXTURE_NOT_SOURCE
    )

    add_source_plugin_fixture(quemusic_incompatible_sdk_source_fixture incompatible)
    target_compile_definitions(quemusic_incompatible_sdk_source_fixture PRIVATE
        QUEMUSIC_FIXTURE_SOURCE_ID="fixture.incompatible-sdk"
        QUEMUSIC_FIXTURE_SOURCE_NAME="Incompatible SDK Source"
        QUEMUSIC_FIXTURE_SDK_VERSION="2.0"
        QUEMUSIC_FIXTURE_INITIALIZE_FAILS
    )

    add_source_plugin_fixture(quemusic_optional_artwork_without_metadata_fixture
                              optional-without-metadata)
    target_compile_definitions(quemusic_optional_artwork_without_metadata_fixture PRIVATE
        QUEMUSIC_FIXTURE_SOURCE_ID="fixture.optional-without-metadata"
        QUEMUSIC_FIXTURE_SOURCE_NAME="Optional Artwork Without Metadata"
        QUEMUSIC_FIXTURE_SDK_VERSION="1.0"
        QUEMUSIC_FIXTURE_ARTWORK_INTERFACE
    )

    qt_add_executable(quemusic_source_manager_test
        tests/tst_SourceManager.cpp
    )
    target_compile_features(quemusic_source_manager_test PRIVATE cxx_std_17)
    target_compile_definitions(quemusic_source_manager_test PRIVATE
        QUEMUSIC_TEST_PLUGIN_ROOT="${CMAKE_CURRENT_BINARY_DIR}/test-source-plugins"
        QUEMUSIC_TEST_SOURCE_PLUGIN_DIR="${CMAKE_CURRENT_BINARY_DIR}/plugins/source"
        QUEMUSIC_TEST_FROZEN_V1_PLUGIN_DIR="${CMAKE_CURRENT_BINARY_DIR}/test-source-plugins/frozen-v1"
    )
    target_link_libraries(quemusic_source_manager_test PRIVATE
        quemusic_source_manager
        Qt6::Test
    )
    add_dependencies(quemusic_source_manager_test
        quemusic_test_source
        quemusic_frozen_v1_source_fixture
        quemusic_duplicate_source_fixture_one
        quemusic_duplicate_source_fixture_two
        quemusic_invalid_valid_fixture
        quemusic_empty_id_source_fixture
        quemusic_empty_sdk_source_fixture
        quemusic_missing_name_source_fixture
        quemusic_initialization_failure_fixture
        quemusic_not_source_fixture
        quemusic_incompatible_sdk_source_fixture
        quemusic_optional_artwork_without_metadata_fixture
    )
    add_test(NAME quemusic_source_manager_test COMMAND quemusic_source_manager_test)

    qt_add_executable(quemusic_source_v1_abi_test
        tests/tst_SourceV1Abi.cpp
    )
    target_compile_features(quemusic_source_v1_abi_test PRIVATE cxx_std_17)
    target_compile_definitions(quemusic_source_v1_abi_test PRIVATE
        QUEMUSIC_TEST_FROZEN_V1_PLUGIN_DIR="${CMAKE_CURRENT_BINARY_DIR}/test-source-plugins/frozen-v1"
    )
    target_link_libraries(quemusic_source_v1_abi_test PRIVATE
        quemusic_source_manager
        Qt6::Test
    )
    add_dependencies(quemusic_source_v1_abi_test
        quemusic_frozen_v1_source_fixture
    )
    add_test(NAME quemusic_source_v1_abi_test COMMAND quemusic_source_v1_abi_test)

    qt_add_executable(quemusic_plugin_startup_test
        tests/tst_PluginStartup.cpp
    )
    target_compile_features(quemusic_plugin_startup_test PRIVATE cxx_std_17)
    target_link_libraries(quemusic_plugin_startup_test PRIVATE
        quemusic_source_startup
        Qt6::Qml
        Qt6::Quick
        Qt6::Test
    )
    add_test(NAME quemusic_plugin_startup_test COMMAND quemusic_plugin_startup_test)
endif()

qt_add_executable(${PROJECT_NAME}
    main.cpp
    $<$<PLATFORM_ID:Windows>:icon.rc>
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/api
)

# 按平台设置应用类型：Windows 用 GUI 子系统，macOS 生成 .app bundle
if(WIN32)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        WIN32_EXECUTABLE TRUE
    )
elseif(APPLE)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "QueMusic"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.bronekox.quemusic"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    )
    # CI 中由 workflow 生成 icon.icns；本地不存在也不影响配置
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/resources/app/icon.icns")
        set_target_properties(${PROJECT_NAME} PROPERTIES
            MACOSX_BUNDLE_ICON_FILE "icon.icns"
            RESOURCE "${CMAKE_CURRENT_SOURCE_DIR}/resources/app/icon.icns"
        )
    endif()
endif()

set_source_files_properties(
    components/Style.qml
    components/Options.qml
    PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

file(GLOB_RECURSE QML_COMPONENTS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} CONFIGURE_DEPENDS "components/*.qml")


qt6_add_qml_module(${PROJECT_NAME} # 默认导入项目的
    URI QueMusic
    VERSION 1.0
    QML_FILES
        main.qml
        SettingsView.qml
        layout/MainContent.qml
        layout/PlayerControl.qml
        layout/PlayerMaxCenter.qml
        layout/LeftSideBar.qml
        pages/HomePage.qml
        pages/PlaylistPage.qml
        pages/FavouritePage.qml
        pages/FilePage.qml
        pages/DownloadPage.qml
        pages/SearchPage.qml
        api/qrcode.js
        ${QML_COMPONENTS}

    RESOURCES
        resources/window-bar/close.svg resources/window-bar/closed.svg
        resources/window-bar/airplay.svg resources/window-bar/airplayd.svg
        resources/window-bar/maximize.svg resources/window-bar/maximized.svg
        resources/window-bar/minimize.svg resources/window-bar/minimized.svg
        resources/window-bar/restore.svg resources/window-bar/restored.svg
        resources/window-bar/return.svg resources/window-bar/returnd.svg
        resources/window-bar/setting.svg resources/window-bar/settingd.svg
        resources/icon.ico
        resources/loader.gif
        resources/pic/back1.jpg
        resources/pic/back2.jpg
        resources/pic/back3.jpg
        resources/pic/cloudRainbow.png
        resources/app/header.png
        resources/app/rainbowMusicIcon.png
        resources/fonts/feather.ttf
        resources/fonts/poppins.ttf
        resources/app/musicpic.png
        resources/app/cd.png
        resources/app/cdpoint.png
        resources/app/icons/bronekox.jpg
        resources/app/icons/amll.svg
        resources/app/icons/qticon.png
        resources/app/icons/qwk.png
    SOURCES
        cpp/CoverHelper.cpp
        cpp/CoverHelper.h
        cpp/ColorExtractor.cpp
        cpp/ColorExtractor.h
        cpp/GetWave.cpp
        cpp/GetWave.h
        cpp/FolderModel.cpp
        cpp/FolderModel.h
        cpp/DownloadManager.cpp
        cpp/DownloadManager.h
        cpp/DownloadedMusicModel.cpp
        cpp/DownloadedMusicModel.h
        cpp/Favorites.cpp
        cpp/Favorites.h
        cpp/AccountManager.cpp
        cpp/AccountManager.h
        cpp/CustomFlow.cpp
        cpp/CustomFlow.h
        cpp/WindowsSmtcManager.cpp
        cpp/WindowsSmtcManager.h
        cpp/LogManager.cpp
        cpp/LogManager.h
        api/MusicApiService.cpp
        api/MusicApiService.h
        api/OnlineListModel.cpp
        api/OnlineListModel.h
        api/NeteaseCloudApi.cpp
        api/NeteaseCloudApi.h
        api/KugouApi.cpp
        api/KugouApi.h
        api/ApiCommon.h
)

qt6_add_shaders(${PROJECT_NAME} "effectshaders"
    PREFIX
        "/shaders"
    BATCHABLE
    FILES
        resources/app/shaders/lyricfade.frag
        meshgradient/shaders/meshgradient.frag
        meshgradient/shaders/meshgradient.vert
)

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_17)

# QCloudMusicApi：第三方网易云音乐在线接口
set(QCLOUDMUSICAPI_BUILD_TEST OFF CACHE BOOL "" FORCE)
set(QCLOUDMUSICAPI_BUILD_SHARED OFF CACHE BOOL "" FORCE)
add_subdirectory(api/QCloudMusicApi)
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/api/QCloudMusicApi/QCloudMusicApi)

target_link_libraries(${PROJECT_NAME}
    PRIVATE 
        Qt6::Core
        Qt6::Sql
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        Qt6::Network
        Qt6::Multimedia
        Qt6::Concurrent
        QCloudMusicApi
        cryptopp::cryptopp
        quemusic_meshgradientplugin
        quemusic_source_startup
        z
    PUBLIC QWindowKit::Quick
)
# Windows SMTC 互操作使用 RoGetActivationFactory / RoActivateInstance，需要 runtimeobject 导入库；
# 应用身份注册（AUMID 快捷方式/注册表）使用 COM 与注册表 API，需要 ole32/shell32/advapi32。
if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE runtimeobject ole32 shell32 advapi32)
endif()

# macOS 上由 macdeployqt 统一部署，避免与 qt_deploy_runtime 冲突
if(NOT APPLE)
    qt_deploy_runtime(${PROJECT_NAME})
endif()
