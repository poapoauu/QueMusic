# Phase 4 Queue / History Identity Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make Local and Navidrome share a restart-safe, path-free Host queue and recent-play history while retaining QueMusic's existing UI.

**Architecture:** Local first replaces public path-shaped entity IDs with per-instance opaque UUIDs in its private persisted index. A Host codec and store then persist typed Coordinator queue occurrences and successful-start history; restore never autoplays and playback still resolves through the current Source Plugin. QML only changes where necessary to distinguish a restored queue from active playback.

**Tech Stack:** C++17, Qt 6 Core/QSaveFile/Qt Test, QML, CMake/CTest.

**Spec:** [Phase 4 design](../specs/2026-09-29-phase-4-queue-history-identity-design.md); [final architecture](../../architecture/QueMusic-Plugin-Architecture-Final-Plan.md) remains authoritative.

## Global Constraints

- Do not change `sdk/source/v2` ABI/layout; `sourcePluginId` remains the v2 field name but stores Source ID, not package ID.
- Store `MediaRefV2`'s complete five-field identity and an occurrence ID for each queue entry; never persist a stream URL, token, direct path locator or arbitrary plugin metadata in the Host queue/history JSON.
- `schemaVersion` is `1`; queue limit 1000, recent-play limit 200, each display field at most 512 Unicode characters, at most 16 artists, each identity field at most 1024 characters, file at most 16 MiB.
- The Local private identity index may retain canonical paths; the Host queue/history file may not. Same-path successful rescan/restart keeps ID, removal followed by recreation gets a fresh ID, movement gets a fresh ID, and different SourceInstances never share identity namespaces.
- A restored queue never autoplays or changes a running Legacy playback context. Phase 5 removes Legacy playback; Phase 6 finishes dynamic UI. Neither is part of this plan.
- Use test-injected paths and hooks so tests never write real user application data. Work in the existing isolated checkout; preserve unrelated changes, do not push/merge without a separate user choice.

## Review Focus

1. Removed then recreated local file at the same path must not inherit the deleted file's ID — Task 1 `removedThenRecreatedGetsNewId`.
2. An old cached Local `file://` reference must not be shown or queued after the ID switch — Task 3 `rejectsPathLocatorCachedPage`.
3. Duplicate references must retain distinct queue occurrences after a restart and delete the intended occurrence — Task 5 `restoresDuplicateOccurrencesAndRemovesById`.
4. A future-version or corrupt store must never be overwritten during startup or on the next queue signal — Task 6 `futureVersionIsReadOnly` and `corruptFileRequiresBackupBeforeWrite`.
5. A nonempty restored queue must not seize Legacy transport controls, yet remain selectable — Task 7 `restoredQueueDoesNotSeizeLegacyPlayback`.

## File map and sequence

- Plugin-private identity: new `plugins/local-source/LocalIdentityStore.{h,cpp}`; adapt `LocalLibraryIndex.{h,cpp}` and `LocalSourceSession.cpp`. `LocalSourceScanner` stays the internal canonical-path validator; it does not publish its path-shaped ID after the index projection.
- Cache boundary: `core/music/PageCache.cpp` rejects disk entries containing direct media locators before they reach the UI; old artwork/lyrics keys become unreachable when public Local IDs change.
- Host typed state: new `core/music/QueueHistoryTypes.h`, `QueueHistoryCodec.{h,cpp}`, and `QueueHistoryStore.{h,cpp}`; adjust `PlaybackCoordinator.{h,cpp}` and `main.cpp`. Keep codec separate from Coordinator and disk lifecycle.
- UI: targeted changes in `main.qml`, `components/PlayList.qml`, and their existing QML tests. No new history page or source-specific Host UI.
- Build: add only the necessary files/targets in `CMakeLists.txt`; update Phase 4 acceptance documentation after tests.

---

### Task 1: Local private, durable opaque ID allocation

**Files:** Create `plugins/local-source/LocalIdentityStore.h`, `.cpp`; modify `CMakeLists.txt` local-index target; create `tests/tst_LocalIdentityStore.cpp`.

