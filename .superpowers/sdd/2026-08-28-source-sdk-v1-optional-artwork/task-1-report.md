# Task 1 Report: Optional Artwork Session Interface

Date: 2026-08-28
Worktree: `/Users/liqiang/Documents/ChatGPT/QueMusic/.worktrees/codex-source-sdk-v1-artwork`

## Files changed

- `sdk/source/IMusicSourceArtworkSession.h` (new)
- `sdk/source/IMusicSourceSession.h`
- `sdk/source/IMusicSourcePlugin.h`
- `CMakeLists.txt`
- `tests/tst_SourcePluginContract.cpp`
- `tests/fixtures/FakeSourcePlugin.cpp`
- `plugins/test-source/TestSourceSession.h`
- `tests/tst_SourceManager.cpp`

## Design decisions

- Added `IMusicSourceArtworkSession` as a separately versioned optional interface with IID `org.quemusic.MusicSourceArtworkSession/1.0`.
- Kept the base plugin IID exactly `org.quemusic.MusicSourcePlugin/1.0`.
- Removed only `fetchArtwork(const TrackRef &track)` from `IMusicSourceSession`; all remaining base method order, signatures, and async request signals are unchanged.
- Used `Q_INTERFACES(IMusicSourceArtworkSession)` on artwork-capable fake/test sessions so optional capability discovery uses Qt interface casting instead of changing the base vtable.
- Updated the source-manager artwork test path to cast sessions to `IMusicSourceArtworkSession` before requesting artwork, matching the new optional contract.
- Kept supporting fixture sessions base-only by removing the obsolete base-session `fetchArtwork()` override.

## Commands and outputs

### Read task brief

Command:

```bash
sed -n '1,260p' /Users/liqiang/Documents/ChatGPT/QueMusic/.worktrees/codex-source-sdk-v1-artwork/.superpowers/sdd/2026-08-28-source-sdk-v1-optional-artwork/task-1-brief.md
```

Output:

```text
Read the Task 1 brief that required:
- a new sdk/source/IMusicSourceArtworkSession.h
- removal of fetchArtwork from IMusicSourceSession
- unchanged org.quemusic.MusicSourcePlugin/1.0 IID
- Q_INTERFACES(IMusicSourceArtworkSession) in the fake artwork-capable session
- focused contract build/test commands
- git commit -m "fix: version source artwork capability separately"
```

### Verify initial `fetchArtwork` touch points

Command:

```bash
rg -n "fetchArtwork" /Users/liqiang/Documents/ChatGPT/QueMusic/.worktrees/codex-source-sdk-v1-artwork
```

Output:

