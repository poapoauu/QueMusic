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
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# TagLib is vendored so local metadata parsing works consistently on all
# supported platforms without requiring a system package.
set(BUILD_BINDINGS OFF CACHE BOOL "" FORCE)
# TagLib uses BUILD_TESTING as a directory option. Keep it disabled only while
# configuring that dependency; forcing its cache entry would also disable this
# project's PluginCore test targets.
set(_quemusic_build_testing ${BUILD_TESTING})
set(BUILD_TESTING OFF)
# qwindowkit may set BUILD_SHARED_LIBS globally. Keep TagLib static so the
# application bundle does not require a separately deployed libtag dylib.
set(_quemusic_build_shared_libs ${BUILD_SHARED_LIBS})
set(BUILD_SHARED_LIBS OFF)
add_subdirectory(ThirdParty/taglib ${CMAKE_BINARY_DIR}/_deps/taglib-build EXCLUDE_FROM_ALL)
set(BUILD_SHARED_LIBS ${_quemusic_build_shared_libs})
set(BUILD_TESTING ${_quemusic_build_testing})
unset(_quemusic_build_shared_libs)
unset(_quemusic_build_testing)
#add_subdirectory(api)
#include_directories(api)

# 独立 Mesh Gradient 组件（AGPL-3.0，衍生自 AMLL）：以独立库 target 聚合
add_subdirectory(meshgradient)

add_library(quemusic_source_sdk STATIC
    sdk/source/v2/IMusicSourcePluginV2.h
    sdk/source/v2/IMusicSourceSessionV2.h
    sdk/source/v2/ISourceProvidersV2.h
    sdk/source/v2/SourceV2Types.cpp
    sdk/source/v2/SourceV2Types.h
    sdk/source/v2/SourceSecretsV2.cpp
    sdk/source/v2/SourceSecretsV2.h
)
add_library(quemusic_local_media STATIC core/local-media/LocalMediaFiles.cpp core/local-media/LocalMediaFiles.h)
set(QUEMUSIC_LOCAL_TAGLIB_INCLUDES
    ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/taglib/taglib
    ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/taglib/taglib/toolkit
    ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/taglib/taglib/mpeg
    ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/taglib/taglib/mpeg/id3v2
    ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/taglib/taglib/mpeg/id3v2/frames
    ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/taglib/taglib/riff
    ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/taglib/taglib/riff/wav)
