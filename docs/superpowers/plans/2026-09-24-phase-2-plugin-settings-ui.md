# Phase 2 Plugin Settings 双层 UI Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将 Plugin UI API 1.0 接入现有设置页，为插件提供 Host Schema 表单或受控 Custom Management 页面。

**Architecture:** Host 在设置控制器与管理页会话中验证插件、实例、模式和 Provider，并以 `PluginLease` 覆盖 backend/QML 整个生命期。QML 只接收 Plugin UI 公开组件、只读 `PluginUiContext` 和绑定当前实例的 Host settings/host service；Secret 写入始终经过 `SourceAccountStore`。

**Tech Stack:** C++20, Qt 6.11 (QObject, QML, QQmlComponent, Qt Quick Controls), CMake, Qt Test, QML.

**Spec:** `docs/architecture/phase-2-plugin-settings-ui-spec.md`

## Global Constraints

- Source SDK v2 IID 保持 `org.quemusic.MusicSourcePlugin/2.0`，ABI 保持 `2`；不修改任何 v2 接口布局。
- `ISourceManagementUiProvider` 既有 IID、descriptor 与创建方法保持不变。
- Plugin UI API 1.0 仅做向后兼容的 Host 接入和可选公开 QML 类型增补；Schema-only 插件和现有 v1.0 UI 插件继续可用。
- 插件管理 QML 仍只能 import 公开 Qt 模块与 `QueMusic.PluginUI 1.0`，并遵守包内相对资源规则。
- 页面 teardown 顺序固定为：context 失效、QML 页面销毁、backend 销毁、lease 释放。
- QML 不得读取 Secret 明文、使用 Host 私有 QueMusic QML 模块，或访问全局应用对象。

## Review Focus

- 插件卸载/重载与 QML 页面销毁重入竞争：要保证插件库在所有 provider-owned 对象析构前仍被 lease 固定（Task 4 测试 `clearsPageBeforeReleasingLease`）。
- Secret 保存成功、失败和重复提交：要保证明文不进入 presentation、错误键、日志或通知（Task 1 测试 `secretOperationsNeverExposeSecretValues`）。
- 快速切换实例后迟到的异步目录/Secret结果：只能影响创建请求时的实例且不能更新新页面（Task 2 测试 `ignoresResultsAfterSessionInvalidation`）。
- 包内 QML 相对资源与非法 descriptor：只允许已校验包根目录下的组件并拒绝模式/API不匹配（Task 2 测试 `rejectsUnsupportedModeAndInvalidComponentPath`）。
- Schema-only 插件进入、离开和重进设置页：现有表单、目录选择和 Secret 草稿清理语义必须不变（Task 4 测试 `schemaOnlyPluginRetainsCurrentSettingsFlow`）。

---

### Task 1: Host settings bridge 与公开 Schema 表单

**Files:**
- Create: `plugin-ui/PluginUiSettingsBridge.h`
- Create: `plugin-ui/PluginUiSettingsBridge.cpp`
- Create: `plugin-ui/PluginUiHostServices.h`
- Create: `plugin-ui/PluginUiHostServices.cpp`
- Create: `plugin-ui/qml/PluginSettingsForm.qml`
- Modify: `CMakeLists.txt`
- Test: `tests/tst_PluginUiSettingsBridge.cpp`
- Modify: `docs/PLUGIN_UI_API.md`

**Interfaces:**
- Consumes: `SourceAccountStore::storedAccount()`, `saveValidatedV2()`, `PluginUiContextData`, and `SettingsSchemaV2`.
- Produces: `PluginUiSettingsBridge(QObject)` with read-only `sections`/`publicValues`, `secretConfigured(QString fieldId) -> bool`, `savePublicValues(QVariantMap values) -> QUuid`, `saveSecret(QString fieldId, QString value) -> QUuid`, and `clearSecret(QString fieldId) -> QUuid`; each operation emits `operationFinished(QUuid requestId, bool success, QString reasonKey)`. `PluginUiHostServices(QObject)` provides `requestDirectory() -> QUuid`, `directorySelected(QUuid requestId, QUrl localDirectory)`, and `notify(QString message, bool isError)`. Public QML `PluginSettingsForm.settings` is bound to `PluginUiSettingsBridge`.

- [ ] **Step 1: Write failing tests for bridge identity and Secret isolation**