```text
/Users/liqiang/Documents/ChatGPT/QueMusic/.worktrees/codex-source-sdk-v1-artwork/docs/superpowers/specs/2026-08-27-nas-plugin-architecture-design.md:110:`fetchArtwork(const TrackRef &)`. Other optional capabilities follow the same
/Users/liqiang/Documents/ChatGPT/QueMusic/.worktrees/codex-source-sdk-v1-artwork/docs/superpowers/specs/2026-08-27-nas-plugin-architecture-design.md:139:`fetchArtwork(const TrackRef &)`. A session may implement that interface in
/Users/liqiang/Documents/ChatGPT/QueMusic/.worktrees/codex-source-sdk-v1-artwork/docs/superpowers/plans/2026-08-28-source-sdk-v1-optional-artwork.md:55:implements `QUuid fetchArtwork(const TrackRef &track) override`. Add a test
/Users/liqiang/Documents/ChatGPT/QueMusic/.worktrees/codex-source-sdk-v1-artwork/docs/superpowers/plans/2026-08-28-source-sdk-v1-optional-artwork.md:59:ID. Remove `fetchArtwork()` from the base-only fake session.
/Users/liqiang/Documents/ChatGPT/QueMusic/.worktrees/codex-source-sdk-v1-artwork/docs/superpowers/plans/2026-08-28-source-sdk-v1-optional-artwork.md:69:exist, and the current base session still owns `fetchArtwork()`.
/Users/liqiang/Documents/ChatGPT/QueMusic/.worktrees/codex-source-sdk-v1-artwork/docs/superpowers/plans/2026-08-28-source-sdk-v1-optional-artwork.md:86:    virtual QUuid fetchArtwork(const TrackRef &track) = 0;
/Users/liqiang/Documents/ChatGPT/QueMusic/.worktrees/codex-source-sdk-v1-artwork/docs/superpowers/plans/2026-08-28-source-sdk-v1-optional-artwork.md:96:Delete only `fetchArtwork` from `IMusicSourceSession`. Keep the order and
/Users/liqiang/Documents/ChatGPT/QueMusic/.worktrees/codex-source-sdk-v1-artwork/docs/superpowers/plans/2026-08-28-source-sdk-v1-optional-artwork.md:138:const QUuid requestId = artworkSession->fetchArtwork(track);
/Users/liqiang/Documents/ChatGPT/QueMusic/.worktrees/codex-source-sdk-v1-artwork/docs/superpowers/plans/2026-08-28-source-sdk-v1-optional-artwork.md:159:existing `fetchArtwork()` implementation and asynchronous JSON payload in
/Users/liqiang/Documents/ChatGPT/QueMusic/.worktrees/codex-source-sdk-v1-artwork/docs/superpowers/plans/2026-08-28-source-sdk-v1-optional-artwork.md:160:`TestSourceSession.cpp`. Remove the obsolete `fetchArtwork()` override from
/Users/liqiang/Documents/ChatGPT/QueMusic/.worktrees/codex-source-sdk-v1-artwork/docs/PLUGIN_API.md:78:  `search`, `browse`, `resolveStream`, `fetchArtwork`, `fetchLyrics`, and `cancel`.
/Users/liqiang/Documents/ChatGPT/QueMusic/.worktrees/codex-source-sdk-v1-artwork/docs/PLUGIN_API.md:84:  `Artwork` completes `fetchArtwork`
/Users/liqiang/Documents/ChatGPT/QueMusic/.worktrees/codex-source-sdk-v1-artwork/docs/PLUGIN_API.md:102:    QUuid fetchArtwork(const TrackRef &) override { return QUuid::createUuid(); }
/Users/liqiang/Documents/ChatGPT/QueMusic/.worktrees/codex-source-sdk-v1-artwork/sdk/source/IMusicSourceSession.h:19:    virtual QUuid fetchArtwork(const TrackRef &track) = 0;
/Users/liqiang/Documents/ChatGPT/QueMusic/.worktrees/codex-source-sdk-v1-artwork/tests/tst_SourceManager.cpp:226:    const QUuid requestId = session->fetchArtwork(track);
/Users/liqiang/Documents/ChatGPT/QueMusic/.worktrees/codex-source-sdk-v1-artwork/tests/fixtures/FakeSourcePlugin.cpp:15:    QUuid fetchArtwork(const TrackRef &) override { return completeUnsupported(); }
/Users/liqiang/Documents/ChatGPT/QueMusic/.worktrees/codex-source-sdk-v1-artwork/tests/tst_SourcePluginContract.cpp:76:    QUuid fetchArtwork(const TrackRef &track) override
/Users/liqiang/Documents/ChatGPT/QueMusic/.worktrees/codex-source-sdk-v1-artwork/plugins/test-source/TestSourceSession.cpp:36:QUuid TestSourceSession::fetchArtwork(const TrackRef &track)
/Users/liqiang/Documents/ChatGPT/QueMusic/.worktrees/codex-source-sdk-v1-artwork/plugins/test-source/TestSourceSession.h:18:    QUuid fetchArtwork(const TrackRef &track) override;
```

### Red-step contract commands from the brief

Command:

```bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/cmake --build /private/tmp/quemusic-final-fix-verify-2 --target quemusic_source_plugin_contract_test
```

Output:

```text
[  0%] Built target quemusic_source_sdk_autogen_timestamp_deps
[ 10%] Automatic MOC for target quemusic_source_sdk

AutoGen error
-------------
Info error in info file
"/tmp/quemusic-final-fix-verify-2/CMakeFiles/quemusic_source_sdk_autogen.dir/AutogenInfo.json":
The header file "/tmp/quemusic-final-fix-repo/sdk/source/IMusicSourcePlugin.h" does not exist.

make[3]: *** [quemusic_source_sdk_autogen/timestamp] Error 1
make[2]: *** [CMakeFiles/quemusic_source_sdk_autogen.dir/all] Error 2
make[1]: *** [CMakeFiles/quemusic_source_plugin_contract_test.dir/rule] Error 2
make: *** [quemusic_source_plugin_contract_test] Error 2
```

Command:

```bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/ctest --test-dir /private/tmp/quemusic-final-fix-verify-2 -R quemusic_source_plugin_contract_test --output-on-failure
```

Output:

