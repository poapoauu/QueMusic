# Original UI / Unified Source Adapter Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Restore QueMusic's upstream music UI while routing plugin-backed music through the existing source SDK v2 data and playback layers.

**Architecture:** Upstream commit `a63511d` is the visual and interaction baseline. A host-owned `OriginalUiMusicAdapter` flattens `MusicHub` sections into `OnlineListModel`-compatible presentation models and preserves full v2 item identity for actions and playback; QML keeps its original hierarchy and delegates and changes only at data/action boundaries.

**Tech Stack:** C++20, Qt 6.11.1 Core/Qml/Quick/Test/Multimedia, QML, CMake/CTest

**Spec:** `docs/superpowers/specs/2026-09-12-original-ui-data-adapter-design.md`

## Global Constraints

- The visual baseline for the six music-shell surfaces is upstream commit `a63511d`.
- Do not restore source SDK v1, `MediaBridge`, `SourceManager`, `SourceSessionRegistry`, or `SourceLibraryPage`.
- Keep source SDK v2, plugin settings, MusicHub, capability routing and secure playback intact.
- QML must never receive authenticated stream URLs, HTTP headers, passwords, tokens, salts, or raw server error bodies.
- Existing user-owned dirty `CMakeLists.txt`, logging, storage and macOS packaging changes must remain unstaged and unmodified except for isolated, reviewed CMake hunks required by this plan.
- Real-server GUI validation starts read-only; mutation smoke requires a designated test item and explicit authorization.

---

### Task 1: Lock and restore the upstream visual shell

**Files:**
- Create: `tests/tst_OriginalUiStructure.cpp`
- Modify: `CMakeLists.txt`
- Modify: `layout/LeftSideBar.qml`
- Modify: `layout/MainContent.qml`
- Modify: `pages/HomePage.qml`
- Modify: `pages/PlaylistPage.qml`
- Modify: `pages/FavouritePage.qml`
- Modify: `pages/SearchPage.qml`
- Modify: `main.qml`

**Interfaces:**
- Consumes: upstream blobs from `git show a63511d:<path>` as read-only references.
- Produces: original navigation indices `0=推荐, 1=分类, 2=separator, 3=收藏, 4=本地, 5=下载`; original page/tab hierarchy; injectable properties `property var musicAdapter` and `property var playbackAdapter` without structural redesign.

- [ ] **Step 1: Write a failing structural regression test**

Create a QtTest that reads each current QML file and asserts the upstream-defining structures: six sidebar entries plus separator, `QPages`/`QBlurTapBar` in original pages, original loader transition animation, no `MusicSectionView`, no standalone “音乐源” navigation, and no white replacement root.

```cpp
void OriginalUiStructureTest::musicShellMatchesRequiredStructure()
{
    const QString sidebar = readSource("layout/LeftSideBar.qml");
    QCOMPARE(sidebar.count("ListElement {"), 6);
    QVERIFY(sidebar.contains("display: \"推荐\""));
    QVERIFY(sidebar.contains("display: \"本地\""));
    QVERIFY(!sidebar.contains("display: \"音乐源\""));

    const QString home = readSource("pages/HomePage.qml");
    QVERIFY(home.contains("QPages"));
    QVERIFY(home.contains("QBlurTapBar"));
    QVERIFY(!home.contains("MusicSectionView"));
}
```

- [ ] **Step 2: Run the test and verify RED**

Run:

```bash
cmake --build build/bridge --target quemusic_original_ui_structure_test -j8
ctest --test-dir build/bridge -R '^quemusic_original_ui_structure_test$' --output-on-failure
```

Expected: FAIL because commit `9714c5e` replaced the original page structures.

- [ ] **Step 3: Restore the visual baseline without restoring v1 objects**

Start each listed QML file from `a63511d`, retain its original controls/layout/animation, remove calls that require deleted v1 bridge objects, and add only nullable adapter properties at the root:

```qml
property var musicAdapter: null
property var playbackAdapter: null
```

Keep the plugin settings entry/wiring outside these music pages. Keep the source-library navigation removed. Preserve original local/download loaders and visual placement.

- [ ] **Step 4: Verify structure, QML loading and main build GREEN**

```bash
cmake --build build/bridge --target quemusic_original_ui_structure_test quemusic_music_hub_qml_test QueMusic -j8
ctest --test-dir build/bridge -R '^(quemusic_original_ui_structure_test|quemusic_music_hub_qml_test|quemusic_plugin_startup_test)$' --output-on-failure
```

Expected: all selected tests pass and QML loads without missing v1 context properties.

- [ ] **Step 5: Commit the visual baseline restoration**

