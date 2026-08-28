# Native Plugin Core Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a native Qt package manager that discovers, loads, unloads, and reloads source plugins safely from Settings.

**Architecture:** `PluginManager` owns package manifests, `QPluginLoader` instances, and active-use leases. `SourceManager` delegates loading to it and parents a lease guard to every source session, preventing use-after-unload.

**Tech Stack:** C++17, Qt 6 Core/Network/Qml/Quick/Test, CMake, Qt `QPluginLoader`.

**Spec:** `docs/superpowers/specs/2026-08-29-native-plugin-core-design.md`

## Global Constraints

- Use C++17 and Qt 6; add no third-party plugin framework.
- Native source SDK v1 is not a hard compatibility constraint; use it only where it keeps the implementation clear and safe.
- Validate manifests before executing plugin code.
- Reject unload/reload while active leases exist.
- Do not implement JavaScript runtime support in this plan.

---

### Task 1: Parse and validate external package manifests

**Files:**
- Create: `core/plugins/PluginManifest.h`
- Create: `core/plugins/PluginManifest.cpp`
- Create: `tests/tst_PluginManifest.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces `PluginManifest PluginManifest::fromFile(const QString &, QString *)`.
- Produces `bool PluginManifest::isValid() const` and `QString PluginManifest::libraryAbsolutePath() const`.

- [ ] **Step 1: Write the failing test**

```cpp
void PluginManifestTest::acceptsNativeSourcePackage()
{
    QTemporaryDir directory;
    writeManifest(directory.path(), validNativeSourceManifest());
    QString error;
    const PluginManifest manifest = PluginManifest::fromFile(
        QDir(directory.path()).filePath("manifest.json"), &error);
    QVERIFY2(manifest.isValid(), qPrintable(error));
    QCOMPARE(manifest.id(), "org.quemusic.source.fixture");
    QCOMPARE(manifest.category(), PluginCategory::Source);
}
```

- [ ] **Step 2: Run the test and verify RED**

Run: `cmake --build <build-dir> --target quemusic_plugin_manifest_test && ctest --test-dir <build-dir> -R '^quemusic_plugin_manifest_test$' --output-on-failure`

Expected: the target or `PluginManifest` is missing.

- [ ] **Step 3: Implement minimal validation**

```cpp
class PluginManifest {
public:
    static PluginManifest fromFile(const QString &manifestPath, QString *error = nullptr);
    bool isValid() const;
    QString id() const;
    PluginCategory category() const;
    QString libraryAbsolutePath() const;
};
```

Parse JSON and reject empty IDs, non-`native-qt` runtime, non-`source`
category, an empty/absolute/`..` library path, invalid plugin API major, and a
source package missing `org.quemusic.MusicSourcePlugin/1.0`.

- [ ] **Step 4: Add the traversal rejection test**

```cpp
void PluginManifestTest::rejectsLibraryOutsidePackage()
{
    QTemporaryDir directory;
    writeManifest(directory.path(), manifestWithLibrary("../outside.dylib"));
    QString error;
    const PluginManifest manifest = PluginManifest::fromFile(
        QDir(directory.path()).filePath("manifest.json"), &error);
    QVERIFY(!manifest.isValid());
    QVERIFY(error.contains("library"));
}
```

- [ ] **Step 5: Run the test and verify GREEN**

Run: `ctest --test-dir <build-dir> -R '^quemusic_plugin_manifest_test$' --output-on-failure`

Expected: PASS.

- [ ] **Step 6: Commit**

Run: `git add core/plugins/PluginManifest.h core/plugins/PluginManifest.cpp tests/tst_PluginManifest.cpp CMakeLists.txt && git commit -m "feat: validate native plugin manifests"`

### Task 2: Implement loader ownership, state, and active leases

**Files:**
- Create: `core/plugins/PluginManager.h`
- Create: `core/plugins/PluginManager.cpp`
- Create: `tests/tst_PluginManager.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes `PluginManifest` from Task 1.
- Produces `PluginState`, `PluginOperationResult`, `PluginLease`, and `PluginManager::{addSearchPath, discover, load, unload, reload, acquire, plugins}`.
- Emits `pluginChanged(QString)` and `pluginLoadFailed(QString, QString)`.

