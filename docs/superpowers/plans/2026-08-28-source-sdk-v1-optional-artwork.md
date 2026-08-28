# Source SDK v1 Optional Artwork Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Preserve the `IMusicSourceSession` v1 ABI while moving artwork fetching into the independently versioned optional `IMusicSourceArtworkSession` capability interface.

**Architecture:** Keep `IMusicSourceSession` as the stable asynchronous base contract for search, browse, stream resolution, lyrics, and cancellation. Add a non-`QObject` optional interface with its own Qt IID; source sessions that advertise `Artwork` implement it alongside the base session, and the host discovers it with `qobject_cast` only when needed. Existing base-only sessions remain valid and continue to rely on advertised capabilities.

**Tech Stack:** Qt 6, C++17, Qt plugin metadata/IIDs, QtTest, CMake, `QPluginLoader`, `Q_OBJECT`/`Q_INTERFACES`.

**Spec:** `docs/superpowers/specs/2026-08-27-nas-plugin-architecture-design.md`

## Global Constraints

- The base `IMusicSourceSession` contract and `org.quemusic.MusicSourcePlugin/1.0` IID are immutable after publication.
- Artwork uses `IMusicSourceArtworkSession` with IID `org.quemusic.MusicSourceArtworkSession/1.0`.
- The host discovers the optional artwork interface with `qobject_cast` only when the source advertises `SourceCapability::Artwork`.
- Request results use request IDs and asynchronous signals; plugins must not block the GUI thread.
- Keep QueMusic's current Qt baseline and asynchronous style; do not make QCoro/C++20 a prerequisite yet.
- SDK types remain independent of QML and provider-specific classes.
- The existing Issue #12 branch remains independent from this architecture branch.

## File Map

- Create: `sdk/source/IMusicSourceArtworkSession.h` — optional artwork capability contract and IID.
- Modify: `sdk/source/IMusicSourceSession.h` — remove only the artwork pure virtual; leave the remaining base methods and signals unchanged.
- Modify: `sdk/source/IMusicSourcePlugin.h` — document the optional capability relationship without changing the base IID.
- Modify: `CMakeLists.txt` — include the new public header in the source SDK target.
- Modify: `plugins/test-source/TestSourceSession.h` — implement the optional artwork interface explicitly.
- Modify: `plugins/test-source/TestSourceSession.cpp` — retain the asynchronous artwork fixture behavior.
- Modify: `tests/fixtures/FakeSourcePlugin.cpp` — keep the loader fixture base-only to represent an older-style session.
- Modify: `tests/tst_SourcePluginContract.cpp` — cover optional-interface discovery and base-only compatibility.
- Modify: `tests/tst_SourceManager.cpp` — obtain Artwork through the optional interface.
- Modify: `tests/tst_SourceTypes.cpp` — retain capability serialization coverage and lock the Artwork bit behavior.
- Create: `.superpowers/sdd/2026-08-28-source-sdk-v1-artwork/verification-report.md` — exact focused-harness evidence.

### Task 1: Add the independently versioned artwork interface

**Files:**
- Create: `sdk/source/IMusicSourceArtworkSession.h`
- Modify: `sdk/source/IMusicSourceSession.h`
- Modify: `sdk/source/IMusicSourcePlugin.h`
- Modify: `CMakeLists.txt`
- Modify: `tests/tst_SourcePluginContract.cpp`

**Interfaces:**
- Consumes: existing `TrackRef`, `QUuid`, and `IMusicSourceSession` request signals.
- Produces: `IMusicSourceArtworkSession`, `QUEMUSIC_MUSIC_SOURCE_ARTWORK_SESSION_IID`, and an unchanged base-session vtable.

- [ ] **Step 1: Write the failing contract test**

Add a base-only fake session and an artwork-capable fake session to
`tests/tst_SourcePluginContract.cpp`. The artwork-capable class inherits from
both interfaces, declares `Q_INTERFACES(IMusicSourceArtworkSession)`, and
implements `QUuid fetchArtwork(const TrackRef &track) override`. Add a test
that casts the base-only object to `IMusicSourceArtworkSession` and expects
`nullptr`, casts the artwork-capable object and expects non-null, then waits
for its asynchronous `requestSucceeded` signal and checks the returned request
ID. Remove `fetchArtwork()` from the base-only fake session.

