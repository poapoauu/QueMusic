# Task 3 Report: SourceManager Plugin Discovery

## Status

Implemented the NAS-first source-plugin discovery foundation. `SourceManager`
loads real Qt dynamic plugins, validates their stable music-source contract,
retains successful loaders for the manager lifetime, and creates sessions with
the caller-provided QObject parent.

## Files changed

- `core/source/SourceManager.h`: public source-discovery API, load signals,
  loaded-source state, and persistent `std::vector<std::unique_ptr<QPluginLoader>>`.
- `core/source/SourceManager.cpp`: recursive plugin discovery, validation,
  failure isolation, metadata projection, and parent-owned session creation.
- `tests/tst_SourceManager.cpp`: focused real-plugin integration tests.
- `tests/fixtures/FakeSourcePlugin.cpp`: minimal test-only Qt plugin fixture,
  compiled into valid, duplicate, and invalid dynamic-plugin variants.
- `CMakeLists.txt`: production `quemusic_source_manager` target plus focused
  fixture/test targets under `BUILD_TESTING`.

## Implementation details

- Discovery walks each configured path with `QDirIterator`, filters candidates
  via `QLibrary::isLibrary`, and loads with `QPluginLoader`.
- Each candidate loader receives `QLibrary::PreventUnloadHint`; successful
  loaders remain in the required persistent vector until `SourceManager`
  destruction.
- The manager rejects a non-source QObject, empty or duplicate source ID,
  empty SDK version, missing display name, and `initialize()` failures. Every
  such case emits `sourceLoadFailed(path, reason)` and traversal continues.
- `availableSources()` returns `QVariantMap` entries with `id`, `name`,
  `version`, `protocol`, and numeric `capabilities`; `createSession()` returns
  the plugin session using the supplied parent.
- `sourceLoaded` and `sourceChanged` emit after each successful load.

## Test record

### Required RED command

```bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/ctest --test-dir build/mac-clang-release -R quemusic_source_manager_test --output-on-failure
```

Result before implementation: `No tests were found!!!` (exit 0 despite the
absent test target). This established the expected pre-implementation red
state.

### Required project build command

```bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/cmake --build build/mac-clang-release --target quemusic_source_manager_test -j 8
```

Result: blocked before compilation because the existing cache declares Ninja
but has `CMAKE_MAKE_PROGRAM-NOTFOUND`. CMake returned exit 1 with:

```text
permission denied
CMake Error: Generator: execution of make failed. Make command was:  -j 8 quemusic_source_manager_test
```

### Fresh top-level configure

```bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/cmake -S . -B build/task-3-top-level -G "Unix Makefiles" -DBUILD_TESTING=ON -DQt6_DIR=/Users/liqiang/Qt/6.11.1/macos/lib/cmake/Qt6
```

Result: blocked by unrelated, pre-existing repository inputs: the linked
worktree lacks `ThirdParty/qwindowkit/CMakeLists.txt`, and Crypto++ configuration
then cannot find `api/QCloudMusicApi/3rdparty/cryptopp/TestPrograms/test_x86_sse2.cpp`.

### Narrow focused validation

The temporary harness at `/private/tmp/quemusic-task3-harness` compiles the
same SDK, manager, fixture modules, and test target without QML, qwindowkit,
or Crypto++.

```bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/cmake -S /private/tmp/quemusic-task3-harness -B build/task-3-source-manager -G "Unix Makefiles" -DQUEMUSIC_SOURCE_ROOT=/Users/liqiang/Documents/ChatGPT/QueMusic/.worktrees/feat-nas-plugin-architecture -DQt6_DIR=/Users/liqiang/Qt/6.11.1/macos/lib/cmake/Qt6
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/cmake --build build/task-3-source-manager --target quemusic_source_manager_test -j 8
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/ctest --test-dir build/task-3-source-manager -R quemusic_source_manager_test --output-on-failure
./build/task-3-source-manager/quemusic_source_manager_test
```

Exact final result: the build completed with `[100%] Built target
quemusic_source_manager_test`; CTest reported `1/1` passing and `100% tests
passed`; the Qt test executable reported `7 passed, 0 failed, 0 skipped`.
The five behavior slots cover a valid plugin, a missing path, unknown source
lookup, duplicate source ID, and all five invalid plugin conditions while
continuing to the valid fixture.

## Test-only fixture ruling

The pre-dispatch ruling permits a minimal dynamic test fixture because Task 3
must exercise `QPluginLoader` before Task 4 creates the formal
`plugins/test-source` MODULE target. This task intentionally adds only
`tests/fixtures/FakeSourcePlugin.cpp` and its test-build variants; it does not
implement Task 4's production fake source. Task 4 must create its planned
formal target and converge or remove overlapping fixture coverage.

## Self-review

- Reused the existing `SourceDescriptor`, `SourceAccount`,
  `IMusicSourcePlugin`, and `IMusicSourceSession` types without duplicates.
- Confirmed the fixture declares the exact existing plugin IID through
  `QUEMUSIC_MUSIC_SOURCE_PLUGIN_IID`.
- Confirmed all required rejection branches emit `sourceLoadFailed` and do not
  abort later traversal.
- Confirmed session ownership is asserted against the caller-provided parent.
- Confirmed the manager target is available outside `BUILD_TESTING`; only
  fixtures and tests are test-only.
- Confirmed no QML, `MusicApiService`, Issue #12, meshgradient, or playback
  code changed.
- Ran `git diff --check` successfully before commit.

## Concerns

Full-project configuration remains unavailable due to the pre-existing missing
qwindowkit and Crypto++ files, plus the stale Ninja cache. The narrow CMake
harness provides successful compile and dynamic-plugin integration-test
evidence for the complete Task 3 scope.