- [ ] **Step 1: Write the failing lease test**

```cpp
void PluginManagerTest::refusesUnloadWhileLeaseIsActive()
{
    PluginManager manager;
    manager.addSearchPath(fixtureDirectory());
    QCOMPARE(manager.discover(), 1);
    QVERIFY(manager.load("org.quemusic.source.fixture"));
    auto lease = manager.acquire("org.quemusic.source.fixture");
    QVERIFY(lease);
    QCOMPARE(manager.unload("org.quemusic.source.fixture"), PluginOperationResult::Busy);
}
```

- [ ] **Step 2: Run the test and verify RED**

Run: `cmake --build <build-dir> --target quemusic_plugin_manager_test && ctest --test-dir <build-dir> -R '^quemusic_plugin_manager_test$' --output-on-failure`

Expected: lifecycle APIs are missing.

- [ ] **Step 3: Implement lifecycle and loader rules**

```cpp
enum class PluginState { Discovered, Loaded, Failed, Unloaded };
enum class PluginOperationResult { Success, NotFound, Busy, Failed };

class PluginManager : public QObject {
    Q_OBJECT
public:
    int discover();
    bool load(const QString &packageId);
    PluginOperationResult unload(const QString &packageId);
    PluginOperationResult reload(const QString &packageId);
    PluginLease acquire(const QString &packageId);
};
```

Discover package directories without loading libraries. Before loading, check
host plugin API major/minor, Qt major, and `QSysInfo::currentCpuArchitecture()`.
Do not set `QLibrary::PreventUnloadHint`. Retain one loader only for
`Loaded`; a lease holds a shared release callback and updates active-use count.

- [ ] **Step 4: Add reload and failure-isolation tests**

```cpp
void PluginManagerTest::reloadsAfterFinalLeaseIsReleased()
{
    PluginManager manager;
    manager.addSearchPath(fixtureDirectory());
    manager.discover();
    QVERIFY(manager.load("org.quemusic.source.fixture"));
    { auto lease = manager.acquire("org.quemusic.source.fixture"); QVERIFY(lease); }
    QCOMPARE(manager.reload("org.quemusic.source.fixture"), PluginOperationResult::Success);
    QCOMPARE(manager.plugin("org.quemusic.source.fixture").state, PluginState::Loaded);
}
```

Also assert a malformed neighboring package fails without blocking a valid
package.

- [ ] **Step 5: Run the test and verify GREEN**

Run: `ctest --test-dir <build-dir> -R '^quemusic_plugin_manager_test$' --output-on-failure`

Expected: PASS.

- [ ] **Step 6: Commit**

Run: `git add core/plugins/PluginManager.h core/plugins/PluginManager.cpp tests/tst_PluginManager.cpp CMakeLists.txt && git commit -m "feat: add native plugin lifecycle manager"`

### Task 3: Route source sessions through PluginManager

