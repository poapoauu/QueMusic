# Source SDK v1 Optional Artwork Final-Fix Implementation Plan

> **For agentic workers:** Execute this final-review wave inline in the named
> worktree. Do not dispatch subagents or reviewers. Steps use checkbox
> (`- [ ]`) syntax for tracking.

**Goal:** Preserve the already-published `IMusicSourceSession` v1 ABI while
adding `IMusicSourceArtworkSession` as an independently versioned capability
discovery interface and enforcing a safe metadata/runtime policy.

**Architecture:** `MusicSourcePlugin/1.0` retains the exact base virtual order
`search → browse → resolveStream → fetchArtwork → fetchLyrics → cancel`.
Artwork metadata gates all host dispatch. The host prefers the additive
optional interface for new plugins and falls back to the retained base slot for
legacy v1 plugins that advertise Artwork but predate the optional IID.

**Tech Stack:** Qt 6, C++17, Qt plugin metadata/IIDs, QtTest, CMake,
`QPluginLoader`, `Q_OBJECT`, and `Q_INTERFACES`.

**Spec:** `docs/superpowers/specs/2026-08-27-nas-plugin-architecture-design.md`

## Global Constraints

- Keep `org.quemusic.MusicSourcePlugin/1.0` and its complete published vtable
  unchanged. Removing or reordering any v1 virtual requires a new major IID.
- Keep `fetchArtwork(const TrackRef &)` between `resolveStream` and
  `fetchLyrics` in `IMusicSourceSession`.
- Keep `IMusicSourceArtworkSession` additive with IID
  `org.quemusic.MusicSourceArtworkSession/1.0`.
- Without `SourceCapability::Artwork`, the host must not call either artwork
  interface, even if the optional interface is present.
- With Artwork metadata, prefer the optional interface; if it is absent, call
  the retained v1 base method.
- Request methods return request IDs and complete asynchronously through
  `requestSucceeded` or `requestFailed` unless cancelled.
- Preserve Qt 6/C++17, SDK independence from QML/provider code, and Issue #12
  branch independence.

## File Map

- Modify: `sdk/source/IMusicSourceSession.h` — restore and label the immutable
  v1 artwork slot.
- Modify: `sdk/source/IMusicSourceArtworkSession.h` and
  `sdk/source/IMusicSourcePlugin.h` — document the additive relationship.
- Modify: `core/source/SourceManager.*` — add metadata-gated artwork dispatch
  with optional-interface preference and v1 fallback.
- Create: `tests/fixtures/frozen-v1-sdk/*` — frozen copies of the published v1
  declarations, independent from HEAD SDK headers.
- Create: `tests/fixtures/FrozenV1SourcePlugin.cpp` — real MODULE plugin built
  only against the frozen v1 declarations.
- Create: `tests/tst_SourceV1Abi.cpp` — load the frozen plugin through
  `SourceManager` and exercise `fetchLyrics` and `cancel` through the new host.
- Modify: `tests/fixtures/FakeSourcePlugin.cpp` and `tests/tst_SourceManager.cpp`
  — cover capability metadata/runtime mismatches and fallback behavior.
- Modify: `CMakeLists.txt` — build the independent frozen SDK/MODULE, dedicated
  ABI test, and optional-without-metadata fixture.
- Modify: `docs/PLUGIN_API.md`, the architecture spec, and this plan — state
  the retained-slot ruling and async skeleton contract without contradiction.
- Refresh: `.superpowers/sdd/2026-08-28-source-sdk-v1-optional-artwork/verification-report.md`.
- Create: `.superpowers/sdd/2026-08-28-source-sdk-v1-artwork/final-fix-report.md`.

### Task 1: Prove and repair the v1 ABI regression

**Files:**

