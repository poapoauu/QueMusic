# Plugin UI API 1.0 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Freeze a standalone QueMusic Plugin UI API 1.0 without changing Source SDK v2, including the public C++ provider contract, manifest validation, versioned QML module, semantic theme, complete first-version UI Kit, import lint, and an external fixture.

**Architecture:** Add a header-only public `quemusic_plugin_ui_sdk`, a separately versioned `QueMusic.PluginUI 1.0` QML/runtime target, and a Host-private theme adapter. `PluginManager` validates the optional UI Provider against manifest declarations after the existing Source v2 cast, while the existing `PluginLease` remains the only unload guard.

**Tech Stack:** C++17, Qt 6 Core/Qml/Quick/Quick Controls Basic/Test, CMake 3.30, QTest, QML offscreen tests.

**Spec:** `docs/superpowers/specs/2026-09-23-plugin-ui-api-v1-design.md`

## Global Constraints

- Keep `QUEMUSIC_MUSIC_SOURCE_PLUGIN_V2_IID` exactly `org.quemusic.MusicSourcePlugin/2.0` and `QUEMUSIC_MUSIC_SOURCE_SDK_V2_ABI` exactly `2`.
- Do not add UI methods or fields to `IMusicSourcePluginV2` or `IMusicSourceSessionV2`.
- Plugin UI Provider IID is exactly `org.quemusic.SourceManagementUiProvider/1.0`; Plugin UI API version is exactly `1.0`.
- Public plugin include paths contain `sdk/plugin-ui/v1` and existing public Source SDK paths only; never expose `core/`, `components/`, or the repository root.
- `QueMusic.PluginUI 1.0` must not import `QueMusic 1.0` or use private QML components.
- Custom page hosting, navigation integration, plugin-specific login UI, and Local Source remain outside Phase 1.
- Every production behavior follows RED → observed expected failure → minimal GREEN → relevant suite → commit.

## Review Focus

1. **Manifest/Provider mismatch:** a declaration without a Provider, or a Provider without a declaration, must fail load without retaining a loader; covered in Task 3.
2. **Path escape and import escape:** encoded, normalized, absolute, remote, and `..` paths must not leave the plugin package; covered in Tasks 2 and 7.
3. **Half-applied themes:** controls must never observe a mixed old/new token set during runtime theme changes; covered in Task 4.
4. **Null and empty plugin data:** every public component must instantiate without a backend/model/image and emit no QML warning; covered in Tasks 5 and 6.
5. **Unload race:** active Provider/backend use must hold the existing lease until context teardown; covered in Task 3.

---

### Task 1: Freeze the Public C++ Plugin UI Contract

**Files:**
- Create: `sdk/plugin-ui/v1/PluginUiTypes.h`
- Create: `sdk/plugin-ui/v1/ISourceManagementUiProvider.h`
- Create: `tests/tst_PluginUiContract.cpp`
- Modify: `CMakeLists.txt:31-52`
- Modify: `CMakeLists.txt:209-226`

**Interfaces:**
- Consumes: Qt Core value types and the unchanged Source SDK v2 public headers.
- Produces: `PluginUiMode`, `PluginUiContextData`, `ManagementUiDescriptor`, `ISourceManagementUiProvider`, `quemusic_plugin_ui_sdk`.

- [ ] **Step 1: Write the failing contract test**

Create `tests/tst_PluginUiContract.cpp` with literal expectations independent of implementation helpers:

```cpp
#include "plugin-ui/v1/ISourceManagementUiProvider.h"
#include "v2/IMusicSourcePluginV2.h"

#include <QTest>

class PluginUiContractTest final : public QObject {
    Q_OBJECT
private slots:
    void freezesVersionedIdentifiers();
    void descriptorDefaultsFailClosed();
    void contextCarriesStableIdentity();
};

void PluginUiContractTest::freezesVersionedIdentifiers()
{
    QCOMPARE(QString::fromLatin1(QUEMUSIC_PLUGIN_UI_PROVIDER_V1_IID),
             QStringLiteral("org.quemusic.SourceManagementUiProvider/1.0"));
    QCOMPARE(QString::fromLatin1(QUEMUSIC_PLUGIN_UI_API_V1), QStringLiteral("1.0"));
    QCOMPARE(QString::fromLatin1(QUEMUSIC_MUSIC_SOURCE_PLUGIN_V2_IID),
             QStringLiteral("org.quemusic.MusicSourcePlugin/2.0"));
    QCOMPARE(QUEMUSIC_MUSIC_SOURCE_SDK_V2_ABI, 2);
}

void PluginUiContractTest::descriptorDefaultsFailClosed()
{
    const ManagementUiDescriptor descriptor;
    QVERIFY(descriptor.uiApiVersion.isEmpty());
    QVERIFY(descriptor.componentUrl.isEmpty());
    QVERIFY(!descriptor.supportsCreate);
    QVERIFY(!descriptor.supportsEdit);
}

void PluginUiContractTest::contextCarriesStableIdentity()
{
    const PluginUiContextData context{QStringLiteral("org.example.source"),
                                      QStringLiteral("example"),
                                      QStringLiteral("example/home"),
                                      QStringLiteral("home"),
                                      PluginUiMode::Edit};
    QCOMPARE(context.pluginPackageId, QStringLiteral("org.example.source"));
    QCOMPARE(context.sourceId, QStringLiteral("example"));
    QCOMPARE(context.sourceInstanceId, QStringLiteral("example/home"));
    QCOMPARE(context.accountId, QStringLiteral("home"));
    QCOMPARE(context.mode, PluginUiMode::Edit);
}

QTEST_MAIN(PluginUiContractTest)
#include "tst_PluginUiContract.moc"
```

