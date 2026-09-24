cmake_minimum_required(VERSION 3.16)

foreach(required PROJECT_SOURCE_DIR PROJECT_BUILD_DIR QT6_DIR HOST_ARCHITECTURE
                 HOST_BUILD_KEY LOADER PLUGIN_UI_RUNTIME)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "Missing ${required}")
    endif()
endforeach()

set(stage "${PROJECT_BUILD_DIR}/plugin-ui-external-stage")
set(fixture_source "${PROJECT_SOURCE_DIR}/tests/fixtures/plugin-ui-external")
set(fixture_build "${PROJECT_BUILD_DIR}/plugin-ui-external-build")

# Avoid stale archives from an earlier packaging layout masking install errors.
file(REMOVE_RECURSE "${stage}")

function(run_checked description)
    execute_process(COMMAND ${ARGN}
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "${description} failed (${result}):\n${output}\n${error}")
    endif()
endfunction()

run_checked("Host loader build" "${CMAKE_COMMAND}" --build "${PROJECT_BUILD_DIR}"
    --target quemusic_plugin_ui_external_loader --config "${HOST_BUILD_KEY}" --parallel 4)
run_checked("SDK install" "${CMAKE_COMMAND}" --install "${PROJECT_BUILD_DIR}"
    --config "${HOST_BUILD_KEY}" --component PluginSdk --prefix "${stage}")
run_checked("External configure" "${CMAKE_COMMAND}"
    -S "${fixture_source}" -B "${fixture_build}"
    "-DCMAKE_PREFIX_PATH=${stage}" "-DQt6_DIR=${QT6_DIR}"
    "-DCMAKE_BUILD_TYPE=${HOST_BUILD_KEY}"
    "-DHOST_ARCHITECTURE=${HOST_ARCHITECTURE}"
    "-DHOST_BUILD_KEY=${HOST_BUILD_KEY}")
run_checked("External build" "${CMAKE_COMMAND}" --build "${fixture_build}"
    --config "${HOST_BUILD_KEY}" --parallel 4)
run_checked("External QML import lint" "${CMAKE_COMMAND}"
    "-DROOT=${fixture_source}" "-DINPUT=${fixture_source}/ManagementPage.qml"
    -P "${PROJECT_SOURCE_DIR}/cmake/ValidatePluginQmlImports.cmake")
run_checked("Installed QML module probe" "${CMAKE_COMMAND}" -E env
    "QT_QPA_PLATFORM=offscreen" "${fixture_build}/external_qml_probe" "${stage}")
run_checked("External plugin and QML load" "${CMAKE_COMMAND}" -E env
    "QT_QPA_PLATFORM=offscreen" "${LOADER}" "${fixture_build}" "${stage}")

if(NOT EXISTS "${stage}/share/quemusic/qml/QueMusic/PluginUI/qmldir")
    message(FATAL_ERROR "Installed QML module qmldir is missing")
endif()
if(CMAKE_HOST_WIN32 AND NOT EXISTS
        "${stage}/share/quemusic/qml/QueMusic/PluginUI/${PLUGIN_UI_RUNTIME}")
    message(FATAL_ERROR "Installed QML module is missing its colocated Windows runtime")
endif()
