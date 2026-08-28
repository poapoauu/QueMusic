# Final Fix Report: Plugin Foundation Review Findings

Date: 2026-08-28

## Status

Implemented the four Important whole-branch review findings and the startup
test flake fix in one focused change set. The source and playback contracts
remain separate; no QML, `MusicApiService`, Issue #12, meshgradient, provider,
NAS, MV, or real network behavior was changed.

## Findings Resolved

### 1. PIC-safe static SDKs linked by MODULE plugins

- `quemusic_source_sdk` and `quemusic_playback_sdk` now explicitly set
  `POSITION_INDEPENDENT_CODE ON`.
- The shared fixture helper now sets both `LIBRARY_OUTPUT_DIRECTORY` and
  `RUNTIME_OUTPUT_DIRECTORY`, keeping MODULE outputs predictable on Windows as
  well as Unix-like platforms.
- The focused clean harness compiled the source SDK into the formal
  `quemusic_test_source` MODULE and all dynamic fixture MODULEs. This is the
  narrowest available real module-link validation while the top-level project
  remains blocked. Its CMake target definitions mirror the production PIC and
  output properties.

### 2. Authoritative source SDK compatibility policy

- `IMusicSourcePlugin.h` defines the single host contract version,
  `QUEMUSIC_MUSIC_SOURCE_SDK_VERSION` (`1.0`), and
  `isMusicSourceSdkVersionCompatible()`.
- IID 1.0 accepts only an exact `sdkVersion == "1.0"`; empty, older, newer, or
  differently formatted values are incompatible until an explicit host-policy
  change adds support and tests.
- `SourceManager` performs this check before `initialize()` and registration.
- A dynamic `2.0` fixture is intentionally configured to fail initialization.
  `rejectsUnsupportedSdkVersionWithoutStoppingOtherSources` asserts the version
  failure text and proves the following valid `test-source` plugin still loads.
  Receiving the `2.0` version error rather than the fixture initialization error
  proves initialization was not reached.

### 3. Active playback-engine lifetime

- `PlaybackEngineManager` now records the QObject supplying the active engine.
- The registered plugin's synchronous `destroyed(QObject *)` handler deletes
  and nulls that engine before removing the registration.
- `destroysActiveEngineWhenSupplyingPluginIsDestroyed` activates a fake engine,
  destroys its plugin, asserts `currentEngine() == nullptr`, and successfully
  registers and activates a replacement engine afterward.

### 4. Fake-source capabilities and completion semantics

- `IMusicSourceSession` now includes `fetchArtwork(const TrackRef &)`, before
  the current IID 1.0 contract is frozen.
- `SourceErrorKind::Unsupported` provides an explicit terminal failure for
  unsupported operations.
- The formal `test-source` keeps its planned `Search | StreamAudio | Artwork`
  capability set. It completes each advertised non-cancelled operation
  asynchronously:
  - `search` returns the namespaced fake track;
  - `resolveStream` returns a JSON `StreamDescriptor` DTO with a valid fake
    `.invalid` URL, MIME type, media kind, and seekability;
  - `fetchArtwork` returns a JSON artwork DTO with track, URL, and MIME type.
- Non-advertised browse and lyrics operations now complete asynchronously with
  `SourceErrorKind::Unsupported`, rather than silently hanging. The manager
  fixture and source-contract fake implementations were updated to have a
  terminal result for every operation as well.
- Source-manager regressions assert stream and artwork completion payloads.
- `docs/PLUGIN_API.md` documents the exact compatibility policy, artwork
  operation, DTO payload expectations, and the capability-completion rule. Its
  minimal skeleton advertises no capability until an implementation supplies
  terminal results.

### Minor: startup filesystem-state flake

- Removed `QStandardPaths::setTestModeEnabled(true)` and the assertion that the
  test-mode app-data plugin directory does not exist. The startup test now
  verifies only deterministic path construction, application ownership, engine
  usability, and raw-QML isolation.

## Changed Files

- `CMakeLists.txt`
- `.superpowers/sdd/2026-08-27-quemusic-plugin-foundation/final-fix-report.md`
- `core/playback/PlaybackEngineManager.cpp`
- `core/playback/PlaybackEngineManager.h`
- `core/source/SourceManager.cpp`
- `docs/PLUGIN_API.md`
- `plugins/test-source/TestSourceSession.cpp`
- `plugins/test-source/TestSourceSession.h`
- `sdk/source/IMusicSourcePlugin.h`
- `sdk/source/IMusicSourceSession.h`
- `sdk/source/SourceTypes.cpp`
- `sdk/source/SourceTypes.h`
- `tests/fixtures/FakeSourcePlugin.cpp`
- `tests/tst_PlaybackPluginContract.cpp`
- `tests/tst_PluginStartup.cpp`
- `tests/tst_SourceManager.cpp`
- `tests/tst_SourcePluginContract.cpp`

## TDD Evidence

1. Source red: after adding the artwork regression, the focused source-manager
   harness failed to compile with `no member named 'fetchArtwork' in
   'IMusicSourceSession'`.
2. Playback red: the focused playback contract test failed exactly at
   `manager.currentEngine() == nullptr` after destroying its supplying plugin.
3. Green: the clean final harness compiled all source SDKs, real MODULE plugins,
   fixtures, startup code, and playback contracts, then passed every registered
   test.

## Exact Validation Results

Focused clean harness:

```bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/cmake \
  -S /private/tmp/quemusic-task6-harness \
  -B /private/tmp/quemusic-final-fix-verify-2 \
  -G "Unix Makefiles" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=/Users/liqiang/Qt/6.11.1/macos \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/cmake \
  --build /private/tmp/quemusic-final-fix-verify-2 -j 8
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/ctest \
  --test-dir /private/tmp/quemusic-final-fix-verify-2 --output-on-failure
```

Result: build completed at 100%, including `quemusic_test_source` and every
fixture MODULE. CTest passed 5/5:

- `quemusic_source_manager_test`
- `quemusic_source_types_test`
- `quemusic_source_plugin_contract_test`
- `quemusic_playback_plugin_contract_test`
- `quemusic_plugin_startup_test`

`git diff --check` completed with no diagnostics.

Top-level fresh configure attempt:

```bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/cmake \
  -S . -B build/final-fix-top-level -G "Unix Makefiles" \
  -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=/Users/liqiang/Qt/6.11.1/macos \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0
```

Result: blocked by pre-existing unrelated inputs:

- `ThirdParty/qwindowkit` has no `CMakeLists.txt`;
- Crypto++ `try_compile` cannot find
  `api/QCloudMusicApi/3rdparty/cryptopp/TestPrograms/test_x86_sse2.cpp`.

This confirms the known qwindowkit/Crypto++ top-level configure blocker. The
existing release cache's separate Ninja issue was not used for the focused
evidence.

## Deferred Minor

The remaining logging observation is intentionally deferred: startup load
failures are emitted through `qWarning()`, while the existing `LogManager`
defaults to Error-level persistence. Changing severity or `LogManager`
threshold behavior would broaden this plugin-foundation fix into application
logging-policy work. The existing startup documentation continues to describe
the warning path; no claim is made that warning-only failures are persisted by
the default logger configuration.
