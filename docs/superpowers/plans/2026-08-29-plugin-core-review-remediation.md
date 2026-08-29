# PluginCore Review Remediation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Correct the native PluginCore review findings so package discovery,
lifecycle, source registration, and macOS deployment remain safe and truthful.

**Architecture:** `PluginManager` remains the native-loader owner. It rejects
duplicate source identities before code execution and owns every transition
that unloads or fails a loaded library. `SourceManager` reports source-adapter
activation failure back to that owner and holds its injected manager weakly.

**Tech Stack:** C++17, Qt 6 Core/Qml/Test, CMake, Qt `QPluginLoader`.

**Spec:** `docs/superpowers/specs/2026-08-29-plugin-core-review-remediation-design.md`

## Global Constraints

- Do not add third-party plugin frameworks or JavaScript runtime support.
- Native packages remain trusted in-process code, but their library must resolve
  inside the canonical package root before loading.
- Never retain more than one `QPluginLoader` for one package entry.
- Keep current source SDK v1; no public source ABI change is needed.
- Each behavior change starts with a failing QtTest and ends with focused tests.
- Finish with the default CMake/Ninja build and complete CTest suite.

---

### Task 1: Reject duplicate source packages and symlink escapes at discovery

**Files:**
- Modify: `core/plugins/PluginManifest.cpp`
- Modify: `core/plugins/PluginManager.cpp`
- Modify: `tests/tst_PluginManifest.cpp`
- Modify: `tests/tst_PluginManager.cpp`

**Interfaces:**
- Consumes: `PluginManifest::fromFile(const QString &, QString *)`.
- Produces: manifests whose `libraryAbsolutePath()` is a canonical file under
  the package directory, and `PluginManager::discover()` that accepts each
  `PluginSpec::sourceId` at most once.

- [x] **Step 1: Write the failing symlink-boundary test**

```cpp
void PluginManifestTest::rejectsLibrarySymlinkOutsidePackage()
{
    QTemporaryDir package;
    QTemporaryDir outside;
    const QString external = QDir(outside.path()).filePath("external.dylib");
    QFile externalFile(external);
    QVERIFY(externalFile.open(QIODevice::WriteOnly));
    externalFile.close();
    QVERIFY(QFile::link(external, QDir(package.path()).filePath("escape.dylib")));
    writeManifest(package.path(), manifestWithLibrary("escape.dylib"));

    QString error;
    const PluginManifest manifest = PluginManifest::fromFile(
        QDir(package.path()).filePath("manifest.json"), &error);
    QVERIFY(!manifest.isValid());
    QVERIFY(error.contains("package"));
}
```

- [x] **Step 2: Run the manifest test to verify RED**

Run:
`/Users/liqiang/Qt/Tools/Ninja/ninja -C build/task1 quemusic_plugin_manifest_test && /Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/ctest --test-dir build/task1 -R '^quemusic_plugin_manifest_test$' --output-on-failure`

Expected: `rejectsLibrarySymlinkOutsidePackage` fails because the textual
relative-path check accepts the symlink.

- [x] **Step 3: Implement canonical library-root validation**

```cpp
const QFileInfo packageRootInfo(QFileInfo(manifestPath).absolutePath());
const QFileInfo libraryInfo(QDir(packageRootInfo.absoluteFilePath()).filePath(library));
const QString packageRoot = packageRootInfo.canonicalFilePath();
const QString libraryPath = libraryInfo.canonicalFilePath();
if (packageRoot.isEmpty() || libraryPath.isEmpty() || !libraryInfo.isFile()
    || !libraryPath.startsWith(packageRoot + QDir::separator())) {
    return invalidManifest(QStringLiteral("Manifest library escapes package directory"), error);
}
manifest.m_libraryAbsolutePath = libraryPath;
```

- [x] **Step 4: Write the failing duplicate-source discovery test**