Add Qt tests covering: public settings omit all secret values; unknown or non-secret IDs are rejected by Secret methods; operations stay bound to the supplied plugin/source/account tuple; success/failure signals contain request ID and host-owned reason key only.

```cpp
void PluginUiSettingsBridgeTest::publicValuesOmitSecrets();
void PluginUiSettingsBridgeTest::rejectsSecretFieldOutsideSchema();
void PluginUiSettingsBridgeTest::secretOperationsNeverExposeSecretValues();
void PluginUiSettingsBridgeTest::bindsWritesToCurrentInstance();
void PluginUiSettingsBridgeTest::directoryRequestReturnsOnlyLocalUrl();
void PluginUiSettingsBridgeTest::notificationDoesNotIncludeSecretData();
```

- [ ] **Step 2: Run the focused test and observe the expected compile/test failure**

Run: `cmake -S . -B build-phase2 -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON -DCMAKE_PREFIX_PATH=/Users/liqiang/Qt/6.11.1/macos`
Run: `cmake --build build-phase2 --target quemusic_plugin_ui_settings_bridge_test --parallel 4`
Expected: the new target or bridge API is absent, so the focused test does not pass before implementation.

- [ ] **Step 3: Implement schema snapshot and Host-validated async settings operations**

Use the existing validation and `SourceAccountStore` save path. Copy/detach schema and public values before publishing; omit Secret `value` fields; validate every field against the active schema and current account identity; never include inputs or backend errors in completion payloads. Keep secret bytes in local scope only until secure-store completion.

```cpp
class PluginUiSettingsBridge final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList sections READ sections NOTIFY changed)
    Q_PROPERTY(QVariantMap publicValues READ publicValues NOTIFY changed)
public:
    Q_INVOKABLE bool secretConfigured(const QString &fieldId) const;
    Q_INVOKABLE QUuid savePublicValues(const QVariantMap &values);
    Q_INVOKABLE QUuid saveSecret(const QString &fieldId, const QString &value);
    Q_INVOKABLE QUuid clearSecret(const QString &fieldId);
signals:
    void changed();
    void operationFinished(QUuid requestId, bool success, QString reasonKey);
};
```

- [ ] **Step 4: Add Host UI services and the public form component**

Implement the request-directory invokable with a `QUuid` and matching directory result signal plus a Host notification signal. Implement `PluginSettingsForm` with Plugin UI Kit controls, public-value editing, Secret input/clear state, Save/Cancel, accessible labels, and busy/error states. It must import only `QtQuick`, `QtQuick.Controls.Basic`, `QtQuick.Layouts`, `QtQml`, `QtCore`, and same-module `QueMusic.PluginUI` types.

```qml
PluginSettingsForm {
    settings: pluginUiContext.settings
    host: pluginUiContext.host
}
```

- [ ] **Step 5: Run bridge, UI and import-boundary tests**

Run: `cmake --build build-phase2 --target quemusic_plugin_ui_settings_bridge_test quemusic_plugin_ui_qml_test --parallel 4`
Run: `ctest --test-dir build-phase2 --output-on-failure -R 'quemusic_plugin_ui_(settings_bridge|qml|import)'`
Expected: all selected tests pass; a public-module import scan finds no private `QueMusic 1.0` dependency.

- [ ] **Step 6: Commit the Host bridge and public form**

```bash
git add CMakeLists.txt plugin-ui sdk/plugin-ui tests/tst_PluginUiSettingsBridge.cpp docs/PLUGIN_UI_API.md
git commit -m "feat: add host plugin ui settings bridge"
```

### Task 2: Lease-owning management UI session

**Files:**
- Create: `plugin-ui/PluginManagementUiSession.h`
- Create: `plugin-ui/PluginManagementUiSession.cpp`
- Modify: `plugin-ui/PluginUiContext.h`
- Modify: `plugin-ui/PluginUiContext.cpp`
- Modify: `CMakeLists.txt`
- Test: `tests/tst_PluginManagementUiSession.cpp`
- Modify: `tests/fixtures/PluginUiFixturePlugin.*`