Register `quemusic_plugin_ui_contract_test` under `BUILD_TESTING` and link `quemusic_plugin_ui_sdk`, `quemusic_source_sdk`, and `Qt6::Test`.

- [ ] **Step 2: Run RED and verify the missing public header is the failure**

Run:

```bash
cmake --build build-phase1 --target quemusic_plugin_ui_contract_test --parallel 4
```

Expected: compile failure because `plugin-ui/v1/ISourceManagementUiProvider.h` does not exist. A CMake syntax error or unrelated target failure is not an acceptable RED.

- [ ] **Step 3: Add the minimal public contract and INTERFACE target**

`PluginUiTypes.h`:

```cpp
#pragma once

#include <QString>
#include <QUrl>

#define QUEMUSIC_PLUGIN_UI_API_V1 "1.0"

enum class PluginUiMode { Create, Edit };

struct PluginUiContextData {
    QString pluginPackageId;
    QString sourceId;
    QString sourceInstanceId;
    QString accountId;
    PluginUiMode mode = PluginUiMode::Create;
};

struct ManagementUiDescriptor {
    QString uiApiVersion;
    QUrl componentUrl;
    bool supportsCreate = false;
    bool supportsEdit = false;
};
```

`ISourceManagementUiProvider.h`:

```cpp
#pragma once

#include "PluginUiTypes.h"

#include <QObject>
#include <QtPlugin>

#define QUEMUSIC_PLUGIN_UI_PROVIDER_V1_IID \
    "org.quemusic.SourceManagementUiProvider/1.0"

class ISourceManagementUiProvider {
public:
    virtual ~ISourceManagementUiProvider() = default;
    virtual ManagementUiDescriptor managementUi() const = 0;
    virtual QObject *createManagementBackend(const PluginUiContextData &context,
                                             QObject *parent) = 0;
};

Q_DECLARE_INTERFACE(ISourceManagementUiProvider,
                    QUEMUSIC_PLUGIN_UI_PROVIDER_V1_IID)
```

Add:

```cmake
add_library(quemusic_plugin_ui_sdk INTERFACE)
target_compile_features(quemusic_plugin_ui_sdk INTERFACE cxx_std_17)
target_link_libraries(quemusic_plugin_ui_sdk INTERFACE Qt6::Core)
target_include_directories(quemusic_plugin_ui_sdk INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/sdk)
```

- [ ] **Step 4: Run GREEN and the Source v2 contract regression**

Run:

```bash
cmake --build build-phase1 --target quemusic_plugin_ui_contract_test quemusic_source_v2_contract_test --parallel 4
ctest --test-dir build-phase1 --output-on-failure \
  -R 'quemusic_(plugin_ui_contract|source_v2_contract)_test'
```

Expected: 2/2 tests pass.

- [ ] **Step 5: Commit the frozen contract**

```bash
git add CMakeLists.txt sdk/plugin-ui/v1 tests/tst_PluginUiContract.cpp
git commit -m "feat: define plugin ui api v1 contract"
```

---

### Task 2: Validate Plugin UI Manifest Declarations

**Files:**
- Modify: `core/plugins/PluginManifest.h`
- Modify: `core/plugins/PluginManifest.cpp`
- Create: `tests/tst_PluginUiManifest.cpp`
- Modify: `CMakeLists.txt:813-850`

**Interfaces:**
- Consumes: `QUEMUSIC_PLUGIN_UI_API_V1` from Task 1.
- Produces: `hasManagementUi()`, `pluginUiApiVersion()`, `managementUiRelativePath()` and strict package-contained path validation.

- [ ] **Step 1: Add failing table-driven manifest tests**

Create `PluginUiManifestTest` with:

```cpp
void acceptsSchemaOnlyPluginWithoutUi();
void acceptsPluginUiV1Declaration();
void rejectsInvalidPluginUiDeclaration_data();
void rejectsInvalidPluginUiDeclaration();
```

The valid case creates `qml/ManagementPage.qml`, adds the following literal JSON, and checks all three getters:

```cpp
manifest.insert(QStringLiteral("pluginUiApi"), QStringLiteral("1.0"));
manifest.insert(QStringLiteral("ui"),
                QJsonObject{{QStringLiteral("management"),
                             QStringLiteral("qml/ManagementPage.qml")}});
```

The data rows are:

```text
api-missing-with-ui       -> Manifest management UI requires pluginUiApi
api-number                -> Manifest plugin UI API must be a string
api-unsupported           -> Manifest plugin UI API is unsupported
ui-missing-with-api       -> Manifest plugin UI management path is required
ui-array                  -> Manifest UI must be an object
management-empty          -> Manifest plugin UI management path is invalid
management-absolute       -> Manifest plugin UI management path must be package-relative
management-parent         -> Manifest plugin UI management path escapes package directory
management-encoded-parent -> Manifest plugin UI management path escapes package directory
management-dot            -> Manifest plugin UI management path escapes package directory
management-remote         -> Manifest plugin UI management path must be package-relative
management-missing-file   -> Manifest plugin UI management file does not exist
```

Also assert that a schema-only manifest yields `hasManagementUi() == false` and remains valid.

- [ ] **Step 2: Run RED and confirm the new valid manifest lacks getters**

Run:

```bash
cmake --build build-phase1 --target quemusic_plugin_ui_manifest_test --parallel 4
```

Expected: compile failure on the missing PluginManifest UI getters.

- [ ] **Step 3: Implement strict UI declaration parsing**

Add fields and getters to `PluginManifest`:

```cpp
bool hasManagementUi() const;
QString pluginUiApiVersion() const;
QString managementUiRelativePath() const;

bool m_hasManagementUi = false;
QString m_pluginUiApiVersion;
QString m_managementUiRelativePath;
```

