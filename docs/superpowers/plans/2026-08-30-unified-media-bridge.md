# Unified Media Bridge Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a UI-facing media bridge that makes Navidrome search, browsing, artwork, lyrics, and playback use one normalized model without exposing source plugins to QML.

**Architecture:** `MediaBridge` owns normalized `MediaListModel` instances and coordinates source sessions through `SourceSessionRegistry`. The Source SDK remains the protocol boundary; Navidrome returns its existing DTOs, which the bridge maps into `MediaItem`, `MediaPage`, and `PlaybackEntry`. QML consumes only bridge models, account records, and bridge signals. Existing `MusicApiService` pages remain unchanged until an individual legacy provider is intentionally migrated.

**Tech Stack:** C++17, Qt 6 Core/QML/Network/Test/Multimedia, QSettings, macOS Security framework, native Qt plugins, existing Source SDK v1.

**Spec:** `docs/superpowers/specs/2026-08-30-unified-media-bridge-design.md`

## Global Constraints

- Continue using `IMusicSourceSession` and Source SDK v1 without reordering or changing its virtual ABI.
- Do not expose `SourceManager`, `PluginManager`, `IMusicSourceSession`, raw plugin `QObject`s, or raw provider JSON to QML.
- Store only non-sensitive account fields and a keychain reference in QSettings; secrets must use the platform keychain.
- Use macOS Security.framework for the first keychain backend; unsupported platforms must report account-secret storage as unavailable rather than write plaintext.
- Preserve CMake 3.16 compatibility and existing macOS plugin package deployment.
- Preserve existing `MusicApiService` UI behavior; do not migrate NetEase, Kugou, QQ, or local files in this milestone.
- All new asynchronous state must be cancellable when its account is disabled, source package is unloaded, or bridge is destroyed.

---

### Task 1: Define normalized media contracts and QML list model

**Files:**
- Create: `core/media/MediaTypes.h`
- Create: `core/media/MediaTypes.cpp`
- Create: `core/media/MediaListModel.h`
- Create: `core/media/MediaListModel.cpp`
- Create: `tests/tst_MediaTypes.cpp`
- Create: `tests/tst_MediaListModel.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: Qt Core/QML model APIs only.
- Produces: `MediaId`, `MediaKind`, `MediaItem`, `MediaPage`, `PlaybackEntry`, `MediaRequestState`, `MediaError`, and `MediaListModel` for later bridge tasks.

- [ ] **Step 1: Write the failing contract/model tests**

Create tests proving that a `MediaId` round-trips through `QVariantMap`, two IDs differ when their account differs, a `MediaItem` exposes stable QML roles, and `MediaListModel` transitions through `Loading`, `Ready`, `Empty`, and `Failed` without losing its previous role map.

```cpp
const MediaId id{"navidrome", "home", "song-1", MediaKind::Track};
QCOMPARE(mediaIdFromVariantMap(mediaIdToVariantMap(id)), id);

model.beginRequest(requestId);
model.replacePage({{}, {}, false});
QCOMPARE(model.requestState(), MediaRequestState::Empty);
```

- [ ] **Step 2: Run the new tests to verify they fail**

Run:

```bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/cmake --build build --target quemusic_media_types_test quemusic_media_list_model_test -j 1
```

Expected: CMake cannot find the new targets before they are added.

- [ ] **Step 3: Implement the value contracts**

Add `MediaKind { Track, Album, Artist, Playlist, Directory }` and structures with these fields:

```cpp
struct MediaId { QString sourceId; QString accountId; QString nativeId; MediaKind kind; };
struct MediaItem { MediaId id; QString title; QString subtitle; QStringList artists;
                   QString albumTitle; qint64 durationMs; QUrl artworkUrl;
                   bool playable; bool container; QVariantMap extra; };
