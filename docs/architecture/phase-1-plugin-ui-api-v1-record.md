# Phase 1 Plugin UI API 1.0 Baseline Record

> Completed: 2026-09-24
> Architecture baseline: `QueMusic-Plugin-Architecture-Final-Plan.md`
> Phase 0 base commit: `85b089c`
> Phase 1 implementation commit before this evidence record: `e8d01fb`

## Outcome

Phase 1 adds a separately versioned Plugin UI API 1.0 on top of the existing
Source SDK v2. It does not add UI methods to `IMusicSourcePluginV2` or
`IMusicSourceSessionV2`. Native Source plugins may optionally implement
`ISourceManagementUiProvider`; schema-only plugins remain valid.

The Host now validates manifest and runtime Provider agreement, publishes the
standalone `QueMusic.PluginUI 1.0` QML module, atomically maps the existing Host
theme to public read-only tokens, enforces a QML import allowlist, and installs
public CMake targets and QML artifacts. An out-of-repository fixture configures,
builds, passes import lint, loads through `PluginManager`, casts the Provider,
and instantiates its management page using the installed module. A second
Qt-only executable imports that installed module without linking any QueMusic
target, proving the generated shared QML plugin is independently deployable.

## Frozen API and ABI evidence

Source SDK v2 remains unchanged at:

```text
IID: org.quemusic.MusicSourcePlugin/2.0
ABI: 2
```

The final scan found the exact literals in
`sdk/source/v2/IMusicSourcePluginV2.h` and found no `settingsPage`,
`ManagementUi`, or `PluginUi` symbol in either Source v2 interface header.

Plugin UI 1.0 publishes:

```text
Provider IID: org.quemusic.SourceManagementUiProvider/1.0
Plugin UI API: 1.0
```

`PluginUiContext`, `PluginTheme`, and `ISourceManagementUiProvider` are separate
from Source SDK v2. Existing `PluginLease` semantics protect an active Provider
and backend; no second loader lifecycle or lease ABI was introduced.
`PluginUiContext.mode` retains the public C++ `enum class PluginUiMode` contract,
is exposed as that enum rather than an integer property, and publishes the
stable QML constants `PluginUiMode.Create` and `PluginUiMode.Edit`.

## Public QML surface

The public module contains 28 types:

- Containers: `PluginPage`, `PluginScrollPage`, `PluginSection`, `PluginGroup`.
- Text: `PluginLabel`, `PluginDescription`, `PluginSeparator`.
- Actions: `PluginButton`, `PluginPrimaryButton`, `PluginDangerButton`, `PluginIconButton`.
- Inputs: `PluginTextField`, `PluginPasswordField`, `PluginNumberField`, `PluginUrlField`, `PluginDirectoryField`.
- Selection: `PluginSwitch`, `PluginCheckBox`, `PluginComboBox`.
- State: `PluginStatus`, `PluginBadge`, `PluginBusyIndicator`, `PluginErrorState`, `PluginEmptyState`.
- Management: `PluginAccountCard`, `PluginServerCard`, `PluginQrCode`, `PluginQrLogin`.

The read-only `PluginTheme` singleton exposes colors `background`, `surface`,
`surfaceHover`, `primary`, `textPrimary`, `textSecondary`, `border`, `success`,
`warning`, and `danger`; spacing and radius small/medium/large values; caption,
body, and title fonts; `scaleFactor`, `dark`, and `reducedMotion`. A complete
token set is applied on the GUI thread before one `themeChanged` signal.

The public-boundary scan found no private `QueMusic 1.0` import or references to
`Style`, `QButton`, `QCard`, `Options`, `mainMedia`, or `MusicApi` under
`plugin-ui`, `sdk/plugin-ui`, or the external fixture.

Interaction coverage verifies forward Tab and Backtab focus traversal,
disabled-control skipping, accessible names, scale changes, and long text in a
120-pixel-wide layout without `QQmlEngine` warnings. `PluginQrCode` accepts
package-relative local assets while continuing to reject remote URLs.

## Manifest and import rules

A custom management page requires both:

```json
"pluginUiApi": "1.0",
"ui": { "management": "qml/ManagementPage.qml" }
```

The path must be a local package-relative existing QML file. Absolute, remote,
empty, missing, dot-segment, parent-escape, and encoded parent-escape paths are
rejected during discovery. Load rejects declaration-without-Provider,
Provider-without-declaration, and descriptor version/path/mode mismatch.

