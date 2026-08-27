# QueMusic Plugin Foundation Implementation Plan

> For agentic workers: REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox syntax for tracking.

**Goal:** Add a tested, versioned Qt music-source plugin foundation to QueMusic while preserving the current MusicApiService, QML playback path, and Issue #12 branch independently.

**Architecture:** Adapt the author's soundwave plugin lifecycle into a QueMusic SourceManager, then define a separate IMusicSourcePlugin contract for protocol providers. Add the playback contract as an independent SDK boundary, but do not reroute the existing QML/Qt Multimedia player until a later playback-migration plan.

**Tech Stack:** C++17, Qt 6 Core/Network/Test, CMake 3.16+, Ninja, QPluginLoader, Q_PLUGIN_METADATA, Q_DECLARE_INTERFACE, QtTest.

**Spec:** docs/superpowers/specs/2026-08-27-nas-plugin-architecture-design.md

## Global Constraints

- The first source target is Navidrome/OpenSubsonic, but this plan only builds the source-plugin foundation and a fake plugin; live NAS integration is a later plan.
- Keep QueMusic's current Qt baseline and asynchronous style; do not make QCoro or C++20 a prerequisite.
- Keep the Issue #12 branch independent from this architecture branch.
- QML must not perform source HTTP requests, store tokens, or access plugin objects directly; expose future source use cases through ViewModels.
- IMusicSourcePlugin and IPlaybackPlugin are separate contracts: one describes where media comes from, the other describes how media is played.
- Plugin loaders remain alive until application exit; do not implement runtime unload or hot replacement of native plugins.
- Plugins are trusted native code. This phase does not implement remote installation, sandboxing, or an online marketplace.
- The host must validate plugin IID, SDK version, source ID, duplicate IDs, and load errors before registration.
- New source SDK code is Apache-2.0 and must remain outside the existing AGPL-3.0 meshgradient/ component.
- Before copying soundwave source files into QueMusic, add an explicit license to soundwave and retain all Qt, QCoro, libmpv, and other third-party notices.
- Every task ends with its focused QtTest/build command and a separate commit.

---

## File Map

The first implementation uses focused directories rather than extending the already large api/MusicApiService.* files:

~~~text
sdk/source/                         Stable music-source contract types
sdk/playback/                       Stable playback contract types
core/source/                        Host-side plugin discovery and sessions
core/playback/                      Host-side playback plugin registry
plugins/test-source/                Build-only fake source plugin
tests/                              QtTest coverage for contracts and loading
docs/PLUGIN_API.md                  Public plugin author guide
ARCHITECTURE.md                     Updated runtime boundaries
~~~

The existing api/, cpp/, QML files, and MusicApiService remain unchanged until
the source adapters are implemented in later plans.

## Preconditions

Before the first source file is copied from soundwave, add an Apache-2.0
LICENSE to the soundwave repository if all of its original application code is
owned by the author. Add a third-party notice file there for Qt, QCoro, and
libmpv. No Qt source code is copied into QueMusic; only original application
code and interface patterns are adapted.

### Task 1: Define Source SDK Types

**Files:**
- Create: sdk/source/SourceTypes.h
- Create: sdk/source/SourceTypes.cpp
- Create: tests/tst_SourceTypes.cpp
- Modify: CMakeLists.txt

**Interfaces:**
- Produces SourceCapability, SourceCapabilities, SourceErrorKind, SourceError, SourceDescriptor, SourceAccount, TrackRef, SearchQuery, BrowseQuery, and StreamDescriptor.
- Produces QJsonObject sourceDescriptorToJson(const SourceDescriptor &) and SourceDescriptor sourceDescriptorFromJson(const QJsonObject &).

- [ ] Step 1: Write the failing type and serialization tests

Add these test cases to tests/tst_SourceTypes.cpp:

~~~cpp
void roundTripsDescriptorWithCapabilities();
void keepsProviderIdsNamespacedBySource();
void preservesExpiringStreamHeadersAndMediaKind();
void mapsUnknownErrorKindToUnknown();
~~~