struct MediaPage { QList<MediaItem> items; QString nextCursor; bool hasMore; };
struct PlaybackEntry { MediaId id; QUrl streamUrl; QMap<QString, QString> headers;
                       QDateTime expiresAt; QUrl artworkUrl; QString lyrics; };
```

Create a fixed-role `MediaListModel` rather than reusing dynamic-role `OnlineListModel`. Expose `count`, `requestState`, `errorKind`, `errorMessage`, `hasMore`, `canRetry`, `get(index)`, and operations `beginRequest`, `replacePage`, `appendPage`, `setFailure`, and `clear`.

- [ ] **Step 4: Register and pass the focused tests**

Add `quemusic_media_types_test` and `quemusic_media_list_model_test` to the root CMake test block, build them, and run:

```bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/ctest --test-dir build -R 'quemusic_media_(types|list_model)_test' --output-on-failure
```

Expected: both tests pass.

- [ ] **Step 5: Commit the foundation**

```bash
git add CMakeLists.txt core/media/MediaTypes.* core/media/MediaListModel.* tests/tst_MediaTypes.cpp tests/tst_MediaListModel.cpp
git commit -m "feat: add normalized media contracts"
```

### Task 2: Add keychain-backed source account storage

**Files:**
- Create: `core/media/SourceAccountStore.h`
- Create: `core/media/SourceAccountStore.cpp`
- Create: `core/media/MacKeychainSecretStore.h`
- Create: `core/media/MacKeychainSecretStore.mm`
- Create: `tests/tst_SourceAccountStore.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `SourceAccount`, QSettings, Qt Core; on macOS, Security.framework.
- Produces: `StoredSourceAccount`, `SourceAccountStore`, and injectable `ISecretStore` for the session registry.

- [ ] **Step 1: Write failing account-store tests using an in-memory secret fake**

Use a temporary QSettings INI path and a test `ISecretStore`. Verify the settings file contains source ID, account ID, display name, enabled flag, server URL, and a generated secret reference, but never the password bytes. Verify removal deletes both settings metadata and the secret-store record.

```cpp
store.upsert(accountWithSecret("navidrome", "home", "secret"));
QVERIFY(!settingsFile.readAll().contains("secret"));
QCOMPARE(secretStore.value(store.secretReference("navidrome", "home")), QByteArray("secret"));
```

- [ ] **Step 2: Run the test to verify it fails**

Run:

```bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/cmake --build build --target quemusic_source_account_store_test -j 1
```

Expected: target does not exist before the account store is added.

- [ ] **Step 3: Implement account and secret storage**

Define `ISecretStore::write/read/remove(reference)` and `StoredSourceAccount` with non-sensitive fields. `SourceAccountStore::upsert` writes the secret first, then persists a versioned `sources/<sourceId>/<accountId>` record with only `secretReference`; on a settings-write failure it removes the newly written secret. `remove` clears both stores. `sourceAccount()` reconstructs a `SourceAccount` only after reading a secret.

Implement `MacKeychainSecretStore` with `SecItemAdd`, `SecItemCopyMatching`, and `SecItemDelete`, service name `com.bronekox.QueMusic.source-account`. Link Security.framework only on `APPLE`; add an unsupported backend that returns an explicit error without persisting a secret elsewhere.

- [ ] **Step 4: Run focused account tests**

Run:

```bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/ctest --test-dir build -R quemusic_source_account_store_test --output-on-failure
```

Expected: metadata persistence, secret omission, and removal all pass.

- [ ] **Step 5: Commit secure account persistence**

```bash
git add CMakeLists.txt core/media/SourceAccountStore.* core/media/MacKeychainSecretStore.* tests/tst_SourceAccountStore.cpp
git commit -m "feat: persist source accounts securely"
```

### Task 3: Create the source session registry