```cpp
void PluginManagerTest::rejectsDuplicateSourceIdBeforeLoad()
{
    PluginManager manager;
    manager.addSearchPath(packageRootWithDuplicateSourceId());
    QCOMPARE(manager.discover(), 1);
    QCOMPARE(manager.plugins().size(), 1);
    QVERIFY(!manager.load(QStringLiteral("org.quemusic.source.duplicate")));
}
```

Create two valid package manifests with distinct package IDs but the same
`sourceId`; use the existing fixture MODULE for the first and a copied package
directory for the second.

- [x] **Step 5: Run the manager test to verify RED**

Run:
`/Users/liqiang/Qt/Tools/Ninja/ninja -C build/task1 quemusic_plugin_manager_test && /Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/ctest --test-dir build/task1 -R '^quemusic_plugin_manager_test$' --output-on-failure`

Expected: both packages are discovered and the duplicate package can load.

- [x] **Step 6: Reject duplicate source IDs while discovering**

```cpp
const bool duplicateSource = std::any_of(m_entries.cbegin(), m_entries.cend(),
    [&manifest](const auto &entry) { return entry->spec.sourceId == manifest.sourceId(); });
if (duplicateSource) {
    emit pluginLoadFailed(manifest.id(), QStringLiteral("Duplicate plugin source ID"));
    continue;
}
```

- [x] **Step 7: Run focused tests and commit**

Run both test expressions from Steps 2 and 5; both must pass.

```bash
git add core/plugins/PluginManifest.cpp core/plugins/PluginManager.cpp \
  tests/tst_PluginManifest.cpp tests/tst_PluginManager.cpp
git commit -m "fix: reject unsafe native package discovery"
```

### Task 2: Make source activation and loader failure states truthful

**Files:**
- Modify: `core/plugins/PluginManager.h`
- Modify: `core/plugins/PluginManager.cpp`
- Modify: `core/source/SourceManager.h`
- Modify: `core/source/SourceManager.cpp`
- Modify: `tests/tst_PluginManager.cpp`
- Modify: `tests/tst_SourceManager.cpp`

**Interfaces:**
- Consumes: a loaded package ID plus source validation result.
- Produces: `PluginManager::failLoadedPlugin(const QString &, const QString &)`;
  it unloads a just-loaded library, persists `Failed` plus the adapter error,
  and returns false when it cannot release the library. `SourceManager` stores
  `QPointer<PluginManager>` and calls this method on descriptor/init failure.

- [x] **Step 1: Write the failing activation-status test**

```cpp
void SourceManagerTest::failedSourceInitializationMarksPackageFailed()
{
    PluginManager plugins;
    plugins.addSearchPath(fixtureDirectory("initialization-failure-package"));
    SourceManager sources(&plugins);

    QCOMPARE(sources.loadAll(), 0);
    const PluginSpec package = plugins.plugin("org.quemusic.source.initialization-failure");
    QCOMPARE(package.state, PluginState::Failed);
    QVERIFY(package.error.contains("initialization"));
}
```

- [x] **Step 2: Run the source-manager test to verify RED**

Run:
`/Users/liqiang/Qt/Tools/Ninja/ninja -C build/task1 quemusic_source_manager_test && /Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/ctest --test-dir build/task1 -R '^quemusic_source_manager_test$' --output-on-failure`

Expected: the package stays `Loaded` with an empty error.

- [x] **Step 3: Write the failing failed-activation retry test**

```cpp
void SourceManagerTest::failedSourceActivationCanBeRetriedWithoutSecondLoader()
{
    PluginManager plugins;
    plugins.addSearchPath(fixtureDirectory("initialization-failure-package"));
    SourceManager sources(&plugins);

    QVERIFY(!sources.loadSourcePackage("org.quemusic.source.initialization-failure"));
    QCOMPARE(plugins.plugin("org.quemusic.source.initialization-failure").state,
             PluginState::Failed);
    QVERIFY(!sources.loadSourcePackage("org.quemusic.source.initialization-failure"));
}
```