**Interfaces:** `LocalIdentityStore(QString filePath, QString sourceInstanceId)`; `bool reconcile(const QStringList &trackPaths, const QStringList &directoryPaths, LocalIdentitySnapshot *out, QString *errorKey)` where `LocalIdentitySnapshot` has typed `trackIdsByPath` and `directoryIdsByPath` maps. Input paths are already canonical and root-bounded by `LocalSourceScanner`; allocation is per-instance and happens only for a complete successful scan.

- [ ] **Step 1: Write failing tests.** `samePathKeepsIdAcrossRestart`, `independentInstancesDoNotShareIds`, `removedThenRecreatedGetsNewId`, `failedAtomicWriteDoesNotPublishNewId`, and `corruptIndexDoesNotReuseAnOldId`; assert UUID-shaped IDs, no `file:` prefix, private file version, and no cross-kind alias.
- [ ] **Step 2: Run RED.** Configure/build `quemusic_local_identity_store_test`, then `ctest --test-dir build-phase3 -R '^quemusic_local_identity_store_test$' --output-on-failure`; expect missing target/interface or failing assertions.
- [ ] **Step 3: Implement.** Keep only currently scanned paths in the versioned private map, generate fresh UUIDs for new paths, and write with `QSaveFile` before returning `out`. Verify that the file's stored instance ID matches the constructor's ID. On failed write return false without publishing a partially reconciled map.
- [ ] **Step 4: Run GREEN** with the same target and CTest command; all five cases pass.
- [ ] **Step 5: Commit** identity store, test and CMake change: `feat: add durable local opaque identities`.

### Task 2: Publish opaque Local refs and resolve them through the current scan

**Files:** Modify `plugins/local-source/LocalLibraryIndex.{h,cpp}`, `LocalSourceSession.cpp`, `tests/tst_LocalLibraryIndex.cpp`, `tests/tst_LocalSource.cpp`, `tests/tst_LocalSourceIntegration.cpp`, and focused `CMakeLists.txt` wiring if needed.

**Interfaces:** `LocalIndexSnapshot` gains `QHash<QString, QString> trackPathById`, `directoryPathById`, and matching path-to-ID maps. Extend `LocalLibraryIndex(..., QObject *parent = nullptr, Hooks hooks = {}, QString identityFile = {})` and `LocalLibraryIndexPool(LocalLibraryIndex::Hooks hooks = {}, QString identityRoot = {})`; the empty root resolves to plugin-private `QStandardPaths::AppDataLocation` and tests inject a temporary root. Preserve existing constructor argument order. Session emits opaque `MediaRefV2.entityId`, resolves `directoryId`/Track only through the matching snapshot map, and retains the existing canonical-root/readability checks. Task 1's `reconcile(...)` is the only allocator.

- [ ] **Step 1: Write failing tests.** Verify root/child Directory and Track refs contain UUIDs, same-path restart IDs match, wrong-kind/other-instance/deleted IDs and legacy `file://` IDs fail, moved file gets a new ID, scan write failure retains the previous published snapshot, and forged/symlink-escaped refs never resolve. Replace tests that directly construct public `directoryId` from `QUrl::fromLocalFile` with IDs returned by the provider; retain scanner-internal path-validation tests.
- [ ] **Step 2: Run RED.** Build `quemusic_local_library_index_test`, `quemusic_local_source_test`, `quemusic_local_source_integration_test`; run the corresponding `ctest -R 'quemusic_local_(library_index|source|source_integration)_test' --output-on-failure`; expect assertions to fail on path-shaped IDs.
- [ ] **Step 3: Implement.** Reconcile a successful scan before atomic snapshot publication, project both Track and Directory IDs, then look up paths only inside the owning session's current snapshot. Hash the instance ID for the private filename; never use a display name or raw user string as a path component. Remove public path-ID acceptance from normal page/resource requests; do not weaken `validatedPath`'s private filesystem boundary checks.
- [ ] **Step 4: Run GREEN** for the three test executables and the Local scanner test; all pass.
- [ ] **Step 5: Commit** plugin integration and test changes: `feat: publish opaque local media references`.

