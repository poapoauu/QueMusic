# Optional Artwork ABI Verification Report

- Date: 2026-08-28
- Worktree: `/Users/liqiang/Documents/ChatGPT/QueMusic/.worktrees/codex-source-sdk-v1-artwork`
- Focused harness source: `/tmp/quemusic-task6-harness`
- Focused harness build directory: `/private/var/folders/9_/7tqxtfdd1sz8h0f4_qcz9nd00000gn/T/quemusic-source-sdk-v1-artwork-final2`
- Root-project configure build directory: `/private/var/folders/9_/7tqxtfdd1sz8h0f4_qcz9nd00000gn/T/quemusic-root-build-7EjYuA`
- Test count: 6
- Result: PASS

## Initial branch diff snapshot before the final ABI fix wave

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
.../verification-report.md                        | 129 +++
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
14 files changed, 1472 insertions(+), 13 deletions(-)
```

The branch diff in this historical snapshot is not product-only. Intentional documentation/specification/plan/report artifacts are:

```text
.superpowers/sdd/2026-08-28-source-sdk-v1-optional-artwork/task-1-report.md
.superpowers/sdd/2026-08-28-source-sdk-v1-optional-artwork/verification-report.md
docs/PLUGIN_API.md
docs/superpowers/plans/2026-08-28-source-sdk-v1-optional-artwork.md
docs/superpowers/specs/2026-08-27-nas-plugin-architecture-design.md
```

The product/build and focused test files are:

```text
CMakeLists.txt
plugins/test-source/TestSourceSession.h
sdk/source/IMusicSourceArtworkSession.h
sdk/source/IMusicSourcePlugin.h
sdk/source/IMusicSourceSession.h
tests/fixtures/FakeSourcePlugin.cpp
tests/tst_SourceManager.cpp
tests/tst_SourcePluginContract.cpp
tests/tst_SourceTypes.cpp
```

## Root-project BUILD_TESTING configure

Command, run from the isolated worktree:

```bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/cmake -S /Users/liqiang/Documents/ChatGPT/QueMusic/.worktrees/codex-source-sdk-v1-artwork -B /private/var/folders/9_/7tqxtfdd1sz8h0f4_qcz9nd00000gn/T/quemusic-root-build-7EjYuA -DBUILD_TESTING=ON -DCMAKE_PREFIX_PATH=/Users/liqiang/Qt/6.11.1/macos
```

Result: exit status 1. Exact blocking output:

```text
-- [MyApp] Using ThirdParty/qwindowkit
CMake Error at cmake/external/qwindowkit.cmake:33 (add_subdirectory):
  The source directory

    /Users/liqiang/Documents/ChatGPT/QueMusic/.worktrees/codex-source-sdk-v1-artwork/ThirdParty/qwindowkit

  does not contain a CMakeLists.txt file.

-- QCloudMusicApi: 使用本地 crypto++ 源码 /Users/liqiang/Documents/ChatGPT/QueMusic/.worktrees/codex-source-sdk-v1-artwork/api/QCloudMusicApi/3rdparty/cryptopp（跳过联网下载）
CMake Error at /private/var/folders/9_/7tqxtfdd1sz8h0f4_qcz9nd00000gn/T/quemusic-root-build-7EjYuA/CMakeFiles/CMakeTmp/CMakeLists.txt:24 (target_sources):
  Cannot find source file:

    /Users/liqiang/Documents/ChatGPT/QueMusic/.worktrees/codex-source-sdk-v1-artwork/api/QCloudMusicApi/3rdparty/cryptopp/TestPrograms/test_x86_sse2.cpp

CMake Error at api/QCloudMusicApi/3rdparty/cryptopp-cmake/cryptopp/CMakeLists.txt:172 (try_compile):
  Failed to generate test project build system.