- Create: `tests/fixtures/frozen-v1-sdk/SourceTypes.h`
- Create: `tests/fixtures/frozen-v1-sdk/SourcePluginContext.h`
- Create: `tests/fixtures/frozen-v1-sdk/IMusicSourceSession.h`
- Create: `tests/fixtures/frozen-v1-sdk/IMusicSourcePlugin.h`
- Create: `tests/fixtures/frozen-v1-sdk/FrozenV1Sdk.cpp`
- Create: `tests/fixtures/FrozenV1SourcePlugin.cpp`
- Create: `tests/tst_SourceV1Abi.cpp`
- Modify: `sdk/source/IMusicSourceSession.h`
- Modify: `tests/fixtures/FakeSourcePlugin.cpp`
- Modify: `tests/tst_SourcePluginContract.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**

- Consumes: the exact pre-change `MusicSourcePlugin/1.0` declarations from
  main commit `8ad8e59`.
- Produces: a separately compiled MODULE with no `quemusic_source_sdk` link and
  an immutable host-side regression for the trailing vtable slots.

- [x] **Step 1: Add the frozen declarations, MODULE fixture, and failing test**

The frozen session declares this exact sequence:

```cpp
virtual QUuid search(const SearchQuery &query) = 0;
virtual QUuid browse(const BrowseQuery &query) = 0;
virtual QUuid resolveStream(const TrackRef &track) = 0;
virtual QUuid fetchArtwork(const TrackRef &track) = 0;
virtual QUuid fetchLyrics(const TrackRef &track) = 0;
virtual void cancel(const QUuid &requestId) = 0;
```

Load the MODULE with `SourceManager`. Call `fetchLyrics` and assert the
operation name/payload, then issue a second lyrics request, call `cancel`, and
assert no success signal arrives.

- [x] **Step 2: Verify RED before changing the host SDK**

Run the focused manager test against the slot-removed host. Expected and
observed failure: the host's `fetchLyrics` call dispatches the frozen plugin's
`fetchArtwork` implementation, yielding `"fetchArtwork"` instead of
`"fetchLyrics"`.

- [x] **Step 3: Restore the legacy slot and concrete overrides**

Restore only `fetchArtwork(const TrackRef &)` at its original base position.
Do not change the plugin IID or any other signature/order. Add required legacy
overrides to current concrete base-session test fixtures.

- [x] **Step 4: Verify GREEN through the dedicated ABI target**

```bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/cmake --build \
  /private/var/folders/9_/7tqxtfdd1sz8h0f4_qcz9nd00000gn/T/quemusic-source-sdk-v1-artwork-final \
  --target quemusic_source_v1_abi_test
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/ctest --test-dir \
  /private/var/folders/9_/7tqxtfdd1sz8h0f4_qcz9nd00000gn/T/quemusic-source-sdk-v1-artwork-final \
  -R '^quemusic_source_v1_abi_test$' --output-on-failure
```

Expected: the frozen cross-DSO lyrics dispatch and cancellation both pass.

### Task 2: Make Artwork metadata/interface policy executable

**Files:**

- Modify: `core/source/SourceManager.h`
- Modify: `core/source/SourceManager.cpp`
- Modify: `tests/fixtures/FakeSourcePlugin.cpp`
- Modify: `tests/tst_SourceManager.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**

- Produces:
  `QUuid SourceManager::requestArtwork(const QString &, IMusicSourceSession *,
  const TrackRef &) const`.

- [x] **Step 1: Write the policy tests before the helper**

Cover all three host branches:

1. Artwork metadata plus optional IID uses the optional interface.
2. Artwork metadata without the optional IID uses the retained base slot.
3. Optional IID without Artwork metadata is not invoked and returns a null
   request ID without terminal signals.

The second case uses the frozen v1 MODULE. The third uses a current fixture
that intentionally exposes `IMusicSourceArtworkSession` while advertising only
Search. No test or implementation may branch on a provider name.

- [x] **Step 2: Verify RED**

Build the manager test before adding the helper. Expected and observed failure:
compilation reports that `SourceManager` has no `requestArtwork` member.

- [x] **Step 3: Implement the narrow dispatch helper**