Parse the pair as one optional unit. Validate the path with slash-normalized components and canonical containment:

```cpp
const QString decoded = QUrl::fromPercentEncoding(path.toUtf8());
const QStringList parts = decoded.split(QLatin1Char('/'), Qt::KeepEmptyParts);
if (QFileInfo(decoded).isAbsolute() || !QUrl(path).scheme().isEmpty()) {
    return invalidManifest(
        QStringLiteral("Manifest plugin UI management path must be package-relative"), error);
}
if (parts.contains(QString()) || parts.contains(QStringLiteral("."))
    || parts.contains(QStringLiteral(".."))) {
    return invalidManifest(
        QStringLiteral("Manifest plugin UI management path escapes package directory"), error);
}
const QFileInfo pageInfo(QDir(packageRoot).filePath(decoded));
const QString pagePath = pageInfo.canonicalFilePath();
if (!pageInfo.isFile() || !pagePath.startsWith(packageRoot + QDir::separator())) {
    return invalidManifest(
        QStringLiteral("Manifest plugin UI management file does not exist"), error);
}
```

Use the exact errors from Step 1. Store the normalized slash-separated relative path, not the absolute path.

- [ ] **Step 4: Run GREEN and existing manifest/manager tests**

```bash
cmake --build build-phase1 --target \
  quemusic_plugin_ui_manifest_test quemusic_plugin_manifest_test \
  quemusic_plugin_manager_test --parallel 4
ctest --test-dir build-phase1 --output-on-failure \
  -R 'quemusic_(plugin_ui_manifest|plugin_manifest|plugin_manager)_test'
```

Expected: all three test executables pass; schema-only Navidrome remains discoverable.

- [ ] **Step 5: Commit manifest support**

```bash
git add core/plugins/PluginManifest.h core/plugins/PluginManifest.cpp \
  tests/tst_PluginUiManifest.cpp CMakeLists.txt
git commit -m "feat: validate plugin ui manifest declarations"
```

---

### Task 3: Enforce Provider Consistency and Existing Lease Semantics

**Files:**
- Create: `tests/fixtures/PluginUiFixturePlugin.h`
- Create: `tests/fixtures/PluginUiFixturePlugin.cpp`
- Create: `tests/fixtures/plugin-ui-manifest.json.in`
- Create: `tests/fixtures/plugin-ui/ManagementPage.qml`
- Create: `tests/tst_PluginUiProvider.cpp`
- Create: `tests/tst_PluginUiUnload.cpp`
- Modify: `core/plugins/PluginManager.cpp:204-265`
- Modify: `CMakeLists.txt:780-850`

**Interfaces:**
- Consumes: Task 1 Provider interface, Task 2 manifest getters, existing `PluginLease` and Source v2 fixture conventions.
- Produces: load-time manifest/Provider consistency and tested UI Provider lease usage.

- [ ] **Step 1: Add fixture variants and failing runtime tests**

Build three packages from one fixture source using compile definitions:

```text
plugin-ui-valid              manifest + matching Provider
plugin-ui-provider-missing   manifest + no Provider
plugin-ui-undeclared         no manifest UI + Provider
```

The valid fixture implements both interfaces:

```cpp
class PluginUiFixturePlugin final : public QObject,
                                    public IMusicSourcePluginV2,
                                    public ISourceManagementUiProvider {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QUEMUSIC_MUSIC_SOURCE_PLUGIN_V2_IID)
    Q_INTERFACES(IMusicSourcePluginV2 ISourceManagementUiProvider)
public:
    SourceDescriptorV2 descriptor() const override;
    IMusicSourceSessionV2 *createSession(const SourceConfigurationV2 &, QObject *) override;
    ManagementUiDescriptor managementUi() const override;
    QObject *createManagementBackend(const PluginUiContextData &, QObject *) override;
};
```

Return this exact descriptor:

```cpp
return {QStringLiteral("1.0"),
        QUrl(QStringLiteral("qml/ManagementPage.qml")), true, true};
```

The backend factory returns `new QObject(parent)` unless the fixture definition requests a null backend.

In `tst_PluginUiProvider.cpp`, add tests that:

- load the valid package and cast both interfaces;
- reject declaration-without-Provider;
- reject Provider-without-declaration;
- reject descriptor version/path/mode mismatch rows;
- verify mismatch packages have zero active leases and no exposed instance.

In `tst_PluginUiUnload.cpp`, load the valid package and exercise only the ownership sequence:

```cpp
PluginLease lease = manager.acquire(packageId);
QVERIFY(lease.isValid());
auto *provider = qobject_cast<ISourceManagementUiProvider *>(manager.pluginInstance(packageId));
QVERIFY(provider != nullptr);
QScopedPointer<QObject> owner(new QObject);
QObject *backend = provider->createManagementBackend(editContext, owner.data());
QVERIFY(backend != nullptr);
QCOMPARE(backend->parent(), owner.data());
QCOMPARE(manager.unload(packageId), PluginOperationResult::Busy);
owner.reset();
lease = {};
QCOMPARE(manager.unload(packageId), PluginOperationResult::Success);
```

Add a second row proving a failed Provider mismatch leaves `activeLeases == 0` and unload is safe.

- [ ] **Step 2: Run RED and confirm invalid packages currently load**

```bash
cmake --build build-phase1 --target \
  quemusic_plugin_ui_provider_test quemusic_plugin_ui_unload_test --parallel 4
ctest --test-dir build-phase1 --output-on-failure \
  -R 'quemusic_plugin_ui_(provider|unload)_test'
```

Expected: the mismatch assertions fail because `PluginManager::load()` currently validates only `IMusicSourcePluginV2`.

- [ ] **Step 3: Add Provider/manifest validation after the Source v2 cast**