-- Configuring incomplete, errors occurred!
```

This confirms that the required fresh root-project `BUILD_TESTING=ON` configuration is blocked by the known missing qwindowkit/Crypto++ inputs; it does not produce a usable root build tree.

## Focused harness refresh (historical five-test snapshot)

The existing `/private/tmp/quemusic-final-fix-verify-2` build tree was stale for this branch because `/tmp/quemusic-task6-harness/CMakeLists.txt` still contained the exact line `set(REPO_ROOT "/private/tmp/quemusic-final-fix-repo")`. I refreshed the harness source in place by replacing only that line with `set(REPO_ROOT "/Users/liqiang/Documents/ChatGPT/QueMusic/.worktrees/codex-source-sdk-v1-artwork")`; no harness files were copied and no product files were changed. The focused harness CMakeLists registers all five test executables and `add_test` entries unconditionally, so its `BUILD_TESTING=ON` configure warning is expected and the equivalent focused configuration is reproducible without that unused flag. I used a real temp build path instead of `/private/tmp` to avoid a Qt autogen relative-include failure caused by the `/tmp -> /private/tmp` symlink boundary.

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

## Focused build

Command:

```bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/cmake --build /private/var/folders/9_/7tqxtfdd1sz8h0f4_qcz9nd00000gn/T/quemusic-final-fix-verify-M4c1kI --target quemusic_source_manager_test quemusic_source_types_test quemusic_source_plugin_contract_test quemusic_playback_plugin_contract_test quemusic_plugin_startup_test
```

Output:

```text
[  0%] Built target quemusic_source_sdk_autogen_timestamp_deps
[  7%] Built target quemusic_source_sdk
[100%] Built target quemusic_source_manager_test
[100%] Built target quemusic_source_types_test
[100%] Built target quemusic_source_plugin_contract_test
[100%] Built target quemusic_playback_plugin_contract_test
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
1/5 Test #1: quemusic_source_manager_test .............   Passed    0.10 sec
    Start 2: quemusic_source_types_test
2/5 Test #2: quemusic_source_types_test ...............   Passed    0.03 sec
    Start 3: quemusic_source_plugin_contract_test
3/5 Test #3: quemusic_source_plugin_contract_test .....   Passed    0.03 sec
    Start 4: quemusic_playback_plugin_contract_test
4/5 Test #4: quemusic_playback_plugin_contract_test ...   Passed    0.03 sec
    Start 5: quemusic_plugin_startup_test
5/5 Test #5: quemusic_plugin_startup_test .............   Passed    0.19 sec

100% tests passed, 0 tests failed out of 5

Total Test time (real) =   0.38 sec
```

The build and ctest commands above were rerun after the report edits. The
explicit `--target` list is the covering build evidence for all five focused
test executables.

## Known full-application blockers

The focused harness is green, but the full application must still be treated as blocked by the pre-existing qwindowkit and Crypto++ issues reproduced above. The Ninja issue is previously reported and was not reproduced in this run. This verification does not claim a full-app green build.

## Final covering rerun after report edits

Build command:

```bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/cmake --build /private/var/folders/9_/7tqxtfdd1sz8h0f4_qcz9nd00000gn/T/quemusic-final-fix-verify-M4c1kI --target quemusic_source_manager_test quemusic_source_types_test quemusic_source_plugin_contract_test quemusic_playback_plugin_contract_test quemusic_plugin_startup_test
```

Output (all five requested target completion lines):

```text
[100%] Built target quemusic_source_manager_test
[100%] Built target quemusic_source_types_test
[100%] Built target quemusic_source_plugin_contract_test
[100%] Built target quemusic_playback_plugin_contract_test
[100%] Built target quemusic_plugin_startup_test
```

Test command:

```bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/ctest --test-dir /private/var/folders/9_/7tqxtfdd1sz8h0f4_qcz9nd00000gn/T/quemusic-final-fix-verify-M4c1kI --output-on-failure
```

Output:

```text
1/5 Test #1: quemusic_source_manager_test .............   Passed    0.10 sec
2/5 Test #2: quemusic_source_types_test ...............   Passed    0.03 sec
3/5 Test #3: quemusic_source_plugin_contract_test .....   Passed    0.03 sec
4/5 Test #4: quemusic_playback_plugin_contract_test ...   Passed    0.03 sec
5/5 Test #5: quemusic_plugin_startup_test .............   Passed    0.19 sec

100% tests passed, 0 tests failed out of 5

Total Test time (real) =   0.38 sec
```