Look up the registered descriptor by source ID. Return a null UUID for a null
session, unknown source, or missing Artwork capability. For advertised Artwork,
call the optional interface when `qobject_cast` succeeds; otherwise call the
retained `IMusicSourceSession::fetchArtwork` method.

- [x] **Step 4: Verify the manager and ABI tests**

```bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/ctest --test-dir \
  /private/var/folders/9_/7tqxtfdd1sz8h0f4_qcz9nd00000gn/T/quemusic-source-sdk-v1-artwork-final \
  -R 'quemusic_(source_manager|source_v1_abi)_test' \
  --output-on-failure
```

Expected: both targets pass.

### Task 3: Correct public documentation and examples

**Files:**

- Modify: `docs/PLUGIN_API.md`
- Modify: `docs/superpowers/specs/2026-08-27-nas-plugin-architecture-design.md`
- Modify: `docs/superpowers/plans/2026-08-28-source-sdk-v1-optional-artwork.md`
- Modify: `sdk/source/IMusicSourceSession.h`
- Modify: `sdk/source/IMusicSourceArtworkSession.h`
- Modify: `sdk/source/IMusicSourcePlugin.h`

- [x] **Step 1: State the binding ABI ruling without euphemism**

Document the exact v1 virtual order, the retained legacy artwork slot, and that
removing the slot under IID 1.0 is a binary break. Describe the optional
interface as additive and independently versioned.

- [x] **Step 2: Document the metadata/runtime policy**

State metadata-first gating, optional-interface preference, legacy v1 fallback,
and the expectation that newly written Artwork sources pair metadata with
`Q_INTERFACES(IMusicSourceArtworkSession)`.

- [x] **Step 3: Replace the misleading implementation skeleton**

Use declarations only. Explicitly say bodies are omitted and every real request
must asynchronously emit one matching success/failure signal unless cancelled;
returning a UUID alone violates the contract. Keep dual inheritance and
`Q_INTERFACES` in the declaration.

### Task 4: Final verification, reporting, and commit

**Files:**

- Refresh: `.superpowers/sdd/2026-08-28-source-sdk-v1-optional-artwork/verification-report.md`
- Create: `.superpowers/sdd/2026-08-28-source-sdk-v1-artwork/final-fix-report.md`

- [ ] **Step 1: Configure and build a fresh focused harness**

Because the root configure is independently blocked by missing qwindowkit and
Crypto++ inputs, use the repository's focused harness mirror and record its
exact source/build paths. Build all six focused executables, including
`quemusic_source_v1_abi_test`.

- [ ] **Step 2: Run all focused tests**

```bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/ctest \
  --test-dir /private/var/folders/9_/7tqxtfdd1sz8h0f4_qcz9nd00000gn/T/quemusic-source-sdk-v1-artwork-final \
  --output-on-failure
```

Expected: six tests, zero failures.

- [ ] **Step 3: Run final diff and compatibility checks**

```bash
git diff --check
git status --short
git diff --stat main...HEAD
git diff main...HEAD -- sdk/source/IMusicSourceSession.h
```

The session-header diff must not remove or reorder any v1 virtual.

- [ ] **Step 4: Refresh both reports accurately**

Record changed files, design choices, RED/GREEN evidence, exact commands and
outputs, six-test results, and remaining limitations. Label branch statistics
as captured before the report/commit when appropriate. Describe Ninja only as
a previously reported issue not reproduced in this run.

- [ ] **Step 5: Commit the complete final-fix wave**

Stage the ignored report explicitly, inspect the staged diff, run a final
covering test pass on the staged tree, and create one final-fix commit.

## Execution Notes

- Work only in
  `/Users/liqiang/Documents/ChatGPT/QueMusic/.worktrees/codex-source-sdk-v1-artwork`.
- Keep the parent Issue #12 worktree untouched.
- The cosmetic Task-1 report-round naming remains deferred.
- Do not claim a full-application build while root configuration is blocked by
  unrelated missing third-party sources.