```bash
git add CMakeLists.txt tests/tst_OriginalUiStructure.cpp layout/LeftSideBar.qml layout/MainContent.qml pages/HomePage.qml pages/PlaylistPage.qml pages/FavouritePage.qml pages/SearchPage.qml main.qml
git commit -m "fix: restore original music UI shell"
```

---

### Task 2: Add the presentation-only v2 adapter

**Files:**
- Create: `core/music/OriginalUiMusicAdapter.h`
- Create: `core/music/OriginalUiMusicAdapter.cpp`
- Create: `tests/tst_OriginalUiMusicAdapter.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `MusicHub *`, its four `MusicPageModel` instances, `MediaActionRouter *`, and `PlaybackCoordinator *`.
- Produces:

```cpp
class OriginalUiMusicAdapter final : public QObject {
    Q_OBJECT
    Q_PROPERTY(OnlineListModel *recommendSongs READ recommendSongs CONSTANT)
    Q_PROPERTY(OnlineListModel *categoryItems READ categoryItems CONSTANT)
    Q_PROPERTY(OnlineListModel *favoriteSongs READ favoriteSongs CONSTANT)
    Q_PROPERTY(OnlineListModel *favoriteLists READ favoriteLists CONSTANT)
    Q_PROPERTY(OnlineListModel *searchSongs READ searchSongs CONSTANT)
    Q_PROPERTY(QVariantList sourceOptions READ sourceOptions NOTIFY sourceOptionsChanged)
    Q_PROPERTY(QString selectedSourceInstanceId READ selectedSourceInstanceId
               WRITE setSelectedSourceInstanceId NOTIFY selectedSourceInstanceIdChanged)
public:
    explicit OriginalUiMusicAdapter(MusicHub *hub,
                                    PlaybackCoordinator *playback,
                                    QObject *parent = nullptr);
    Q_INVOKABLE void activatePage(int pageKind);
    Q_INVOKABLE void search(const QString &text);
    Q_INVOKABLE bool browse(const QVariantMap &presentationItem);
    Q_INVOKABLE QUuid play(const QVariantMap &presentationItem);
    Q_INVOKABLE QUuid enqueue(const QVariantMap &presentationItem);
    Q_INVOKABLE QUuid setFavorite(const QVariantMap &presentationItem, bool favorite);
    Q_INVOKABLE QVariantMap fullItem(const QVariantMap &presentationItem) const;
};
```

- [ ] **Step 1: Write failing mapping and identity tests**

Cover section flattening and exact presentation roles used by original delegates:

```cpp
QCOMPARE(row["title"], QStringLiteral("Track 42"));
QCOMPARE(row["artist"], QStringLiteral("Artist"));
QCOMPARE(row["album"], QStringLiteral("Album"));
QCOMPARE(row["source"], QStringLiteral("navidrome"));
QCOMPARE(adapter.fullItem(row)["ref"].toMap()["entityId"], QStringLiteral("42"));
QVERIFY(!row.contains("url"));
QVERIFY(!row.contains("headers"));
```

Also cover duplicate titles from different sources, aggregate partial failure, specific scope, favorites split by entity type, and stale generation replacement.

- [ ] **Step 2: Run the adapter test and verify RED**

```bash
cmake --build build/bridge --target quemusic_original_ui_music_adapter_test -j8
ctest --test-dir build/bridge -R '^quemusic_original_ui_music_adapter_test$' --output-on-failure
```

Expected: build fails because `OriginalUiMusicAdapter` does not exist.

- [ ] **Step 3: Implement deterministic presentation mapping**

Use owned `OnlineListModel` instances. Store an opaque integer adapter key in each presentation row and keep `QHash<quint64, QVariantMap> m_fullItems` privately. Rebuild only from accepted `MusicPageModel` contents. Copy only allowlisted roles (`title`, `artist`, `album`, `cover`, `duration`, `source`, `entityType`, display subtitle); never blindly copy the full v2 map into QML roles.

```cpp
QVariantMap OriginalUiMusicAdapter::fullItem(const QVariantMap &row) const
{
    bool ok = false;
    const quint64 key = row.value(QStringLiteral("_adapterKey")).toULongLong(&ok);
    return ok ? m_fullItems.value(key) : QVariantMap{};
}
```

Action methods reject unknown keys and delegate complete private items to `MusicHub::actions()` or `PlaybackCoordinator`.

- [ ] **Step 4: Verify adapter and existing data-layer tests GREEN**

```bash
cmake --build build/bridge --target quemusic_original_ui_music_adapter_test quemusic_music_hub_test quemusic_playback_coordinator_test -j8
ctest --test-dir build/bridge -R '^(quemusic_original_ui_music_adapter_test|quemusic_music_hub_test|quemusic_playback_coordinator_test)$' --output-on-failure
```

- [ ] **Step 5: Commit the adapter**

```bash
git add CMakeLists.txt core/music/OriginalUiMusicAdapter.h core/music/OriginalUiMusicAdapter.cpp tests/tst_OriginalUiMusicAdapter.cpp
git commit -m "feat: adapt source v2 data to original UI models"
```

---

### Task 3: Compose the adapter and reconnect recommendation/category pages

**Files:**
- Modify: `main.cpp`
- Modify: `main.qml`
- Modify: `layout/MainContent.qml`
- Modify: `pages/HomePage.qml`
- Modify: `pages/PlaylistPage.qml`
- Create: `tests/tst_OriginalUiRecommendationQml.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `OriginalUiMusicAdapter` from Task 2.
- Produces: context property `originalUiMusic`; original recommendation/category controls backed by v2 data; aggregate/specific selector styled with existing `QButton`/`QDrop` components.