The tests must assert that descriptor JSON preserves id, name, version, protocol,
sdkVersion, and every capability bit; that TrackRef compares the pair
(sourceId, nativeId) rather than only the native ID; and that a StreamDescriptor
retains headers, MIME type, expiresAt, video, and seekable.

- [ ] Step 2: Run the focused test and verify it fails

Run:

~~~bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/cmake --preset mac-clang-release -S . -B build/mac-clang-release -DBUILD_TESTING=ON
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/cmake --build build/mac-clang-release --target quemusic_source_types_test -j 8
~~~

Expected: configuration or compilation fails because SourceTypes and the test
target do not exist yet.

- [ ] Step 3: Implement the minimal SDK types

Use C++17-compatible Qt types. Define capabilities as QFlags over a quint64-backed
enum. The required fields are:

~~~cpp
enum class SourceCapability : quint64 {
    None = 0,
    Search = 1ull << 0,
    Browse = 1ull << 1,
    StreamAudio = 1ull << 2,
    StreamVideo = 1ull << 3,
    Artwork = 1ull << 4,
    Lyrics = 1ull << 5,
    PlaylistRead = 1ull << 6,
    PlaylistWrite = 1ull << 7,
    Favorites = 1ull << 8,
    Download = 1ull << 9,
    Scrobble = 1ull << 10,
};

struct TrackRef { QString sourceId; QString nativeId; };

struct StreamDescriptor {
    TrackRef track;
    QUrl url;
    QMap<QString, QString> headers;
    QString mimeType;
    QDateTime expiresAt;
    bool video = false;
    bool seekable = true;
};
~~~

SourceDescriptor must carry the plugin source ID, display name, plugin version,
protocol name, SDK version, and capabilities. SourceError must carry an enum kind,
user-facing message, and optional HTTP status. Do not put tokens or passwords in
any serializable SDK type.

- [ ] Step 4: Run the focused test and verify it passes

Run:

~~~bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/cmake --build build/mac-clang-release --target quemusic_source_types_test -j 8
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/ctest --test-dir build/mac-clang-release -R quemusic_source_types_test --output-on-failure
~~~

Expected: all four source-type tests pass.

- [ ] Step 5: Commit

~~~bash
git add sdk/source/SourceTypes.h sdk/source/SourceTypes.cpp tests/tst_SourceTypes.cpp CMakeLists.txt
git commit -m "feat: add music source SDK types"
~~~

### Task 2: Define Music-Source Plugin Interfaces

**Files:**
- Create: sdk/source/IMusicSourceSession.h
- Create: sdk/source/IMusicSourcePlugin.h
- Create: sdk/source/SourcePluginContext.h
- Create: tests/tst_SourcePluginContract.cpp
- Modify: CMakeLists.txt

**Interfaces:**
- IMusicSourcePlugin::SourceDescriptor descriptor() const
- IMusicSourcePlugin::bool initialize(SourcePluginContext &context)
- IMusicSourcePlugin::IMusicSourceSession *createSession(const SourceAccount &, QObject *parent)
- IMusicSourceSession::QUuid search(const SearchQuery &query)
- IMusicSourceSession::QUuid browse(const BrowseQuery &query)
- IMusicSourceSession::QUuid resolveStream(const TrackRef &track)
- IMusicSourceSession::QUuid fetchLyrics(const TrackRef &track)
- IMusicSourceSession::void cancel(const QUuid &requestId)
- Session signals requestSucceeded(QUuid, QString, QJsonValue), requestFailed(QUuid, SourceError), and authenticationChanged(bool).

- [ ] Step 1: Write the failing contract test

Create a compile-time fake session and fake plugin in
tests/tst_SourcePluginContract.cpp. Assert that a fake plugin can expose a
descriptor, initialize with a SourcePluginContext, create a session for a
SourceAccount, return a non-null request ID from search(), and emit the success
signal with operation name search.

~~~cpp
void createsSessionThroughStableContract();
void reportsAsyncOperationByRequestId();
void exposesCapabilitiesWithoutProviderBranching();
~~~

- [ ] Step 2: Run the contract test and verify it fails

Run:

~~~bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/ctest --test-dir build/mac-clang-release -R quemusic_source_plugin_contract_test --output-on-failure
~~~