**Interfaces:**
- Consumes: Task 1 bridge/service objects; `PluginManager::acquire(packageId)`; `ISourceManagementUiProvider::managementUi()` and `createManagementBackend()`; the validated manifest management URL.
- Produces: `PluginManagementUiSession(QObject)` exposing read-only `componentUrl`, `context`, `state`, and `errorKey`, plus `invalidateContext()` and `release()`. The session owns `PluginLease`, `PluginUiContext`, settings/host bridges and backend. The QML page host calls `invalidateContext()`, clears its Loader item synchronously, then calls `release()`; `release()` destroys the backend and bridges before dropping the lease and is idempotent.

- [ ] **Step 1: Add lifecycle and descriptor failure tests**

Use a fixture Provider with QObject destruction probes. Test Create/Edit identity rules, unsupported modes, missing backend, invalid component URL, lease count while active, idempotent close, context invalidation, and destruction order.

```cpp
void PluginManagementUiSessionTest::createsEditContextFromHostIdentity();
void PluginManagementUiSessionTest::rejectsUnsupportedModeAndInvalidComponentPath();
void PluginManagementUiSessionTest::releasesLeaseAfterBackendIsDestroyed();
void PluginManagementUiSessionTest::ignoresResultsAfterSessionInvalidation();
```

- [ ] **Step 2: Run the focused tests and confirm failure before implementation**

Run: `cmake --build build-phase2 --target quemusic_plugin_management_ui_session_test --parallel 4`
Run: `ctest --test-dir build-phase2 --output-on-failure -R quemusic_plugin_management_ui_session_test`
Expected: compilation or the new assertions fail because no session implementation exists.

- [ ] **Step 3: Implement session creation with lease acquired before Provider access**

Validate package state, manifest/descriptor agreement, API version, supported mode and package-contained QML URL. Acquire the lease before casting the Provider; create backend with session-owned parent; initialize context using host-validated identities and bridge objects. Failed creation returns a stable `errorKey` and releases every partially created object safely.

```cpp
PluginLease lease = manager->acquire(packageId);
if (!lease.isValid()) return failure("plugin.ui.unavailable");
auto *provider = qobject_cast<ISourceManagementUiProvider *>(manager->pluginInstance(packageId));
// Validate descriptor and mode while lease is held, then create backend/context.
```

- [ ] **Step 4: Implement close/destruction fencing**

Make `invalidateContext()` cancel Host service requests and mark context invalid. After the QML host clears its Loader item, `release()` destroys backend and bridge objects, then releases the lease. Ignore completions whose session generation is no longer current.

```cpp
void PluginManagementUiSession::invalidateContext(); // context.valid becomes false
void PluginManagementUiSession::release();           // after Loader item is cleared
```

- [ ] **Step 5: Run session, provider and unload tests**

Run: `cmake --build build-phase2 --target quemusic_plugin_management_ui_session_test quemusic_plugin_ui_provider_test quemusic_plugin_ui_unload_test --parallel 4`
Run: `ctest --test-dir build-phase2 --output-on-failure -R 'quemusic_plugin_management_ui_session|quemusic_plugin_ui_(provider|unload)'`
Expected: all lifecycle, Provider agreement and unload tests pass.

- [ ] **Step 6: Commit the management session**

```bash
git add CMakeLists.txt plugin-ui tests/tst_PluginManagementUiSession.cpp tests/fixtures/PluginUiFixturePlugin.*
git commit -m "feat: manage plugin ui page lifecycle"
```

### Task 3: Settings controller integration