**Files:**
- Create: `core/media/SourceSessionRegistry.h`
- Create: `core/media/SourceSessionRegistry.cpp`
- Create: `tests/tst_SourceSessionRegistry.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `SourceManager::createSession`, `SourceAccountStore`, `MediaId` identity rules.
- Produces: one leased `IMusicSourceSession` per enabled source/account pair and deterministic cleanup signals.

- [ ] **Step 1: Write failing registry tests**

Use the existing `plugins/test-source` fixture and a fake account store. Assert repeated `sessionFor("test-source", "home")` calls return the same session, disabling an account cancels its tracked request and destroys its session, and a removed source package makes future lookup fail without dereferencing the old object.

```cpp
auto *first = registry.sessionFor({"test-source", "home"});
QCOMPARE(registry.sessionFor({"test-source", "home"}), first);
registry.disable("test-source", "home");
QVERIFY(sessionDestroyed.wait(1000));
```

- [ ] **Step 2: Run the new test to verify it fails**

Run:

```bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/cmake --build build --target quemusic_source_session_registry_test -j 1
```

Expected: target does not exist before implementation.

- [ ] **Step 3: Implement registry ownership and cleanup**

Key active sessions by `(sourceId, accountId)`. Construct sessions with the registry as parent through `SourceManager`; retain only `QPointer<IMusicSourceSession>`. Track request UUIDs per session, call `cancel` before delete, and connect to `SourceManager::sourceChanged` so a missing source invalidates its sessions. Expose `sessionFor`, `disable`, `remove`, `enabledAccounts`, and `sessionInvalidated`.

- [ ] **Step 4: Run focused tests**

Run:

```bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/ctest --test-dir build -R quemusic_source_session_registry_test --output-on-failure
```

Expected: reuse, cancellation, plugin removal, and cleanup pass.

- [ ] **Step 5: Commit registry lifecycle handling**

```bash
git add CMakeLists.txt core/media/SourceSessionRegistry.* tests/tst_SourceSessionRegistry.cpp
git commit -m "feat: manage source account sessions"
```

### Task 4: Implement MediaBridge normalization and request state

**Files:**
- Create: `core/media/MediaBridge.h`
- Create: `core/media/MediaBridge.cpp`
- Create: `tests/tst_MediaBridge.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `MediaListModel`, `SourceSessionRegistry`, `SourceError`, and `IMusicSourceSession` success/failure signals.
- Produces: QML-safe `MediaBridge` invokables, per-operation models, and normalized Navidrome result mapping.

- [ ] **Step 1: Write failing bridge tests**

Use a local `QTcpServer` Navidrome response fixture. Test that `search("navidrome/home", "Song")` maps a Subsonic song, album, and artist into fixed `MediaItem` roles; `browse` maps root artists and directories; an empty provider response sets `Empty`; a SourceError sets `Failed` with the matching error kind; and retry replays only the saved search or browse intent.

```cpp
bridge.search("navidrome/home", "Song", 20);
QTRY_COMPARE(bridge.searchResults()->requestState(), MediaRequestState::Ready);
QCOMPARE(bridge.searchResults()->get(0).value("sourceId"), "navidrome");
QCOMPARE(bridge.searchResults()->get(0).value("accountId"), "home");
```

- [ ] **Step 2: Run the bridge test to verify it fails**

Run:

```bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/cmake --build build --target quemusic_media_bridge_test -j 1
```

Expected: target does not exist before `MediaBridge` is registered.

- [ ] **Step 3: Implement normalization and request dispatch**

Expose `searchResults`, `browseResults`, `requestState`, and Q_INVOKABLE methods `search(sourceScope, keyword, limit)`, `browse(sourceId, accountId, nativeId, kind, limit)`, `open(item)` (browse a container or play a track), `loadMore`, `retry`, and `cancel`. Map the existing Navidrome normalized JSON fields (`kind`, `id`, `sourceId`, `title`, `artist`, `album`, `duration`, `coverArtId`) to `MediaItem` without changing the source-plugin ABI. Associate each provider UUID with a bridge operation and model; ignore terminal callbacks for cancelled or invalidated requests.