```text
Internal ctest changing into directory: /tmp/quemusic-final-fix-verify-2
Test project /tmp/quemusic-final-fix-verify-2
    Start 3: quemusic_source_plugin_contract_test
1/1 Test #3: quemusic_source_plugin_contract_test ...   Passed    0.03 sec

100% tests passed, 0 tests failed out of 1

Total Test time (real) =   0.04 sec
```

Note: the existing focused build tree was stale. It was configured from `/tmp/quemusic-task6-harness` and still expected `/private/tmp/quemusic-final-fix-repo` to point at another worktree.

### Diagnose stale focused harness

Command:

```bash
rg -n "CMAKE_HOME_DIRECTORY" /private/tmp/quemusic-final-fix-verify-2/CMakeCache.txt
```

Output:

```text
415:CMAKE_HOME_DIRECTORY:INTERNAL=/tmp/quemusic-task6-harness
```

Command:

```bash
ls -la /private/tmp/quemusic-final-fix-repo
```

Output:

```text
lrwxr-xr-x@ 1 liqiang  wheel  81 Aug 28 09:23 /private/tmp/quemusic-final-fix-repo -> /Users/liqiang/Documents/ChatGPT/QueMusic/.worktrees/feat-nas-plugin-architecture
```

### Repoint and refresh the focused harness

Command:

```bash
ln -sfn /Users/liqiang/Documents/ChatGPT/QueMusic/.worktrees/codex-source-sdk-v1-artwork /private/tmp/quemusic-final-fix-repo
```

Output:

```text
(no stdout/stderr)
```

Command:

```bash
readlink /private/tmp/quemusic-final-fix-repo
```

Output:

```text
/Users/liqiang/Documents/ChatGPT/QueMusic/.worktrees/codex-source-sdk-v1-artwork
```

Command:

```bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/cmake --fresh -S /tmp/quemusic-task6-harness -B /private/tmp/quemusic-final-fix-verify-2 -DCMAKE_PREFIX_PATH=/Users/liqiang/Qt/6.11.1/macos
```

Output:

```text
-- The CXX compiler identification is AppleClang 21.0.0.21000101
-- Detecting CXX compiler ABI info
-- Detecting CXX compiler ABI info - done
-- Check for working CXX compiler: /Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/c++ - skipped
-- Detecting CXX compile features
-- Detecting CXX compile features - done
-- Performing Test CMAKE_HAVE_LIBC_PTHREAD
-- Performing Test CMAKE_HAVE_LIBC_PTHREAD - Success
-- Found Threads: TRUE
-- Performing Test HAVE_STDATOMIC
-- Performing Test HAVE_STDATOMIC - Success
-- Found WrapAtomic: TRUE
-- Found OpenGL: /Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX26.5.sdk/System/Library/Frameworks/OpenGL.framework
-- Found WrapOpenGL: TRUE
-- Configuring done (2.6s)
-- Generating done (0.5s)
-- Build files have been written to: /tmp/quemusic-final-fix-verify-2
```

### Green-step required focused contract build and test

Command:

```bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/cmake --build /private/tmp/quemusic-final-fix-verify-2 --target quemusic_source_plugin_contract_test
```

Output:

```text
[  0%] Built target quemusic_source_sdk_autogen_timestamp_deps
[ 10%] Automatic MOC for target quemusic_source_sdk
[ 10%] Built target quemusic_source_sdk_autogen
[ 20%] Building CXX object CMakeFiles/quemusic_source_sdk.dir/quemusic_source_sdk_autogen/mocs_compilation.cpp.o
[ 30%] Building CXX object CMakeFiles/quemusic_source_sdk.dir/tmp/quemusic-final-fix-repo/sdk/source/SourceTypes.cpp.o
[ 40%] Linking CXX static library libquemusic_source_sdk.a
[ 50%] Built target quemusic_source_sdk
[ 50%] Built target quemusic_source_plugin_contract_test_autogen_timestamp_deps
[ 60%] Automatic MOC for target quemusic_source_plugin_contract_test
[ 60%] Built target quemusic_source_plugin_contract_test_autogen
[ 70%] Building CXX object CMakeFiles/quemusic_source_plugin_contract_test.dir/quemusic_source_plugin_contract_test_autogen/mocs_compilation.cpp.o
[ 80%] Building CXX object CMakeFiles/quemusic_source_plugin_contract_test.dir/tmp/quemusic-final-fix-repo/tests/tst_SourcePluginContract.cpp.o
[ 90%] Linking CXX executable quemusic_source_plugin_contract_test
[100%] Built target quemusic_source_plugin_contract_test
```

