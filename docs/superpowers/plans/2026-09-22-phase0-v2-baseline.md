# Phase 0 V2 Baseline Integration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Establish a clean development branch that preserves the completed Source SDK v2 work, carries the approved architecture documents, and records a reproducible build/test baseline without changing production behavior.

**Architecture:** Start an isolated branch at the already-tested `codex/unified-media-bridge` commit `b94e181`, then add the approved design baseline and audit documents as a separate documentation commit. Verify provenance, legacy boundaries, and a clean configure/build/test run; record the evidence in a Phase 0 baseline document. No Source SDK, plugin, playback, settings, or QML production code is changed in this phase.

**Tech Stack:** Git worktrees, CMake 4.x, Qt 6.11.1, CTest, Markdown, zsh

**Spec:** `docs/architecture/QueMusic-Plugin-Architecture-Final-Plan.md`

## Global Constraints

- Preserve the current checkout's `ThirdParty/qwindowkit` modification and `.ui-screenshots/` directory unchanged.
- Preserve the existing `codex/unified-media-bridge` worktree's uncommitted CMake, logging, storage-path, and test changes unchanged.
- Do not reimplement or redesign Source SDK v2.
- Do not change production behavior in Phase 0.
- Use `codex/plugin-architecture-phase0` in `/Users/liqiang/.codex/worktrees/plugin-architecture-phase0/QueMusic` as the isolated implementation branch.
- Treat `QueMusic.PluginUI 1.0`, Local Source Plugin, queue migration, and Legacy removal as later phases.
- A clean configure, build, and complete CTest run are required before Phase 0 is complete.

## Review Focus

- A dirty source worktree must not contaminate the isolated branch; expected behavior is that only committed `b94e181` content plus explicit Phase 0 documentation appears.
- The architecture documents must be present on the isolated branch; expected behavior is that downstream phases can resolve the spec path without depending on the original checkout.
- The clean build must discover the same intended production targets and tests without relying on stale generated files.
- Source SDK v1 or `SourceManager` must not return to the production target; test fixtures may mention compatibility only when explicitly isolated.
- The recorded test count must come from the fresh build and must not copy the historical count from the design document.

---

### Task 1: Commit the Approved Architecture Documents

**Files:**
- Create: `docs/architecture/QueMusic-Plugin-Architecture-Final-Plan.md`
- Create: `docs/architecture/current-state-gap-analysis.md`
- Create: `docs/superpowers/plans/2026-09-22-phase0-v2-baseline.md`

**Interfaces:**
- Consumes: approved final architecture document and Gap Analysis in the current checkout
- Produces: one documentation commit that later tasks cherry-pick onto `codex/plugin-architecture-phase0`

- [ ] **Step 1: Verify that only the intended documentation paths are staged**

Run:

```bash
git add docs/architecture/QueMusic-Plugin-Architecture-Final-Plan.md \
  docs/architecture/current-state-gap-analysis.md \
  docs/superpowers/plans/2026-09-22-phase0-v2-baseline.md
git diff --cached --name-status
```

Expected: exactly three added Markdown files; no `ThirdParty/qwindowkit` or `.ui-screenshots/` entry.

- [ ] **Step 2: Validate the staged documentation**

Run:

```bash
git diff --cached --check -- \
  docs/architecture/current-state-gap-analysis.md \
  docs/superpowers/plans/2026-09-22-phase0-v2-baseline.md
cmp -s \
  <(perl -0777 -pe 's/\n+\z/\n/' /Users/liqiang/Downloads/QueMusic-Plugin-Architecture-Final-Plan.md) \
  <(perl -0777 -pe 's/\n+\z/\n/' docs/architecture/QueMusic-Plugin-Architecture-Final-Plan.md)
```

Expected: both commands exit 0; the authored documents have no whitespace errors, and the imported final plan matches its source after normalizing trailing blank lines.

- [ ] **Step 3: Commit the documentation**

Run:

```bash
git commit -m "docs: establish plugin architecture baseline"
```

Expected: one commit containing only the three documentation files.

- [ ] **Step 4: Cherry-pick the documentation commit into the isolated branch**

Run from `/Users/liqiang/.codex/worktrees/plugin-architecture-phase0/QueMusic`:

```bash
git cherry-pick codex/issue-12-local-lyrics
```

Expected: the branch advances from `b94e181`; the three documents are present; the worktree remains clean.

### Task 2: Record Provenance and Dirty-Worktree Isolation

**Files:**
- Create: `docs/architecture/phase-0-v2-baseline-record.md`

**Interfaces:**
- Consumes: the isolated branch from Task 1 and the two pre-existing worktrees' read-only status
- Produces: auditable commit/branch provenance and an explicit exclusion list for uncommitted changes

- [ ] **Step 1: Capture commit topology and worktree status**

Run read-only commands:

```bash
git rev-parse HEAD
git log --oneline --decorate -5
git worktree list --porcelain
git -C /Users/liqiang/Documents/ChatGPT/QueMusic status --short
git -C /Users/liqiang/Documents/ChatGPT/QueMusic/.worktrees/codex-unified-media-bridge status --short
```

Expected: the isolated branch contains `b94e181` plus the documentation commit; the original dirty paths are visible only in their original worktrees.

- [ ] **Step 2: Write the baseline record**

Create `docs/architecture/phase-0-v2-baseline-record.md` with:

- base commit and documentation commit IDs;
- branch/worktree path;
- excluded uncommitted paths from both original worktrees;
- toolchain values from the fresh CMake configure;
- production v2 boundary checks;
- build and CTest results from Tasks 3 and 4.

Expected: all statements are backed by commands run in this plan; unknown values remain absent until measured rather than guessed.

### Task 3: Perform a Fresh Configure and Build

**Files:**
- Create generated directory: `build-phase0-clean/` (not committed)
- Modify: `docs/architecture/phase-0-v2-baseline-record.md`

**Interfaces:**
- Consumes: clean isolated source tree from Task 1
- Produces: reproducible build artifacts and measured toolchain/configuration evidence

- [ ] **Step 0: Initialize pinned Git submodules in the isolated worktree**

Run:

```bash
git submodule update --init --recursive
git submodule status --recursive
```

Expected: every required submodule is checked out at the commit pinned by the superproject; no status line begins with `-` or `+`.

- [ ] **Step 1: Configure from an empty build directory**

Run:

```bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/cmake \
  --fresh \
  -S . \
  -B build-phase0-clean \
  -G "Unix Makefiles" \
  -DCMAKE_BUILD_TYPE=Debug \
  -DBUILD_TESTING=ON \
  -DCMAKE_PREFIX_PATH=/Users/liqiang/Qt/6.11.1/macos
```

Expected: configure and generate complete with exit 0; build files are written only under `build-phase0-clean/`.

- [ ] **Step 2: Build all configured targets**

Run:

```bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/cmake \
  --build build-phase0-clean \
  --parallel 4
```

Expected: exit 0 and the QueMusic application, Navidrome plugin, SDK libraries, fixtures, and test executables are built.

- [ ] **Step 3: Record measured toolchain values**

Read `build-phase0-clean/CMakeCache.txt` for generator, build type, Qt path, C++ compiler, architecture, and build-key-relevant values. Add only measured values to the baseline record.

Expected: the record identifies the environment that produced the baseline.

### Task 4: Verify Tests and Architectural Boundaries

**Files:**
- Modify: `docs/architecture/phase-0-v2-baseline-record.md`

**Interfaces:**
- Consumes: fresh build from Task 3
- Produces: Phase 0 acceptance evidence for later Plugin UI and Local Source work

- [ ] **Step 1: Run the complete fresh-build test suite**

Run:

```bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/ctest \
  --test-dir build-phase0-clean \
  --output-on-failure
```

Expected: 100% pass; record the measured test count and duration.

- [ ] **Step 2: Confirm the v2 production boundary**

Run:

```bash
rg -n "sdk/source/(IMusicSourcePlugin|IMusicSourceSession|SourceTypes)|SourceManager" \
  CMakeLists.txt main.cpp core sdk plugins components layout pages
```

Expected: no production target dependency on Source SDK v1 or `SourceManager`; any hit must be classified and explained before completion.

- [ ] **Step 3: Confirm required v2 and transitional components**

Run:

```bash
for path in \
  sdk/source/v2/IMusicSourcePluginV2.h \
  core/source/SourceRegistry.h \
  core/music/CapabilityResolver.h \
  core/music/MediaActionRouter.h \
  core/music/PlaybackCoordinator.h \
  core/music/OriginalUiMusicAdapter.h \
  core/settings/PluginSettingsController.h \
  plugins/navidrome-source/NavidromeSourcePlugin.h; do
  test -f "$path" || exit 1
done
```

Expected: exit 0.

- [ ] **Step 4: Record known transitional conflicts without changing them**

Run targeted searches for `MusicApi`, `NeteaseCloudApi`, `KugouApi`, `FolderModel`, `source == -1`, and direct `MediaPlayer.source` assignments. Summarize the paths in the baseline record.

Expected: the record confirms these are deferred to Phases 3–10; no production code is changed in Phase 0.

- [ ] **Step 5: Commit the Phase 0 record**

Run:

```bash
git add docs/architecture/phase-0-v2-baseline-record.md
git commit -m "docs: record verified v2 integration baseline"
```

Expected: one documentation-only commit; generated build artifacts remain untracked/ignored.

- [ ] **Step 6: Run the final completion command**

Run the complete CTest command again through the execution ledger's `task-done` gate.

Expected: 100% pass with no test failures, followed by a clean tracked worktree and only ignored build output.
