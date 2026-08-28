# Optional Artwork ABI Final-Fix Report

- Date: 2026-08-28
- Worktree: `/Users/liqiang/Documents/ChatGPT/QueMusic/.worktrees/codex-source-sdk-v1-artwork`
- Final focused build directory: `/private/var/folders/9_/7tqxtfdd1sz8h0f4_qcz9nd00000gn/T/quemusic-source-sdk-v1-artwork-final2`

## Final-review findings addressed

- Restored `IMusicSourceSession::fetchArtwork(const TrackRef &)` between
  `resolveStream` and `fetchLyrics`, preserving the existing v1 vtable while
  retaining `IMusicSourceArtworkSession` as an additive interface.
- Added a frozen v1 SDK declaration set, a separately compiled MODULE plugin,
  and `quemusic_source_v1_abi_test`; the test exercises `fetchLyrics` and
  cancellation through the current host.
- Added `SourceManager::requestArtwork()` with metadata gating, optional
  interface preference, and retained-v1 fallback. Tests cover optional IID
  dispatch, legacy fallback, and optional IID without Artwork metadata.
- Reworked `docs/PLUGIN_API.md` into a declaration-only asynchronous skeleton,
  and documented the retained-slot ABI policy in the public API, architecture
  spec, and implementation plan.
- The verification report records the root-project configure blockers and the
  reproducible focused-harness fallback. Ninja is identified as previously
  reported and not freshly reproduced in the final root configure attempt.

## Verification

Commands:

```bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/cmake -S /tmp/quemusic-task6-harness -B /private/var/folders/9_/7tqxtfdd1sz8h0f4_qcz9nd00000gn/T/quemusic-source-sdk-v1-artwork-final2 -DCMAKE_PREFIX_PATH=/Users/liqiang/Qt/6.11.1/macos
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/cmake --build /private/var/folders/9_/7tqxtfdd1sz8h0f4_qcz9nd00000gn/T/quemusic-source-sdk-v1-artwork-final2
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/ctest --test-dir /private/var/folders/9_/7tqxtfdd1sz8h0f4_qcz9nd00000gn/T/quemusic-source-sdk-v1-artwork-final2 --output-on-failure
```

Result: build succeeded and all 6/6 tests passed with 0 failures:

- `quemusic_source_manager_test`
- `quemusic_source_v1_abi_test`
- `quemusic_source_types_test`
- `quemusic_source_plugin_contract_test`
- `quemusic_playback_plugin_contract_test`
- `quemusic_plugin_startup_test`

Additional root-project configure verification:

```bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/cmake -S /Users/liqiang/Documents/ChatGPT/QueMusic/.worktrees/codex-source-sdk-v1-artwork -B /private/tmp/quemusic-source-sdk-v1-artwork-local -DBUILD_TESTING=ON -DCMAKE_PREFIX_PATH=/Users/liqiang/Qt/6.11.1/macos
```

Result: configuration remains blocked by the pre-existing missing
`ThirdParty/qwindowkit/CMakeLists.txt` and missing Crypto++
`TestPrograms/test_x86_sse2.cpp` inputs. No full application green-build claim
is made.

## Final hygiene

```bash
git diff --check
```

Result: no whitespace errors.