### Task 3: Stop old cached path refs at the Host cache boundary

**Files:** Modify `core/music/PageCache.cpp`, `tests/tst_MusicCaches.cpp` (and `PageCache.h` only if its public API needs no-layout helpers).

**Interfaces:** The existing `readPage` deserialization helper in `PageCache.cpp` rejects a persisted item if its `MediaRefV2.entityId` is a direct `file://`, stream URL or absolute filesystem path. It does not branch on Local package/source names; unaffected opaque Navidrome IDs still load. Failed reads follow the existing cache-miss path and can be refetched through the current plugin.

- [ ] **Step 1: Write failing tests** `rejectsPathLocatorCachedPage`, `opaqueCacheSurvivesRestart`, and `oldArtworkOrLyricsKeyIsNotReused`: a disk page with a legacy Local ref returns cache miss, an opaque page round-trips, and a new Local ref cannot hit an old resource key.
- [ ] **Step 2: Run RED:** `ctest --test-dir build-phase3 -R '^quemusic_music_caches_test$' --output-on-failure`; expect the legacy page to be returned.
- [ ] **Step 3: Implement** validation during cache deserialization, not by deleting all Source caches. Keep stream resources out of logs and reject before UI publication.
- [ ] **Step 4: Run GREEN** for `quemusic_music_caches_test` and `quemusic_page_repository_test`.
- [ ] **Step 5: Commit**: `fix: reject legacy path identities from page cache`.

### Task 4: Typed Host queue/history codec and one-shot legacy input

**Files:** Create `core/music/QueueHistoryTypes.h`, `QueueHistoryCodec.{h,cpp}`, `tests/tst_QueueHistoryCodec.cpp`; add CMake library/test targets.

**Interfaces:** `QueueOccurrence` contains `QUuid occurrenceId`, `MediaRefV2 ref`, title/artists/album/duration and `bool playableAtEnqueue`; `RecentPlay` contains the same identity/presentation and `QDateTime playedAt`; `QueueHistorySnapshot` contains ordered lists plus `int legacyImportVersion = 0`. `enum class QueueHistoryDecodeStatus { Ok, Corrupt, UnsupportedVersion, TooLarge }`; `DecodeResult` contains status and snapshot; `LegacyImportResult` contains accepted occurrences, rejected count and parsed flag. `QueueHistoryCodec::encode(const QueueHistorySnapshot &) -> std::optional<QByteArray>`, `decode(const QByteArray &) -> DecodeResult`, and `importLegacy(const QByteArray &) -> LegacyImportResult`. These types are Host-private, not Source SDK additions.

- [ ] **Step 1: Write failing tests** `mixedQueueRoundTripsWithoutLocators`, `rejectsMalformedAndFutureVersion`, `enforcesAllSizeLimits`, `duplicateOccurrencesStayDistinct`, and `legacyInputOnlyAcceptsCompleteOpaqueRefs`. Assert `schemaVersion == 1`, 1000/200 limits, 512/16/1024/16 MiB limits, UTC timestamps, no URL/token/path or unapproved keys in output, and rejected legacy counts.
- [ ] **Step 2: Run RED:** build and run `quemusic_queue_history_codec_test`; expect missing interface/target or assertions.
- [ ] **Step 3: Implement** whitelist serialization and strict full-document validation. Never deserialize arbitrary plugin metadata or `availableActions`; reject locator-shaped entity IDs before encode and decode. `importLegacy` accepts explicit bytes only and never guesses from `path/hash/source` integers.
- [ ] **Step 4: Run GREEN:** `ctest --test-dir build-phase3 -R '^quemusic_queue_history_codec_test$' --output-on-failure`.
- [ ] **Step 5: Commit** codec and tests: `feat: add versioned queue history codec`.

### Task 5: Coordinator snapshot/restore and occurrence-safe queue mutation