**Files:**
- Modify: `core/source/SourceManager.h`
- Modify: `core/source/SourceManager.cpp`
- Modify: `core/source/SourceStartup.h`
- Modify: `core/source/SourceStartup.cpp`
- Modify: `plugins/navidrome-source/CMakeLists.txt`
- Modify: `plugins/test-source/CMakeLists.txt`
- Create: `plugins/navidrome-source/manifest.json`
- Create: `plugins/test-source/manifest.json`
- Modify: `tests/tst_SourceManager.cpp`
- Modify: `tests/tst_PluginStartup.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes `PluginManager::acquire()` from Task 2.
- Produces source package loading and a session-owned lease guard.

- [ ] **Step 1: Write the failing source-unload test**

```cpp
void SourceManagerTest::sourceSessionKeepsPluginLoaded()
{
    PluginManager plugins;
    SourceManager sources(&plugins);
    configureFixturePackage(plugins);
    QVERIFY(sources.loadSourcePackage("org.quemusic.source.fixture"));
    QObject owner;
    IMusicSourceSession *session = sources.createSession("fixture", account(), &owner);
    QVERIFY(session);
    QCOMPARE(plugins.unload("org.quemusic.source.fixture"), PluginOperationResult::Busy);
    delete session;
    QCOMPARE(plugins.unload("org.quemusic.source.fixture"), PluginOperationResult::Success);
}
```

- [ ] **Step 2: Run the test and verify RED**

Run: `ctest --test-dir <build-dir> -R '^quemusic_source_manager_test$' --output-on-failure`

Expected: unload succeeds while a session exists, or source manager does not
accept the plugin manager.

- [ ] **Step 3: Implement the source adapter**

```cpp
class SourceManager : public QObject {
public:
    explicit SourceManager(PluginManager *plugins, QObject *parent = nullptr);
    bool loadSourcePackage(const QString &packageId);
    IMusicSourceSession *createSession(const QString &sourceId,
                                       const SourceAccount &account, QObject *parent);
};
```

Map loaded package IDs to verified source descriptors. Parent a private guard
holding the package lease to each returned session. Retain `loadAll()`,
`sourceIds()`, and `availableSources()` as adapter methods while callers
migrate. If the current source SDK makes a safe lease guard impossible, define a
new major source interface and migrate Navidrome/test fixtures in this task.

- [ ] **Step 4: Package CMake outputs and test roots**

Use `configure_file(... COPYONLY)` to put each manifest beside its MODULE
library. Update fixtures to use package directories. Add a startup test for the
development plugin root and a macOS bundle-root path calculation.

- [ ] **Step 5: Run source tests and verify GREEN**

Run: `ctest --test-dir <build-dir> -R '^(quemusic_source_manager_test|quemusic_plugin_startup_test|quemusic_source_v1_abi_test)$' --output-on-failure`

Expected: PASS. If a source v2 migration is chosen, replace the v1 ABI test
with a v2 contract test in this same task.

- [ ] **Step 6: Commit**

Run: `git add core/source plugins/navidrome-source plugins/test-source tests/tst_SourceManager.cpp tests/tst_PluginStartup.cpp CMakeLists.txt && git commit -m "feat: route source plugins through native core"`

### Task 4: Add Settings controls and verify the full build

**Files:**
- Modify: `main.cpp`
- Modify: `SettingsView.qml`
- Modify: `tests/tst_PluginStartup.cpp`
- Modify: `docs/PLUGIN_API.md`
- Modify: `ARCHITECTURE.md`

**Interfaces:**
- Consumes `PluginManager` from Tasks 2–3.
- Produces the QML context property `pluginManager` and package actions.

- [ ] **Step 1: Write the failing QML exposure test**

```cpp
void PluginStartupTest::exposesPluginManagerToQml()
{
    QQmlApplicationEngine engine;
    PluginManager manager;
    engine.rootContext()->setContextProperty("pluginManager", &manager);
    QCOMPARE(engine.rootContext()->contextProperty("pluginManager").value<QObject *>(), &manager);
}
```

- [ ] **Step 2: Run the test and verify RED**

Run: `ctest --test-dir <build-dir> -R '^quemusic_plugin_startup_test$' --output-on-failure`

Expected: startup does not publish the manager to QML.

- [ ] **Step 3: Implement the QML bridge and controls**

Publish `pluginManager` before loading QML. Replace the Settings placeholder
with a repeater that displays name, category, state, active lease count, and
last error, plus Discover/Load/Unload/Reload actions. Disable unload/reload for
Busy packages; show the failure text returned by the manager.

- [ ] **Step 4: Run tests and full build**

Run: `cmake --build <build-dir> --target QueMusic && ctest --test-dir <build-dir> --output-on-failure`

Expected: full build succeeds and all tests PASS.

- [ ] **Step 5: Document and commit**

Document package layout, manifest fields, ABI gate, plugin search paths, and
busy-unload policy. Record that JavaScript runtime work is deferred.

Run: `git add main.cpp SettingsView.qml tests/tst_PluginStartup.cpp docs/PLUGIN_API.md ARCHITECTURE.md && git commit -m "feat: manage native plugins from settings"`