- [ ] **Step 2: Run the test to verify it fails**

```bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/cmake --build /private/tmp/quemusic-final-fix-verify-2 --target quemusic_source_plugin_contract_test
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/ctest --test-dir /private/tmp/quemusic-final-fix-verify-2 -R quemusic_source_plugin_contract_test --output-on-failure
```

Expected: compilation fails because the optional header and interface do not
exist, and the current base session still owns `fetchArtwork()`.

- [ ] **Step 3: Implement the minimal SDK change**

Create `sdk/source/IMusicSourceArtworkSession.h` with this exact public shape:

```cpp
#pragma once

#include "SourceTypes.h"

#include <QtPlugin>
#include <QUuid>

class IMusicSourceArtworkSession {
public:
    virtual ~IMusicSourceArtworkSession() = default;
    virtual QUuid fetchArtwork(const TrackRef &track) = 0;
};

#define QUEMUSIC_MUSIC_SOURCE_ARTWORK_SESSION_IID \
    "org.quemusic.MusicSourceArtworkSession/1.0"

Q_DECLARE_INTERFACE(IMusicSourceArtworkSession,
                    QUEMUSIC_MUSIC_SOURCE_ARTWORK_SESSION_IID)
```

Delete only `fetchArtwork` from `IMusicSourceSession`. Keep the order and
signatures of `search`, `browse`, `resolveStream`, `fetchLyrics`, and `cancel`
unchanged, and do not alter its signals. Add the header to the
`quemusic_source_sdk` source list. Add a short comment to
`IMusicSourcePlugin.h`; do not change `QUEMUSIC_MUSIC_SOURCE_PLUGIN_IID`.

- [ ] **Step 4: Run the focused contract test to verify it passes**

```bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/cmake --build /private/tmp/quemusic-final-fix-verify-2 --target quemusic_source_plugin_contract_test
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/ctest --test-dir /private/tmp/quemusic-final-fix-verify-2 -R quemusic_source_plugin_contract_test --output-on-failure
```

Expected: the base-only cast is null and the optional artwork request passes.

- [ ] **Step 5: Commit the SDK contract change**

```bash
git add sdk/source/IMusicSourceArtworkSession.h sdk/source/IMusicSourceSession.h sdk/source/IMusicSourcePlugin.h CMakeLists.txt tests/tst_SourcePluginContract.cpp
git commit -m "fix: version source artwork capability separately"
```

### Task 2: Adapt the built-in test source and loader fixtures

**Files:**
- Modify: `plugins/test-source/TestSourceSession.h`
- Modify: `plugins/test-source/TestSourceSession.cpp`
- Modify: `tests/fixtures/FakeSourcePlugin.cpp`
- Modify: `tests/tst_SourceManager.cpp`

**Interfaces:**
- Consumes: `IMusicSourceArtworkSession` and `SourceCapability::Artwork`.
- Produces: a source whose Artwork metadata matches its optional interface,
  plus a loadable base-only fixture with no Artwork capability.

- [ ] **Step 1: Write the failing manager assertions**

Change `fakeSourceCompletesArtworkFetch()` to call:

```cpp
auto *artworkSession = qobject_cast<IMusicSourceArtworkSession *>(session);
QVERIFY(artworkSession != nullptr);
const QUuid requestId = artworkSession->fetchArtwork(track);
```

Add a manager test for the base-only fixture that creates its session and
asserts `qobject_cast<IMusicSourceArtworkSession *>(session) == nullptr`.
Keep that fixture's descriptor limited to `SourceCapability::Search`.

- [ ] **Step 2: Run the manager test to verify it fails**

```bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/cmake --build /private/tmp/quemusic-final-fix-verify-2 --target quemusic_source_manager_test
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/ctest --test-dir /private/tmp/quemusic-final-fix-verify-2 -R quemusic_source_manager_test --output-on-failure
```

Expected: the test does not compile until the built-in source implements the
new optional interface and the manager test includes its header.

- [ ] **Step 3: Implement the fixture migration**

Include the optional header in `TestSourceSession.h`, inherit from both
interfaces, and add `Q_INTERFACES(IMusicSourceArtworkSession)`. Keep the
existing `fetchArtwork()` implementation and asynchronous JSON payload in
`TestSourceSession.cpp`. Remove the obsolete `fetchArtwork()` override from
`tests/fixtures/FakeSourcePlugin.cpp`; do not add Artwork to that fixture.

