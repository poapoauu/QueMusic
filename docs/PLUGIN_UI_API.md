# QueMusic Plugin UI API 1.0

This is the public management-UI contract for native Source SDK v2 plugins. It
extends Source SDK v2 without changing `IMusicSourcePluginV2`, its IID, or its
ABI. The final architecture baseline is
[`docs/architecture/QueMusic-Plugin-Architecture-Final-Plan.md`](architecture/QueMusic-Plugin-Architecture-Final-Plan.md).

## C++ contract

Include `plugin-ui/v1/ISourceManagementUiProvider.h` and implement the optional
second interface on the same plugin QObject as `IMusicSourcePluginV2`:

```cpp
class MySource : public QObject, public IMusicSourcePluginV2,
                 public ISourceManagementUiProvider {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QUEMUSIC_MUSIC_SOURCE_PLUGIN_V2_IID)
    Q_INTERFACES(IMusicSourcePluginV2 ISourceManagementUiProvider)
    // Source SDK v2 methods plus managementUi() and createManagementBackend().
};
```

The Provider IID is `org.quemusic.SourceManagementUiProvider/1.0`; the UI API
version is `1.0`. A descriptor specifies that version, a package-relative local
QML component URL, and at least one supported mode (`supportsCreate` or
`supportsEdit`). The Host rejects a mismatch between descriptor and manifest.
The Host acquires a `PluginLease` before using the Provider and retains it until
the QML page, context, and backend have been destroyed. A non-null backend must
be parented to the supplied QObject. A null backend means the plugin cannot
serve that context; the future Host page container displays the generic error.

`PluginUiContextData` carries the Host-supplied package, source, instance and
account IDs and Create/Edit mode. Create may omit instance/account IDs; Edit
requires them. The plugin must not derive trusted identity from QML inputs.
`PluginUiContext` exposes these fields, backend, settings, capabilities, host,
and `valid` as read-only QML properties. On teardown the Host sets `valid=false`
and clears object pointers. Phase 2 will wire the live container and lifecycle.

## Manifest and QML

Add both fields to the Source v2 manifest:

```json
"pluginUiApi": "1.0",
"ui": { "management": "qml/ManagementPage.qml" }
```

The page must be inside the plugin package. Absolute, remote, missing, or
escaping paths are rejected. A plugin without a UI declaration must not
implement the Provider; it continues to use Host-rendered Settings Schema.

Import `QueMusic.PluginUI 1.0` and only the public Qt modules `QtQuick`,
`QtQuick.Controls.Basic`, `QtQuick.Layouts`, `QtQml`, and `QtCore`. Relative
imports are allowed only when they resolve inside the plugin's `qml/` root.
The package validation script is `cmake/ValidatePluginQmlImports.cmake`; pass
`-DROOT=<plugin-qml-directory>`. Private `QueMusic 1.0` and Host implementation
types are not part of this contract.

The 28 QML types are:

- Containers: `PluginPage`, `PluginScrollPage`, `PluginSection`, `PluginGroup`.
- Text: `PluginLabel`, `PluginDescription`, `PluginSeparator`.
- Actions: `PluginButton`, `PluginPrimaryButton`, `PluginDangerButton`, `PluginIconButton`.
- Input: `PluginTextField`, `PluginPasswordField`, `PluginNumberField`, `PluginUrlField`, `PluginDirectoryField`.
- Selection: `PluginSwitch`, `PluginCheckBox`, `PluginComboBox`.
- State: `PluginStatus`, `PluginBadge`, `PluginBusyIndicator`, `PluginErrorState`, `PluginEmptyState`.
- Management: `PluginAccountCard`, `PluginServerCard`, `PluginQrCode`, `PluginQrLogin`.

`PluginTheme` is a QML-read-only singleton. Its semantic colors are
`background`, `surface`, `surfaceHover`, `primary`, `textPrimary`,
`textSecondary`, `border`, `success`, `warning`, and `danger`. It also exposes
`spacingSmall/Medium/Large`, `radiusSmall/Medium/Large`,
`fontCaption/Body/Title`, `scaleFactor`, `dark`, and `reducedMotion`. The Host
updates a complete token set atomically; plugin QML cannot write these values.
No public component depends on the main application's private QML controls.

## Installation and secrets

Build the `PluginSdk` install component and consume the installed CMake package:

```cmake
find_package(Qt6 REQUIRED COMPONENTS Core Qml Quick)
find_package(QueMusicPluginSdk 1.0 REQUIRED)
target_link_libraries(my_source PRIVATE
    QueMusic::source_sdk QueMusic::plugin_ui_sdk Qt6::Core)
```

`QueMusic::plugin_ui` and `QueMusic::plugin_ui_qml_plugin` are the public Host
runtime/QML module targets. The installed QML module is under
`share/quemusic/qml/QueMusic/PluginUI`. The reference external package in
`tests/fixtures/plugin-ui-external` builds only from installed public targets.

The Host service surface never exposes Secret plaintext to plugin QML. QML may
request asynchronous save/clear and ask whether a Secret is configured; native
plugin backends use the Host-managed secure C++ storage path for credentials.
Phase 1 publishes the contract and UI Kit; Phase 2 adds the live Custom
Management Page container, navigation, error view, and teardown handling.