Add `ISourceManagementUiProvider.h` to `PluginManager.cpp` and apply this decision before storing `entry->loader`:

```cpp
auto *uiProvider = qobject_cast<ISourceManagementUiProvider *>(instance);
if (entry->manifest.hasManagementUi() != (uiProvider != nullptr)) {
    loader->unload();
    fail(*entry, entry->manifest.hasManagementUi()
        ? QStringLiteral("Package declares management UI but does not implement its Provider")
        : QStringLiteral("Package implements management UI Provider without manifest declaration"));
    return false;
}
if (uiProvider != nullptr) {
    const ManagementUiDescriptor ui = uiProvider->managementUi();
    if (ui.uiApiVersion != entry->manifest.pluginUiApiVersion()
        || ui.componentUrl.toString() != entry->manifest.managementUiRelativePath()
        || (!ui.supportsCreate && !ui.supportsEdit)) {
        loader->unload();
        fail(*entry, QStringLiteral("Package management UI Provider does not match manifest"));
        return false;
    }
}
```

Link `quemusic_plugin_core` publicly to `quemusic_plugin_ui_sdk`. Do not add a new lease class.

- [ ] **Step 4: Run GREEN plus loader lifecycle regressions**

```bash
cmake --build build-phase1 --target \
  quemusic_plugin_ui_provider_test quemusic_plugin_ui_unload_test \
  quemusic_plugin_manager_test \
  quemusic_plugin_startup_test --parallel 4
ctest --test-dir build-phase1 --output-on-failure \
  -R 'quemusic_(plugin_ui_(provider|unload)|plugin_manager|plugin_startup)_test'
```

Expected: 4/4 test executables pass and mismatch packages expose no instance.

- [ ] **Step 5: Commit runtime validation**

```bash
git add CMakeLists.txt core/plugins/PluginManager.cpp \
  tests/fixtures/PluginUiFixturePlugin.* \
  tests/fixtures/plugin-ui-manifest.json.in \
  tests/fixtures/plugin-ui/ManagementPage.qml \
  tests/tst_PluginUiProvider.cpp tests/tst_PluginUiUnload.cpp
git commit -m "feat: enforce plugin ui provider declarations"
```

---

### Task 4: Add PluginUiContext, Atomic PluginTheme, and the Public QML Module

**Files:**
- Create: `plugin-ui/PluginUiContext.h`
- Create: `plugin-ui/PluginUiContext.cpp`
- Create: `plugin-ui/PluginTheme.h`
- Create: `plugin-ui/PluginTheme.cpp`
- Create: `core/plugin-ui/PluginThemeAdapter.h`
- Create: `core/plugin-ui/PluginThemeAdapter.cpp`
- Create: `core/plugin-ui/PluginThemeBinding.qml`
- Create: `plugin-ui/qml/PluginPage.qml`
- Create: `tests/tst_PluginUiTheme.cpp`
- Create: `tests/tst_PluginUiQml.cpp`
- Modify: `CMakeLists.txt:31-205`
- Modify: `main.cpp`
- Modify: `main.qml`

**Interfaces:**
- Consumes: Task 1 context value type, existing private `Style` values.
- Produces: `QueMusic.PluginUI 1.0`, read-only `PluginTheme`, uncreatable `PluginUiContext`, Host-private adapter.

- [ ] **Step 1: Write failing theme and import tests**

`tst_PluginUiTheme.cpp` creates the singleton, applies two literal palettes through `PluginThemeAdapter`, and asserts:

```cpp
QCOMPARE(theme->background(), QColor("#ffffff"));
QCOMPARE(theme->textPrimary(), QColor("#101014"));
QCOMPARE(theme->dark(), false);
QSignalSpy changed(theme, &PluginTheme::themeChanged);
adapter.apply(darkTokens);
QCOMPARE(changed.count(), 1);
QCOMPARE(theme->background(), QColor("#101014"));
QCOMPARE(theme->danger(), QColor("#ff6b6b"));
QCOMPARE(theme->scaleFactor(), 1.25);
QCOMPARE(theme->dark(), true);
```

`tst_PluginUiQml.cpp` loads this source with `QQmlEngine`:

```qml
import QtQuick
import QueMusic.PluginUI 1.0
PluginPage {
    width: 480
    title: "Fixture"
}
```

It asserts a non-null root and an empty warning list.

- [ ] **Step 2: Run RED and verify the missing QML URI is reported**

```bash
cmake --build build-phase1 --target \
  quemusic_plugin_ui_theme_test quemusic_plugin_ui_qml_test --parallel 4
ctest --test-dir build-phase1 --output-on-failure \
  -R 'quemusic_plugin_ui_(theme|qml)_test'
```

Expected: QML import failure for `QueMusic.PluginUI`; the theme target also fails because runtime classes are missing.

- [ ] **Step 3: Implement readonly context and atomic theme state**

`PluginUiContext` has constant/read-only Q_PROPERTY declarations for the nine fields in the spec and Host-only setters used by the future container. `invalidate()` clears QObject pointers, sets `valid=false`, and emits one `contextChanged()` signal.

Define one value object for atomic theme updates:

```cpp
struct PluginThemeTokens {
    QColor background, surface, surfaceHover, primary;
    QColor textPrimary, textSecondary, border, success, warning, danger;
    qreal spacingSmall = 6, spacingMedium = 12, spacingLarge = 20;
    qreal radiusSmall = 6, radiusMedium = 10, radiusLarge = 16;
    QFont fontCaption, fontBody, fontTitle;
    qreal scaleFactor = 1.0;
    bool dark = false;
    bool reducedMotion = false;
};
```

`PluginTheme::apply(const PluginThemeTokens &)` assigns all members first, then emits exactly one `themeChanged()`. Expose Q_PROPERTY getters only; do not mark `apply` invokable or slot.

