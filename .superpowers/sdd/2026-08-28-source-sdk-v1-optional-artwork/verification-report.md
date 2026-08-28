# Optional Artwork ABI Verification Report

- Date: 2026-08-28
- Worktree: `/Users/liqiang/Documents/ChatGPT/QueMusic/.worktrees/codex-source-sdk-v1-artwork`
- Focused harness source: `/tmp/quemusic-task6-harness`
- Focused harness build directory: `/private/var/folders/9_/7tqxtfdd1sz8h0f4_qcz9nd00000gn/T/quemusic-final-fix-verify-M4c1kI`
- Test count: 5
- Result: PASS

## Final diff checks

Command:

```bash
git diff --check
```

Output:

```text
(no output)
```

Command:

```bash
git status --short
```

Output:

```text
(no output before writing this report)
```

Command:

```bash
git diff --stat main...HEAD
```

Output:

```text
.../task-1-report.md                               | 881 +++++++++++++++++++++
CMakeLists.txt                                     |   1 +
docs/PLUGIN_API.md                                 |  26 +-
.../2026-08-28-source-sdk-v1-optional-artwork.md   | 274 +++++++
.../2026-08-27-nas-plugin-architecture-design.md   |  31 +-
plugins/test-source/TestSourceSession.h            |   5 +-
sdk/source/IMusicSourceArtworkSession.h            |  18 +
sdk/source/IMusicSourcePlugin.h                    |   2 +
sdk/source/IMusicSourceSession.h                   |   1 -
tests/fixtures/FakeSourcePlugin.cpp                |   1 -
tests/tst_SourceManager.cpp                        |  28 +-
tests/tst_SourcePluginContract.cpp                 |  77 +-
tests/tst_SourceTypes.cpp                          |  11 +
13 files changed, 1343 insertions(+), 13 deletions(-)
```

## Focused harness refresh

The existing `/private/tmp/quemusic-final-fix-verify-2` build tree was stale for this branch because the harness source still referenced an older temp repo snapshot. I refreshed the equivalent out-of-tree harness to point at this worktree and used a real temp build path instead of `/private/tmp` to avoid a Qt autogen relative-include failure caused by the `/tmp -> /private/tmp` symlink boundary.

Command:

```bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/cmake -S /tmp/quemusic-task6-harness -B /private/var/folders/9_/7tqxtfdd1sz8h0f4_qcz9nd00000gn/T/quemusic-final-fix-verify-M4c1kI -DBUILD_TESTING=ON -DCMAKE_PREFIX_PATH=/Users/liqiang/Qt/6.11.1/macos
```

Output:

```text
-- Configuring done (2.2s)
CMake Warning:
  Manually-specified variables were not used by the project:

    BUILD_TESTING

-- Generating done (0.5s)
-- Build files have been written to: /private/var/folders/9_/7tqxtfdd1sz8h0f4_qcz9nd00000gn/T/quemusic-final-fix-verify-M4c1kI
```

## Build

Command:

```bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/cmake --build /private/var/folders/9_/7tqxtfdd1sz8h0f4_qcz9nd00000gn/T/quemusic-final-fix-verify-M4c1kI
```

Output:

```text
[100%] Built target quemusic_plugin_startup_test
```

## Focused tests

Command:

```bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/ctest --test-dir /private/var/folders/9_/7tqxtfdd1sz8h0f4_qcz9nd00000gn/T/quemusic-final-fix-verify-M4c1kI --output-on-failure
```

Output:

```text
Internal ctest changing into directory: /private/var/folders/9_/7tqxtfdd1sz8h0f4_qcz9nd00000gn/T/quemusic-final-fix-verify-M4c1kI
Test project /private/var/folders/9_/7tqxtfdd1sz8h0f4_qcz9nd00000gn/T/quemusic-final-fix-verify-M4c1kI
    Start 1: quemusic_source_manager_test
1/5 Test #1: quemusic_source_manager_test .............   Passed    5.36 sec
    Start 2: quemusic_source_types_test
2/5 Test #2: quemusic_source_types_test ...............   Passed    0.49 sec
    Start 3: quemusic_source_plugin_contract_test
3/5 Test #3: quemusic_source_plugin_contract_test .....   Passed    0.49 sec
    Start 4: quemusic_playback_plugin_contract_test
4/5 Test #4: quemusic_playback_plugin_contract_test ...   Passed    0.51 sec
    Start 5: quemusic_plugin_startup_test
5/5 Test #5: quemusic_plugin_startup_test .............   Passed    0.76 sec

100% tests passed, 0 tests failed out of 5

Total Test time (real) =   7.62 sec
```

## Known full-application blockers

The focused harness is green, but the full application must still be treated as blocked by the pre-existing qwindowkit, Crypto++, and Ninja issues called out in the plan. This verification does not claim a full-app green build.