**Files:** Modify `core/music/PlaybackCoordinator.{h,cpp}`, `tests/tst_PlaybackCoordinator.cpp`; link Task 4's Host-private types via CMake.

**Interfaces:** `QList<QueueOccurrence> exportQueue() const`, `bool restoreQueue(const QList<QueueOccurrence> &items)`, and `Q_INVOKABLE bool removeOccurrence(const QUuid &id)`. `restoreQueue` runs only on an idle Coordinator, validates every item, preserves occurrence IDs and order, emits `queueChanged` once, and never resolves a stream. `queue()` exposes a presentation-only `unavailable` flag and a source label derived from the current registry or Source ID fallback; `playQueueEntry` retains exact instance/account checks.

- [ ] **Step 1: Write failing tests** `restoresDuplicateOccurrencesAndRemovesById`, `restoreNeverAutoplays`, `missingInstanceRemainsVisibleAndCannotPlay`, `sameNameDifferentAccountNeverRebinds`, and `removeEarlierOccurrenceKeepsCurrentIndex`. Assert null generation/current index after restore; missing source does not delete an item; rejected invalid restore leaves prior queue unchanged.
- [ ] **Step 2: Run RED:** `ctest --test-dir build-phase3 -R '^quemusic_playback_coordinator_test$' --output-on-failure`; expect missing interface/failed assertions.
- [ ] **Step 3: Implement** a private restored-entry construction path, not `enqueue(queue())`; store only a conservative Play hint and re-run registry/capability checks plus provider resolution at play time. Recompute availability on instance changes; never treat the stored hint as authorization. Removing the current occurrence is rejected, matching the existing UI restriction.
- [ ] **Step 4: Run GREEN** for `quemusic_playback_coordinator_test` and existing SourceRegistry/Local integration tests.
- [ ] **Step 5: Commit**: `feat: restore coordinator queue by stable occurrence`.

### Task 6: Atomic Host store, playback history and protected import

**Files:** Create `core/music/QueueHistoryStore.{h,cpp}`, `tests/tst_QueueHistoryStore.cpp`; adjust CMake target from Task 4.

**Interfaces:** `QueueHistoryStore(PlaybackCoordinator *coordinator, QString filePath, QObject *parent = nullptr, Hooks hooks = {})`, `bool loadAndAttach()`, `QList<RecentPlay> history() const`, `LegacyImportResult importLegacyFile(const QString &path)`, and `Q_PROPERTY(QString warningKey ... NOTIFY warningChanged)` / `backupPath` read access. `Hooks` exposes injectable `write(path, bytes) -> bool` and `backup(source, destination) -> bool`; defaults use `QSaveFile` and a protected file copy. `loadAndAttach` loads/restores before connecting to queue/playback signals. `playbackStarted` records only the matching current generation/occurrence, once per successful start.

- [ ] **Step 1: Write failing tests** `persistsMixedQueueAcrossRestart`, `successfulStartOnlyAddsHistoryOnce`, `futureVersionIsReadOnly`, `corruptFileRequiresBackupBeforeWrite`, `failedWriteRetainsPreviousSnapshot`, and `legacyImportPreservesUnmappedBytesAndWarns`. Use temporary paths; prove no active file overwrite on a future version, failed quarantine backup, or atomic write failure.
- [ ] **Step 2: Run RED:** build/run `quemusic_queue_history_store_test`; expect missing target/interface or assertions.
- [ ] **Step 3: Implement** `QSaveFile` writes and bounded history. Corrupt/oversize files require a unique protected copy before new writes; future versions remain in-place read-only. Explicit legacy input is backed up first, imported at most once, and never auto-discovered by guessed filename. Never log raw input or stream/path secrets.
- [ ] **Step 4: Run GREEN:** `ctest --test-dir build-phase3 -R '^quemusic_queue_history_(codec|store)_test$' --output-on-failure`.
- [ ] **Step 5: Commit**: `feat: persist host queue and recent play history`.

### Task 7: Wire startup and preserve original queue/player context