`PluginThemeAdapter` owns a pointer to the singleton and is registered only in the private `QueMusic` module. Its private `apply(QVariantMap)` converts one complete map to `PluginThemeTokens`, validates every required key, and calls `apply()` only after the full map is valid.

`core/plugin-ui/PluginThemeBinding.qml` is part of the private `QueMusic 1.0` module and is instantiated once by `main.qml`. It is the only QML file allowed to read `Style` for Plugin UI:

```qml
import QtQuick
import QueMusic.PluginUI 1.0

QtObject {
    id: root
    function synchronize() {
        PluginThemeAdapter.apply({
            background: Style.themes.fullColor,
            surface: Style.themes.primaryColor,
            surfaceHover: Style.themes.hoverColor,
            primary: Style.themes.themeColor,
            textPrimary: Style.themes.fontColor,
            textSecondary: Style.themes.textColor,
            border: Style.themes.borderColor,
            success: Style.darkis ? "#55d98a" : "#168447",
            warning: Style.darkis ? "#ffd166" : "#9a6500",
            danger: Style.darkis ? "#ff6b6b" : "#b42318",
            spacingSmall: 6,
            spacingMedium: 12,
            spacingLarge: 20,
            radiusSmall: 6,
            radiusMedium: 10,
            radiusLarge: 16,
            fontCaptionPixelSize: Style.settings.textTip,
            fontBodyPixelSize: Style.settings.text,
            fontTitlePixelSize: Style.settings.textH2,
            fontFamily: Style.settings.fontFamily,
            scaleFactor: 1.0,
            dark: Style.darkis,
            reducedMotion: !Style.settings.premiumAnime
        })
    }
    Component.onCompleted: synchronize()
    Connections {
        target: Style
        function onChangeTheme() { root.synchronize() }
        function onChangeUi() { root.synchronize() }
    }
}
```

The private adapter is added to the existing `QueMusic` module `SOURCES`; the binding is added to that module's `QML_FILES`. Public module files never import the private URI.

- [ ] **Step 4: Register the standalone QML module**

Add a dedicated target:

```cmake
qt_add_library(quemusic_plugin_ui STATIC)
qt_add_qml_module(quemusic_plugin_ui
    URI QueMusic.PluginUI
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    SOURCES
        plugin-ui/PluginUiContext.cpp plugin-ui/PluginUiContext.h
        plugin-ui/PluginTheme.cpp plugin-ui/PluginTheme.h
    QML_FILES plugin-ui/qml/PluginPage.qml)
target_link_libraries(quemusic_plugin_ui PUBLIC
    quemusic_plugin_ui_sdk Qt6::Core Qt6::Qml Qt6::Quick)
```

Register `PluginTheme` as a singleton and `PluginUiContext` as uncreatable in the generated module registration path. Link the app and both tests to the module/plugin target and set `QML_IMPORT_PATH=${CMAKE_CURRENT_BINARY_DIR}` for offscreen tests.

`PluginPage.qml` uses only public imports and provides `title`, default content, Theme background, padding, and accessible name.

- [ ] **Step 5: Run GREEN and verify readonly/atomic behavior**

```bash
cmake --build build-phase1 --target \
  quemusic_plugin_ui_theme_test quemusic_plugin_ui_qml_test QueMusic --parallel 4
ctest --test-dir build-phase1 --output-on-failure \
  -R 'quemusic_plugin_ui_(theme|qml)_test'
```

Expected: both tests pass offscreen; a QML assignment probe to `PluginTheme.primary` fails with a readonly-property error captured as the expected test result.

- [ ] **Step 6: Commit module foundation**

```bash
git add CMakeLists.txt main.cpp plugin-ui core/plugin-ui \
  tests/tst_PluginUiTheme.cpp tests/tst_PluginUiQml.cpp
git commit -m "feat: add plugin ui qml module and theme"
```

---

### Task 5: Implement Containers, Text, Actions, Inputs, and Selection Controls

**Files:**
- Create: `plugin-ui/qml/PluginScrollPage.qml`
- Create: `plugin-ui/qml/PluginSection.qml`
- Create: `plugin-ui/qml/PluginGroup.qml`
- Create: `plugin-ui/qml/PluginLabel.qml`
- Create: `plugin-ui/qml/PluginDescription.qml`
- Create: `plugin-ui/qml/PluginSeparator.qml`
- Create: `plugin-ui/qml/PluginButton.qml`
- Create: `plugin-ui/qml/PluginPrimaryButton.qml`
- Create: `plugin-ui/qml/PluginDangerButton.qml`
- Create: `plugin-ui/qml/PluginIconButton.qml`
- Create: `plugin-ui/qml/PluginTextField.qml`
- Create: `plugin-ui/qml/PluginPasswordField.qml`
- Create: `plugin-ui/qml/PluginNumberField.qml`
- Create: `plugin-ui/qml/PluginUrlField.qml`
- Create: `plugin-ui/qml/PluginDirectoryField.qml`
- Create: `plugin-ui/qml/PluginSwitch.qml`
- Create: `plugin-ui/qml/PluginCheckBox.qml`
- Create: `plugin-ui/qml/PluginComboBox.qml`
- Modify: `tests/tst_PluginUiQml.cpp`
- Modify: `tests/tst_PluginUiTheme.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: Task 4 module and Theme tokens.
- Produces: the complete general-purpose layout, text, action, input, and selection surface.

- [ ] **Step 1: Add a failing component matrix test**

Use a literal list of all 18 new type names and instantiate each through `QQmlComponent`:

```cpp
const QStringList types{
    "PluginScrollPage", "PluginSection", "PluginGroup",
    "PluginLabel", "PluginDescription", "PluginSeparator",
    "PluginButton", "PluginPrimaryButton", "PluginDangerButton", "PluginIconButton",
    "PluginTextField", "PluginPasswordField", "PluginNumberField", "PluginUrlField",
    "PluginDirectoryField", "PluginSwitch", "PluginCheckBox", "PluginComboBox"
};
```

For behavior tests, load one form containing every interactive control and verify:

- Tab advances in declaration order and Backtab reverses it;
- disabled button does not emit `clicked`;
- password echo mode is `TextInput.Password`;
- number boundaries clamp/reject outside values;
- malformed URL makes `acceptableInput == false`;
- directory browse click emits exactly one `browseRequested`;
- switch/checkbox/combobox preserve literal values;
- scale factor 1.25 increases body text and control implicit heights.

- [ ] **Step 2: Run RED and confirm missing QML type errors name the first type**

```bash
cmake --build build-phase1 --target quemusic_plugin_ui_qml_test --parallel 4
ctest --test-dir build-phase1 --output-on-failure \
  -R quemusic_plugin_ui_qml_test