Expected: the target is missing or compilation fails because the interfaces do
not exist.

- [ ] Step 3: Implement the versioned Qt interfaces

Declare the plugin interface with this exact IID:

~~~cpp
#define QUEMUSIC_MUSIC_SOURCE_PLUGIN_IID "org.quemusic.MusicSourcePlugin/1.0"
Q_DECLARE_INTERFACE(IMusicSourcePlugin, QUEMUSIC_MUSIC_SOURCE_PLUGIN_IID)
~~~

IMusicSourceSession is a QObject-derived abstract class with Q_OBJECT; it uses
request IDs and signals for asynchronous results. SourcePluginContext contains
only host-owned facilities needed in this phase:

~~~cpp
struct SourcePluginContext {
    QNetworkAccessManager *network = nullptr;
    QString cacheRoot;
};
~~~

The context must not contain QQmlApplicationEngine, the main window,
MusicApiService, or a raw token store. IMusicSourcePlugin is a pure interface;
concrete plugins inherit QObject and IMusicSourcePlugin, use
Q_INTERFACES(IMusicSourcePlugin), and export metadata with Q_PLUGIN_METADATA.

- [ ] Step 4: Run the contract test and verify it passes

Run:

~~~bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/cmake --build build/mac-clang-release --target quemusic_source_plugin_contract_test -j 8
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/ctest --test-dir build/mac-clang-release -R quemusic_source_plugin_contract_test --output-on-failure
~~~

Expected: all three contract tests pass.

- [ ] Step 5: Commit

~~~bash
git add sdk/source/IMusicSourceSession.h sdk/source/IMusicSourcePlugin.h sdk/source/SourcePluginContext.h tests/tst_SourcePluginContract.cpp CMakeLists.txt
git commit -m "feat: define music source plugin contract"
~~~

### Task 3: Implement SourceManager and Plugin Discovery

**Files:**
- Create: core/source/SourceManager.h
- Create: core/source/SourceManager.cpp
- Create: tests/tst_SourceManager.cpp
- Modify: CMakeLists.txt

**Interfaces:**
- void addSearchPath(const QString &path)
- int loadAll()
- QVariantList availableSources() const
- QStringList sourceIds() const
- IMusicSourceSession *createSession(const QString &sourceId, const SourceAccount &account, QObject *parent)
- Signals sourceLoaded(QString), sourceLoadFailed(QString, QString), and sourceChanged().

- [ ] Step 1: Write the failing manager tests

Add tests for a valid fake plugin directory, a nonexistent directory, an
unknown source ID, and a duplicate source ID. The valid case must assert that
loadAll() returns one, availableSources() contains the descriptor, and a session
can be created. The failure cases must assert a signal is emitted and the
process remains usable.

~~~cpp
void loadsValidSourcePlugin();
void ignoresMissingSearchPath();
void rejectsUnknownSourceId();
void rejectsDuplicateSourceIdWithoutStoppingOtherSources();
~~~

- [ ] Step 2: Run the manager tests and verify they fail

Run:

~~~bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/ctest --test-dir build/mac-clang-release -R quemusic_source_manager_test --output-on-failure
~~~

Expected: the target is missing or fails because SourceManager is not implemented.

- [ ] Step 3: Implement discovery and lifetime management

Adapt the soundwave PluginManager pattern using QDirIterator,
QLibrary::isLibrary, and QPluginLoader. Store loaders in
std::vector<std::unique_ptr<QPluginLoader>> and set
QLibrary::PreventUnloadHint. For each loaded object:

1. qobject_cast<IMusicSourcePlugin *> must succeed.
2. Read descriptor() and reject an empty or duplicate source ID.
3. Reject an empty SDK version or descriptor with no display name.
4. Call initialize(SourcePluginContext &), and reject false.
5. Keep the plugin and loader alive until SourceManager destruction.

availableSources() returns maps with id, name, version, protocol, and numeric
capabilities fields. A failed plugin emits sourceLoadFailed and does not prevent
later files from loading.

- [ ] Step 4: Run the manager tests and verify they pass

Run:

~~~bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/cmake --build build/mac-clang-release --target quemusic_source_manager_test -j 8
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/ctest --test-dir build/mac-clang-release -R quemusic_source_manager_test --output-on-failure
~~~

Expected: all four manager tests pass.

- [ ] Step 5: Commit

~~~bash
git add core/source/SourceManager.h core/source/SourceManager.cpp tests/tst_SourceManager.cpp CMakeLists.txt
git commit -m "feat: load music source plugins"
~~~

### Task 4: Add a Build-Only Fake Source Plugin

**Files:**
- Create: plugins/test-source/TestSourcePlugin.h
- Create: plugins/test-source/TestSourcePlugin.cpp
- Create: plugins/test-source/TestSourceSession.h
- Create: plugins/test-source/TestSourceSession.cpp
- Create: plugins/test-source/plugin.json
- Create: plugins/test-source/CMakeLists.txt
- Modify: CMakeLists.txt
- Modify: tests/tst_SourceManager.cpp

**Interfaces:**
- Plugin ID: org.quemusic.test-source
- Source ID: test-source
- Capabilities: Search | StreamAudio | Artwork
- Session search returns one JSON track with sourceId = test-source and nativeId = test-track-1.

- [ ] Step 1: Add the plugin target and metadata test

Define a MODULE target named quemusic_test_source, link only against
Qt6::Core, Qt6::Network, and the source SDK target, and place its output in
the build plugins/source directory. The metadata file must contain:

~~~json
{
  "Keys": ["org.quemusic.test-source"],
  "sourceId": "test-source",
  "sdkVersion": "1.0"
}
~~~

Add the plugin output directory to SourceManager's test search path.

- [ ] Step 2: Run the manager test and verify the plugin is found

Run:

~~~bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/cmake --build build/mac-clang-release --target quemusic_source_manager_test -j 8
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/ctest --test-dir build/mac-clang-release -R quemusic_source_manager_test --output-on-failure
~~~

Expected: the test plugin is built, discovered, and loaded by SourceManager.

- [ ] Step 3: Implement the fake session behavior

Implement search() using QTimer::singleShot(0, ...) so the contract is
asynchronous without network access. Emit
requestSucceeded(requestId, "search", QJsonArray{trackObject}). Implement
cancel() by preventing an uncancelled request from emitting a result.

- [ ] Step 4: Add cancellation and failure assertions

Extend tests/tst_SourceManager.cpp with:

~~~cpp
void fakeSourceReturnsNamespacedTrack();
void fakeSourceCancellationSuppressesResult();
~~~

- [ ] Step 5: Run the full test suite for the phase

Run:

~~~bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/ctest --test-dir build/mac-clang-release --output-on-failure
~~~

Expected: every configured test passes, including all source SDK and manager
tests.

- [ ] Step 6: Commit

~~~bash
git add plugins/test-source CMakeLists.txt tests/tst_SourceManager.cpp
git commit -m "test: add dynamic music source plugin"
~~~

### Task 5: Port the Playback Contract Without Changing QML Playback

**Files:**
- Create: sdk/playback/PlaybackTypes.h
- Create: sdk/playback/IPlaybackEngine.h
- Create: sdk/playback/IPlaybackPlugin.h
- Create: core/playback/PlaybackEngineManager.h
- Create: core/playback/PlaybackEngineManager.cpp
- Create: tests/tst_PlaybackPluginContract.cpp
- Modify: CMakeLists.txt

**Interfaces:**
- void IPlaybackEngine::open(const StreamDescriptor &source)
- void IPlaybackEngine::play(), pause(), stop(), seek(qint64), and setVolume(double)
- PlaybackCapabilities IPlaybackEngine::capabilities() const
- QString IPlaybackPlugin::engineId() const
- QString IPlaybackPlugin::engineName() const
- IPlaybackEngine *IPlaybackPlugin::createEngine(QObject *parent)
- bool PlaybackEngineManager::registerPlugin(IPlaybackPlugin *)
- QStringList PlaybackEngineManager::engineIds() const

- [ ] Step 1: Write the failing playback contract tests

Use a fake playback plugin and fake engine to assert that an engine can be
registered, duplicate IDs are rejected, and the engine receives a
StreamDescriptor containing headers and media kind without knowing anything
about a source plugin.