**Files:** Modify `main.cpp`, `main.qml`, `components/PlayList.qml`, `tests/tst_LegacyQueueQml.cpp`, `tests/tst_OriginalUiPlaybackQml.cpp`; add QML fixtures only if the current tests cannot express the restored-state case.

**Interfaces:** `main.cpp` creates `QueueHistoryStore` with `QStandardPaths::AppDataLocation` after Coordinator construction and calls `loadAndAttach()` before QML engine load. QML receives `queueHistoryStore` for warning display. `main.qml` separates `secureQueueAvailable` (queue nonempty) from `securePlaybackActive` (Coordinator current item exists). `PlayList.qml` exposes an explicit restored-queue choice when Legacy is active, dispatches a secure-list click directly to `playbackCoordinator.playQueueEntry(index)`, and deletes secure rows by `occurrenceId` rather than modifying `playListModel`.

- [ ] **Step 1: Write failing QML tests** `restoredQueueDoesNotSeizeLegacyPlayback`, `restoredItemCanBeSelectedWithoutLegacyDispatch`, `secureDeleteDoesNotTouchLegacyQueue`, and `startupWarningIsVisible`. Test a nonempty restored Coordinator queue with `currentIndex == -1` while Legacy media is active; original control actions stay Legacy until explicit selection.
- [ ] **Step 2: Run RED:** `ctest --test-dir build-phase3 -R 'quemusic_(legacy_queue_qml|original_ui_playback_qml)_test' --output-on-failure`; expect mode/dispatch assertions to fail.
- [ ] **Step 3: Implement** only the required mode split, queue selector, warning binding and secure deletion routing. Keep the current popup structure, theme, and existing Legacy behavior; do not add a history page or Local-specific Host UI.
- [ ] **Step 4: Run GREEN** for the two QML tests, `quemusic_original_ui_actions_qml_test`, and `quemusic_plugin_startup_test`.
- [ ] **Step 5: Commit**: `feat: restore queue without replacing legacy playback context`.

### Task 8: End-to-end acceptance and Phase 4 record

**Files:** Create `tests/tst_QueueHistoryIntegration.cpp`, `docs/architecture/phase-4-queue-history-identity-record.md`; modify `CMakeLists.txt` and `docs/architecture/current-state-gap-analysis.md` only for registration/verified progress.

**Interfaces:** Use real Local package, Navidrome fixture, SourceRegistry, Coordinator/fake sink, and temporary Host/plugin stores. The test is the phase exit gate, not an alternate production path.

- [ ] **Step 1: Write failing integration tests** `localAndNavidromeSurviveRestartWithoutPaths`, `missingSourceThenReturnPreservesIdentity`, and `staleLocalCacheCannotEnterRestoredQueue`; inspect the saved Host JSON for absence of stream URL, `file://`, token and canonical path.
- [ ] **Step 2: Run RED:** build/run `quemusic_queue_history_integration_test`; expect a missing target or failure before final wiring is complete.
- [ ] **Step 3: Finish only integration fixes**, then record evidence and remaining Phase 5/6 limits in the acceptance document; update the Gap Analysis's stale Phase 3 count with actual observed results.
- [ ] **Step 4: Run GREEN:** build all; run `QT_QPA_PLATFORM=offscreen ctest --test-dir build-phase3 --output-on-failure -j4`; run `git diff 56b968f -- sdk/source/v2` and `git diff --check`. Record exact test count, skipped platforms and any failure honestly.
- [ ] **Step 5: Commit** tests/docs/final integration fixes: `test: accept phase 4 queue history identity`.

## Plan self-review and handoff

The eight tasks cover spec §§2.1–5 in dependency order: Local identity and cache safety precede Host persistence; typed codec precedes Coordinator restoration; store precedes UI; integration checks the phase exit. Do not mark Phase 5/6 complete. After Task 8, obtain a fresh whole-branch review, fix validated findings, rerun the full suite, then ask the user how to integrate the branch. No push, PR or merge is implied by this plan.