**Files:**
- Modify: `core/settings/PluginSettingsController.h`
- Modify: `core/settings/PluginSettingsController.cpp`
- Modify: `tests/tst_PluginSettingsController.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `PluginManagementUiSession` from Task 2 and current selected plugin/instance snapshots.
- Produces: controller properties `managementUiAvailable`, `managementUiSession`, and `managementUiErrorKey`; invokables `openManagementUi(PluginUiMode mode)`, `beginCloseManagementUi()`, and `finishCloseManagementUi()`; signal `managementUiCloseRequested()`; selection changes close stale sessions; remove/disable/unload/reload requests affecting the active session wait until the page host completes teardown, then apply once.

- [ ] **Step 1: Add controller tests for Create/Edit and selection changes**

Test schema-only fallback, Custom UI availability, the exact Create/Edit IDs passed to the session, closing an old session when plugin or instance selection changes, and no retained session after unload. Verify remove, disable, unload and reload requests for the active package/instance wait for page teardown and run once afterward.

- [ ] **Step 2: Run focused controller tests and confirm failure**

Run: `cmake --build build-phase2 --target quemusic_plugin_settings_controller_test --parallel 4`
Run: `ctest --test-dir build-phase2 --output-on-failure -R quemusic_plugin_settings_controller_test`
Expected: tests fail because the controller does not yet expose management UI state or actions.

- [ ] **Step 3: Implement selection-bound session ownership**

Create the session only for the currently selected package and mode. Any selection change closes and clears the session before publishing the changed snapshot. If remove/disable/unload/reload targets the active package or instance, store one pending operation, invalidate context and request page closure; `finishCloseManagementUi()` releases the session before executing that operation once. Controller destruction cancels pending operations and releases all Host-owned session state. Preserve current schema operations and all existing property meanings.

```cpp
void PluginSettingsController::beginCloseManagementUi() {
    if (!d->managementUiSession) return;
    d->managementUiSession->invalidateContext();
}
```

On PluginManager unload/reload or selected-instance invalidation, the controller calls `beginCloseManagementUi()` and emits `managementUiCloseRequested()`. The QML handler clears the Loader item and then calls `finishCloseManagementUi()`.

- [ ] **Step 4: Re-run controller and Source/Plugin lifecycle regressions**

Run: `cmake --build build-phase2 --target quemusic_plugin_settings_controller_test quemusic_source_registry_v2_test quemusic_plugin_manager_test --parallel 4`
Run: `ctest --test-dir build-phase2 --output-on-failure -R 'quemusic_plugin_settings_controller|quemusic_source_registry_v2|quemusic_plugin_manager'`
Expected: all selected tests pass and Source SDK v2 IID/ABI scan remains unchanged.

- [ ] **Step 5: Commit controller integration**

```bash
git add core/settings/PluginSettingsController.h core/settings/PluginSettingsController.cpp tests/tst_PluginSettingsController.cpp CMakeLists.txt
git commit -m "feat: expose plugin management ui in settings controller"
```

### Task 4: Settings panel and generic management container

**Files:**
- Create: `components/PluginManagementPageHost.qml`
- Modify: `components/PluginSettingsPanel.qml`
- Modify: `SettingsView.qml`
- Modify: `tests/tst_PluginSettingsQml.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: Task 3 controller properties/actions and `PluginManagementUiSession.componentUrl/context`.
- Produces: settings panel behavior where a Custom UI session is loaded inside the generic host container, and Schema-only plugins retain the existing `SchemaSettingsForm` route. The container renders loading/error/retry/return states with PluginTheme and closes the controller session on unload/destruction.

- [ ] **Step 1: Add QML tests for schema-only and Custom UI navigation**

Cover the path from the settings panel to Create/Edit management pages, context injection, return to the plugin/instance list, theme changes, load errors, and schema-only form behavior including Secret draft clearing.

```cpp
void PluginSettingsQmlTest::clearsPageBeforeReleasingLease();
void PluginSettingsQmlTest::schemaOnlyPluginRetainsCurrentSettingsFlow();
void PluginSettingsQmlTest::customUiReceivesOnlyPluginUiContext();
void PluginSettingsQmlTest::loadFailureShowsGenericErrorState();
```

- [ ] **Step 2: Run the focused QML test and observe expected failures**

Run: `cmake --build build-phase2 --target quemusic_plugin_settings_qml_test --parallel 4`
Run: `ctest --test-dir build-phase2 --output-on-failure -R quemusic_plugin_settings_qml_test`
Expected: new navigation and context assertions fail before the host container is wired.

- [ ] **Step 3: Implement generic QML page host and panel navigation**

Load only the controller-provided validated component URL. Pass only the `pluginUiContext` initial property to the plugin root item; handle asynchronous Loader status, clear the item before closing the session, and return to the existing panel on close/error. Keep all headings, margins, list and instance actions in the Host panel.

```qml
Loader {
    id: managementLoader
    asynchronous: true
}
function closeManagementPage() {
    root.controller.beginCloseManagementUi()
    managementLoader.source = ""
    root.controller.finishCloseManagementUi()
}
function openManagementPage() {
    var session = root.controller.managementUiSession
    managementLoader.setSource(session.componentUrl,
                               { "pluginUiContext": session.context })
}
Connections {
    target: root.controller
    function onManagementUiCloseRequested() { root.closeManagementPage() }
}
```

- [ ] **Step 4: Run panel, public QML and UI-structure regressions**

