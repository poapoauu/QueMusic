# Task 6 Report

Date: 2026-08-28

## Status

Completed Task 6 in `/Users/liqiang/Documents/ChatGPT/QueMusic/.worktrees/feat-nas-plugin-architecture`.

## Files Changed

- Modified: `main.cpp`
- Modified: `CMakeLists.txt`
- Added: `core/source/SourceStartup.h`
- Added: `core/source/SourceStartup.cpp`
- Added: `tests/tst_PluginStartup.cpp`
- Added: `docs/PLUGIN_API.md`
- Added: `ARCHITECTURE.md`

## Startup Ownership and Search Paths

- Application startup now creates exactly one `SourceManager` after
  `QGuiApplication application(argc, argv);` and before `engine.load(...)`.
- That manager is created by `createAndLoadSourceManager(application, &application)`,
  so application lifetime ownership is explicit.
- Default plugin search paths are:
  - `QCoreApplication::applicationDirPath()/plugins/source`
  - `QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)/plugins/source`
- `createAndLoadSourceManager(...)` connects `sourceLoadFailed` to `qWarning()`.
  Because `LogManager` is already installed as the Qt message handler before the
  helper is called, plugin loader failures are captured by the existing logging
  path without adding new QML exposure.
- `loadAll()` is called exactly once during startup helper execution.

## QML Boundary Proof

- `main.cpp` adds no new `engine.rootContext()->setContextProperty(...)` calls
  for `SourceManager`, sessions, or plugin instances.
- `tests/tst_PluginStartup.cpp` asserts that `sourceManager`, `sourceSession`,
  and `musicSourcePlugin` are not valid QML context properties on a fresh
  `QQmlApplicationEngine`.
- `MusicApiService::setSharedAccountManager(accountManager)`, local lyrics,
  playback wiring, and the existing QML context properties were left unchanged.

## Docs Coverage

`docs/PLUGIN_API.md` covers:

- ABI interface and current IID
- required descriptor metadata
- startup search paths
- capability bitmask rules
- trusted native-code execution model
- minimal plugin skeleton

`ARCHITECTURE.md` covers:

- the runtime startup boundary
- ownership model
- deliberate non-goals, including no `MusicApiService`, playback, local lyrics,
  Issue #12, or `meshgradient` changes

## Tests and Commands

### Required red command

Command:

```bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/ctest --test-dir build/mac-clang-release -R quemusic_plugin_startup_test --output-on-failure
```

Result:

```text
Internal ctest changing into directory: /Users/liqiang/Documents/ChatGPT/QueMusic/.worktrees/feat-nas-plugin-architecture/build/mac-clang-release
Test project /Users/liqiang/Documents/ChatGPT/QueMusic/.worktrees/feat-nas-plugin-architecture/build/mac-clang-release
No tests were found!!!
```

### Requested full-tree verification attempts

Command:

```bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/cmake --build build/mac-clang-release -j 8
```

Result:

```text
permission denied
CMake Error: Generator: execution of make failed. Make command was:  -j 8
```

Inspection of `build/mac-clang-release/CMakeCache.txt` showed:

```text
CMAKE_GENERATOR:INTERNAL=Ninja
CMAKE_MAKE_PROGRAM:FILEPATH=CMAKE_MAKE_PROGRAM-NOTFOUND
```

Fresh configure attempt:

```bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/cmake -S . -B build/task6-make-release -G "Unix Makefiles" -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/Users/liqiang/Qt/6.11.1/macos -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0
```

Result:

- failed in `cmake/external/qwindowkit.cmake` because
  `ThirdParty/qwindowkit/CMakeLists.txt` is missing
- later configure also failed in Crypto++ `try_compile` because
  `api/QCloudMusicApi/3rdparty/cryptopp/TestPrograms/test_x86_sse2.cpp` is missing

These are the existing qwindowkit/Ninja/Crypto++ blockers referenced in the task
brief, so I switched to a focused harness.

### Focused harness verification

Harness configure:

```bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/cmake -S /private/tmp/quemusic-task6-harness -B /tmp/quemusic-task6-harness/build -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/Users/liqiang/Qt/6.11.1/macos -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0
```

Result: success

Harness build:

```bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/cmake --build /tmp/quemusic-task6-harness/build -j 8
```

Result: success

Focused tests:

```bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/ctest --test-dir /tmp/quemusic-task6-harness/build -R "quemusic_(plugin_startup|source_manager)_test" --output-on-failure
```

Result:

```text
Test project /tmp/quemusic-task6-harness/build
    Start 1: quemusic_source_manager_test
1/2 Test #1: quemusic_source_manager_test .....   Passed    0.09 sec
    Start 2: quemusic_plugin_startup_test
2/2 Test #2: quemusic_plugin_startup_test .....   Passed    0.19 sec

100% tests passed, 0 tests failed out of 2
```

Formatting check:

```bash
git diff --check
```

Result: no diagnostics