The import validator allows `QtQuick`, `QtQuick.Controls.Basic`,
`QtQuick.Layouts`, `QtQml`, `QtCore`, exact `QueMusic.PluginUI 1.x`, and relative
imports that resolve inside the supplied QML root. Tests reject the private Host
module, parent escape, remote imports, and unapproved vendor modules. The public
module itself is also scanned by this validator.

## Toolchain and reproducible verification

Verification environment:

```text
macOS 26.6.2 (25G83)
x86_64
Apple clang 21.0.0
CMake 3.30.5
Qt 6.11.1
Debug build, Unix Makefiles
```

Fresh configure and build:

```bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/cmake --fresh \
  -S . -B build-phase1-final -G "Unix Makefiles" \
  -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON \
  -DCMAKE_PREFIX_PATH=/Users/liqiang/Qt/6.11.1/macos
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/cmake \
  --build build-phase1-final --parallel 4
```

Both commands exited successfully. Configure retained pre-existing MeshGradient
output-directory and main-module Qt policy warnings. The Plugin UI QML module
uses a URI-matching output directory, maps its 28 public files directly into the
module root, and produced no corresponding warning.

Full regression command:

```bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/ctest \
  --test-dir build-phase1-final --output-on-failure
```

Result: **49/49 passed, 0 failed**, total test time 74.62 seconds. This exceeds
the Phase 0 baseline of 36 tests. The authoritative inventory was:

```text
quemusic_plugin_ui_import_allowed_test
quemusic_plugin_ui_import_private-import_test
quemusic_plugin_ui_import_parent-import_test
quemusic_plugin_ui_import_remote-import_test
quemusic_plugin_ui_import_unknown-import_test
quemusic_plugin_ui_import_public_module_test
quemusic_plugin_ui_external_test
quemusic_plugin_ui_theme_test
quemusic_plugin_ui_qml_test
quemusic_plugin_ui_contract_test
quemusic_plugin_ui_manifest_test
quemusic_plugin_ui_provider_test
quemusic_plugin_ui_unload_test
quemusic_navidrome_smoke_state_test
quemusic_source_v2_types_test
quemusic_music_page_model_test
quemusic_source_v2_contract_test
quemusic_source_settings_storage_test
quemusic_source_account_store_test
quemusic_playback_plugin_contract_test
quemusic_plugin_settings_operation_test
quemusic_plugin_settings_controller_test
quemusic_music_hub_test
quemusic_original_ui_music_adapter_test
quemusic_playback_coordinator_test
quemusic_qt_playback_controller_test
quemusic_playback_controls_adapter_qml_test
quemusic_legacy_queue_qml_test
quemusic_music_hub_qml_test
quemusic_original_ui_recommendation_qml_test
quemusic_original_ui_actions_qml_test
quemusic_original_ui_playback_qml_test
quemusic_sensitive_playback_logging_test
quemusic_original_ui_structure_test
quemusic_capability_resolver_test
quemusic_media_action_router_v2_test
quemusic_aggregate_composer_test
quemusic_page_repository_test
quemusic_music_caches_test
quemusic_plugin_startup_test
quemusic_plugin_manifest_test
quemusic_plugin_manager_test
quemusic_plugin_v2_fixture_package_test
quemusic_source_registry_v2_test
quemusic_plugin_settings_qml_test
quemusic_macos_bundle_plugin_test
quemusic_macos_bundle_plugin_incremental_test
quemusic_local_lyrics_test
quemusic_navidrome_source_test
```

The final ABI/private-boundary commands and `git diff --check 85b089c..HEAD`
all exited successfully. The installed CMake metadata contains no checkout,
`core/`, or `components/` path. The staged QML plugin has install RPATH
`@loader_path/../../../../../lib`, resolving its shared Host runtime from the
same installation prefix.

## Deferred to Phase 2 and later

- Phase 2: Host Custom Management Page container, navigation entry, context and
  backend lifecycle wiring, unload-time page closure, and generic error view.
- Phase 2: composition of Settings Schema and Custom UI, directory picker Host
  service, notification service, and secure Secret save/clear/configured calls.
- Phase 3: convert Local Music into a standard Source Plugin and remove the
  long-lived Legacy Local special path.
- Later migration stages: route every Source through `PlaybackCoordinator` and
  retire remaining source-specific Host UI/configuration paths according to the
  final architecture plan.

Phase 1 intentionally does not add a Navidrome, Kugou, Netease, or Local Music
custom management page and does not replace Source SDK v2.