- [ ] **Step 4: Run focused tests**

Run:

```bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/ctest --test-dir build -R quemusic_media_bridge_test --output-on-failure
```

Expected: mapping, empty state, failure state, retry, cancellation, and source invalidation pass.

- [ ] **Step 5: Commit bridge request handling**

```bash
git add CMakeLists.txt core/media/MediaBridge.* tests/tst_MediaBridge.cpp
git commit -m "feat: bridge source results to media models"
```

### Task 5: Add artwork, lyrics, and playback-entry resolution

**Files:**
- Modify: `core/media/MediaBridge.h`
- Modify: `core/media/MediaBridge.cpp`
- Create: `tests/tst_MediaBridgePlayback.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `MediaId`, `SourceSessionRegistry`, `IMusicSourceSession::resolveStream/fetchLyrics`, and `SourceManager::requestArtwork`.
- Produces: `artworkReady`, `lyricsReady`, `playbackReady`, and QML invokables `loadArtwork`, `loadLyrics`, `play`, and `enqueue`.

- [ ] **Step 1: Write failing media-action tests**

Use the existing Navidrome HTTP fixtures to prove that `play` emits a `PlaybackEntry` with the same media identity and an expiring authenticated URL, `loadArtwork` emits the cover URL, and `loadLyrics` emits the normalized lyric payload. Add a test that a source returning non-empty HTTP headers creates a playback failure with `Unsupported` until the native playback backend supports request headers.

```cpp
QSignalSpy ready(&bridge, &MediaBridge::playbackReady);
bridge.play(trackItem);
QVERIFY(ready.wait(1000));
QCOMPARE(ready.constFirst().at(0).toMap().value("nativeId"), "song-1");
```

- [ ] **Step 2: Run the test to verify it fails**

Run:

```bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/cmake --build build --target quemusic_media_bridge_playback_test -j 1
```

Expected: target does not exist before the actions are added.

- [ ] **Step 3: Implement media actions**

Resolve source ownership from `MediaId`, then dispatch `resolveStream`, `requestArtwork`, and `fetchLyrics` to the registry session. `enqueue(item)` emits a normalized queue-item map without resolving its stream; `play(item)` resolves it and emits a `PlaybackEntry` map with `url`, `title`, `artist`, `albumTitle`, `artworkUrl`, `durationMs`, and `mediaId`. Retain no stream URL after the entry is replaced. For this milestone, only URL-authenticated entries such as Navidrome are playable in the current QML `MediaPlayer`; header-bearing entries fail explicitly rather than silently dropping headers.

- [ ] **Step 4: Run focused tests**

Run:

```bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/ctest --test-dir build -R quemusic_media_bridge_playback_test --output-on-failure
```

Expected: stream, artwork, lyrics, unsupported headers, and cancellation behavior pass.

- [ ] **Step 5: Commit media actions**

```bash
git add CMakeLists.txt core/media/MediaBridge.* tests/tst_MediaBridgePlayback.cpp
git commit -m "feat: resolve bridge artwork lyrics and playback"
```

### Task 6: Expose the bridge and add source-account and browsing UI

**Files:**
- Create: `pages/SourceLibraryPage.qml`
- Modify: `SettingsView.qml`
- Modify: `main.cpp`
- Modify: `main.qml`
- Modify: `CMakeLists.txt`
- Create: `tests/tst_MediaBridgeQml.cpp`

**Interfaces:**
- Consumes: `MediaBridge` QML API and `SourceAccountStore` account records.
- Produces: a QML `mediaBridge` context property, Navidrome account management controls, unified search/library browsing UI, and playback-queue integration.

- [ ] **Step 1: Write the failing QML-boundary test**

Create a headless `QQmlApplicationEngine` test that initializes application startup and the bridge. Assert `mediaBridge` exists, `sourceManager` and raw sessions remain absent from the root context, and a bridge playback-ready map can be accepted by a queue adapter without reading provider-specific fields.

```cpp
QVERIFY(engine.rootContext()->contextProperty("mediaBridge").isValid());
QVERIFY(!engine.rootContext()->contextProperty("sourceManager").isValid());
```

- [ ] **Step 2: Run the test to verify it fails**

Run:

```bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/cmake --build build --target quemusic_media_bridge_qml_test -j 1
```

Expected: target does not exist before main application wiring.

- [ ] **Step 3: Wire C++ and QML**

Construct `SourceAccountStore`, `SourceSessionRegistry`, and `MediaBridge` in `main.cpp` after `SourceManager` initialization; parent them to the QML engine and expose only `mediaBridge`. Add a Music Sources panel to `SettingsView.qml` that lists plugin sources and accounts, creates/edits/enables/removes a Navidrome account, and reports storage or authentication errors without displaying a secret.

Create `SourceLibraryPage.qml` with source/account selection, search input, normalized result delegates, browse navigation, loading/empty/error/retry states, artwork loading, and track actions. In `main.qml`, add the page to navigation, subscribe to `mediaBridge.playbackReady`, append a queue item that stores the serialized `MediaId`, and set `mainMedia.source` from the playback entry URL. Update the queue's next/previous action to call `mediaBridge.play` for bridge queue entries while retaining existing `MusicApi` and local-file behavior.

- [ ] **Step 4: Run focused UI-boundary tests and application build**

Run:

```bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/ctest --test-dir build -R quemusic_media_bridge_qml_test --output-on-failure
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/cmake --build build --target QueMusic -j 1
```

Expected: the QML boundary test passes and QueMusic.app builds with the Navidrome package deployed.

- [ ] **Step 5: Commit UI integration**

```bash
git add CMakeLists.txt main.cpp main.qml SettingsView.qml pages/SourceLibraryPage.qml tests/tst_MediaBridgeQml.cpp
git commit -m "feat: add unified source library UI"
```

### Task 7: Run end-to-end verification and document the migration boundary

**Files:**
- Modify: `docs/PLUGIN_API.md`
- Modify: `ARCHITECTURE.md`
- Modify: `docs/superpowers/specs/2026-08-30-unified-media-bridge-design.md`
- Create: `.superpowers/sdd/2026-08-30-unified-media-bridge/verification-report.md`

**Interfaces:**
- Consumes: every bridge component and existing Navidrome smoke executable.
- Produces: verified behavior and documentation of the legacy-provider migration boundary.

- [ ] **Step 1: Update documentation before final verification**

Document the `MediaBridge` boundary, fixed list roles, account keychain rule, playback-header limitation, and the rule that legacy providers remain behind `MusicApiService` until an adapter is complete. Mark the design accepted only after all checks below pass.

- [ ] **Step 2: Run the full automated suite**

Run:

```bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/cmake --build build -j 1
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/ctest --test-dir build --output-on-failure
```

Expected: every registered test passes in the permitted local environment.

- [ ] **Step 3: Run the opt-in Navidrome smoke test**

Run with credentials supplied through the caller's environment only:

```bash
build/bin/quemusic_navidrome_smoke
```

Expected: ping, search, browse, stream, artwork, and lyric operations report success when the selected server contains a matching playable track. Do not put credentials, tokens, or server response data in the report.

- [ ] **Step 4: Perform manual GUI verification**

Launch `build/bin/QueMusic.app`. Add a Navidrome account, search and browse the library, play a track, verify artwork and lyrics behavior, disable the account while idle, and confirm the source disappears without crashes or raw QObject warnings.

- [ ] **Step 5: Record results and commit**

Record only command outcomes, commit IDs, and non-sensitive observations in the SDD report. Then commit documentation:

```bash
git add ARCHITECTURE.md docs/PLUGIN_API.md docs/superpowers/specs/2026-08-30-unified-media-bridge-design.md
git commit -m "docs: document unified media bridge"
```