Run: `cmake --build build-phase2 --target quemusic_plugin_settings_qml_test quemusic_plugin_ui_qml_test quemusic_original_ui_structure_test --parallel 4`
Run: `ctest --test-dir build-phase2 --output-on-failure -R 'quemusic_plugin_settings_qml|quemusic_plugin_ui_qml|quemusic_original_ui_structure'`
Expected: all selected tests pass with no QML binding/import warnings.

- [ ] **Step 5: Commit settings UI integration**

```bash
git add CMakeLists.txt components/PluginManagementPageHost.qml components/PluginSettingsPanel.qml SettingsView.qml tests/tst_PluginSettingsQml.cpp
git commit -m "feat: host custom plugin management pages"
```

### Task 5: End-to-end fixtures, lifecycle regressions and documentation

**Files:**
- Create: `tests/fixtures/plugin-ui-qr/ManagementPage.qml`
- Modify: `tests/fixtures/PluginUiFixturePlugin.*`
- Modify: `tests/tst_PluginUiExternal.cpp`
- Modify: `tests/tst_PluginUiUnload.cpp`
- Modify: `tests/tst_PluginSettingsQml.cpp`
- Modify: `docs/PLUGIN_UI_API.md`
- Modify: `docs/architecture/phase-2-plugin-settings-ui-spec.md`
- Create: `docs/architecture/phase-2-plugin-settings-ui-record.md`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: Tasks 1–4 public component and Host lifecycle contract.
- Produces: an installed-package QR-style fixture demonstrating the supported plugin flow, complete lifecycle and compatibility evidence, and `docs/architecture/phase-2-plugin-settings-ui-record.md` recording verification.

- [ ] **Step 1: Add an external QR-management fixture**

Implement a deterministic fake QR state machine (request, pending, success, error) with no network calls. The page uses only `QueMusic.PluginUI 1.0`, `PluginUiContext`, `PluginSettingsForm`, `PluginQrLogin`, and `PluginTheme`; credentials are submitted to the fake Host Secret bridge and are never readable back.

```qml
PluginQrLogin {
    objectName: "fixtureQrLogin"
    state: fixtureBackend.loginState
    onRequestLogin: fixtureBackend.requestLogin()
    onCancelLogin: fixtureBackend.cancelLogin()
}
PluginSettingsForm { settings: pluginUiContext.settings }
```

- [ ] **Step 2: Add end-to-end unload and stale-callback assertions**

Verify that switching instances, closing settings, unloading and reloading the plugin destroy the loaded component and backend before the lease releases; fire a queued fake completion after close and assert that it is ignored.

- [ ] **Step 3: Run the integration regressions and import validation**

Run: `cmake --build build-phase2 --parallel 4`
Run: `ctest --test-dir build-phase2 --output-on-failure -R 'quemusic_plugin_ui|quemusic_plugin_settings|quemusic_source_registry_v2'`
Run: `git diff --check`
Expected: all selected tests pass; the external fixture builds from installed public SDK targets and has no private Host QML imports.

- [ ] **Step 4: Record compatibility and verification results**

Update `docs/PLUGIN_UI_API.md` and create the Phase 2 record with exact CMake/Qt versions, Source SDK v2 IID/ABI values, focused test counts, full CTest result and any platform-specific limitation. Mark the spec completed only after all acceptance criteria have test evidence.

- [ ] **Step 5: Run the complete test suite and ABI checks**

Run: `ctest --test-dir build-phase2 --output-on-failure`
Run: `git diff --check`
Expected: full CTest passes; the Source SDK v2 IID remains `org.quemusic.MusicSourcePlugin/2.0`; ABI remains `2`; Plugin UI public-import validation passes.

- [ ] **Step 6: Commit final fixtures and verification record**

```bash
git add CMakeLists.txt tests docs/PLUGIN_UI_API.md docs/architecture/phase-2-plugin-settings-ui-spec.md docs/architecture/phase-2-plugin-settings-ui-record.md
git commit -m "test: verify plugin management ui integration"
```

## Final review and completion

After all tasks, inspect the full branch diff against the Phase 2 starting commit. Re-grade findings by user-visible effect. Fix Critical/Important findings with a failing regression test first, rerun the full suite, and record deferred Minor findings. Do not alter Source SDK v2 ABI or push/merge as part of this plan.