```

Expected: test fails because `PluginScrollPage` is unavailable.

- [ ] **Step 3: Implement shared public behavior without private components**

Use Qt Quick Controls Basic directly. The base button shape is:

```qml
import QtQuick
import QtQuick.Controls.Basic

Button {
    id: root
    property color accentColor: PluginTheme.surface
    Accessible.name: text
    activeFocusOnTab: true
    implicitHeight: 36 * PluginTheme.scaleFactor
    leftPadding: PluginTheme.spacingMedium
    rightPadding: PluginTheme.spacingMedium
    background: Rectangle {
        radius: PluginTheme.radiusMedium
        color: !root.enabled ? PluginTheme.surface
              : root.down ? Qt.darker(root.accentColor, 1.08)
              : root.hovered ? PluginTheme.surfaceHover : root.accentColor
        border.color: root.activeFocus ? PluginTheme.primary : PluginTheme.border
        border.width: root.activeFocus ? 2 : 1
    }
    contentItem: Text {
        text: root.text
        color: root.enabled ? PluginTheme.textPrimary : PluginTheme.textSecondary
        font: PluginTheme.fontBody
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
}
```

`PluginPrimaryButton` and `PluginDangerButton` derive from `PluginButton` and set `accentColor` to `primary`/`danger`. `PluginIconButton` adds `iconText` and a square implicit size.

Input components derive from Qt Basic controls, expose only their semantic values, and use the same focus border. `PluginDirectoryField` composes a readonly text field plus button and emits `browseRequested()`. Containers expose default-property content; text types expose `text`, wrapping, and the matching Theme font.

- [ ] **Step 4: Register every QML file explicitly and run GREEN**

Add each file explicitly to the `QML_FILES` block of `qt_add_qml_module(quemusic_plugin_ui)`; do not use a glob for the public API surface.

```bash
cmake --build build-phase1 --target \
  quemusic_plugin_ui_qml_test quemusic_plugin_ui_theme_test --parallel 4
ctest --test-dir build-phase1 --output-on-failure \
  -R 'quemusic_plugin_ui_(qml|theme)_test'
```

Expected: matrix, focus, disabled, validation, signal, and scaling tests pass with no QML warnings.

- [ ] **Step 5: Commit general UI Kit controls**

```bash
git add CMakeLists.txt plugin-ui/qml tests/tst_PluginUiQml.cpp \
  tests/tst_PluginUiTheme.cpp
git commit -m "feat: add plugin ui form and action controls"
```

---

### Task 6: Implement Status, Empty/Error, Account/Server, and QR Components

**Files:**
- Create: `plugin-ui/qml/PluginStatus.qml`
- Create: `plugin-ui/qml/PluginBadge.qml`
- Create: `plugin-ui/qml/PluginBusyIndicator.qml`
- Create: `plugin-ui/qml/PluginErrorState.qml`
- Create: `plugin-ui/qml/PluginEmptyState.qml`
- Create: `plugin-ui/qml/PluginAccountCard.qml`
- Create: `plugin-ui/qml/PluginServerCard.qml`
- Create: `plugin-ui/qml/PluginQrCode.qml`
- Create: `plugin-ui/qml/PluginQrLogin.qml`
- Modify: `tests/tst_PluginUiQml.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: Tasks 4-5 Theme and control primitives.
- Produces: all remaining public UI Kit types; the complete Phase 1 QML surface.

- [ ] **Step 1: Add failing state and business-component tests**

Extend the literal matrix with all nine types. Add tests for:

```text
PluginStatus      neutral/success/warning/error maps to textSecondary/success/warning/danger
PluginBadge       empty text remains valid and has zero/compact content width
BusyIndicator    running=false is not animated; reducedMotion avoids rotation animation
ErrorState       actionVisible=false is safe; triggering visible action emits retryRequested once
EmptyState       empty title/description is safe; optional action emits actionRequested once
AccountCard      empty account and null actions instantiate without warnings
ServerCard       disconnected status uses warning/error semantic state
QrCode           empty/invalid URL enters error state without engine warning
QrLogin          refresh/cancel emit once; null backend and empty QR are safe
```

- [ ] **Step 2: Run RED and confirm the first missing status type**

```bash
cmake --build build-phase1 --target quemusic_plugin_ui_qml_test --parallel 4
ctest --test-dir build-phase1 --output-on-failure \
  -R quemusic_plugin_ui_qml_test
```

Expected: missing `PluginStatus` causes the targeted failure.

- [ ] **Step 3: Implement semantic state and composite components**

Use one literal semantic mapping in `PluginStatus`:

```qml
readonly property color statusColor:
    status === "success" ? PluginTheme.success
  : status === "warning" ? PluginTheme.warning
  : status === "error" ? PluginTheme.danger
  : PluginTheme.textSecondary
```

`PluginBadge` reuses the same mapping. `PluginErrorState` and `PluginEmptyState` compose public text/button types only. Account and server cards expose generic `title`, `subtitle`, `status`, `statusText`, `actionText`, and action signals; no provider-specific fields.

`PluginQrCode` uses a guarded `Image` and changes to error state on `Image.Error`. `PluginQrLogin` exposes `qrSource`, `status`, `statusText`, `busy`, `refreshRequested()`, and `cancelRequested()` and contains no timer, network call, or authentication state machine.

- [ ] **Step 4: Register files and run the complete QML/Theme tests**

```bash
cmake --build build-phase1 --target \
  quemusic_plugin_ui_qml_test quemusic_plugin_ui_theme_test --parallel 4
ctest --test-dir build-phase1 --output-on-failure \
  -R 'quemusic_plugin_ui_(qml|theme)_test'
```

Expected: all 28 public QML types (`PluginPage` plus 18 Task 5 types plus 9 Task 6 types) instantiate and all behavior rows pass without QML warnings.

- [ ] **Step 5: Commit the complete UI Kit**

```bash
git add CMakeLists.txt plugin-ui/qml tests/tst_PluginUiQml.cpp
git commit -m "feat: complete plugin ui status and management kit"
```

---

### Task 7: Enforce Imports and Prove an External Public-SDK Build

**Files:**
- Create: `cmake/ValidatePluginQmlImports.cmake`
- Create: `cmake/VerifyPluginUiExternalFixture.cmake`
- Create: `cmake/QueMusicPluginSdkConfig.cmake.in`
- Create: `tests/fixtures/plugin-ui-external/CMakeLists.txt`
- Create: `tests/fixtures/plugin-ui-external/ExternalPlugin.cpp`
- Create: `tests/fixtures/plugin-ui-external/ManagementPage.qml`
- Create: `tests/fixtures/plugin-ui-lint/allowed.qml`
- Create: `tests/fixtures/plugin-ui-lint/private-import.qml`
- Create: `tests/fixtures/plugin-ui-lint/parent-import.qml`
- Create: `tests/fixtures/plugin-ui-lint/remote-import.qml`
- Create: `tests/fixtures/plugin-ui-lint/unknown-import.qml`
- Create: `docs/PLUGIN_UI_API.md`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: installed/exported targets and complete QML module from Tasks 1-6.
- Produces: deterministic import lint, external build/load proof, public developer documentation.

- [ ] **Step 1: Add failing CTest scripts for allowed and rejected imports**

Register one success test and four `WILL_FAIL` tests. The linter receives `ROOT`, scans `*.qml`, and reports `file:line: rejected import <uri>`.

Fixture imports are literal:

```qml
// allowed.qml
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QueMusic.PluginUI 1.0

// private-import.qml
import QueMusic 1.0

// parent-import.qml
import "../private"

// remote-import.qml
import "https://example.test/qml"

// unknown-import.qml
import Vendor.Unapproved 1.0
```

Before implementing the linter, point tests at the missing script so the success test fails for the expected reason.

- [ ] **Step 2: Run RED for lint and external fixture**

```bash
ctest --test-dir build-phase1 --output-on-failure \
  -R 'quemusic_plugin_ui_(import|external)'
```

Expected: lint success and external configure tests fail because scripts/exports do not exist.

- [ ] **Step 3: Implement the allowlist parser**

The CMake script reads each file line-by-line, matches only QML import statements, and accepts these module prefixes exactly:

```cmake
set(allowed_modules
    QtQuick
    QtQuick.Controls.Basic
    QtQuick.Layouts
    QtQml
    QtCore
    QueMusic.PluginUI)
```

Quoted relative imports must resolve canonically beneath `ROOT`; reject absolute paths, URL schemes, missing targets, and parent escape. Reject `QueMusic` unless the URI is exactly `QueMusic.PluginUI`. Report failure with `message(FATAL_ERROR "${file}:${line}: rejected import ${uri}")` so file and one-based line number are preserved.

- [ ] **Step 4: Export/install public targets and build the external fixture**

Install/export only the public SDK surface:

```text
quemusic_source_sdk
quemusic_plugin_ui_sdk
quemusic_plugin_ui
Source SDK v2 public headers
public plugin-ui headers
QueMusic/PluginUI QML module artifacts
```

Change both public SDK targets to generator-expression include paths so the export cannot contain a checkout path:

```cmake
target_include_directories(quemusic_source_sdk PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/sdk/source>
    $<INSTALL_INTERFACE:include/quemusic/source>)
target_include_directories(quemusic_plugin_ui_sdk INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/sdk>
    $<INSTALL_INTERFACE:include/quemusic>)
```

Install the targets in one `QueMusicPluginSdkTargets` export and install the `sdk/source/v2` and `sdk/plugin-ui/v1` headers under those matching include roots. Do not export `quemusic_plugin_core`, `core/`, the application target, or the private `QueMusic 1.0` QML module.

Set stable imported names before export:

```cmake
set_target_properties(quemusic_source_sdk PROPERTIES EXPORT_NAME source_sdk)
set_target_properties(quemusic_plugin_ui_sdk PROPERTIES EXPORT_NAME plugin_ui_sdk)
set_target_properties(quemusic_plugin_ui PROPERTIES EXPORT_NAME plugin_ui)
install(EXPORT QueMusicPluginSdkTargets
    NAMESPACE QueMusic::
    DESTINATION lib/cmake/QueMusicPluginSdk)
```

Generate/install `QueMusicPluginSdkConfig.cmake`, its version file, and `QueMusicPluginSdkTargets.cmake` with `CMakePackageConfigHelpers`. The config must call `find_dependency(Qt6 REQUIRED COMPONENTS Core Qml Quick)` before including the targets file. The external fixture uses:

```cmake
find_package(Qt6 REQUIRED COMPONENTS Core Qml Quick)
find_package(QueMusicPluginSdk 1.0 REQUIRED)
target_link_libraries(external_plugin PRIVATE
    QueMusic::source_sdk QueMusic::plugin_ui_sdk Qt6::Core)
```

The external fixture CMake file uses `find_package(Qt6 REQUIRED COMPONENTS Core Qml Quick)` plus `find_package(QueMusicPluginSdk 1.0 REQUIRED)` and imported QueMusic public targets. Its C++ plugin implements both Source v2 and UI Provider; its page imports only `QueMusic.PluginUI 1.0` and creates `PluginPage`, `PluginSection`, `PluginTextField`, and `PluginPrimaryButton`.

`VerifyPluginUiExternalFixture.cmake` must:

1. install QueMusic public artifacts to a temporary prefix;
2. configure the fixture in a separate build directory using that prefix;
3. build the fixture;
4. run import lint on its QML;
5. copy its manifest/QML/library into a package directory;
6. invoke a small loader test that discovers, loads, and Provider-casts it.

- [ ] **Step 5: Document the frozen public API**

`docs/PLUGIN_UI_API.md` records exact IID/API values, manifest shape, Provider ownership, lease requirement, complete component list, Theme tokens, import allowlist, Secret rule, and Phase 2 hosting boundary. Examples use only public imports/includes.

- [ ] **Step 6: Run GREEN for lint, external build, and public boundary scan**

```bash
cmake --build build-phase1 --parallel 4
ctest --test-dir build-phase1 --output-on-failure \
  -R 'quemusic_plugin_ui_(import|external)'
rg -n 'import QueMusic 1\.0|\bStyle\b|\bQButton\b|\bQCard\b|\bOptions\b|\bmainMedia\b|\bMusicApi\b' \
  plugin-ui sdk/plugin-ui tests/fixtures/plugin-ui-external
```

Expected: lint/external tests pass; the boundary scan has no production/public fixture match. Test code may mention forbidden names only in rejection fixtures or literal assertions.

- [ ] **Step 7: Commit packaging and public documentation**

```bash
git add CMakeLists.txt cmake/ValidatePluginQmlImports.cmake \
  cmake/VerifyPluginUiExternalFixture.cmake \
  cmake/QueMusicPluginSdkConfig.cmake.in \
  tests/fixtures/plugin-ui-external tests/fixtures/plugin-ui-lint \
  docs/PLUGIN_UI_API.md
git commit -m "feat: publish and validate plugin ui sdk"
```

---

### Task 8: Full Regression, ABI Evidence, and Phase 1 Record

**Files:**
- Create: `docs/architecture/phase-1-plugin-ui-api-v1-record.md`
- Modify only if verification exposes a tested defect: the owning production/test files from Tasks 1-7.

**Interfaces:**
- Consumes: all Phase 1 deliverables.
- Produces: reproducible release evidence and a clean reviewed branch.

- [ ] **Step 1: Fresh configure and full build**

```bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/cmake --fresh \
  -S . -B build-phase1-final -G "Unix Makefiles" \
  -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON \
  -DCMAKE_PREFIX_PATH=/Users/liqiang/Qt/6.11.1/macos
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/cmake \
  --build build-phase1-final --parallel 4
```

Expected: build succeeds. Record existing unrelated warnings separately; no new Plugin UI warning is acceptable.

- [ ] **Step 2: Run all tests and capture the authoritative inventory**

```bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/ctest \
  --test-dir build-phase1-final --output-on-failure
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/ctest \
  --test-dir build-phase1-final -N
```

Expected: 0 failures. The count must exceed the Phase 0 baseline of 36 and include every test introduced by this plan.

- [ ] **Step 3: Run ABI/API and private-boundary checks**

```bash
rg -n '#define QUEMUSIC_MUSIC_SOURCE_PLUGIN_V2_IID "org\.quemusic\.MusicSourcePlugin/2\.0"' \
  sdk/source/v2/IMusicSourcePluginV2.h
rg -n '#define QUEMUSIC_MUSIC_SOURCE_SDK_V2_ABI 2' \
  sdk/source/v2/IMusicSourcePluginV2.h
if rg -n 'settingsPage|ManagementUi|PluginUi' \
  sdk/source/v2/IMusicSourcePluginV2.h sdk/source/v2/IMusicSourceSessionV2.h; then
  exit 1
fi
if rg -n 'import QueMusic 1\.0|\bStyle\b|\bQButton\b|\bQCard\b|\bOptions\b|\bmainMedia\b|\bMusicApi\b' \
  plugin-ui sdk/plugin-ui tests/fixtures/plugin-ui-external; then
  exit 1
fi
git diff --check 85b089c..HEAD
```

Expected: Source v2 literals remain exact; no UI symbol enters Source v2; no private dependency enters public code; diff check is clean.

- [ ] **Step 4: Write the Phase 1 evidence record**

Record:

- base commit and final commit;
- toolchain and architecture;
- Source v2 IID/ABI unchanged evidence;
- Plugin UI IID/API values;
- public components and Theme tokens;
- manifest and allowlist rules;
- fresh configure/build commands;
- complete new/full CTest names and count;
- external fixture result;
- known Phase 2 deferrals.

- [ ] **Step 5: Commit evidence and verify a clean branch**

```bash
git add docs/architecture/phase-1-plugin-ui-api-v1-record.md
git commit -m "docs: record plugin ui api v1 baseline"
git status --short --branch
git diff --check 85b089c..HEAD
```

Expected: only the branch header appears in status; all Phase 1 changes are committed.