~~~cpp
void registersPlaybackPlugin();
void rejectsDuplicatePlaybackEngineId();
void forwardsStreamDescriptorWithoutSourceDependency();
~~~

- [ ] Step 2: Run the focused test and verify it fails

Run:

~~~bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/ctest --test-dir build/mac-clang-release -R quemusic_playback_plugin_contract_test --output-on-failure
~~~

Expected: the target is missing or fails because the new playback SDK does not
exist.

- [ ] Step 3: Implement the playback boundary

Adapt the soundwave playback types and plugin interface, replacing Songloft's
MediaSource dependency with the QueMusic StreamDescriptor. The manager owns the
current engine and plugin registrations, but it must not be exposed to QML in
this phase. Do not add MediaPlayer, MPV calls, or provider-specific logic to the
contract.

- [ ] Step 4: Run the focused test and verify it passes

Run:

~~~bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/cmake --build build/mac-clang-release --target quemusic_playback_plugin_contract_test -j 8
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/ctest --test-dir build/mac-clang-release -R quemusic_playback_plugin_contract_test --output-on-failure
~~~

Expected: all three playback contract tests pass.

- [ ] Step 5: Commit

~~~bash
git add sdk/playback core/playback tests/tst_PlaybackPluginContract.cpp CMakeLists.txt
git commit -m "feat: add replaceable playback contract"
~~~

### Task 6: Integrate Source Discovery at Application Startup

**Files:**
- Modify: main.cpp
- Modify: CMakeLists.txt
- Create: docs/PLUGIN_API.md
- Create: ARCHITECTURE.md
- Create: tests/tst_PluginStartup.cpp

**Interfaces:**
- Application startup constructs one SourceManager owned by the application lifetime.
- Application plugin search paths are applicationDirPath()/plugins/source and QStandardPaths::AppDataLocation/plugins/source.
- No plugin manager or source session is registered as a direct QML context property.

- [ ] Step 1: Write the startup boundary test

Test that startup can create SourceManager, add nonexistent application and user
plugin paths, call loadAll(), and continue without changing existing QML context
properties. Assert that no MusicApiService API is required to load the manager.

~~~cpp
void startupToleratesMissingPluginDirectories();
void sourceManagerIsNotExposedAsRawQmlContextObject();
~~~

- [ ] Step 2: Run the startup test and verify it fails

Run:

~~~bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/ctest --test-dir build/mac-clang-release -R quemusic_plugin_startup_test --output-on-failure
~~~

Expected: the target is missing or fails because startup integration and
architecture documentation do not exist.

- [ ] Step 3: Integrate non-invasive startup loading

Create SourceManager after QGuiApplication and before engine.load(). Add the two
search paths, connect sourceLoadFailed to qWarning()/LogManager, and call
loadAll(). Do not change the MusicApiService singleton, local lyrics flow, or
existing QML context properties.

Document the plugin ABI, metadata, search paths, capability rules, native-code
trust model, and a minimal plugin skeleton in docs/PLUGIN_API.md. Document the
runtime boundary and deliberate non-goals in ARCHITECTURE.md.

- [ ] Step 4: Run the full phase verification

Run:

~~~bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/cmake --build build/mac-clang-release -j 8
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/ctest --test-dir build/mac-clang-release --output-on-failure
git diff --check
~~~

Expected: the build succeeds, all tests pass, and git diff --check prints no
diagnostics.

- [ ] Step 5: Commit

~~~bash
git add main.cpp CMakeLists.txt docs/PLUGIN_API.md ARCHITECTURE.md tests/tst_PluginStartup.cpp
git commit -m "feat: initialize music source plugins at startup"
~~~

## Execution Notes

After this plan is complete, the next plan should implement the Local source
adapter and then a Navidrome/OpenSubsonic adapter. That later plan may modify
MusicApiService only through an explicit compatibility adapter and must add fake
HTTP tests before introducing live-server behavior. Kugou and NetEase should be
migrated only after source-model parity tests exist.

The current plan deliberately does not alter QML playback, move the existing
provider code, or add MV rendering. Those are separate reviewable changes.