Command:

```bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/ctest --test-dir /private/tmp/quemusic-final-fix-verify-2 -R quemusic_source_plugin_contract_test --output-on-failure
```

Output:

```text
Internal ctest changing into directory: /tmp/quemusic-final-fix-verify-2
Test project /tmp/quemusic-final-fix-verify-2
    Start 3: quemusic_source_plugin_contract_test
1/1 Test #3: quemusic_source_plugin_contract_test ...   Passed    0.02 sec

100% tests passed, 0 tests failed out of 1

Total Test time (real) =   0.03 sec
```

### Additional focused source-manager regression check

Command:

```bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/cmake --build /private/tmp/quemusic-final-fix-verify-2 --target quemusic_source_manager_test
```

Output:

```text
[100%] Built target quemusic_source_manager_test
```

Command:

```bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/ctest --test-dir /private/tmp/quemusic-final-fix-verify-2 -R quemusic_source_manager_test --output-on-failure
```

Output:

```text
Internal ctest changing into directory: /tmp/quemusic-final-fix-verify-2
Test project /tmp/quemusic-final-fix-verify-2
    Start 1: quemusic_source_manager_test
1/1 Test #1: quemusic_source_manager_test .....   Passed    0.09 sec

100% tests passed, 0 tests failed out of 1

Total Test time (real) =   0.09 sec
```

### Final diff inspection before commit

Command:

```bash
git -C /Users/liqiang/Documents/ChatGPT/QueMusic/.worktrees/codex-source-sdk-v1-artwork diff --check
```

Output:

```text
(no output)
```

Command:

```bash
git -C /Users/liqiang/Documents/ChatGPT/QueMusic/.worktrees/codex-source-sdk-v1-artwork status --short
```

Output:

```text
 M CMakeLists.txt
 M plugins/test-source/TestSourceSession.h
 M sdk/source/IMusicSourcePlugin.h
 M sdk/source/IMusicSourceSession.h
 M tests/fixtures/FakeSourcePlugin.cpp
 M tests/tst_SourceManager.cpp
 M tests/tst_SourcePluginContract.cpp
?? sdk/source/IMusicSourceArtworkSession.h
```

Command:

```bash
git -C /Users/liqiang/Documents/ChatGPT/QueMusic/.worktrees/codex-source-sdk-v1-artwork diff --stat
```

Output:

```text
 CMakeLists.txt                          |  1 +
 plugins/test-source/TestSourceSession.h |  5 ++-
 sdk/source/IMusicSourcePlugin.h         |  2 +
 sdk/source/IMusicSourceSession.h        |  1 -
 tests/fixtures/FakeSourcePlugin.cpp     |  1 -
 tests/tst_SourceManager.cpp             |  5 ++-
 tests/tst_SourcePluginContract.cpp      | 73 ++++++++++++++++++++++++++++++++-
 7 files changed, 83 insertions(+), 5 deletions(-)
```

## Concerns

- `docs/PLUGIN_API.md` still documents `fetchArtwork` as part of the base `IMusicSourceSession` contract. I left documentation untouched because the approved Task 1 brief scoped the implementation to SDK/tests, but the docs should be updated in a follow-up so they match the new optional-interface contract.

## Fix round 1

### Files changed

- `docs/PLUGIN_API.md`
- `.superpowers/sdd/2026-08-28-source-sdk-v1-optional-artwork/task-1-report.md`

### Documentation changes

- Removed `fetchArtwork` from the documented required `IMusicSourceSession` pure-virtual list.
- Added `IMusicSourceArtworkSession` and IID `org.quemusic.MusicSourceArtworkSession/1.0` to the ABI section.
- Documented that `SourceCapability::Artwork` pairs with `IMusicSourceArtworkSession`, which should be declared with `Q_INTERFACES(IMusicSourceArtworkSession)` and discovered with `qobject_cast<IMusicSourceArtworkSession *>(session)`.
- Updated the minimal skeleton so artwork support is optional and shown through the separate interface instead of the base session contract.

### Commands and outputs

Command:

```bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/cmake --build /private/tmp/quemusic-final-fix-verify-2 --target quemusic_source_plugin_contract_test
```

Output:

```text
[  0%] Built target quemusic_source_sdk_autogen_timestamp_deps
[  1%] Automatic MOC for target quemusic_source_sdk
[  1%] Built target quemusic_source_sdk_autogen
[  7%] Built target quemusic_source_sdk
[  7%] Built target quemusic_source_plugin_contract_test_autogen_timestamp_deps
[  9%] Automatic MOC for target quemusic_source_plugin_contract_test
[  9%] Built target quemusic_source_plugin_contract_test_autogen
[ 16%] Built target quemusic_source_plugin_contract_test
```