- [ ] **Step 4: Run both source tests**

```bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/ctest --test-dir /private/tmp/quemusic-final-fix-verify-2 -R 'quemusic_(source_manager|source_plugin_contract)_test' --output-on-failure
```

Expected: both tests pass; the built-in source exposes Artwork and the
base-only fixture remains valid.

- [ ] **Step 5: Commit the fixture migration**

```bash
git add plugins/test-source/TestSourceSession.h plugins/test-source/TestSourceSession.cpp tests/fixtures/FakeSourcePlugin.cpp tests/tst_SourceManager.cpp
git commit -m "test: migrate artwork fixture to optional source capability"
```

### Task 3: Lock the ABI and metadata regression cases

**Files:**
- Modify: `tests/tst_SourcePluginContract.cpp`
- Modify: `tests/tst_SourceManager.cpp`
- Modify: `tests/tst_SourceTypes.cpp`

**Interfaces:**
- Consumes: stable base IID, optional Artwork IID, source capability metadata,
  and loader failure isolation.
- Produces: regression coverage showing optional capability absence through
  interface discovery rather than provider-name branching.

- [ ] **Step 1: Add exact IID and capability assertions**

Add these QtTest assertions:

```cpp
QCOMPARE(QString::fromLatin1(QUEMUSIC_MUSIC_SOURCE_PLUGIN_IID),
         QStringLiteral("org.quemusic.MusicSourcePlugin/1.0"));
QCOMPARE(QString::fromLatin1(QUEMUSIC_MUSIC_SOURCE_ARTWORK_SESSION_IID),
         QStringLiteral("org.quemusic.MusicSourceArtworkSession/1.0"));
```

Verify that the Artwork-advertising test source casts successfully and the
base-only fixture does not. Keep the existing descriptor JSON round-trip and
all capability-bit coverage.

- [ ] **Step 2: Run the regression suite**

```bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/ctest --test-dir /private/tmp/quemusic-final-fix-verify-2 -R 'quemusic_(source_types|source_manager|source_plugin_contract)_test' --output-on-failure
```

Expected: all selected tests pass after the implementation from Tasks 1–2.

- [ ] **Step 3: Commit the regression coverage**

```bash
git add tests/tst_SourcePluginContract.cpp tests/tst_SourceManager.cpp tests/tst_SourceTypes.cpp
git commit -m "test: lock source capability ABI versions"
```

### Task 4: Verify the focused harness and record the handoff

**Files:**
- Create: `.superpowers/sdd/2026-08-28-source-sdk-v1-artwork/verification-report.md`

**Interfaces:**
- Consumes: all SDK, fixture, manager, and test changes from Tasks 1–3.
- Produces: reproducible verification evidence and a clean reviewable branch.

- [ ] **Step 1: Check the final diff**

Run:

```bash
git diff --check
git status --short
git diff --stat main...HEAD
```

Expected: no whitespace errors and only the optional-artwork SDK, fixture,
test, and verification files are changed.

- [ ] **Step 2: Build and run all focused tests**

Use a fresh out-of-tree `BUILD_TESTING=ON` configuration if the existing
focused build is stale, then run:

```bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/cmake --build /private/tmp/quemusic-final-fix-verify-2
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/ctest --test-dir /private/tmp/quemusic-final-fix-verify-2 --output-on-failure
```

Expected: the five focused tests pass with 0 failures. Do not claim the full
application build is green while the known qwindowkit/Crypto++/Ninja blockers
remain.

- [ ] **Step 3: Write and commit the verification report**

Record the exact build directory, commands, test count, pass/fail result, and
any pre-existing full-application blockers in
`.superpowers/sdd/2026-08-28-source-sdk-v1-artwork/verification-report.md`.

```bash
git diff --check
git add .superpowers/sdd/2026-08-28-source-sdk-v1-artwork/verification-report.md
git commit -m "docs: record optional artwork ABI verification"
```

## Execution Notes

- Run commands from `/Users/liqiang/Documents/ChatGPT/QueMusic/.worktrees/codex-source-sdk-v1-artwork`.
- Keep the parent worktree on `codex/issue-12-local-lyrics` untouched.
- After implementation, use `superpowers:verification-before-completion` before claiming completion and `superpowers:requesting-code-review` before asking to merge or publish the branch.