## Self-Review

- The startup helper keeps Task 6 isolated and lets the QtTest verify the
  boundary without exposing new QML context objects.
- `LogManager` integration is indirect by design through `qWarning()`, which is
  appropriate because the existing message handler is already installed.
- The helper adds only the two required search paths and does not modify
  `MusicApiService`, local lyrics, playback, or unrelated UI code.

## Concerns

- Full top-level project build/test verification is still blocked by pre-existing
  build environment issues:
  - configured release tree uses `Ninja` with `CMAKE_MAKE_PROGRAM-NOTFOUND`
  - fresh configure fails on missing `ThirdParty/qwindowkit/CMakeLists.txt`
  - fresh configure fails on missing Crypto++ `test_x86_sse2.cpp`
- Because of those blockers, validation was completed with a focused harness
  covering the touched startup boundary and existing `SourceManager` behavior
  instead of the full application tree.

## Follow-up Fix: Startup Boundary Test Gap

Reviewer finding addressed on 2026-08-28.

### What changed

- `tests/tst_PluginStartup.cpp` now exercises the production startup boundary on
  the same `QQmlApplicationEngine` instance used for the assertion boundary.
- `core/source/SourceStartup.h` and `core/source/SourceStartup.cpp` now expose a
  narrow `initializeSourceStartupBoundary(...)` helper that delegates to
  `createAndLoadSourceManager(...)` while fixing application-lifetime ownership
  to the real `QCoreApplication` instance.
- `main.cpp` now uses `initializeSourceStartupBoundary(application, engine)` so
  the tested path matches production startup more closely.

### Red step for the review fix

Command:

```bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/cmake --build /tmp/quemusic-task6-harness/build -j 8
```

Result:

```text
/tmp/quemusic-task6-repo/tests/tst_PluginStartup.cpp:44:9: error: use of undeclared identifier 'initializeSourceStartupBoundary'
```

This confirmed the revised test was demanding a production startup-boundary
entry point that did not yet exist.

### Verification after the fix

Build command:

```bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/cmake --build /tmp/quemusic-task6-harness/build -j 8
```

Result: success

Focused test command:

```bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/ctest --test-dir /tmp/quemusic-task6-harness/build -R "quemusic_(plugin_startup|source_manager)_test" --output-on-failure
```

Result:

```text
Test project /tmp/quemusic-task6-harness/build
    Start 1: quemusic_source_manager_test
1/2 Test #1: quemusic_source_manager_test .....   Passed    0.09 sec
    Start 2: quemusic_plugin_startup_test
2/2 Test #2: quemusic_plugin_startup_test .....   Passed    0.21 sec

100% tests passed, 0 tests failed out of 2
```

Formatting command:

```bash
git diff --check
```

Result: no diagnostics

### Follow-up self-review

- The startup test now proves that missing user plugin directories do not block
  startup progress: the manager is created through the production boundary,
  discovers the bundled test plugin, and the same engine context remains usable.
- The no-raw-QML-exposure check now runs against the same `QQmlApplicationEngine`
  passed through the startup boundary helper, not a disconnected empty engine.
- The test still avoids any `MusicApiService` dependency.

## Follow-up Fix: Align Startup Boundary Test With Real Runtime Layout

Reviewer finding addressed on 2026-08-28.

### What changed

- `tests/tst_PluginStartup.cpp` no longer requires
  `manager->sourceIds() == ["test-source"]`.
- The startup-boundary test still invokes
  `initializeSourceStartupBoundary(*QCoreApplication::instance(), engine)` on
  the same `QQmlApplicationEngine`.
- It continues to verify:
  - application-lifetime parenting on the real application object
  - startup usability while the user plugin directory is missing
  - no raw `sourceManager`, `sourceSession`, or `musicSourcePlugin` context
    properties on that same engine
- Production search paths and runtime behavior were left unchanged.

### Verification after the alignment fix

Build command:

```bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/cmake --build /tmp/quemusic-task6-harness/build -j 8
```

Result: success

Focused test command:

```bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/ctest --test-dir /tmp/quemusic-task6-harness/build -R "quemusic_(plugin_startup|source_manager)_test" --output-on-failure
```

Result:

```text
Test project /tmp/quemusic-task6-harness/build
    Start 1: quemusic_source_manager_test
1/2 Test #1: quemusic_source_manager_test .....   Passed    0.09 sec
    Start 2: quemusic_plugin_startup_test
2/2 Test #2: quemusic_plugin_startup_test .....   Passed    0.72 sec

100% tests passed, 0 tests failed out of 2
```

Formatting command:

```bash
git diff --check
```

Result: no diagnostics

### Alignment self-review

- The startup-boundary test is back to its intended scope: startup ownership,
  missing-directory tolerance, same-engine usability, and QML isolation.
- Formal dynamic plugin discovery remains covered by the dedicated source/plugin
  tests instead of being reasserted here through an accidental runtime-layout
  assumption.
