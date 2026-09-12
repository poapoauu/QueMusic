# Task 14 Report

## Result

Implementation complete; completion gate pending. The implementation removes
the active v1 source SDK files, fixtures, targets, and upper-layer entry points;
manifest and plugin loading reject v1; the Navidrome smoke path uses v2 providers;
and the plugin API and smoke runbook describe the v2-only contract.

## Verification

- `quemusic_plugin_manifest_test`: passed.
- `quemusic_navidrome_smoke`: built successfully without running or accessing a server.
- `quemusic_navidrome_smoke_state_test`: passed; covers ordered playlist/favorite
  compensation, original favorite restoration, and bounded cleanup failure.
- `quemusic_playback_plugin_contract_test`: passed with `expiresAt` UTC,
  future-expiry, and exact round-trip assertions.
- Main controller verification: `quemusic_navidrome_source_test` passed 1/1 in 15.24s.
- Default build, including `QueMusic` and test targets: passed.
- Task14-relevant CTest run excluding Navidrome listener tests and unrelated user-dirty
  storage/log/macOS-runtime tests: 28/28 passed.
- Active production/test v1 reference scan: no matches for the Task14 removal patterns.
- `git diff --check`: passed.

## Preserved user work

The commit excludes the pre-existing dirty changes in `cpp/AccountManager.cpp`,
`cpp/LogManager.cpp`, `cpp/LogManager.h`, `cmake/QueMusicInfo.plist.in`,
`cmake/VerifyMacOSLocalNetworkUsage.cmake`,
`cmake/VerifyMacOSRuntimeLibraries.cmake`, `cpp/AppStoragePaths.cpp`,
`cpp/AppStoragePaths.h`, `tests/tst_AppStoragePaths.cpp`,
`tests/tst_LogManager.cpp`, and their corresponding `CMakeLists.txt` hunks.

The nested `index -> artist` parsing fix in
`plugins/navidrome-source/NavidromeSourceSession.cpp` and its coverage in
`tests/tst_NavidromeSource.cpp` remain preserved while Task14 removes the v1
surface from those same files. The equivalent user patch formerly present in
`tests/tst_MediaBridge.cpp` cannot remain as a file-level change because Task14
deletes that obsolete v1 test file; this is explicitly recorded here rather
than silently dropping the provenance.

## Concerns and deferred checks

- The real remote Navidrome smoke was not run; it requires explicit credentials
  and mutates a designated test account under the runbook safeguards.
- GUI and end-to-end playback smoke checks remain pending.
- `codesign --verify --deep --strict` reports that the locally built app is not
  signed. Signing verification has therefore not passed and remains a
  controller/release-environment responsibility.
- A broad CTest attempt reached the unrelated user-dirty `quemusic_log_manager_test`,
  which failed because its expected log file was absent. That test and its
  implementation/CMake hunks are intentionally outside Task14 and excluded from
  this commit.

Commit: this Task14 commit (`refactor: complete source sdk v2 migration`).