- [ ] **Step 1: Write failing QML interaction tests**

Instantiate restored pages with an adapter double. Assert page activation calls `activatePage(0/1)`, scope selection writes `selectedSourceInstanceId`, the original delegate receives mapped rows, pagination calls the adapter, category drill-down retains full private identity, and original click gestures call `play()`.

- [ ] **Step 2: Run and verify RED**

```bash
cmake --build build/bridge --target quemusic_original_ui_recommendation_qml_test -j8
ctest --test-dir build/bridge -R '^quemusic_original_ui_recommendation_qml_test$' --output-on-failure
```

- [ ] **Step 3: Add application composition**

Construct the adapter after `MusicHub` and `PlaybackCoordinator` and before loading QML:

```cpp
OriginalUiMusicAdapter originalUiMusic(&musicHub, &playbackCoordinator);
engine.rootContext()->setContextProperty(QStringLiteral("originalUiMusic"),
                                         &originalUiMusic);
```

Pass it through `MainContent` into original pages. Add the scope selector inside the existing top toolbar without changing its height, margins, colors, typography, page title, or transitions.

- [ ] **Step 4: Replace only recommendation/category data boundaries**

Bind existing list/card models to `musicAdapter.recommendSongs` and `musicAdapter.categoryItems`. Replace direct remote `MusicApi` calls for those plugin-backed rows with adapter `activatePage`, `browse`, `play`, `enqueue`, and continuation calls. Keep original local and legacy fallback paths distinguishable until their plugins are migrated.

- [ ] **Step 5: Verify recommendation/category and startup GREEN**

```bash
cmake --build build/bridge --target QueMusic quemusic_original_ui_recommendation_qml_test quemusic_plugin_startup_test -j8
ctest --test-dir build/bridge -R '^(quemusic_original_ui_recommendation_qml_test|quemusic_original_ui_structure_test|quemusic_plugin_startup_test)$' --output-on-failure
```

- [ ] **Step 6: Commit recommendation/category reconnection**

```bash
git add CMakeLists.txt main.cpp main.qml layout/MainContent.qml pages/HomePage.qml pages/PlaylistPage.qml tests/tst_OriginalUiRecommendationQml.cpp
git commit -m "feat: feed original discovery UI from source v2"
```

---

### Task 4: Reconnect favorites, search and capability-bounded actions