The test exercises the same invariant without relying on platform-specific
`QPluginLoader::unload()` failure injection: source activation failure must
release its first loader before a retry may create another one.

- [x] **Step 4: Run source-manager test to verify RED**

Run the focused source-manager CTest command from Step 2.

Expected: the first activation failure leaves the package `Loaded` rather than
releasing it and recording `Failed`.

- [x] **Step 5: Implement loader-preserving failure transitions**

```cpp
bool PluginManager::failLoadedPlugin(const QString &packageId, const QString &error)
{
    Entry *entry = findEntry(packageId);
    if (entry == nullptr || entry->loader == nullptr || entry->spec.activeLeases != 0) {
        return false;
    }
    if (!entry->loader->unload()) {
        fail(*entry, error + QStringLiteral(": ") + entry->loader->errorString());
        return false;
    }
    entry->loader.reset();
    entry->instance = nullptr;
    fail(*entry, error);
    return true;
}

PluginOperationResult PluginManager::unload(const QString &packageId)
{
    // Use entry->loader, not PluginState, to decide whether unloading is required.
}
```

`load()` must return false while an entry still owns a loader. `unload()` must
use `entry->loader`, rather than `PluginState`, to decide whether another
unload is required after a failed unload. In
`SourceManager::loadSourcePackage`, turn every descriptor and initialization
failure into a precise `failLoadedPlugin` call. Use `QPointer<PluginManager>`
for `m_pluginManager`, clear package-backed entries on `destroyed`, and return
`nullptr` from `createSession()` when it is null.

- [x] **Step 6: Write and run the manager-destruction regression**

```cpp
void SourceManagerTest::destroyedPluginManagerRemovesPackageSources()
{
    auto *plugins = new PluginManager;
    SourceManager sources(plugins);
    plugins->addSearchPath(QStringLiteral(QUEMUSIC_TEST_PLUGIN_PACKAGE_DIR));
    QCOMPARE(sources.loadAll(), 1);
    delete plugins;

    QCOMPARE(sources.sourceIds(), QStringList());
    QVERIFY(sources.createSession("test-source", {}, nullptr) == nullptr);
}
```

Run the focused source-manager test; it must pass without a crash.

- [x] **Step 7: Run focused tests and commit**

Run focused manager and source-manager CTest targets. Both must pass.

```bash
git add core/plugins/PluginManager.h core/plugins/PluginManager.cpp \
  core/source/SourceManager.h core/source/SourceManager.cpp \
  tests/tst_PluginManager.cpp tests/tst_SourceManager.cpp
git commit -m "fix: preserve native plugin lifecycle invariants"
```

### Task 3: Deploy Navidrome package inside macOS bundles

**Files:**
- Modify: `CMakeLists.txt`
- Modify: `plugins/navidrome-source/NavidromeSourcePlugin.cpp`
- Create: `cmake/VerifyBundledPluginPackage.cmake`
- Modify: `docs/PLUGIN_API.md`
- Modify: `ARCHITECTURE.md`

**Interfaces:**
- Consumes: the `quemusic_navidrome_source` target and generated
  `${CMAKE_BINARY_DIR}/plugins/navidrome/$<CONFIG>/manifest.json`.
- Produces: a generator-safe `quemusic_navidrome_bundle_plugin` target that
  copies the native module and manifest to
  `QueMusic.app/Contents/PlugIns/quemusic/navidrome/`; QueMusic depends on
  that deployment target on macOS.

- [x] **Step 1: Add a macOS CTest package-output assertion**

Create `cmake/VerifyBundledPluginPackage.cmake`:

```cmake
if(NOT EXISTS "${package_dir}/manifest.json")
    message(FATAL_ERROR "Missing bundled plugin manifest: ${package_dir}/manifest.json")
endif()
if(NOT EXISTS "${package_dir}/${library_name}")
    message(FATAL_ERROR "Missing bundled plugin library: ${package_dir}/${library_name}")
endif()
```

Register it only on macOS after defining `QueMusic`:

```cmake
add_test(NAME quemusic_macos_bundle_plugin_test
    COMMAND ${CMAKE_COMMAND}
        "-Dpackage_dir=$<TARGET_BUNDLE_DIR:QueMusic>/Contents/PlugIns/quemusic/navidrome"
        "-Dlibrary_name=$<TARGET_FILE_NAME:quemusic_navidrome_source>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifyBundledPluginPackage.cmake")
```

- [x] **Step 2: Run the package-output assertion to verify RED**

Run:
`/Users/liqiang/Qt/Tools/Ninja/ninja -C build/task1 QueMusic && /Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/ctest --test-dir build/task1 -R '^quemusic_macos_bundle_plugin_test$' --output-on-failure`

Expected on macOS: the registered assertion fails because the package directory
does not exist under `QueMusic.app`.

- [x] **Step 3: Add a generator-safe macOS deployment target**

After defining the `QueMusic` app target, add a deployment target whose
commands use target generator expressions and whose dependencies include both
the Navidrome module and its generated manifest. Make QueMusic depend on that
target:

```cmake
if(APPLE)
    set(quemusic_bundle_plugin_dir
        "$<TARGET_BUNDLE_DIR:QueMusic>/Contents/PlugIns/quemusic/navidrome")
    add_custom_target(quemusic_navidrome_bundle_plugin
        COMMAND ${CMAKE_COMMAND} -E make_directory "${quemusic_bundle_plugin_dir}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:quemusic_navidrome_source>" "${quemusic_bundle_plugin_dir}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${QUEMUSIC_NAVIDROME_MANIFEST}" "${quemusic_bundle_plugin_dir}"
        DEPENDS quemusic_navidrome_source "${QUEMUSIC_NAVIDROME_MANIFEST}")
    add_dependencies(QueMusic quemusic_navidrome_bundle_plugin)
endif()
```

Keep development output unchanged and do not copy test fixtures.

- [x] **Step 4: Align package version and docs**

Change Navidrome's `SourceDescriptor::version` to `1.0.0`, matching its
manifest. Update the API and architecture text to say that macOS builds copy
the bundled source package after linking.

- [x] **Step 5: Run package-output assertion and commit**

Run the command from Step 2; it must pass on macOS.

```bash
git add CMakeLists.txt cmake/VerifyBundledPluginPackage.cmake \
  plugins/navidrome-source/NavidromeSourcePlugin.cpp \
  docs/PLUGIN_API.md ARCHITECTURE.md
git commit -m "fix: deploy navidrome package with mac app"
```

### Task 4: Verify remediation and request final review

**Files:**
- Modify: `docs/superpowers/specs/2026-08-29-plugin-core-review-remediation-design.md`
- Modify: `docs/superpowers/plans/2026-08-29-plugin-core-review-remediation.md`

**Interfaces:**
- Consumes: all corrected PluginCore code and regression tests.
- Produces: a verified branch and recorded review status.

- [x] **Step 1: Build all default targets**

Run:
`/Users/liqiang/Qt/Tools/Ninja/ninja -C build/task1 -j 1`

Expected: exit code 0, including `QueMusic`, Navidrome module, and all tests.

- [x] **Step 2: Run the complete CTest suite**

Run:
`/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/ctest --test-dir build/task1 --output-on-failure`

Expected: every registered test passes; run in the permitted local environment
because Navidrome tests need localhost sockets.

- [ ] **Step 3: Update status and request a fresh independent review**

Record the final build/test result and remediation commit IDs in the SDD
ledger. Dispatch a read-only reviewer for `main..HEAD`; fix any P1/P2 findings
before presenting merge or PR options.

- [x] **Step 4: Commit documentation status**

```bash
git add docs/superpowers/specs/2026-08-29-plugin-core-review-remediation-design.md \
  docs/superpowers/plans/2026-08-29-plugin-core-review-remediation.md
git commit -m "docs: record plugin core remediation"
```