Command:

```bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/cmake --build /private/tmp/quemusic-final-fix-verify-2 --target quemusic_source_manager_test
```

Output:

```text
[  0%] Built target quemusic_source_sdk_autogen_timestamp_deps
[  1%] Automatic MOC for target quemusic_source_sdk
[  1%] Built target quemusic_source_sdk_autogen
[  7%] Built target quemusic_source_sdk
[  7%] Built target quemusic_incompatible_sdk_source_fixture_autogen_timestamp_deps
[ 10%] Automatic MOC for target quemusic_incompatible_sdk_source_fixture
[ 10%] Built target quemusic_incompatible_sdk_source_fixture_autogen
Built target quemusic_incompatible_sdk_source_fixture
Built target quemusic_source_manager_autogen_timestamp_deps
Automatic MOC for target quemusic_source_manager
Built target quemusic_source_manager_autogen
Built target quemusic_source_manager
Built target quemusic_test_source_autogen_timestamp_deps
Automatic MOC for target quemusic_test_source
Built target quemusic_test_source_autogen
Built target quemusic_test_source
Built target quemusic_duplicate_source_fixture_one_autogen_timestamp_deps
Automatic MOC for target quemusic_duplicate_source_fixture_one
Built target quemusic_duplicate_source_fixture_one_autogen
Built target quemusic_duplicate_source_fixture_one
Built target quemusic_duplicate_source_fixture_two_autogen_timestamp_deps
Automatic MOC for target quemusic_duplicate_source_fixture_two
Built target quemusic_duplicate_source_fixture_two_autogen
Built target quemusic_duplicate_source_fixture_two
Built target quemusic_invalid_valid_fixture_autogen_timestamp_deps
Automatic MOC for target quemusic_invalid_valid_fixture
Built target quemusic_invalid_valid_fixture_autogen
Built target quemusic_invalid_valid_fixture
Built target quemusic_empty_id_source_fixture_autogen_timestamp_deps
Automatic MOC for target quemusic_empty_id_source_fixture
Built target quemusic_empty_id_source_fixture_autogen
Built target quemusic_empty_id_source_fixture
Built target quemusic_empty_sdk_source_fixture_autogen_timestamp_deps
Automatic MOC for target quemusic_empty_sdk_source_fixture
Built target quemusic_empty_sdk_source_fixture_autogen
Built target quemusic_empty_sdk_source_fixture
Built target quemusic_missing_name_source_fixture_autogen_timestamp_deps
Automatic MOC for target quemusic_missing_name_source_fixture
Built target quemusic_missing_name_source_fixture_autogen
Built target quemusic_missing_name_source_fixture
Built target quemusic_initialization_failure_fixture_autogen_timestamp_deps
Automatic MOC for target quemusic_initialization_failure_fixture
Built target quemusic_initialization_failure_fixture_autogen
Built target quemusic_initialization_failure_fixture
Built target quemusic_not_source_fixture_autogen_timestamp_deps
Automatic MOC for target quemusic_not_source_fixture
Built target quemusic_not_source_fixture_autogen
Built target quemusic_not_source_fixture
Built target quemusic_source_manager_test_autogen_timestamp_deps
Automatic MOC for target quemusic_source_manager_test
Built target quemusic_source_manager_test_autogen
Built target quemusic_source_manager_test
```

Command:

```bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/ctest --test-dir /private/tmp/quemusic-final-fix-verify-2 -R 'quemusic_source_plugin_contract_test|quemusic_source_manager_test' --output-on-failure
```

Output:

```text
Internal ctest changing into directory: /tmp/quemusic-final-fix-verify-2
Test project /tmp/quemusic-final-fix-verify-2
    Start 1: quemusic_source_manager_test
1/2 Test #1: quemusic_source_manager_test ...........   Passed    5.13 sec
    Start 3: quemusic_source_plugin_contract_test
2/2 Test #3: quemusic_source_plugin_contract_test ...   Passed    0.49 sec

100% tests passed, 0 tests failed out of 2

Total Test time (real) =   5.63 sec
```

### Remaining concerns

- None beyond the previously noted temporary harness setup requirement for `/private/tmp/quemusic-final-fix-verify-2`, which remains outside the repo and was reused successfully for this fix round.