**Files:**
- Modify: `core/music/OriginalUiMusicAdapter.h`
- Modify: `core/music/OriginalUiMusicAdapter.cpp`
- Modify: `pages/FavouritePage.qml`
- Modify: `pages/SearchPage.qml`
- Create: `tests/tst_OriginalUiActionsQml.cpp`
- Modify: `tests/tst_OriginalUiMusicAdapter.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: private full-item lookup and `MediaActionRouter`.
- Produces: `capabilities(row) -> QVariantMap`, `loadMore(pageKind, sectionId)`, `retry(pageKind, sectionId)`, and action wrappers that return null UUID when identity/capability validation fails.

- [ ] **Step 1: Write failing capability and page interaction tests**

Assert that a Navidrome track exposes only its resolved operations; unsupported operations are disabled/hidden using existing menu conventions; a mixed selection computes the intersection; favorites preserve song/playlist separation; search uses the current scope and ignores stale results.

- [ ] **Step 2: Run and verify RED**

```bash
cmake --build build/bridge --target quemusic_original_ui_actions_qml_test quemusic_original_ui_music_adapter_test -j8
ctest --test-dir build/bridge -R '^(quemusic_original_ui_actions_qml_test|quemusic_original_ui_music_adapter_test)$' --output-on-failure
```

- [ ] **Step 3: Implement capability projection and action routing**

Project only action booleans and safe reason keys. For each operation, resolve the private full item and call the existing typed router. Do not translate unsupported actions into other source operations.

- [ ] **Step 4: Bind restored favorites/search controls**

Keep original tabs, selection mode, tool buttons and search layout. Replace their plugin-backed model/action boundaries with adapter calls. Correct `DownloadPage.qml`'s invalid cross-page `favouritePage` reference by moving shared selection state to its own page/root or using the existing local page identifier; add a QML regression assertion that activating DownloadPage produces no `ReferenceError`.

- [ ] **Step 5: Verify GREEN**

```bash
cmake --build build/bridge --target QueMusic quemusic_original_ui_actions_qml_test quemusic_original_ui_music_adapter_test quemusic_media_action_router_v2_test -j8
ctest --test-dir build/bridge -R '^(quemusic_original_ui_actions_qml_test|quemusic_original_ui_music_adapter_test|quemusic_media_action_router_v2_test)$' --output-on-failure
```

- [ ] **Step 6: Commit favorites/search/actions**

```bash
git add CMakeLists.txt core/music/OriginalUiMusicAdapter.h core/music/OriginalUiMusicAdapter.cpp pages/FavouritePage.qml pages/SearchPage.qml pages/DownloadPage.qml tests/tst_OriginalUiActionsQml.cpp tests/tst_OriginalUiMusicAdapter.cpp
git commit -m "feat: route original music actions through source capabilities"
```

---

### Task 5: Preserve original player UI while using secure v2 playback

**Files:**
- Modify: `components/LegacyQueueController.qml`
- Modify: `layout/PlayerControl.qml`
- Modify: `main.qml`
- Modify: `components/QDrop.qml`
- Modify: `components/SearchCard.qml`
- Create: `tests/tst_OriginalUiPlaybackQml.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `PlaybackCoordinator`, `QtPlaybackController`, adapter presentation rows.
- Produces: original player/queue gestures routed by full v2 identity; no stream descriptor in QML.

- [ ] **Step 1: Write failing playback and warning regression tests**

Assert original row click, enqueue, queue-entry activation, pause, resume, seek and stop reach their coordinator/controller doubles. Install a Qt message handler and fail on `QAudioDevice to QString`, `undefined to int`, missing context-property, and ambiguous `QueMusic` import warnings.

- [ ] **Step 2: Run and verify RED**

```bash
cmake --build build/bridge --target quemusic_original_ui_playback_qml_test -j8
ctest --test-dir build/bridge -R '^quemusic_original_ui_playback_qml_test$' --output-on-failure
```

- [ ] **Step 3: Reconnect player and queue without visual changes**

Keep original `PlayerControl` geometry and animations. Queue presentation rows keep adapter keys; activation resolves them below QML. Bind transport controls to `QtPlaybackController` and queue selection to `PlaybackCoordinator::playQueueEntry`.

- [ ] **Step 4: Fix warning sources at their type boundaries**

Give `QDrop` separate typed properties for display text and `QAudioDevice` payload. Give `SearchCard` integer properties explicit defaults and guards. Remove ambiguous module imports by importing the required singleton/type through one canonical URI rather than two resource paths.

- [ ] **Step 5: Verify playback and warning tests GREEN**

```bash
cmake --build build/bridge --target QueMusic quemusic_original_ui_playback_qml_test quemusic_qt_playback_controller_test quemusic_playback_coordinator_test -j8
ctest --test-dir build/bridge -R '^(quemusic_original_ui_playback_qml_test|quemusic_qt_playback_controller_test|quemusic_playback_coordinator_test|quemusic_legacy_queue_qml_test)$' --output-on-failure
```

- [ ] **Step 6: Commit playback integration**

```bash
git add CMakeLists.txt components/LegacyQueueController.qml components/QDrop.qml components/SearchCard.qml layout/PlayerControl.qml main.qml tests/tst_OriginalUiPlaybackQml.cpp
git commit -m "fix: preserve original player over secure v2 playback"
```

---

### Task 6: Redact network logs and complete end-to-end verification

**Files:**
- Create: `core/logging/RuntimeLoggingPolicy.h`
- Create: `core/logging/RuntimeLoggingPolicy.cpp`
- Modify: `main.cpp`
- Create: `tests/tst_SensitivePlaybackLogging.cpp`
- Modify: `CMakeLists.txt`
- Modify: `docs/superpowers/runbooks/navidrome-smoke-test.md`
- Create: `.superpowers/sdd/2026-09-12-original-ui-data-adapter/final-report.md`