target_compile_features(quemusic_local_media PUBLIC cxx_std_17)
set_target_properties(quemusic_local_media PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(quemusic_local_media PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(quemusic_local_media PRIVATE ${QUEMUSIC_LOCAL_TAGLIB_INCLUDES})
target_link_libraries(quemusic_local_media PUBLIC Qt6::Core Qt6::Gui PRIVATE tag)

add_library(quemusic_local_scanner STATIC plugins/local-source/LocalSourceScanner.cpp plugins/local-source/LocalSourceScanner.h)
target_compile_features(quemusic_local_scanner PUBLIC cxx_std_17)
set_target_properties(quemusic_local_scanner PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(quemusic_local_scanner PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(quemusic_local_scanner PRIVATE ${QUEMUSIC_LOCAL_TAGLIB_INCLUDES})
target_link_libraries(quemusic_local_scanner PUBLIC quemusic_source_sdk quemusic_local_media PRIVATE tag)
add_library(quemusic_local_library_index STATIC
    plugins/local-source/LocalLibraryIndex.cpp
    plugins/local-source/LocalLibraryIndex.h
    plugins/local-source/LocalIdentityStore.cpp
    plugins/local-source/LocalIdentityStore.h)
target_compile_features(quemusic_local_library_index PUBLIC cxx_std_17)
set_target_properties(quemusic_local_library_index PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(quemusic_local_library_index PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(quemusic_local_library_index PUBLIC quemusic_local_scanner Qt6::Core)
add_library(quemusic_legacy_local_folder_migration STATIC
    core/migration/LegacyLocalFolderMigration.cpp
    core/migration/LegacyLocalFolderMigration.h)
target_compile_features(quemusic_legacy_local_folder_migration PUBLIC cxx_std_17)
target_include_directories(quemusic_legacy_local_folder_migration PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(quemusic_legacy_local_folder_migration PUBLIC
    quemusic_source_account_store Qt6::Sql)
target_compile_features(quemusic_source_sdk PUBLIC cxx_std_17)
set_target_properties(quemusic_source_sdk PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(quemusic_source_sdk PUBLIC Qt6::Core)
target_include_directories(quemusic_source_sdk PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/sdk/source>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/quemusic/source>
)

# Optional, separately versioned session notifications. The v2 ABI stays frozen.
include(GenerateExportHeader)
set(QUEMUSIC_CONTENT_EVENTS_HEADER_DIR
    "${CMAKE_CURRENT_BINARY_DIR}/generated/source/extensions/content-events/v1")
add_library(quemusic_source_content_events SHARED
    sdk/source/extensions/content-events/v1/ISourceContentEventsProviderV1.h
    sdk/source/extensions/content-events/v1/SourceContentEventsV1.h
    sdk/source/extensions/content-events/v1/SourceContentEventsV1.cpp
)
add_library(QueMusic::source_content_events ALIAS quemusic_source_content_events)
target_compile_features(quemusic_source_content_events PUBLIC cxx_std_17)
target_link_libraries(quemusic_source_content_events PUBLIC quemusic_source_sdk)
target_include_directories(quemusic_source_content_events PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated/source>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/quemusic/source>
)
set_target_properties(quemusic_source_content_events PROPERTIES
    EXPORT_NAME source_content_events VERSION 1.0.0 SOVERSION 1)
generate_export_header(quemusic_source_content_events
    EXPORT_FILE_NAME "${QUEMUSIC_CONTENT_EVENTS_HEADER_DIR}/SourceContentEventsV1Export.h")
install(TARGETS quemusic_source_content_events
    EXPORT QueMusicPluginSdkTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT PluginSdk
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT PluginSdk
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT PluginSdk)
install(DIRECTORY sdk/source/extensions/content-events/v1/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/quemusic/source/extensions/content-events/v1
    COMPONENT PluginSdk FILES_MATCHING PATTERN "*.h")
install(FILES "${QUEMUSIC_CONTENT_EVENTS_HEADER_DIR}/SourceContentEventsV1Export.h"
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/quemusic/source/extensions/content-events/v1
    COMPONENT PluginSdk)

add_library(quemusic_plugin_ui_sdk INTERFACE)
target_compile_features(quemusic_plugin_ui_sdk INTERFACE cxx_std_17)
target_link_libraries(quemusic_plugin_ui_sdk INTERFACE Qt6::Core)
target_include_directories(quemusic_plugin_ui_sdk INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/sdk>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/quemusic>)

set(QUEMUSIC_PLUGIN_UI_QML_FILES
    plugin-ui/qml/PluginPage.qml
    plugin-ui/qml/PluginScrollPage.qml
    plugin-ui/qml/PluginSection.qml
    plugin-ui/qml/PluginGroup.qml
    plugin-ui/qml/PluginLabel.qml
    plugin-ui/qml/PluginDescription.qml
    plugin-ui/qml/PluginSeparator.qml
    plugin-ui/qml/PluginButton.qml
    plugin-ui/qml/PluginPrimaryButton.qml
    plugin-ui/qml/PluginDangerButton.qml
    plugin-ui/qml/PluginIconButton.qml
    plugin-ui/qml/PluginTextField.qml
    plugin-ui/qml/PluginPasswordField.qml
    plugin-ui/qml/PluginNumberField.qml
    plugin-ui/qml/PluginUrlField.qml
    plugin-ui/qml/PluginDirectoryField.qml
    plugin-ui/qml/PluginSwitch.qml
    plugin-ui/qml/PluginCheckBox.qml
    plugin-ui/qml/PluginComboBox.qml
    plugin-ui/qml/PluginStatus.qml
    plugin-ui/qml/PluginBadge.qml
    plugin-ui/qml/PluginBusyIndicator.qml
    plugin-ui/qml/PluginErrorState.qml
    plugin-ui/qml/PluginEmptyState.qml
    plugin-ui/qml/PluginAccountCard.qml
    plugin-ui/qml/PluginServerCard.qml
    plugin-ui/qml/PluginQrCode.qml
    plugin-ui/qml/PluginQrLogin.qml)
list(APPEND QUEMUSIC_PLUGIN_UI_QML_FILES plugin-ui/qml/PluginSettingsForm.qml)
foreach(plugin_ui_qml_file IN LISTS QUEMUSIC_PLUGIN_UI_QML_FILES)
    get_filename_component(plugin_ui_qml_name "${plugin_ui_qml_file}" NAME)
    set_source_files_properties("${plugin_ui_qml_file}" PROPERTIES
        QT_RESOURCE_ALIAS "${plugin_ui_qml_name}")
endforeach()

qt_add_library(quemusic_plugin_ui SHARED)
qt_add_qml_module(quemusic_plugin_ui
    URI QueMusic.PluginUI
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/QueMusic/PluginUI"
    SOURCES
        plugin-ui/PluginUiContext.cpp plugin-ui/PluginUiContext.h
        plugin-ui/PluginTheme.cpp plugin-ui/PluginTheme.h
        plugin-ui/PluginUiSettingsBridge.h
        plugin-ui/PluginUiHostServices.h
    QML_FILES ${QUEMUSIC_PLUGIN_UI_QML_FILES})
target_link_libraries(quemusic_plugin_ui PUBLIC
    quemusic_plugin_ui_sdk Qt6::Core Qt6::Qml Qt6::Quick)
target_include_directories(quemusic_plugin_ui PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/plugin-ui>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/quemusic/runtime>)

set_target_properties(quemusic_source_sdk PROPERTIES EXPORT_NAME source_sdk)
set_target_properties(quemusic_plugin_ui_sdk PROPERTIES EXPORT_NAME plugin_ui_sdk)
set_target_properties(quemusic_plugin_ui PROPERTIES EXPORT_NAME plugin_ui)
install(TARGETS quemusic_source_sdk quemusic_plugin_ui_sdk
        quemusic_plugin_ui
    EXPORT QueMusicPluginSdkTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT PluginSdk
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT PluginSdk
    COMPONENT PluginSdk)
install(TARGETS quemusic_plugin_uiplugin
    LIBRARY DESTINATION "${CMAKE_INSTALL_DATADIR}/quemusic/qml/QueMusic/PluginUI"
        COMPONENT PluginSdk
    RUNTIME DESTINATION "${CMAKE_INSTALL_DATADIR}/quemusic/qml/QueMusic/PluginUI"
        COMPONENT PluginSdk)
if(WIN32)
    # Windows resolves DLL dependencies from the importing module's directory.
    # Keep the exported Host runtime in bin and deploy a second copy beside the
    # QML plugin so a Qt-only process can import the installed module directly.
    install(TARGETS quemusic_plugin_ui
        RUNTIME DESTINATION "${CMAKE_INSTALL_DATADIR}/quemusic/qml/QueMusic/PluginUI"
            COMPONENT PluginSdk)
endif()
if(APPLE)
    set_target_properties(quemusic_plugin_uiplugin PROPERTIES
        INSTALL_RPATH "@loader_path/../../../../../${CMAKE_INSTALL_LIBDIR}")
elseif(UNIX)
    set_target_properties(quemusic_plugin_uiplugin PROPERTIES
        INSTALL_RPATH "\$ORIGIN/../../../../../${CMAKE_INSTALL_LIBDIR}")
endif()
install(DIRECTORY sdk/source/v2/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/quemusic/source/v2
    COMPONENT PluginSdk
    FILES_MATCHING PATTERN "*.h")
install(DIRECTORY sdk/source/extensions/legacy-identity/v1/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/quemusic/source/extensions/legacy-identity/v1
    COMPONENT PluginSdk
    FILES_MATCHING PATTERN "*.h")
install(DIRECTORY sdk/source/extensions/item-lookup/v1/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/quemusic/source/extensions/item-lookup/v1
    COMPONENT PluginSdk
    FILES_MATCHING PATTERN "*.h")
install(DIRECTORY sdk/plugin-ui/v1/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/quemusic/plugin-ui/v1
    COMPONENT PluginSdk
    FILES_MATCHING PATTERN "*.h")
install(FILES plugin-ui/PluginTheme.h plugin-ui/PluginUiContext.h
    plugin-ui/PluginUiSettingsBridge.h plugin-ui/PluginUiHostServices.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/quemusic/runtime
    COMPONENT PluginSdk)
install(DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/QueMusic/PluginUI/"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/quemusic/qml/QueMusic/PluginUI"
    COMPONENT PluginSdk
    FILES_MATCHING PATTERN "*.qml" PATTERN "qmldir" PATTERN "*.qmltypes")
configure_package_config_file(cmake/QueMusicPluginSdkConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/QueMusicPluginSdkConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/QueMusicPluginSdk")
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/QueMusicPluginSdkConfigVersion.cmake"
    VERSION 1.0.0 COMPATIBILITY SameMajorVersion)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/QueMusicPluginSdkConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/QueMusicPluginSdkConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/QueMusicPluginSdk"
    COMPONENT PluginSdk)
install(EXPORT QueMusicPluginSdkTargets NAMESPACE QueMusic::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/QueMusicPluginSdk"
    COMPONENT PluginSdk)

add_library(quemusic_music_page_model STATIC
    core/music/SourceScopeStore.cpp
    core/music/SourceScopeStore.h
    core/music/MusicPageModel.cpp
    core/music/MusicPageModel.h
)
target_compile_features(quemusic_music_page_model PUBLIC cxx_std_17)
target_link_libraries(quemusic_music_page_model PUBLIC quemusic_source_sdk Qt6::Core)
target_include_directories(quemusic_music_page_model PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/core/music
)

add_library(quemusic_plugin_core STATIC
    core/plugins/PluginManifest.cpp
    core/plugins/PluginManifest.h
    core/plugins/PluginManager.cpp
    core/plugins/PluginManager.h
)
target_compile_features(quemusic_plugin_core PUBLIC cxx_std_17)
target_compile_definitions(quemusic_plugin_core PRIVATE
    QUEMUSIC_PLUGIN_BUILD_KEY="$<IF:$<CONFIG:Debug>,Debug,Release>"
)
target_link_libraries(quemusic_plugin_core PUBLIC
    quemusic_source_sdk
    quemusic_plugin_ui_sdk
    Qt6::Core
)
target_include_directories(quemusic_plugin_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/core/plugins
)

add_library(quemusic_source_startup STATIC
    core/source/SourceStartup.cpp
    core/source/SourceStartup.h
)
target_compile_features(quemusic_source_startup PUBLIC cxx_std_17)
target_link_libraries(quemusic_source_startup PUBLIC
    quemusic_plugin_core
    quemusic_plugin_settings
    quemusic_music_hub
    quemusic_playback_coordinator
    quemusic_qt_playback_controller
    Qt6::Core
    Qt6::Qml
)
target_include_directories(quemusic_source_startup PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/core/source
)

add_library(quemusic_source_account_store STATIC
    core/media/SourceAccountStore.cpp
    core/media/SourceAccountStore.h
    core/settings/SourceSettingsValidation.cpp
    core/settings/SourceSettingsValidation.h
    core/media/MacKeychainSecretStore.h
)
target_compile_features(quemusic_source_account_store PUBLIC cxx_std_17)
target_link_libraries(quemusic_source_account_store PUBLIC
    quemusic_source_sdk
    Qt6::Core
)
target_include_directories(quemusic_source_account_store PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/core/media
    ${CMAKE_CURRENT_SOURCE_DIR}/core/settings
)
if(APPLE)
    target_sources(quemusic_source_account_store PRIVATE
        core/media/MacKeychainSecretStore.mm
    )
    target_link_libraries(quemusic_source_account_store PRIVATE "-framework Security")
endif()

add_library(quemusic_source_registry STATIC
    core/source/SourceRegistry.cpp
    core/source/SourceRegistry.h
)
target_compile_features(quemusic_source_registry PUBLIC cxx_std_17)
target_link_libraries(quemusic_source_registry PUBLIC
    quemusic_plugin_core
    quemusic_source_account_store
    quemusic_source_sdk quemusic_source_content_events
    Qt6::Core
)
target_include_directories(quemusic_source_registry PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/core/source
)

add_library(quemusic_legacy_media_identity_resolver STATIC
    core/migration/LegacyMediaIdentityResolver.cpp
    core/migration/LegacyMediaIdentityResolver.h
    core/migration/LegacyCollectionMigration.cpp
    core/migration/LegacyCollectionMigration.h
    core/migration/LegacyCollectionMigrationController.cpp
    core/migration/LegacyCollectionMigrationController.h)
target_compile_features(quemusic_legacy_media_identity_resolver PUBLIC cxx_std_17)
target_include_directories(quemusic_legacy_media_identity_resolver PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/core/migration)
target_link_libraries(quemusic_legacy_media_identity_resolver PUBLIC
    quemusic_source_registry Qt6::Sql)

add_library(quemusic_plugin_settings STATIC
    core/settings/PluginSettingsController.h
    core/settings/PluginSettingsController.cpp
    core/settings/PluginSettingsOperation.h
    core/settings/PluginSettingsOperation.cpp
    core/settings/SettingsSchemaPresentation.h
    core/settings/SettingsSchemaPresentation.cpp
    core/settings/HostPluginUiSettingsBridge.h
    core/settings/HostPluginUiSettingsBridge.cpp
    plugin-ui/PluginManagementUiSession.h
    plugin-ui/PluginManagementUiSession.cpp
)
target_link_libraries(quemusic_plugin_settings PUBLIC quemusic_source_registry quemusic_plugin_ui)

add_library(quemusic_music_repository STATIC
    core/music/CapabilityResolver.cpp
    core/music/CapabilityResolver.h
    core/music/MediaActionRouter.cpp
    core/music/MediaActionRouter.h
    core/music/AggregateComposer.cpp
    core/music/AggregateComposer.h
    core/music/PageRepository.cpp
    core/music/PageRepository.h
    core/music/PageCache.cpp
    core/music/PageCache.h
    core/music/ArtworkCache.cpp
    core/music/ArtworkCache.h
    core/music/MediaAssetRepository.cpp
    core/music/MediaAssetRepository.h
)
target_compile_features(quemusic_music_repository PUBLIC cxx_std_17)
target_include_directories(quemusic_music_repository PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/core/music)
target_link_libraries(quemusic_music_repository PUBLIC quemusic_source_registry Qt6::Concurrent)

add_library(quemusic_playback_coordinator STATIC
    core/music/PlaybackCoordinator.cpp core/music/PlaybackCoordinator.h core/music/PlaybackSink.h)
target_link_libraries(quemusic_playback_coordinator PUBLIC quemusic_music_repository)

add_library(quemusic_queue_history_codec STATIC
    core/music/QueueHistoryCodec.cpp core/music/QueueHistoryCodec.h
    core/music/QueueHistoryTypes.h)
target_compile_features(quemusic_queue_history_codec PUBLIC cxx_std_17)
target_include_directories(quemusic_queue_history_codec PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(quemusic_queue_history_codec PUBLIC quemusic_source_sdk Qt6::Core)
target_link_libraries(quemusic_playback_coordinator PUBLIC quemusic_queue_history_codec)
target_link_libraries(quemusic_legacy_media_identity_resolver PUBLIC
    quemusic_playback_coordinator)

add_library(quemusic_queue_history_store STATIC
    core/music/QueueHistoryStore.cpp core/music/QueueHistoryStore.h)
target_compile_features(quemusic_queue_history_store PUBLIC cxx_std_17)
target_include_directories(quemusic_queue_history_store PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(quemusic_queue_history_store PUBLIC quemusic_playback_coordinator)

add_library(quemusic_qt_playback_controller STATIC
    core/playback/QtPlaybackController.cpp
    core/playback/QtPlaybackController.h)
target_compile_features(quemusic_qt_playback_controller PUBLIC cxx_std_17)
target_include_directories(quemusic_qt_playback_controller PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/core/playback)
target_link_libraries(quemusic_qt_playback_controller PUBLIC
    quemusic_playback_coordinator Qt6::Multimedia)

add_library(quemusic_music_hub STATIC
    core/music/MusicHub.cpp core/music/MusicHub.h
    core/music/DirectoryLibraryController.cpp core/music/DirectoryLibraryController.h)
target_compile_features(quemusic_music_hub PUBLIC cxx_std_17)
target_link_libraries(quemusic_music_hub PUBLIC quemusic_music_repository quemusic_music_page_model)

add_library(quemusic_original_ui_music_adapter STATIC
    core/music/OriginalUiMusicAdapter.cpp
    core/music/OriginalUiMusicAdapter.h
    api/OnlineListModel.cpp
    api/OnlineListModel.h
)
target_compile_features(quemusic_original_ui_music_adapter PUBLIC cxx_std_17)
target_include_directories(quemusic_original_ui_music_adapter PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/api
)
target_link_libraries(quemusic_original_ui_music_adapter PUBLIC
    quemusic_music_hub
    quemusic_playback_coordinator
)

file(GLOB_RECURSE QML_COMPONENTS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} CONFIGURE_DEPENDS "components/*.qml")

if(BUILD_TESTING)
    enable_testing()

    foreach(lint_case allowed private-import parent-import remote-import unknown-import)
        add_test(NAME quemusic_plugin_ui_import_${lint_case}_test
            COMMAND ${CMAKE_COMMAND}
                "-DROOT=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/plugin-ui-lint"
                "-DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/plugin-ui-lint/${lint_case}.qml"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/ValidatePluginQmlImports.cmake")
        if(NOT lint_case STREQUAL "allowed")
            set_tests_properties(quemusic_plugin_ui_import_${lint_case}_test
                PROPERTIES WILL_FAIL TRUE)
        endif()
    endforeach()
    add_test(NAME quemusic_plugin_ui_import_public_module_test
        COMMAND ${CMAKE_COMMAND}
            "-DROOT=${CMAKE_CURRENT_SOURCE_DIR}/plugin-ui/qml"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/ValidatePluginQmlImports.cmake")
    qt_add_executable(quemusic_plugin_ui_external_loader
        tests/tst_PluginUiExternal.cpp)
    target_link_libraries(quemusic_plugin_ui_external_loader PRIVATE
        quemusic_plugin_core quemusic_plugin_ui quemusic_plugin_settings
        Qt6::Core Qt6::Qml Qt6::Quick)
    add_test(NAME quemusic_plugin_ui_external_test
        COMMAND ${CMAKE_COMMAND}
            "-DPROJECT_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
            "-DPROJECT_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}"
            "-DQT6_DIR=${Qt6_DIR}"
            "-DHOST_ARCHITECTURE=${CMAKE_SYSTEM_PROCESSOR}"
            "-DHOST_BUILD_KEY=$<IF:$<CONFIG:Debug>,Debug,Release>"
            "-DLOADER=$<TARGET_FILE:quemusic_plugin_ui_external_loader>"
            "-DPLUGIN_UI_RUNTIME=$<TARGET_FILE_NAME:quemusic_plugin_ui>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifyPluginUiExternalFixture.cmake")

    qt_add_executable(quemusic_plugin_ui_theme_test tests/tst_PluginUiTheme.cpp)
    target_sources(quemusic_plugin_ui_theme_test PRIVATE
        core/plugin-ui/PluginThemeAdapter.cpp core/plugin-ui/PluginThemeAdapter.h)
    target_include_directories(quemusic_plugin_ui_theme_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/core/plugin-ui)
    target_link_libraries(quemusic_plugin_ui_theme_test PRIVATE
        quemusic_plugin_ui Qt6::Test Qt6::Qml)
    add_test(NAME quemusic_plugin_ui_theme_test COMMAND quemusic_plugin_ui_theme_test)

    qt_add_executable(quemusic_plugin_ui_qml_test tests/tst_PluginUiQml.cpp)
    target_link_libraries(quemusic_plugin_ui_qml_test PRIVATE
        quemusic_plugin_ui Qt6::Test Qt6::Qml Qt6::Quick)
    target_compile_definitions(quemusic_plugin_ui_qml_test PRIVATE
        QUEMUSIC_QML_IMPORT_DIR="${CMAKE_CURRENT_BINARY_DIR}"
        QUEMUSIC_PLUGIN_UI_QML_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/doc/example")
    add_test(NAME quemusic_plugin_ui_qml_test COMMAND quemusic_plugin_ui_qml_test)
    set_tests_properties(quemusic_plugin_ui_qml_test PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    function(canonicalize_plugin_architecture architecture output_variable)
        string(TOLOWER "${architecture}" normalized_architecture)
        if(normalized_architecture STREQUAL "x86_64"
                OR normalized_architecture STREQUAL "amd64"
                OR normalized_architecture STREQUAL "x64")
            set(normalized_architecture "x86_64")
        elseif(normalized_architecture STREQUAL "arm64"
                OR normalized_architecture STREQUAL "aarch64")
            set(normalized_architecture "arm64")
        else()
            message(FATAL_ERROR
                "Unsupported source plugin target architecture: ${architecture}")
        endif()
        set(${output_variable} "${normalized_architecture}" PARENT_SCOPE)
    endfunction()

    if(APPLE AND CMAKE_OSX_ARCHITECTURES)
        set(QUEMUSIC_TEST_TARGET_ARCHITECTURES)
        foreach(target_architecture IN LISTS CMAKE_OSX_ARCHITECTURES)
            canonicalize_plugin_architecture(
                "${target_architecture}" canonical_target_architecture)
            list(APPEND QUEMUSIC_TEST_TARGET_ARCHITECTURES
                "${canonical_target_architecture}")
        endforeach()
        list(REMOVE_DUPLICATES QUEMUSIC_TEST_TARGET_ARCHITECTURES)
        list(LENGTH QUEMUSIC_TEST_TARGET_ARCHITECTURES target_architecture_count)
        if(target_architecture_count EQUAL 2)
            list(FIND QUEMUSIC_TEST_TARGET_ARCHITECTURES "x86_64" x86_index)
            list(FIND QUEMUSIC_TEST_TARGET_ARCHITECTURES "arm64" arm_index)
            if(x86_index EQUAL -1 OR arm_index EQUAL -1)
                message(FATAL_ERROR
                    "Unsupported universal source plugin architecture set: ${CMAKE_OSX_ARCHITECTURES}")
            endif()
            set(QUEMUSIC_TEST_ARCHITECTURE "universal")
        elseif(target_architecture_count EQUAL 1)
            list(GET QUEMUSIC_TEST_TARGET_ARCHITECTURES 0 QUEMUSIC_TEST_ARCHITECTURE)
        else()
            message(FATAL_ERROR
                "Unsupported source plugin architecture set: ${CMAKE_OSX_ARCHITECTURES}")
        endif()
    else()
        canonicalize_plugin_architecture(
            "${CMAKE_SYSTEM_PROCESSOR}" QUEMUSIC_TEST_ARCHITECTURE)
    endif()
    set(QUEMUSIC_TEST_BUILD_KEY "$<IF:$<CONFIG:Debug>,Debug,Release>")

    qt_add_executable(quemusic_plugin_ui_contract_test tests/tst_PluginUiContract.cpp)
    target_link_libraries(quemusic_plugin_ui_contract_test PRIVATE
        quemusic_plugin_ui_sdk quemusic_source_sdk Qt6::Test)
    add_test(NAME quemusic_plugin_ui_contract_test
        COMMAND quemusic_plugin_ui_contract_test)

    qt_add_executable(quemusic_plugin_ui_manifest_test tests/tst_PluginUiManifest.cpp)
    target_link_libraries(quemusic_plugin_ui_manifest_test PRIVATE
        quemusic_plugin_core Qt6::Test)
    add_test(NAME quemusic_plugin_ui_manifest_test
        COMMAND quemusic_plugin_ui_manifest_test)

    set(QUEMUSIC_UI_FIXTURE_ROOT "${CMAKE_CURRENT_BINARY_DIR}/test-plugin-ui-packages/$<CONFIG>")
    function(add_plugin_ui_fixture variant)
        set(target "quemusic_ui_fixture_${variant}")
        set(package_dir "${QUEMUSIC_UI_FIXTURE_ROOT}/${variant}")
        set(QUEMUSIC_UI_FIXTURE_PACKAGE_ID "org.quemusic.source.ui-${variant}")
        set(QUEMUSIC_UI_FIXTURE_SOURCE_ID "ui-${variant}")
        set(QUEMUSIC_UI_FIXTURE_LIBRARY "$<TARGET_FILE_NAME:${target}>")
        set(QUEMUSIC_UI_FIXTURE_MANAGEMENT_JSON
            ",\n  \"pluginUiApi\": \"1.0\",\n  \"ui\": {\"management\": \"qml/ManagementPage.qml\"}")
        if(variant STREQUAL "undeclared")
            set(QUEMUSIC_UI_FIXTURE_MANAGEMENT_JSON "")
        endif()
        add_library(${target} MODULE
            tests/fixtures/PluginUiFixturePlugin.cpp
            tests/fixtures/PluginUiFixturePlugin.h tests/fixtures/PluginUiQrBackend.h)
        target_link_libraries(${target} PRIVATE
            quemusic_source_sdk quemusic_plugin_ui_sdk quemusic_plugin_ui)
        target_compile_definitions(${target} PRIVATE
            QUEMUSIC_UI_FIXTURE_PACKAGE_ID="${QUEMUSIC_UI_FIXTURE_PACKAGE_ID}"
            QUEMUSIC_UI_FIXTURE_SOURCE_ID="${QUEMUSIC_UI_FIXTURE_SOURCE_ID}")
        if(variant STREQUAL "provider_missing")
            target_compile_definitions(${target} PRIVATE QUEMUSIC_UI_FIXTURE_NO_PROVIDER)
        elseif(variant STREQUAL "wrong_version")
            target_compile_definitions(${target} PRIVATE QUEMUSIC_UI_FIXTURE_WRONG_VERSION)
        elseif(variant STREQUAL "wrong_path")
            target_compile_definitions(${target} PRIVATE QUEMUSIC_UI_FIXTURE_WRONG_PATH)
        elseif(variant STREQUAL "no_mode")
            target_compile_definitions(${target} PRIVATE QUEMUSIC_UI_FIXTURE_NO_MODE)
        elseif(variant STREQUAL "null_backend")
            target_compile_definitions(${target} PRIVATE QUEMUSIC_UI_FIXTURE_NULL_BACKEND)
        endif()
        set_target_properties(${target} PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${package_dir}")
        configure_file(tests/fixtures/plugin-ui-manifest.json.in
            "${CMAKE_CURRENT_BINARY_DIR}/${target}-manifest.json.in" @ONLY)
        file(GENERATE OUTPUT "${package_dir}/manifest.json"
            INPUT "${CMAKE_CURRENT_BINARY_DIR}/${target}-manifest.json.in")
        file(GENERATE OUTPUT "${package_dir}/qml/ManagementPage.qml"
            INPUT "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/plugin-ui-qr/ManagementPage.qml")
    endfunction()
    foreach(ui_variant valid provider_missing undeclared wrong_version wrong_path no_mode null_backend)
        add_plugin_ui_fixture(${ui_variant})
    endforeach()

    qt_add_executable(quemusic_plugin_ui_provider_test tests/tst_PluginUiProvider.cpp)
    target_link_libraries(quemusic_plugin_ui_provider_test PRIVATE
        quemusic_plugin_core Qt6::Test)
    target_compile_definitions(quemusic_plugin_ui_provider_test PRIVATE
        QUEMUSIC_UI_FIXTURE_ROOT="${QUEMUSIC_UI_FIXTURE_ROOT}")
    foreach(ui_variant valid provider_missing undeclared wrong_version wrong_path no_mode null_backend)
        add_dependencies(quemusic_plugin_ui_provider_test "quemusic_ui_fixture_${ui_variant}")
    endforeach()
    add_test(NAME quemusic_plugin_ui_provider_test COMMAND quemusic_plugin_ui_provider_test)

    qt_add_executable(quemusic_plugin_ui_unload_test tests/tst_PluginUiUnload.cpp)
    target_link_libraries(quemusic_plugin_ui_unload_test PRIVATE
        quemusic_plugin_core Qt6::Test)
    target_compile_definitions(quemusic_plugin_ui_unload_test PRIVATE
        QUEMUSIC_UI_FIXTURE_ROOT="${QUEMUSIC_UI_FIXTURE_ROOT}")
    add_dependencies(quemusic_plugin_ui_unload_test
        quemusic_ui_fixture_valid quemusic_ui_fixture_provider_missing)
    add_test(NAME quemusic_plugin_ui_unload_test COMMAND quemusic_plugin_ui_unload_test)

    qt_add_executable(quemusic_navidrome_smoke_state_test
        tests/tst_NavidromeSmokeState.cpp
        tests/NavidromeSmokeState.h
    )
    target_include_directories(quemusic_navidrome_smoke_state_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
        ${CMAKE_CURRENT_SOURCE_DIR}/sdk/source
    )
    target_link_libraries(quemusic_navidrome_smoke_state_test PRIVATE
        quemusic_source_sdk
        Qt6::Test
    )
    add_test(NAME quemusic_navidrome_smoke_state_test
        COMMAND quemusic_navidrome_smoke_state_test)
endif()

add_subdirectory(plugins/navidrome-source)
add_subdirectory(plugins/local-source)

qt_add_executable(quemusic_navidrome_smoke
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/NavidromeSmoke.cpp
    plugins/navidrome-source/NavidromeApiClient.cpp
    plugins/navidrome-source/NavidromeApiClient.h
    plugins/navidrome-source/NavidromeSourceSession.cpp
    plugins/navidrome-source/NavidromeSourceSession.h
)
target_compile_features(quemusic_navidrome_smoke PRIVATE cxx_std_17)
target_include_directories(quemusic_navidrome_smoke PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/plugins/navidrome-source
)
target_link_libraries(quemusic_navidrome_smoke PRIVATE
    Qt6::Network
    quemusic_source_sdk
)

add_library(quemusic_playback_sdk STATIC
    sdk/playback/IPlaybackEngine.h
    sdk/playback/IPlaybackPlugin.h
    sdk/playback/PlaybackTypes.h
)
target_compile_features(quemusic_playback_sdk PUBLIC cxx_std_17)
set_target_properties(quemusic_playback_sdk PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(quemusic_playback_sdk PUBLIC
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

    qt_add_executable(quemusic_source_v2_types_test
        tests/tst_SourceV2Types.cpp
    )
    target_link_libraries(quemusic_source_v2_types_test PRIVATE
        quemusic_source_sdk
        Qt6::Test
    )
    add_test(NAME quemusic_source_v2_types_test COMMAND quemusic_source_v2_types_test)

    qt_add_executable(quemusic_music_page_model_test
        tests/tst_MusicPageModel.cpp
    )
    target_link_libraries(quemusic_music_page_model_test PRIVATE
        quemusic_music_page_model
        Qt6::Gui
        Qt6::Qml
        Qt6::Test
    )
    add_test(NAME quemusic_music_page_model_test COMMAND quemusic_music_page_model_test)

    qt_add_executable(quemusic_source_v2_contract_test
        tests/tst_SourceV2Contract.cpp
    )
    target_link_libraries(quemusic_source_v2_contract_test PRIVATE
        quemusic_source_sdk
        Qt6::Test
    )
    add_test(NAME quemusic_source_v2_contract_test COMMAND quemusic_source_v2_contract_test)

    qt_add_executable(quemusic_source_content_events_contract_test
        tests/tst_SourceContentEventsContract.cpp
    )
    target_link_libraries(quemusic_source_content_events_contract_test PRIVATE
        quemusic_source_content_events Qt6::Test
    )
    add_test(NAME quemusic_source_content_events_contract_test
        COMMAND quemusic_source_content_events_contract_test)
    add_test(NAME quemusic_source_content_events_external_test
        COMMAND ${CMAKE_COMMAND}
            "-Dbuild_dir=${CMAKE_BINARY_DIR}"
            "-Dfixture_dir=${CMAKE_CURRENT_SOURCE_DIR}/tests/external-source-content-events"
            "-Dqt_dir=${Qt6_DIR}"
            "-Dbuild_config=$<CONFIG>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifyExternalSourceContentEvents.cmake")

    qt_add_executable(quemusic_source_settings_storage_test
        tests/tst_SourceSettingsStorage.cpp
    )
    target_link_libraries(quemusic_source_settings_storage_test PRIVATE
        quemusic_source_account_store
        Qt6::Test
    )
    add_test(NAME quemusic_source_settings_storage_test COMMAND quemusic_source_settings_storage_test)

    qt_add_executable(quemusic_source_account_store_test
        tests/tst_SourceAccountStore.cpp
    )
    target_link_libraries(quemusic_source_account_store_test PRIVATE
        quemusic_source_account_store
        Qt6::Test
    )
    add_test(NAME quemusic_source_account_store_test COMMAND quemusic_source_account_store_test)

    qt_add_executable(quemusic_playback_plugin_contract_test
        tests/tst_PlaybackPluginContract.cpp
    )
    target_link_libraries(quemusic_playback_plugin_contract_test PRIVATE
        quemusic_playback_manager
        Qt6::Test
    )
    add_test(NAME quemusic_playback_plugin_contract_test COMMAND quemusic_playback_plugin_contract_test)

    function(add_v2_source_plugin_fixture target package_directory)
        add_library(${target} MODULE
            tests/fixtures/FakeV2SourcePlugin.cpp
            tests/fixtures/FakeV2SourcePlugin.h
        )
        target_compile_features(${target} PRIVATE cxx_std_17)
        target_link_libraries(${target} PRIVATE quemusic_source_sdk)
        set_target_properties(${target} PROPERTIES
            LIBRARY_OUTPUT_DIRECTORY "${package_directory}"
            RUNTIME_OUTPUT_DIRECTORY "${package_directory}"
        )
    endfunction()

    function(generate_v2_source_plugin_manifest target package_directory)
        set(QUEMUSIC_V2_FIXTURE_LIBRARY "$<TARGET_FILE_NAME:${target}>")
        set(configured_manifest
            "${CMAKE_CURRENT_BINARY_DIR}/${target}-manifest.json.in")
        configure_file(
            tests/fixtures/v2-manifest.json.in
            "${configured_manifest}"
            @ONLY
        )
        file(GENERATE
            OUTPUT "${package_directory}/manifest.json"
            INPUT "${configured_manifest}"
        )
    endfunction()

    set(QUEMUSIC_V2_FIXTURE_PACKAGE_ID "org.quemusic.source.settings-fixture")
    set(QUEMUSIC_V2_FIXTURE_SOURCE_ID "settings-fixture")
    set(QUEMUSIC_V2_FIXTURE_NAME "Settings Fixture")
    set(QUEMUSIC_SETTINGS_PACKAGE_ROOT "${CMAKE_CURRENT_BINARY_DIR}/test-settings-packages/$<CONFIG>")
    set(QUEMUSIC_SETTINGS_PACKAGE_DIR "${QUEMUSIC_SETTINGS_PACKAGE_ROOT}/settings-fixture")
    add_library(quemusic_settings_fixture MODULE
        tests/fixtures/SettingsV2FixturePlugin.cpp tests/fixtures/SettingsV2FixturePlugin.h)
    target_link_libraries(quemusic_settings_fixture PRIVATE quemusic_source_sdk)
    set_target_properties(quemusic_settings_fixture PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${QUEMUSIC_SETTINGS_PACKAGE_DIR}")
    generate_v2_source_plugin_manifest(quemusic_settings_fixture "${QUEMUSIC_SETTINGS_PACKAGE_DIR}")
    qt_add_executable(quemusic_plugin_settings_operation_test tests/tst_PluginSettingsOperation.cpp)
    target_link_libraries(quemusic_plugin_settings_operation_test PRIVATE quemusic_plugin_settings Qt6::Test)
    target_compile_definitions(quemusic_plugin_settings_operation_test PRIVATE QUEMUSIC_SETTINGS_PACKAGES="${QUEMUSIC_SETTINGS_PACKAGE_ROOT}")
    add_dependencies(quemusic_plugin_settings_operation_test quemusic_settings_fixture)
    add_test(NAME quemusic_plugin_settings_operation_test COMMAND quemusic_plugin_settings_operation_test)
    qt_add_executable(quemusic_plugin_settings_controller_test tests/tst_PluginSettingsController.cpp)
    target_link_libraries(quemusic_plugin_settings_controller_test PRIVATE quemusic_plugin_settings Qt6::Test)
    target_compile_definitions(quemusic_plugin_settings_controller_test PRIVATE
        QUEMUSIC_SETTINGS_PACKAGES="${QUEMUSIC_SETTINGS_PACKAGE_ROOT}"
        QUEMUSIC_NO_SETTINGS_PACKAGE="$<TARGET_FILE_DIR:quemusic_fake_v2_source_fixture>")
    target_compile_definitions(quemusic_plugin_settings_controller_test PRIVATE
        QUEMUSIC_UI_FIXTURE_ROOT="${QUEMUSIC_UI_FIXTURE_ROOT}")
    add_dependencies(quemusic_plugin_settings_controller_test quemusic_settings_fixture quemusic_fake_v2_source_fixture quemusic_ui_fixture_valid)
    add_test(NAME quemusic_plugin_settings_controller_test COMMAND quemusic_plugin_settings_controller_test)
    qt_add_executable(quemusic_plugin_ui_settings_bridge_test tests/tst_PluginUiSettingsBridge.cpp)
    target_link_libraries(quemusic_plugin_ui_settings_bridge_test PRIVATE quemusic_plugin_settings Qt6::Test)
    add_test(NAME quemusic_plugin_ui_settings_bridge_test COMMAND quemusic_plugin_ui_settings_bridge_test)
    qt_add_executable(quemusic_plugin_management_ui_session_test tests/tst_PluginManagementUiSession.cpp)
    target_link_libraries(quemusic_plugin_management_ui_session_test PRIVATE quemusic_plugin_settings Qt6::Test)
    target_compile_definitions(quemusic_plugin_management_ui_session_test PRIVATE
        QUEMUSIC_UI_FIXTURE_ROOT="${QUEMUSIC_UI_FIXTURE_ROOT}")
    add_dependencies(quemusic_plugin_management_ui_session_test quemusic_ui_fixture_valid quemusic_ui_fixture_null_backend)
    add_test(NAME quemusic_plugin_management_ui_session_test COMMAND quemusic_plugin_management_ui_session_test)

    # Task7 fixture lives at the provider boundary of the real hub orchestration.
    set(QUEMUSIC_V2_FIXTURE_PACKAGE_ID "org.quemusic.source.task7")
    set(QUEMUSIC_V2_FIXTURE_SOURCE_ID "task7")
    set(QUEMUSIC_V2_FIXTURE_NAME "Task7 Fixture")
    set(QUEMUSIC_TASK7_PACKAGE_ROOT "${CMAKE_CURRENT_BINARY_DIR}/test-task7-packages/$<CONFIG>")
    set(QUEMUSIC_TASK7_PACKAGE_DIR "${QUEMUSIC_TASK7_PACKAGE_ROOT}/task7")
    add_library(quemusic_hub_fixture MODULE tests/tst_MusicHub.cpp)
    target_compile_definitions(quemusic_hub_fixture PRIVATE QUEMUSIC_HUB_FIXTURE)
    target_link_libraries(quemusic_hub_fixture PRIVATE quemusic_source_sdk)
    set_target_properties(quemusic_hub_fixture PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${QUEMUSIC_TASK7_PACKAGE_DIR}")
    generate_v2_source_plugin_manifest(quemusic_hub_fixture "${QUEMUSIC_TASK7_PACKAGE_DIR}")
    qt_add_executable(quemusic_music_hub_test tests/tst_MusicHub.cpp)
    target_link_libraries(quemusic_music_hub_test PRIVATE quemusic_music_hub Qt6::Test)
    target_compile_definitions(quemusic_music_hub_test PRIVATE QUEMUSIC_TASK7_PACKAGES="${QUEMUSIC_TASK7_PACKAGE_ROOT}")
    add_dependencies(quemusic_music_hub_test quemusic_hub_fixture)
    add_test(NAME quemusic_music_hub_test COMMAND quemusic_music_hub_test)
    set_tests_properties(quemusic_music_hub_test PROPERTIES TIMEOUT 30)

    set(QUEMUSIC_V2_FIXTURE_TARGET quemusic_original_ui_adapter_fixture)
    set(QUEMUSIC_V2_FIXTURE_PACKAGE_ID "org.quemusic.source.original-ui-adapter")
    set(QUEMUSIC_V2_FIXTURE_SOURCE_ID "adapter")
    set(QUEMUSIC_V2_FIXTURE_NAME "Original UI Adapter Fixture")
    set(QUEMUSIC_ORIGINAL_UI_ADAPTER_PACKAGE_ROOT
        "${CMAKE_CURRENT_BINARY_DIR}/test-original-ui-adapter-packages/$<CONFIG>")
    set(QUEMUSIC_ORIGINAL_UI_ADAPTER_PACKAGE_DIR
        "${QUEMUSIC_ORIGINAL_UI_ADAPTER_PACKAGE_ROOT}/adapter")
    add_library(quemusic_original_ui_adapter_fixture MODULE
        tests/tst_OriginalUiMusicAdapter.cpp)
    target_compile_definitions(quemusic_original_ui_adapter_fixture PRIVATE
        QUEMUSIC_ORIGINAL_UI_ADAPTER_FIXTURE)
    target_link_libraries(quemusic_original_ui_adapter_fixture PRIVATE
        quemusic_source_sdk)
    set_target_properties(quemusic_original_ui_adapter_fixture PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${QUEMUSIC_ORIGINAL_UI_ADAPTER_PACKAGE_DIR}")
    generate_v2_source_plugin_manifest(quemusic_original_ui_adapter_fixture
        "${QUEMUSIC_ORIGINAL_UI_ADAPTER_PACKAGE_DIR}")
    qt_add_executable(quemusic_original_ui_music_adapter_test
        tests/tst_OriginalUiMusicAdapter.cpp)
    target_link_libraries(quemusic_original_ui_music_adapter_test PRIVATE
        quemusic_original_ui_music_adapter
        Qt6::Test)
    target_compile_definitions(quemusic_original_ui_music_adapter_test PRIVATE
        QUEMUSIC_ORIGINAL_UI_ADAPTER_PACKAGES="${QUEMUSIC_ORIGINAL_UI_ADAPTER_PACKAGE_ROOT}")
    add_dependencies(quemusic_original_ui_music_adapter_test
        quemusic_original_ui_adapter_fixture)
    add_test(NAME quemusic_original_ui_music_adapter_test
        COMMAND quemusic_original_ui_music_adapter_test)

    # Task12c isolated private playback core and dynamically loaded fixture.
    set(QUEMUSIC_V2_FIXTURE_TARGET quemusic_playback_fixture)
    set(QUEMUSIC_V2_FIXTURE_PACKAGE_ID "org.quemusic.source.task12c")
    set(QUEMUSIC_V2_FIXTURE_SOURCE_ID "task12c")
    set(QUEMUSIC_V2_FIXTURE_NAME "Task12c Fixture")
    set(QUEMUSIC_TASK12C_PACKAGE_ROOT "${CMAKE_CURRENT_BINARY_DIR}/test-task12c-packages/$<CONFIG>")
    set(QUEMUSIC_TASK12C_PACKAGE_DIR "${QUEMUSIC_TASK12C_PACKAGE_ROOT}/task12c")
    add_library(quemusic_playback_fixture MODULE tests/tst_PlaybackCoordinator.cpp)
    target_compile_definitions(quemusic_playback_fixture PRIVATE QUEMUSIC_PLAYBACK_FIXTURE)
    target_link_libraries(quemusic_playback_fixture PRIVATE quemusic_source_sdk)
    set_target_properties(quemusic_playback_fixture PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${QUEMUSIC_TASK12C_PACKAGE_DIR}")
    generate_v2_source_plugin_manifest(quemusic_playback_fixture "${QUEMUSIC_TASK12C_PACKAGE_DIR}")
    qt_add_executable(quemusic_playback_coordinator_test tests/tst_PlaybackCoordinator.cpp)
    target_link_libraries(quemusic_playback_coordinator_test PRIVATE quemusic_playback_coordinator Qt6::Test)
    target_compile_definitions(quemusic_playback_coordinator_test PRIVATE QUEMUSIC_TASK12C_PACKAGES="${QUEMUSIC_TASK12C_PACKAGE_ROOT}")
    add_dependencies(quemusic_playback_coordinator_test quemusic_playback_fixture)
    add_test(NAME quemusic_playback_coordinator_test COMMAND quemusic_playback_coordinator_test)
    set_tests_properties(quemusic_playback_coordinator_test PROPERTIES TIMEOUT 30)

    qt_add_executable(quemusic_qt_playback_controller_test
        tests/tst_QtPlaybackController.cpp)
    target_include_directories(quemusic_qt_playback_controller_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/core/playback)
    target_link_libraries(quemusic_qt_playback_controller_test PRIVATE
        quemusic_qt_playback_controller Qt6::Test)
    add_test(NAME quemusic_qt_playback_controller_test
        COMMAND quemusic_qt_playback_controller_test)
    set_tests_properties(quemusic_qt_playback_controller_test PROPERTIES TIMEOUT 30)

    qt_add_executable(quemusic_playback_controls_adapter_qml_test
        tests/tst_PlaybackControlsAdapterQml.cpp)
    target_compile_features(quemusic_playback_controls_adapter_qml_test PRIVATE cxx_std_17)
    qt_add_resources(quemusic_playback_controls_adapter_qml_test
        "quemusic_playback_controls_adapter_qml_test_qml"
        PREFIX "/QueMusic"
        FILES ${QML_COMPONENTS})
    target_link_libraries(quemusic_playback_controls_adapter_qml_test PRIVATE
        Qt6::Qml Qt6::Test)
    add_test(NAME quemusic_playback_controls_adapter_qml_test
        COMMAND quemusic_playback_controls_adapter_qml_test)
    set_tests_properties(quemusic_playback_controls_adapter_qml_test PROPERTIES
        TIMEOUT 30 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    qt_add_executable(quemusic_legacy_queue_qml_test tests/tst_LegacyQueueQml.cpp)
    target_compile_features(quemusic_legacy_queue_qml_test PRIVATE cxx_std_17)
    qt_add_resources(quemusic_legacy_queue_qml_test
        "quemusic_legacy_queue_qml_test_qml"
        PREFIX "/QueMusic"
        FILES components/LegacyQueueController.qml)
    target_link_libraries(quemusic_legacy_queue_qml_test PRIVATE
        Qt6::Gui Qt6::Qml Qt6::Test)
    add_test(NAME quemusic_legacy_queue_qml_test COMMAND quemusic_legacy_queue_qml_test)
    set_tests_properties(quemusic_legacy_queue_qml_test PROPERTIES
        TIMEOUT 30 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    qt_add_executable(quemusic_music_hub_qml_test tests/tst_MusicHubQml.cpp)
    target_compile_features(quemusic_music_hub_qml_test PRIVATE cxx_std_17)
    qt_add_resources(quemusic_music_hub_qml_test "quemusic_music_hub_qml_test_qml"
        PREFIX "/QueMusic" FILES
            ${QML_COMPONENTS}
            components/PluginSettingsText.js)
    qt_add_resources(quemusic_music_hub_qml_test "quemusic_music_hub_qml_test_pages"
        PREFIX "/QueMusic" FILES
            pages/HomePage.qml
            pages/PlaylistPage.qml
            pages/FavouritePage.qml
            pages/SearchPage.qml
            layout/MainContent.qml
            layout/LeftSideBar.qml)
    target_link_libraries(quemusic_music_hub_qml_test PRIVATE
        quemusic_music_page_model Qt6::Qml Qt6::Quick Qt6::Test)
    add_test(NAME quemusic_music_hub_qml_test COMMAND quemusic_music_hub_qml_test)
    set_tests_properties(quemusic_music_hub_qml_test PROPERTIES
        TIMEOUT 30 ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen;QML_IMPORT_PATH=${CMAKE_CURRENT_BINARY_DIR}")

    qt_add_executable(quemusic_original_ui_recommendation_qml_test
        tests/tst_OriginalUiRecommendationQml.cpp)
    target_compile_features(quemusic_original_ui_recommendation_qml_test PRIVATE cxx_std_17)
    qt_add_resources(quemusic_original_ui_recommendation_qml_test
        "quemusic_original_ui_recommendation_qml_test_qml"
        PREFIX "/QueMusic" FILES
            ${QML_COMPONENTS}
            components/PluginSettingsText.js)
    qt_add_resources(quemusic_original_ui_recommendation_qml_test
        "quemusic_original_ui_recommendation_qml_test_pages"
        PREFIX "/QueMusic" FILES
            pages/HomePage.qml
            pages/PlaylistPage.qml)
    target_link_libraries(quemusic_original_ui_recommendation_qml_test PRIVATE
        Qt6::Qml Qt6::Quick Qt6::Test)
    add_test(NAME quemusic_original_ui_recommendation_qml_test
        COMMAND quemusic_original_ui_recommendation_qml_test)
    set_tests_properties(quemusic_original_ui_recommendation_qml_test PROPERTIES
        TIMEOUT 30 ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen;QML_IMPORT_PATH=${CMAKE_CURRENT_BINARY_DIR}")

    qt_add_executable(quemusic_original_ui_actions_qml_test
        tests/tst_OriginalUiActionsQml.cpp)
    target_compile_features(quemusic_original_ui_actions_qml_test PRIVATE cxx_std_17)
    qt_add_resources(quemusic_original_ui_actions_qml_test
        "quemusic_original_ui_actions_qml_test_qml"
        PREFIX "/QueMusic" FILES
            ${QML_COMPONENTS}
            components/PluginSettingsText.js)
    qt_add_resources(quemusic_original_ui_actions_qml_test
        "quemusic_original_ui_actions_qml_test_pages"
        PREFIX "/QueMusic" FILES
            pages/FavouritePage.qml
            pages/SearchPage.qml
            pages/DownloadPage.qml)
    target_link_libraries(quemusic_original_ui_actions_qml_test PRIVATE
        Qt6::Qml Qt6::Quick Qt6::Test)
    add_test(NAME quemusic_original_ui_actions_qml_test
        COMMAND quemusic_original_ui_actions_qml_test)
    set_tests_properties(quemusic_original_ui_actions_qml_test PROPERTIES
        TIMEOUT 30 ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen;QML_IMPORT_PATH=${CMAKE_CURRENT_BINARY_DIR}")

    qt_add_executable(quemusic_original_ui_playback_qml_test
        tests/tst_OriginalUiPlaybackQml.cpp)
    target_compile_features(quemusic_original_ui_playback_qml_test PRIVATE cxx_std_17)
    target_compile_definitions(quemusic_original_ui_playback_qml_test PRIVATE
        QUEMUSIC_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    target_include_directories(quemusic_original_ui_playback_qml_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/core/music ${CMAKE_CURRENT_SOURCE_DIR}/sdk/source)
    qt_add_resources(quemusic_original_ui_playback_qml_test
        "quemusic_original_ui_playback_qml_test_qml"
        PREFIX "/QueMusic"
        FILES
            components/LegacyQueueController.qml
            components/PlaybackLyricsAdapter.qml
            components/PlaybackControlsAdapter.qml)
    target_link_libraries(quemusic_original_ui_playback_qml_test PRIVATE
        Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Test)
    add_test(NAME quemusic_original_ui_playback_qml_test
        COMMAND quemusic_original_ui_playback_qml_test)
    set_tests_properties(quemusic_original_ui_playback_qml_test PROPERTIES
        TIMEOUT 30 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    qt_add_executable(quemusic_artwork_presentation_test
        tests/tst_ArtworkPresentation.cpp
        cpp/ColorExtractor.cpp cpp/ColorExtractor.h
        meshgradient/MeshGradientItem.cpp meshgradient/MeshGradientItem.h)
    target_compile_features(quemusic_artwork_presentation_test PRIVATE cxx_std_17)
    target_include_directories(quemusic_artwork_presentation_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(quemusic_artwork_presentation_test PRIVATE
        Qt6::Gui Qt6::Quick Qt6::Network Qt6::Concurrent Qt6::Test)
    qt_add_resources(quemusic_artwork_presentation_test "artwork_presentation_resources"
        PREFIX "/QueMusic" FILES resources/app/musicpic.png)
    add_test(NAME quemusic_artwork_presentation_test COMMAND quemusic_artwork_presentation_test)
    set_tests_properties(quemusic_artwork_presentation_test PROPERTIES
        TIMEOUT 30 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    qt_add_executable(quemusic_sensitive_playback_logging_test
        tests/tst_SensitivePlaybackLogging.cpp
        core/logging/RuntimeLoggingPolicy.cpp
        core/logging/RuntimeLoggingPolicy.h)
    target_compile_features(quemusic_sensitive_playback_logging_test PRIVATE cxx_std_17)
    target_include_directories(quemusic_sensitive_playback_logging_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/core/logging)
    target_link_libraries(quemusic_sensitive_playback_logging_test PRIVATE
        Qt6::Core Qt6::Test)
    add_test(NAME quemusic_sensitive_playback_logging_test
        COMMAND quemusic_sensitive_playback_logging_test)
    set_tests_properties(quemusic_sensitive_playback_logging_test PROPERTIES TIMEOUT 30)

    qt_add_executable(quemusic_original_ui_structure_test
        tests/tst_OriginalUiStructure.cpp)
    target_compile_features(quemusic_original_ui_structure_test PRIVATE cxx_std_17)
    target_compile_definitions(quemusic_original_ui_structure_test PRIVATE
        QUEMUSIC_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(quemusic_original_ui_structure_test PRIVATE Qt6::Test)
    add_test(NAME quemusic_original_ui_structure_test
        COMMAND quemusic_original_ui_structure_test)

    # Task6 fixture shares its source with the action router contract tests.
    set(QUEMUSIC_V2_FIXTURE_TARGET quemusic_action_fixture)
    set(QUEMUSIC_V2_FIXTURE_PACKAGE_ID "org.quemusic.source.task6")
    set(QUEMUSIC_V2_FIXTURE_SOURCE_ID "task6")
    set(QUEMUSIC_V2_FIXTURE_NAME "Task6 Fixture")
    set(QUEMUSIC_TASK6_PACKAGE_ROOT "${CMAKE_CURRENT_BINARY_DIR}/test-task6-packages/$<CONFIG>")
    set(QUEMUSIC_TASK6_PACKAGE_DIR "${QUEMUSIC_TASK6_PACKAGE_ROOT}/task6")
    add_library(quemusic_action_fixture MODULE tests/tst_MediaActionRouterV2.cpp)
    target_compile_definitions(quemusic_action_fixture PRIVATE QUEMUSIC_ACTION_FIXTURE)
    target_link_libraries(quemusic_action_fixture PRIVATE quemusic_source_sdk)
    set_target_properties(quemusic_action_fixture PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${QUEMUSIC_TASK6_PACKAGE_DIR}")
    generate_v2_source_plugin_manifest(quemusic_action_fixture "${QUEMUSIC_TASK6_PACKAGE_DIR}")
    qt_add_executable(quemusic_capability_resolver_test tests/tst_CapabilityResolver.cpp)
    target_link_libraries(quemusic_capability_resolver_test PRIVATE quemusic_music_repository Qt6::Test)
    qt_add_executable(quemusic_media_action_router_v2_test tests/tst_MediaActionRouterV2.cpp)
    target_link_libraries(quemusic_media_action_router_v2_test PRIVATE
        quemusic_music_repository quemusic_music_page_model Qt6::Test)
    target_compile_definitions(quemusic_media_action_router_v2_test PRIVATE
        QUEMUSIC_TASK6_PACKAGES="${QUEMUSIC_TASK6_PACKAGE_ROOT}")
    add_dependencies(quemusic_media_action_router_v2_test quemusic_action_fixture)
    foreach(task6_target IN ITEMS quemusic_capability_resolver_test quemusic_media_action_router_v2_test)
        add_test(NAME ${task6_target} COMMAND ${task6_target})
        set_tests_properties(${task6_target} PROPERTIES TIMEOUT 30)
    endforeach()

    # Task5 fixture shares its source with the repository contract tests.
    set(QUEMUSIC_V2_FIXTURE_TARGET quemusic_page_fixture)
    set(QUEMUSIC_V2_FIXTURE_PACKAGE_ID "org.quemusic.source.task5")
    set(QUEMUSIC_V2_FIXTURE_SOURCE_ID "task5")
    set(QUEMUSIC_V2_FIXTURE_NAME "Task5 Fixture")
    set(QUEMUSIC_TASK5_PACKAGE_ROOT "${CMAKE_CURRENT_BINARY_DIR}/test-task5-packages/$<CONFIG>")
    set(QUEMUSIC_TASK5_PACKAGE_DIR "${QUEMUSIC_TASK5_PACKAGE_ROOT}/task5")
    add_library(quemusic_page_fixture MODULE tests/tst_PageRepository.cpp)
    target_compile_definitions(quemusic_page_fixture PRIVATE QUEMUSIC_PAGE_FIXTURE)
    target_link_libraries(quemusic_page_fixture PRIVATE quemusic_source_sdk)
    set_target_properties(quemusic_page_fixture PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${QUEMUSIC_TASK5_PACKAGE_DIR}")
    generate_v2_source_plugin_manifest(quemusic_page_fixture "${QUEMUSIC_TASK5_PACKAGE_DIR}")

    foreach(task5_test IN ITEMS AggregateComposer PageRepository MusicCaches)
        if(task5_test STREQUAL "AggregateComposer")
            set(task5_target quemusic_aggregate_composer_test)
        elseif(task5_test STREQUAL "PageRepository")
            set(task5_target quemusic_page_repository_test)
        else()
            set(task5_target quemusic_music_caches_test)
        endif()
        qt_add_executable(${task5_target} tests/tst_${task5_test}.cpp)
        target_link_libraries(${task5_target} PRIVATE
            quemusic_music_page_model quemusic_music_repository Qt6::Test)
        target_compile_definitions(${task5_target} PRIVATE
            QUEMUSIC_TASK5_PACKAGES="${QUEMUSIC_TASK5_PACKAGE_ROOT}")
        add_dependencies(${task5_target} quemusic_page_fixture)
        add_test(NAME ${task5_target} COMMAND ${task5_target})
        set_tests_properties(${task5_target} PROPERTIES TIMEOUT 30)
    endforeach()

    set(QUEMUSIC_V2_PACKAGE_ROOT
        "${CMAKE_CURRENT_BINARY_DIR}/test-source-v2-packages")
    set(QUEMUSIC_V2_FIXTURE_TARGET quemusic_fake_v2_source_fixture)
    set(QUEMUSIC_V2_FIXTURE_PACKAGE_ID "org.quemusic.source.fixture-v2")
    set(QUEMUSIC_V2_FIXTURE_SOURCE_ID "fixture-v2")
    set(QUEMUSIC_V2_FIXTURE_NAME "Fixture V2")
    set(QUEMUSIC_V2_FIXTURE_PACKAGE_DIR
        "${QUEMUSIC_V2_PACKAGE_ROOT}/$<CONFIG>/fixture-v2")
    add_v2_source_plugin_fixture(${QUEMUSIC_V2_FIXTURE_TARGET}
                                 "${QUEMUSIC_V2_FIXTURE_PACKAGE_DIR}")
    generate_v2_source_plugin_manifest(${QUEMUSIC_V2_FIXTURE_TARGET}
                                       "${QUEMUSIC_V2_FIXTURE_PACKAGE_DIR}")

    set(QUEMUSIC_INVALID_V2_PACKAGE_ROOT
        "${CMAKE_CURRENT_BINARY_DIR}/test-source-v2-invalid-packages")
    set(QUEMUSIC_V2_FIXTURE_TARGET quemusic_fake_v2_wrong_contract_fixture)
    set(QUEMUSIC_V2_FIXTURE_PACKAGE_ID "org.quemusic.source.fixture-v2-invalid")
    set(QUEMUSIC_V2_FIXTURE_SOURCE_ID "fixture-v2-invalid")
    set(QUEMUSIC_V2_FIXTURE_NAME "Fixture V2 Invalid")
    set(QUEMUSIC_INVALID_V2_FIXTURE_PACKAGE_DIR
        "${QUEMUSIC_INVALID_V2_PACKAGE_ROOT}/$<CONFIG>/fixture-v2-invalid")
    add_v2_source_plugin_fixture(${QUEMUSIC_V2_FIXTURE_TARGET}
                                 "${QUEMUSIC_INVALID_V2_FIXTURE_PACKAGE_DIR}")
    target_compile_definitions(${QUEMUSIC_V2_FIXTURE_TARGET} PRIVATE
        QUEMUSIC_FAKE_V2_OMIT_INTERFACE
    )
    generate_v2_source_plugin_manifest(${QUEMUSIC_V2_FIXTURE_TARGET}
                                       "${QUEMUSIC_INVALID_V2_FIXTURE_PACKAGE_DIR}")

    set(QUEMUSIC_REGISTRY_V2_PACKAGE_ROOT
        "${CMAKE_CURRENT_BINARY_DIR}/test-source-registry-v2-packages")
    set(QUEMUSIC_V2_FIXTURE_TARGET quemusic_source_registry_v2_fixture)
    set(QUEMUSIC_V2_FIXTURE_PACKAGE_ID "org.quemusic.source.registry-v2")
    set(QUEMUSIC_V2_FIXTURE_SOURCE_ID "registry-v2")
    set(QUEMUSIC_V2_FIXTURE_NAME "Registry V2 Fixture")
    set(QUEMUSIC_REGISTRY_V2_FIXTURE_PACKAGE_DIR
        "${QUEMUSIC_REGISTRY_V2_PACKAGE_ROOT}/$<CONFIG>/registry-v2")
    add_library(${QUEMUSIC_V2_FIXTURE_TARGET} MODULE
        tests/tst_SourceRegistryV2.cpp
    )
    target_compile_features(${QUEMUSIC_V2_FIXTURE_TARGET} PRIVATE cxx_std_17)
    target_compile_definitions(${QUEMUSIC_V2_FIXTURE_TARGET} PRIVATE
        QUEMUSIC_SOURCE_REGISTRY_V2_FIXTURE
        QUEMUSIC_SOURCE_REGISTRY_FIXTURE_PACKAGE_ID="${QUEMUSIC_V2_FIXTURE_PACKAGE_ID}"
        QUEMUSIC_SOURCE_REGISTRY_FIXTURE_SOURCE_ID="${QUEMUSIC_V2_FIXTURE_SOURCE_ID}"
    )
    target_link_libraries(${QUEMUSIC_V2_FIXTURE_TARGET} PRIVATE quemusic_source_sdk)
    set_target_properties(${QUEMUSIC_V2_FIXTURE_TARGET} PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${QUEMUSIC_REGISTRY_V2_FIXTURE_PACKAGE_DIR}"
        RUNTIME_OUTPUT_DIRECTORY "${QUEMUSIC_REGISTRY_V2_FIXTURE_PACKAGE_DIR}"
    )
    generate_v2_source_plugin_manifest(${QUEMUSIC_V2_FIXTURE_TARGET}
                                       "${QUEMUSIC_REGISTRY_V2_FIXTURE_PACKAGE_DIR}")

    set(QUEMUSIC_REGISTRY_V2_SECOND_PACKAGE_ROOT
        "${CMAKE_CURRENT_BINARY_DIR}/test-source-registry-v2-secondary-packages")
    set(QUEMUSIC_V2_FIXTURE_TARGET quemusic_source_registry_v2_secondary_fixture)
    set(QUEMUSIC_V2_FIXTURE_PACKAGE_ID
        "org.quemusic.source.registry-v2-secondary")
    set(QUEMUSIC_V2_FIXTURE_SOURCE_ID "registry-secondary")
    set(QUEMUSIC_V2_FIXTURE_NAME "Registry V2 Secondary Fixture")
    set(QUEMUSIC_REGISTRY_V2_SECOND_FIXTURE_PACKAGE_DIR
        "${QUEMUSIC_REGISTRY_V2_SECOND_PACKAGE_ROOT}/$<CONFIG>/registry-secondary")
    add_library(${QUEMUSIC_V2_FIXTURE_TARGET} MODULE
        tests/tst_SourceRegistryV2.cpp
    )
    target_compile_features(${QUEMUSIC_V2_FIXTURE_TARGET} PRIVATE cxx_std_17)
    target_compile_definitions(${QUEMUSIC_V2_FIXTURE_TARGET} PRIVATE
        QUEMUSIC_SOURCE_REGISTRY_V2_FIXTURE
        QUEMUSIC_SOURCE_REGISTRY_FIXTURE_PACKAGE_ID="${QUEMUSIC_V2_FIXTURE_PACKAGE_ID}"
        QUEMUSIC_SOURCE_REGISTRY_FIXTURE_SOURCE_ID="${QUEMUSIC_V2_FIXTURE_SOURCE_ID}"
    )
    target_link_libraries(${QUEMUSIC_V2_FIXTURE_TARGET} PRIVATE quemusic_source_sdk)
    set_target_properties(${QUEMUSIC_V2_FIXTURE_TARGET} PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${QUEMUSIC_REGISTRY_V2_SECOND_FIXTURE_PACKAGE_DIR}"
        RUNTIME_OUTPUT_DIRECTORY "${QUEMUSIC_REGISTRY_V2_SECOND_FIXTURE_PACKAGE_DIR}"
    )
    generate_v2_source_plugin_manifest(${QUEMUSIC_V2_FIXTURE_TARGET}
                                       "${QUEMUSIC_REGISTRY_V2_SECOND_FIXTURE_PACKAGE_DIR}")

    set(QUEMUSIC_REGISTRY_V2_MISMATCH_PACKAGE_ROOT
        "${CMAKE_CURRENT_BINARY_DIR}/test-source-registry-v2-mismatch-packages")
    set(QUEMUSIC_V2_FIXTURE_TARGET quemusic_source_registry_v2_mismatch_fixture)
    set(QUEMUSIC_V2_FIXTURE_PACKAGE_ID
        "org.quemusic.source.registry-v2-mismatch")
    set(QUEMUSIC_V2_FIXTURE_SOURCE_ID "registry-v2-mismatch")
    set(QUEMUSIC_V2_FIXTURE_NAME "Registry V2 Mismatch Fixture")
    set(QUEMUSIC_REGISTRY_V2_MISMATCH_FIXTURE_PACKAGE_DIR
        "${QUEMUSIC_REGISTRY_V2_MISMATCH_PACKAGE_ROOT}/$<CONFIG>/registry-v2-mismatch")
    add_library(${QUEMUSIC_V2_FIXTURE_TARGET} MODULE
        tests/tst_SourceRegistryV2.cpp
    )
    target_compile_features(${QUEMUSIC_V2_FIXTURE_TARGET} PRIVATE cxx_std_17)
    target_compile_definitions(${QUEMUSIC_V2_FIXTURE_TARGET} PRIVATE
        QUEMUSIC_SOURCE_REGISTRY_V2_FIXTURE
        QUEMUSIC_SOURCE_REGISTRY_FIXTURE_PACKAGE_ID="${QUEMUSIC_V2_FIXTURE_PACKAGE_ID}"
        QUEMUSIC_SOURCE_REGISTRY_FIXTURE_SOURCE_ID="descriptor-other"
    )
    target_link_libraries(${QUEMUSIC_V2_FIXTURE_TARGET} PRIVATE quemusic_source_sdk)
    set_target_properties(${QUEMUSIC_V2_FIXTURE_TARGET} PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${QUEMUSIC_REGISTRY_V2_MISMATCH_FIXTURE_PACKAGE_DIR}"
        RUNTIME_OUTPUT_DIRECTORY "${QUEMUSIC_REGISTRY_V2_MISMATCH_FIXTURE_PACKAGE_DIR}"
    )
    generate_v2_source_plugin_manifest(${QUEMUSIC_V2_FIXTURE_TARGET}
                                       "${QUEMUSIC_REGISTRY_V2_MISMATCH_FIXTURE_PACKAGE_DIR}")

    qt_add_executable(quemusic_plugin_startup_test
        tests/tst_PluginStartup.cpp
    )
    target_compile_features(quemusic_plugin_startup_test PRIVATE cxx_std_17)
    target_link_libraries(quemusic_plugin_startup_test PRIVATE
        quemusic_source_startup
        quemusic_source_account_store
        quemusic_source_registry
        Qt6::Qml
        Qt6::Quick
        Qt6::Test
    )
    add_dependencies(quemusic_plugin_startup_test quemusic_navidrome_source)
    add_test(NAME quemusic_plugin_startup_test COMMAND quemusic_plugin_startup_test)

    qt_add_executable(quemusic_plugin_manifest_test
        core/plugins/PluginManifest.cpp
        core/plugins/PluginManifest.h
        tests/tst_PluginManifest.cpp
    )
    target_compile_features(quemusic_plugin_manifest_test PRIVATE cxx_std_17)
    target_include_directories(quemusic_plugin_manifest_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/core/plugins
    )
    target_link_libraries(quemusic_plugin_manifest_test PRIVATE
        quemusic_plugin_ui_sdk
        Qt6::Core
        Qt6::Test
    )
    add_test(NAME quemusic_plugin_manifest_test COMMAND quemusic_plugin_manifest_test)

    qt_add_executable(quemusic_plugin_manager_test
        tests/tst_PluginManager.cpp
    )
    target_compile_features(quemusic_plugin_manager_test PRIVATE cxx_std_17)
    target_include_directories(quemusic_plugin_manager_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/core/plugins
    )
    target_compile_definitions(quemusic_plugin_manager_test PRIVATE
        QUEMUSIC_TEST_V2_PLUGIN_PACKAGE_DIR="${QUEMUSIC_V2_PACKAGE_ROOT}/$<CONFIG>"
        QUEMUSIC_TEST_INVALID_V2_PLUGIN_PACKAGE_DIR="${QUEMUSIC_INVALID_V2_PACKAGE_ROOT}/$<CONFIG>"
        QUEMUSIC_TEST_BUILD_KEY="$<IF:$<CONFIG:Debug>,Debug,Release>"
    )
    target_link_libraries(quemusic_plugin_manager_test PRIVATE
        quemusic_plugin_core
        Qt6::Test
    )
    add_dependencies(quemusic_plugin_manager_test
        quemusic_fake_v2_source_fixture
        quemusic_fake_v2_wrong_contract_fixture
    )
    add_test(NAME quemusic_plugin_manager_test COMMAND quemusic_plugin_manager_test)
    add_test(NAME quemusic_plugin_v2_fixture_package_test
        COMMAND ${CMAKE_COMMAND}
            "-Dpackage_dir=$<TARGET_FILE_DIR:quemusic_fake_v2_source_fixture>"
            "-Dlibrary_name=$<TARGET_FILE_NAME:quemusic_fake_v2_source_fixture>"
            "-Dbuild_config=$<CONFIG>"
            "-Dexpected_architecture=${QUEMUSIC_TEST_ARCHITECTURE}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifyV2FixturePackage.cmake"
    )

    foreach(content_fixture IN ITEMS quemusic_source_registry_v2_fixture
            quemusic_source_registry_v2_secondary_fixture
            quemusic_source_registry_v2_mismatch_fixture)
        target_link_libraries(${content_fixture} PRIVATE quemusic_source_content_events)
    endforeach()
    qt_add_executable(quemusic_source_registry_v2_test
        tests/tst_SourceRegistryV2.cpp
    )
    target_compile_features(quemusic_source_registry_v2_test PRIVATE cxx_std_17)
    target_compile_definitions(quemusic_source_registry_v2_test PRIVATE
        QUEMUSIC_TEST_REGISTRY_V2_PLUGIN_PACKAGE_DIR="${QUEMUSIC_REGISTRY_V2_PACKAGE_ROOT}/$<CONFIG>"
        QUEMUSIC_TEST_REGISTRY_V2_SECOND_PLUGIN_PACKAGE_DIR="${QUEMUSIC_REGISTRY_V2_SECOND_PACKAGE_ROOT}/$<CONFIG>"
        QUEMUSIC_TEST_REGISTRY_V2_MISMATCH_PLUGIN_PACKAGE_DIR="${QUEMUSIC_REGISTRY_V2_MISMATCH_PACKAGE_ROOT}/$<CONFIG>"
    )
    target_link_libraries(quemusic_source_registry_v2_test PRIVATE
        quemusic_source_registry
        Qt6::Test
    )
    add_dependencies(quemusic_source_registry_v2_test
        quemusic_source_registry_v2_fixture
        quemusic_source_registry_v2_secondary_fixture
        quemusic_source_registry_v2_mismatch_fixture
    )
    add_test(NAME quemusic_source_registry_v2_test COMMAND quemusic_source_registry_v2_test)

    qt_add_executable(quemusic_plugin_settings_qml_test
        tests/tst_PluginSettingsQml.cpp
    )
    target_compile_features(quemusic_plugin_settings_qml_test PRIVATE cxx_std_17)
    target_compile_definitions(quemusic_plugin_settings_qml_test PRIVATE
        QUEMUSIC_QML_IMPORT_DIR="${CMAKE_CURRENT_BINARY_DIR}"
        QUEMUSIC_UI_FIXTURE_ROOT="${QUEMUSIC_UI_FIXTURE_ROOT}"
    )
    qt_add_resources(quemusic_plugin_settings_qml_test "quemusic_plugin_settings_qml_test_qml"
        PREFIX "/QueMusic"
        FILES
            ${QML_COMPONENTS}
            components/PluginSettingsText.js
    )
    target_link_libraries(quemusic_plugin_settings_qml_test PRIVATE
        quemusic_plugin_settings
        Qt6::Qml
        Qt6::Quick
        Qt6::Test
    )
    add_dependencies(quemusic_plugin_settings_qml_test quemusic_ui_fixture_valid)
    add_test(NAME quemusic_plugin_settings_qml_test COMMAND quemusic_plugin_settings_qml_test)
    set_tests_properties(quemusic_plugin_settings_qml_test PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )
endif()

qt_add_executable(${PROJECT_NAME}
    main.cpp
    $<$<PLATFORM_ID:Windows>:icon.rc>
)
target_link_libraries(${PROJECT_NAME} PRIVATE quemusic_plugin_ui
    quemusic_legacy_media_identity_resolver)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/core/plugin-ui
    ${CMAKE_CURRENT_SOURCE_DIR}/cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/api
    ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/taglib/taglib
    ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/taglib/taglib/toolkit
    ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/taglib/taglib/mpeg
    ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/taglib/taglib/mpeg/id3v2
    ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/taglib/taglib/mpeg/id3v2/frames
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

    set(quemusic_bundle_plugin_dir
        "$<TARGET_BUNDLE_DIR:QueMusic>/Contents/PlugIns/quemusic/navidrome")
    add_custom_target(quemusic_navidrome_bundle_plugin
        COMMAND ${CMAKE_COMMAND} -E remove_directory "${quemusic_bundle_plugin_dir}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${quemusic_bundle_plugin_dir}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:quemusic_navidrome_source>" "${quemusic_bundle_plugin_dir}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${QUEMUSIC_NAVIDROME_MANIFEST}" "${quemusic_bundle_plugin_dir}"
        DEPENDS quemusic_navidrome_source "${QUEMUSIC_NAVIDROME_MANIFEST}"
        VERBATIM)
    add_dependencies(QueMusic quemusic_navidrome_bundle_plugin)

    set(quemusic_local_bundle_plugin_dir
        "$<TARGET_BUNDLE_DIR:QueMusic>/Contents/PlugIns/quemusic/local")
    set(quemusic_local_bundle_frameworks_dir
        "$<TARGET_BUNDLE_DIR:QueMusic>/Contents/Frameworks")
    add_custom_target(quemusic_local_bundle_plugin
        COMMAND ${CMAKE_COMMAND} -E remove_directory "${quemusic_local_bundle_plugin_dir}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${quemusic_local_bundle_plugin_dir}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${quemusic_local_bundle_frameworks_dir}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:quemusic_local_source>" "${quemusic_local_bundle_plugin_dir}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${QUEMUSIC_LOCAL_MANIFEST}" "${quemusic_local_bundle_plugin_dir}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:quemusic_source_content_events>"
            "${quemusic_local_bundle_frameworks_dir}/$<TARGET_SONAME_FILE_NAME:quemusic_source_content_events>"
        DEPENDS quemusic_local_source quemusic_source_content_events
            "${QUEMUSIC_LOCAL_MANIFEST}"
        VERBATIM)
    add_dependencies(QueMusic quemusic_local_bundle_plugin)
endif()

if(APPLE)
    add_test(NAME quemusic_macos_local_bundle_plugin_test
        COMMAND ${CMAKE_COMMAND}
            "-Dpackage_dir=$<TARGET_BUNDLE_DIR:QueMusic>/Contents/PlugIns/quemusic/local"
            "-Dlibrary_name=$<TARGET_FILE_NAME:quemusic_local_source>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifyBundledPluginPackage.cmake")
    add_test(NAME quemusic_macos_local_bundle_plugin_incremental_test
        COMMAND ${CMAKE_COMMAND}
            "-Dbuild_dir=${CMAKE_BINARY_DIR}"
            "-Dbuild_config=$<CONFIG>"
            "-Dsource_library=$<TARGET_FILE:quemusic_local_source>"
            "-Dbundled_library=$<TARGET_BUNDLE_DIR:QueMusic>/Contents/PlugIns/quemusic/local/$<TARGET_FILE_NAME:quemusic_local_source>"
            "-Dbuild_target=quemusic_local_bundle_plugin"
            "-Dsource_manifest=${QUEMUSIC_LOCAL_MANIFEST}"
            "-Dbundled_manifest=$<TARGET_BUNDLE_DIR:QueMusic>/Contents/PlugIns/quemusic/local/manifest.json"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifyIncrementalBundledPlugin.cmake")
    set_tests_properties(quemusic_macos_local_bundle_plugin_incremental_test PROPERTIES
        DEPENDS quemusic_macos_local_bundle_plugin_test RUN_SERIAL TRUE)
    add_test(NAME quemusic_macos_bundle_plugin_test
        COMMAND ${CMAKE_COMMAND}
            "-Dpackage_dir=$<TARGET_BUNDLE_DIR:QueMusic>/Contents/PlugIns/quemusic/navidrome"
            "-Dlibrary_name=$<TARGET_FILE_NAME:quemusic_navidrome_source>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifyBundledPluginPackage.cmake")
    add_test(NAME quemusic_macos_bundle_plugin_incremental_test
        COMMAND ${CMAKE_COMMAND}
            "-Dbuild_dir=${CMAKE_BINARY_DIR}"
            "-Dbuild_config=$<CONFIG>"
            "-Dsource_library=$<TARGET_FILE:quemusic_navidrome_source>"
            "-Dbundled_library=$<TARGET_BUNDLE_DIR:QueMusic>/Contents/PlugIns/quemusic/navidrome/$<TARGET_FILE_NAME:quemusic_navidrome_source>"
            "-Dbuild_target=quemusic_navidrome_bundle_plugin"
            "-Dsource_manifest=${QUEMUSIC_NAVIDROME_MANIFEST}"
            "-Dbundled_manifest=$<TARGET_BUNDLE_DIR:QueMusic>/Contents/PlugIns/quemusic/navidrome/manifest.json"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifyIncrementalBundledPlugin.cmake")
    set_tests_properties(quemusic_macos_bundle_plugin_incremental_test PROPERTIES
        DEPENDS quemusic_macos_bundle_plugin_test
        RUN_SERIAL TRUE)
endif()

set_source_files_properties(
    components/Style.qml
    components/Options.qml
    PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt6_add_qml_module(${PROJECT_NAME} # 默认导入项目的
    URI QueMusic
    VERSION 1.0
    QML_FILES
        main.qml
        core/plugin-ui/PluginThemeBinding.qml
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
        components/PluginSettingsText.js
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
        core/plugin-ui/PluginThemeAdapter.cpp
        core/plugin-ui/PluginThemeAdapter.h
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
        cpp/LocalLyricsReader.cpp
        cpp/LocalLyricsReader.h
        cpp/CustomFlow.cpp
        cpp/CustomFlow.h
        cpp/WindowsSmtcManager.cpp
        cpp/WindowsSmtcManager.h
        core/logging/RuntimeLoggingPolicy.cpp
        core/logging/RuntimeLoggingPolicy.h
        cpp/LogManager.cpp
        cpp/LogManager.h
        api/MusicApiService.cpp
        api/MusicApiService.h
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
        tag
        cryptopp::cryptopp
        quemusic_meshgradientplugin
        quemusic_source_startup
        quemusic_source_account_store
        quemusic_source_registry
        quemusic_music_hub
        quemusic_original_ui_music_adapter
        quemusic_playback_coordinator
        quemusic_queue_history_store
        quemusic_qt_playback_controller
        z
        quemusic_plugin_settings
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
enable_testing()
find_package(Qt6 REQUIRED COMPONENTS Test)
add_executable(quemusic_local_lyrics_test
    tests/local_lyrics_reader_test.cpp
    cpp/LocalLyricsReader.cpp
)
target_include_directories(quemusic_local_lyrics_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/taglib/taglib
    ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/taglib/taglib/toolkit
    ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/taglib/taglib/mpeg
    ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/taglib/taglib/mpeg/id3v2
    ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/taglib/taglib/mpeg/id3v2/frames
)
target_link_libraries(quemusic_local_lyrics_test PRIVATE
    Qt6::Core
    Qt6::Test
    tag
)
add_test(NAME quemusic_local_lyrics_test COMMAND quemusic_local_lyrics_test)
target_link_libraries(quemusic_local_lyrics_test PRIVATE quemusic_local_media)
target_link_libraries(${PROJECT_NAME} PRIVATE quemusic_local_media)
target_link_libraries(${PROJECT_NAME} PRIVATE quemusic_legacy_local_folder_migration)

if(BUILD_TESTING)
    add_executable(quemusic_queue_history_codec_test tests/tst_QueueHistoryCodec.cpp)
    target_include_directories(quemusic_queue_history_codec_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(quemusic_queue_history_codec_test PRIVATE quemusic_queue_history_codec Qt6::Test)
    add_test(NAME quemusic_queue_history_codec_test COMMAND quemusic_queue_history_codec_test)
    qt_add_executable(quemusic_queue_history_store_test tests/tst_QueueHistoryStore.cpp)
    target_link_libraries(quemusic_queue_history_store_test PRIVATE quemusic_queue_history_store Qt6::Test)
    target_compile_definitions(quemusic_queue_history_store_test PRIVATE QUEMUSIC_TASK12C_PACKAGES="${QUEMUSIC_TASK12C_PACKAGE_ROOT}")
    add_dependencies(quemusic_queue_history_store_test quemusic_playback_fixture)
    add_test(NAME quemusic_queue_history_store_test COMMAND quemusic_queue_history_store_test)
    qt_add_executable(quemusic_queue_history_integration_test tests/tst_QueueHistoryIntegration.cpp)
    target_link_libraries(quemusic_queue_history_integration_test PRIVATE
        quemusic_queue_history_store Qt6::Test)
    target_compile_definitions(quemusic_queue_history_integration_test PRIVATE
        QUEMUSIC_PHASE4_PACKAGES="${CMAKE_BINARY_DIR}/plugins")
    add_dependencies(quemusic_queue_history_integration_test
        quemusic_local_source quemusic_navidrome_source)
    add_test(NAME quemusic_queue_history_integration_test
        COMMAND quemusic_queue_history_integration_test)
    set_tests_properties(quemusic_queue_history_integration_test PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 30)
    add_executable(quemusic_local_source_scanner_test tests/tst_LocalSourceScanner.cpp)
    target_include_directories(quemusic_local_source_scanner_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_include_directories(quemusic_local_source_scanner_test PRIVATE ${QUEMUSIC_LOCAL_TAGLIB_INCLUDES})
    target_link_libraries(quemusic_local_source_scanner_test PRIVATE
        quemusic_local_scanner Qt6::Gui Qt6::Test tag)
    add_test(NAME quemusic_local_source_scanner_test COMMAND quemusic_local_source_scanner_test)
    add_executable(quemusic_local_library_index_test tests/tst_LocalLibraryIndex.cpp)
    target_include_directories(quemusic_local_library_index_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(quemusic_local_library_index_test PRIVATE
        quemusic_local_library_index Qt6::Test)
    add_test(NAME quemusic_local_library_index_test COMMAND quemusic_local_library_index_test)
    add_executable(quemusic_local_identity_store_test tests/tst_LocalIdentityStore.cpp)
    target_include_directories(quemusic_local_identity_store_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(quemusic_local_identity_store_test PRIVATE
        quemusic_local_library_index Qt6::Test)
    add_test(NAME quemusic_local_identity_store_test COMMAND quemusic_local_identity_store_test)
    add_executable(quemusic_local_source_test tests/tst_LocalSource.cpp)
    target_sources(quemusic_local_source_test PRIVATE
        plugins/local-source/LocalSourcePlugin.cpp plugins/local-source/LocalSourcePlugin.h
        plugins/local-source/LocalSourceSession.cpp plugins/local-source/LocalSourceSession.h)
    target_include_directories(quemusic_local_source_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(quemusic_local_source_test PRIVATE
        quemusic_local_library_index quemusic_source_content_events Qt6::Test)
    add_dependencies(quemusic_local_source_test quemusic_local_source)
    add_test(NAME quemusic_local_source_test COMMAND quemusic_local_source_test)
    add_executable(quemusic_local_source_integration_test tests/tst_LocalSourceIntegration.cpp)
    target_compile_features(quemusic_local_source_integration_test PRIVATE cxx_std_17)
    target_compile_definitions(quemusic_local_source_integration_test PRIVATE
        QUEMUSIC_LOCAL_PACKAGE_ROOT="${CMAKE_BINARY_DIR}/plugins")
    target_link_libraries(quemusic_local_source_integration_test PRIVATE
        quemusic_playback_coordinator quemusic_legacy_media_identity_resolver Qt6::Test)
    add_dependencies(quemusic_local_source_integration_test quemusic_local_source)
    add_test(NAME quemusic_local_source_integration_test
        COMMAND quemusic_local_source_integration_test)
    add_executable(quemusic_local_source_package_probe tests/tst_LocalSourcePackage.cpp)
    target_link_libraries(quemusic_local_source_package_probe PRIVATE quemusic_plugin_core Qt6::Core)
    add_dependencies(quemusic_local_source_package_probe quemusic_local_source)
    add_test(NAME quemusic_local_source_package_test
        COMMAND ${CMAKE_COMMAND}
            "-Dbuild_dir=${CMAKE_BINARY_DIR}"
            "-Dbuild_config=$<CONFIG>"
            "-Dpackage_probe=$<TARGET_FILE:quemusic_local_source_package_probe>"
            "-Dlibrary_name=$<TARGET_FILE_NAME:quemusic_local_source>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifyInstalledLocalSourcePackage.cmake")
    if(APPLE)
        add_test(NAME quemusic_macos_local_relocated_bundle_test
            COMMAND ${CMAKE_COMMAND}
                "-Dapp_path=$<TARGET_BUNDLE_DIR:QueMusic>"
                "-Ddeploy_tool=$<TARGET_FILE_DIR:Qt6::qmake>/macdeployqt"
                "-Dpackage_probe=$<TARGET_FILE:quemusic_local_source_package_probe>"
                "-Dlibrary_name=$<TARGET_FILE_NAME:quemusic_local_source>"
                "-Dwork_root=${CMAKE_BINARY_DIR}/phase3-local-relocation"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifyDeployedLocalSourceBundle.cmake")
        set_tests_properties(quemusic_macos_local_relocated_bundle_test PROPERTIES
            DEPENDS quemusic_macos_local_bundle_plugin_incremental_test
            RUN_SERIAL TRUE)
    endif()
    add_executable(quemusic_legacy_local_folder_migration_test
        tests/tst_LegacyLocalFolderMigration.cpp)
    target_include_directories(quemusic_legacy_local_folder_migration_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(quemusic_legacy_local_folder_migration_test PRIVATE
        quemusic_legacy_local_folder_migration Qt6::Test)
    add_test(NAME quemusic_legacy_local_folder_migration_test
        COMMAND quemusic_legacy_local_folder_migration_test)
    qt_add_executable(quemusic_directory_library_controller_test
        tests/tst_DirectoryLibraryController.cpp)
    target_link_libraries(quemusic_directory_library_controller_test PRIVATE
        quemusic_music_hub Qt6::Test)
    target_compile_definitions(quemusic_directory_library_controller_test PRIVATE
        QUEMUSIC_TASK7_PACKAGES="${QUEMUSIC_TASK7_PACKAGE_ROOT}")
    add_dependencies(quemusic_directory_library_controller_test quemusic_hub_fixture)
    add_test(NAME quemusic_directory_library_controller_test
        COMMAND quemusic_directory_library_controller_test)
    qt_add_executable(quemusic_original_ui_local_directories_test
        tests/tst_OriginalUiLocalDirectories.cpp)
    target_link_libraries(quemusic_original_ui_local_directories_test PRIVATE
        quemusic_original_ui_music_adapter Qt6::Test)
    target_compile_definitions(quemusic_original_ui_local_directories_test PRIVATE
        QUEMUSIC_TASK7_PACKAGES="${QUEMUSIC_TASK7_PACKAGE_ROOT}")
    add_dependencies(quemusic_original_ui_local_directories_test quemusic_hub_fixture)
    add_test(NAME quemusic_original_ui_local_directories_test
        COMMAND quemusic_original_ui_local_directories_test)
    qt_add_executable(quemusic_local_directories_qml_test
        tests/tst_LocalDirectoriesQml.cpp)
    qt_add_resources(quemusic_local_directories_qml_test
        "quemusic_local_directories_qml_test_resources"
        PREFIX "/QueMusic" FILES
            ${QML_COMPONENTS}
            components/PluginSettingsText.js
            pages/FilePage.qml
            resources/app/musicpic.png
            tests/qml/tst_LocalDirectories.qml)
    target_link_libraries(quemusic_local_directories_qml_test PRIVATE
        quemusic_original_ui_music_adapter Qt6::Qml Qt6::Quick Qt6::Test)
    add_test(NAME quemusic_local_directories_qml_test
        COMMAND quemusic_local_directories_qml_test)
    set_tests_properties(quemusic_local_directories_qml_test PROPERTIES
        TIMEOUT 30 ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen;QML_IMPORT_PATH=${CMAKE_CURRENT_BINARY_DIR}")
endif()
