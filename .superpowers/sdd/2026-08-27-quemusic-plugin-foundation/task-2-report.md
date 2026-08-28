# Task 2 Report: Music-Source Plugin Interfaces

## Status

Implemented the stable, versioned Qt music-source plugin/session contract and
its compile-time fake-plugin contract test.

## Files changed

- `sdk/source/SourcePluginContext.h`: host-owned, non-owning network facility
  and cache root.
- `sdk/source/IMusicSourceSession.h`: QObject-derived asynchronous session
  interface, request lifecycle methods, and result/authentication signals.
- `sdk/source/IMusicSourcePlugin.h`: pure plugin interface with the required
  `org.quemusic.MusicSourcePlugin/1.0` IID declaration.
- `tests/tst_SourcePluginContract.cpp`: fake session/plugin contract tests.
- `CMakeLists.txt`: SDK automoc header registration and focused contract-test
  target.

## Interface and lifecycle choices

- `IMusicSourcePlugin` remains a pure C++ interface; concrete plugin classes
  can inherit `QObject` alongside it, declare `Q_INTERFACES`, and add plugin
  metadata without making host-facing contract consumers depend on QObject.
- `initialize(SourcePluginContext &)` receives only a non-owning
  `QNetworkAccessManager *` and cache-root path. It intentionally carries no
  QML engine, main-window, `MusicApiService`, or credential-store dependency.
- `createSession(..., QObject *parent)` makes the host-provided parent the
  session ownership/lifecycle mechanism.
- Session methods return generated request IDs immediately. Completion and
  failure are communicated by signals containing that ID and a provider-neutral
  operation name/result or `SourceError`.
- Source capabilities come from `SourceDescriptor`; consumers can select
  behavior from flags without provider-specific branches.

## Test record

### Required RED command

```bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/ctest --test-dir build/mac-clang-release -R quemusic_source_plugin_contract_test --output-on-failure
```

Result before implementation: `No tests were found!!!`. The command returned
success despite the absent target, rather than the brief's expected nonzero
failure; this still established that the contract test was not registered.

### Required focused project build

```bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/cmake --build build/mac-clang-release --target quemusic_source_plugin_contract_test -j 8
```

Result: blocked before compilation. The existing cache specifies Ninja but has
`CMAKE_MAKE_PROGRAM-NOTFOUND`, producing `permission denied` and an empty make
command. A fresh top-level configure was also blocked because this worktree
does not contain `ThirdParty/qwindowkit` and has no Qt prefix configured.

### Narrow local validation

```bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/cmake -S /private/tmp/quemusic-task2-contract -B /private/tmp/quemusic-task2-contract/build-2 -G Ninja -DCMAKE_MAKE_PROGRAM=/Users/liqiang/Qt/Tools/Ninja/ninja -DCMAKE_PREFIX_PATH=/Users/liqiang/Qt/6.11.1/macos
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/cmake --build /private/tmp/quemusic-task2-contract/build-2 --target quemusic_source_plugin_contract_test -j 8
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/ctest --test-dir /private/tmp/quemusic-task2-contract/build-2 -R quemusic_source_plugin_contract_test --output-on-failure
```

Result: configure succeeded; the target compiled with Qt automoc; ctest passed
`1/1` test. The Qt test executable reported all three contract test slots as
passing.

## Self-review

- Confirmed the IID string exactly matches the brief.
- Confirmed all required plugin and session signatures, including request IDs
  and the three signals, are present and pure virtual where required.
- Confirmed the test exercises descriptor/capabilities, context initialization,
  parent-owned session creation, non-null search IDs, and the `search`
  success-operation signal.
- Confirmed the SDK reuses Task 1 value types and introduces no duplicate
  source models.
- Ran `git diff --check` successfully.

## Concerns

The full worktree preset cannot currently validate the target because its
existing Ninja cache is incomplete and the linked worktree lacks the qwindowkit
submodule content. The isolated SDK CMake harness provides successful compile
and ctest evidence for the Task 2 scope.