**Interfaces:**
- Consumes: Navidrome stream resolution and Qt Multimedia logging categories.
- Produces: diagnostic output containing operation/result identifiers only, never authenticated URLs or secrets.

- [ ] **Step 1: Write a failing sensitive-log test**

Capture Qt messages while resolving/starting a controlled descriptor containing sentinel values. Assert no message contains the URL query, username, token, salt, password, authorization header, or sentinel:

```cpp
QVERIFY2(!joinedMessages.contains("secret-sentinel"), qPrintable(joinedMessages));
QVERIFY(!joinedMessages.contains("Authorization"));
QVERIFY(!joinedMessages.contains("stream.view?"));
```

- [ ] **Step 2: Run and verify RED against the observed leak**

```bash
cmake --build build/bridge --target quemusic_sensitive_playback_logging_test -j8
ctest --test-dir build/bridge -R '^quemusic_sensitive_playback_logging_test$' --output-on-failure
```

- [ ] **Step 3: Remove the leak at the earliest logging boundary**

Implement `RuntimeLoggingPolicy::install()` and call it before constructing
`QGuiApplication`. Do not log `QUrl::toString()` for authenticated media. The
routine policy disables `qt.scenegraph.general.debug`, `qt.rhi.general.debug`,
and `qt.multimedia.ffmpeg.debug`; the FFmpeg category remains disabled even when
the environment variable `QUEMUSIC_GRAPHICS_DEBUG=1` enables the two graphics
categories. Application diagnostics use a fixed operation name, source instance,
safe error kind and elapsed time.

- [ ] **Step 4: Run the full task test matrix**

```bash
cmake --build build/bridge --target QueMusic -j8
ctest --test-dir build/bridge -E '^(quemusic_app_storage_paths_test|quemusic_log_manager_test|quemusic_macos_local_network_usage_test|quemusic_macos_runtime_libraries_test|quemusic_navidrome_source_test)$' --output-on-failure
ctest --test-dir build/bridge -R '^quemusic_navidrome_source_test$' --output-on-failure
git diff --check
```

The second CTest command runs outside the sandbox because its fake server binds localhost. Record excluded user-dirty tests separately; do not report them as passing.

- [ ] **Step 5: Perform controlled GUI verification**

Start with routine Qt debug categories disabled. At 1140×720 and 810×540 inspect 推荐、分类、收藏、搜索、本地、下载, plugin settings, aggregate/source selection and page transitions. Use the configured Navidrome account for read-only page loading and one playback attempt. Confirm no clipping/white replacement UI and no authenticated URL in captured output. Do not alter favorite/playlist state.

- [ ] **Step 6: Record evidence and commit**

Write exact build/test counts, GUI observations, remaining unrelated dirty failures and any skipped mutation/signing checks to the final report.

```bash
git add CMakeLists.txt main.cpp core/logging/RuntimeLoggingPolicy.h core/logging/RuntimeLoggingPolicy.cpp tests/tst_SensitivePlaybackLogging.cpp docs/superpowers/runbooks/navidrome-smoke-test.md .superpowers/sdd/2026-09-12-original-ui-data-adapter/final-report.md
git commit -m "fix: redact authenticated playback diagnostics"
```

---

### Task 7: Independent review and branch readiness

**Files:**
- Modify only files required by verified review findings.
- Update: `.superpowers/sdd/2026-09-12-original-ui-data-adapter/final-report.md`

**Interfaces:**
- Consumes: Tasks 1–6 commits and their verification evidence.
- Produces: independently reviewed original-UI/data-layer integration ready for user acceptance; no merge or push.

- [ ] **Step 1: Request independent spec and code-quality review**

Review the full range from `11107e6` through Task 6 against the design. Require explicit checks for visual baseline preservation, absence of v1 restoration, private v2 identity, capability boundaries, authenticated-data leakage, user-dirty exclusion and deleted legacy behavior.

- [ ] **Step 2: Resolve Critical/Important findings with test-first fix commits**

For each accepted finding, reproduce it with the smallest failing test, implement one root-cause fix, run the affected and adjacent suites, and request a scoped re-review. Do not bundle unrelated cleanup.

- [ ] **Step 3: Run fresh final verification**

Repeat the Task 6 build, CTest, v1 symbol scan, QML-warning scan and controlled GUI checklist from current HEAD. Record exact outputs rather than relying on prior task reports.

- [ ] **Step 4: Update the report and stop before integration**

Mark implementation complete only if all required non-signing gates pass. Keep real mutation smoke and release codesign separately labeled. Present commit range and remaining user-owned dirty files; do not merge, push, reset or discard them.
