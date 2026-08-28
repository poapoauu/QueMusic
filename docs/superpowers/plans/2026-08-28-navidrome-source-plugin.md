# Navidrome Source Plugin Implementation Plan

> For agentic workers: REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox syntax for tracking.

Goal: Add a loadable asynchronous Navidrome source plugin supporting Subsonic authentication, search, simulated browsing, audio stream resolution, artwork, and lyrics while preserving source SDK v1 ABI.

Architecture: Extend SourceAccount only at the end with account parameters and a secret. Keep all Navidrome REST and response normalization inside plugins/navidrome-source. Sessions use the host QNetworkAccessManager, track public request IDs across one or more replies, and emit existing source-session signals. The plugin is integrated as a normal plugins/source module and tested with local QTcpServer fixtures; the private Navidrome instance is used only by an opt-in smoke command.

Tech Stack: C++17, Qt 6 Core/Network/Test, QNetworkAccessManager, QNetworkReply, QTcpServer, CMake, Subsonic REST JSON API v1.16.1.

Spec: docs/superpowers/specs/2026-08-28-navidrome-source-plugin-design.md

## Global Constraints

- Keep org.quemusic.MusicSourcePlugin/1.0 and the exact v1 virtual order search, browse, resolveStream, fetchArtwork, fetchLyrics, cancel.
- Append account data only after the existing SourceAccount identity fields; do not add or reorder virtual functions.
- Use Subsonic JSON requests with v=1.16.1, c=QueMusic, f=json, u, t, and s.
- Generate a fresh salt of at least six characters per request and calculate t = md5(password + salt) using UTF-8 bytes.
- Use only asynchronous Qt network operations; never spin a nested event loop.
- Emit exactly one terminal source signal per non-cancelled public request.
- Never log or commit passwords, tokens, salts, authenticated URLs, private IPs, or private configuration.
- Advertise only Search, Browse, StreamAudio, Artwork, and Lyrics.
- Automated tests use a local QTcpServer; live-server tests are opt-in and are not registered with CTest.
- Run git diff --check, focused source tests, the opt-in smoke test when credentials are available, and the root CMake build before declaring completion.

---

### Task 1: Extend the SourceAccount payload safely

Files:
- Modify: sdk/source/SourceTypes.h:56-61
- Modify: tests/tst_SourceTypes.cpp
- Modify: docs/PLUGIN_API.md

Interfaces:
- Consumes: existing SourceAccount aggregate with sourceId, accountId, and displayName.
- Produces: SourceAccount::parameters as QVariantMap and SourceAccount::secret as QByteArray, appended after the three existing fields.

- [ ] Step 1: Write the failing test

Add a sourceAccountCarriesConnectionParametersWithoutChangingIdentity test slot:

~~~cpp
void SourceTypesTest::sourceAccountCarriesConnectionParametersWithoutChangingIdentity()
{
    const SourceAccount account{
        QStringLiteral("navidrome"),
        QStringLiteral("admin"),
        QStringLiteral("Navidrome Admin"),
        {{QStringLiteral("serverUrl"), QStringLiteral("http://example.invalid:8533")},
         {QStringLiteral("username"), QStringLiteral("admin")}},
        QByteArrayLiteral("test-password")};

    QCOMPARE(account.sourceId, QStringLiteral("navidrome"));
    QCOMPARE(account.accountId, QStringLiteral("admin"));
    QCOMPARE(account.displayName, QStringLiteral("Navidrome Admin"));
    QCOMPARE(account.parameters.value(QStringLiteral("serverUrl")).toString(),
             QStringLiteral("http://example.invalid:8533"));
    QCOMPARE(account.parameters.value(QStringLiteral("username")).toString(),
             QStringLiteral("admin"));
    QCOMPARE(account.secret, QByteArrayLiteral("test-password"));
}
~~~

Declare the slot beside the existing source type tests.

- [ ] Step 2: Run the test to verify it fails

Run:

~~~bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/cmake --build /private/tmp/quemusic-navidrome-build --target quemusic_source_types_test
~~~

Expected: compilation fails because SourceAccount has only three aggregate fields.

- [ ] Step 3: Write the minimal implementation

Add QByteArray and QVariantMap includes and append the fields without changing identity order:

~~~cpp
struct SourceAccount {
    QString sourceId;
    QString accountId;
    QString displayName;
    QVariantMap parameters;
    QByteArray secret;
};
~~~

Document serverUrl, username, and secret as plugin-defined input and state that sensitive values must not be logged.

- [ ] Step 4: Run the test to verify it passes

Run:

~~~bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/ctest --test-dir /private/tmp/quemusic-navidrome-build -R '^quemusic_source_types_test$' --output-on-failure
~~~

Expected: 1/1 test passed.

- [ ] Step 5: Commit

~~~bash
git add sdk/source/SourceTypes.h tests/tst_SourceTypes.cpp docs/PLUGIN_API.md
git commit -m "feat: add source account connection payload"
~~~

### Task 2: Create the Navidrome plugin and session skeleton

Files:
- Create: plugins/navidrome-source/CMakeLists.txt
- Create: plugins/navidrome-source/plugin.json
- Create: plugins/navidrome-source/NavidromeSourcePlugin.h
- Create: plugins/navidrome-source/NavidromeSourcePlugin.cpp
- Create: plugins/navidrome-source/NavidromeSourceSession.h
- Create: plugins/navidrome-source/NavidromeSourceSession.cpp
- Modify: CMakeLists.txt to add the plugin target to the normal application build and expose its output directory to tests.
- Test: tests/tst_NavidromeSource.cpp

Interfaces:
- Consumes: IMusicSourcePlugin, IMusicSourceSession, IMusicSourceArtworkSession, SourceAccount, and SourcePluginContext.
- Produces: NavidromeSourcePlugin::descriptor(), initialize(), createSession(), and a session implementing all six v1 base methods plus optional Artwork.

- [ ] Step 1: Write the failing contract test

Create tests/tst_NavidromeSource.cpp with reportsNavidromeDescriptor and createsConfiguredSession:

~~~cpp
void NavidromeSourceTest::reportsNavidromeDescriptor()
{
    NavidromeSourcePlugin plugin;
    const SourceDescriptor descriptor = plugin.descriptor();

    QCOMPARE(descriptor.id, QStringLiteral("navidrome"));
    QCOMPARE(descriptor.protocol, QStringLiteral("subsonic"));
    QCOMPARE(descriptor.sdkVersion, QStringLiteral("1.0"));
    QVERIFY(descriptor.capabilities.testFlag(SourceCapability::Search));
    QVERIFY(descriptor.capabilities.testFlag(SourceCapability::Browse));
    QVERIFY(descriptor.capabilities.testFlag(SourceCapability::StreamAudio));
    QVERIFY(descriptor.capabilities.testFlag(SourceCapability::Artwork));
    QVERIFY(descriptor.capabilities.testFlag(SourceCapability::Lyrics));
}
~~~

The session test initializes the plugin with QNetworkAccessManager, creates a session from an account containing serverUrl and username, and asserts the session is non-null and qobject_cast<IMusicSourceArtworkSession *>(session) is non-null.

- [ ] Step 2: Run the test to verify it fails

Run:

~~~bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/cmake --build /private/tmp/quemusic-navidrome-build --target quemusic_navidrome_source_test
~~~

Expected: CMake fails because the plugin target and test source do not exist.

- [ ] Step 3: Write the minimal plugin and session

Use the existing test source target pattern. plugin.json contains Keys org.quemusic.navidrome, sourceId navidrome, and sdkVersion 1.0.

NavidromeSourcePlugin uses Q_PLUGIN_METADATA(IID QUEMUSIC_MUSIC_SOURCE_PLUGIN_IID FILE "plugin.json") and Q_INTERFACES(IMusicSourcePlugin). Its descriptor returns id navidrome, protocol subsonic, SDK 1.0, and the five capabilities. initialize stores context.network and returns false when it is null. createSession returns a parented NavidromeSourceSession carrying the account and network manager.

NavidromeSourceSession inherits both interfaces and declares Q_OBJECT and Q_INTERFACES(IMusicSourceArtworkSession). The six methods must be declared in the existing v1 order; fetchArtwork satisfies both interfaces. Until later tasks implement the network methods, each method may asynchronously emit Unsupported.

- [ ] Step 4: Run the test to verify it passes

Run:

~~~bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/ctest --test-dir /private/tmp/quemusic-navidrome-build -R '^quemusic_navidrome_source_test$' --output-on-failure
~~~

Expected: descriptor and session contract tests pass.

- [ ] Step 5: Commit

~~~bash
git add CMakeLists.txt plugins/navidrome-source tests/tst_NavidromeSource.cpp
git commit -m "feat: scaffold Navidrome source plugin"
~~~

### Task 3: Add authenticated asynchronous HTTP infrastructure

Files:
- Modify: plugins/navidrome-source/NavidromeSourceSession.h
- Modify: plugins/navidrome-source/NavidromeSourceSession.cpp
- Modify: tests/tst_NavidromeSource.cpp

Interfaces:
- Consumes: configured SourceAccount and host QNetworkAccessManager from Task 2.
- Produces: private helpers startRequest(), finishSuccess(), finishFailure(), cancel(), and a request-ID-to-reply state map.

- [ ] Step 1: Write the failing authentication/request test

Add a QTcpServer fixture that accepts one request and responds with:

~~~json
{"subsonic-response":{"status":"ok","version":"1.16.1"}}
~~~

Add pingUsesSubsonicTokenAuthentication. It invokes the session connection method, waits for requestSucceeded, and checks that the captured request line contains /rest/ping.view, u=admin, v=1.16.1, c=QueMusic, f=json, s=, and t=, but not the plaintext password. The fixture calculates the expected token from the captured salt and verifies it equals md5("test-password" + salt).

- [ ] Step 2: Run the test to verify it fails

Run the focused Navidrome test. Expected: the request test fails because the skeleton does not issue a real request.

- [ ] Step 3: Implement the request state machine

Add a PendingRequest structure containing public request ID, operation name, endpoint stage, QNetworkReply pointer, and cancellation state. Build URLs by normalizing serverUrl to one /rest path segment and adding query items with QUrlQuery; never concatenate unescaped user data.

Use QCryptographicHash::Md5 over password plus salt, both as UTF-8 bytes. Set Accept: application/json, connect QNetworkReply::finished, remove state before emitting a terminal signal, and make cancel(requestId) abort and delete every reply associated with that public request ID. A cancelled request emits no terminal signal.

Expose QUuid ping() as a public method on the concrete NavidromeSourceSession; the generic v1 interface remains unchanged. The smoke executable and contract test may use the concrete session type, while host callers use the existing v1 methods. ping() uses the same request pipeline as all v1 operations.

- [ ] Step 4: Run the test to verify it passes

Run the focused Navidrome test again. Expected: authentication and cancellation assertions pass and no password appears in captured request text or diagnostics.

- [ ] Step 5: Commit

~~~bash
git add plugins/navidrome-source/NavidromeSourceSession.h plugins/navidrome-source/NavidromeSourceSession.cpp tests/tst_NavidromeSource.cpp
git commit -m "feat: add Navidrome authenticated request pipeline"
~~~

### Task 4: Implement endpoint mappings

Files:
- Modify: plugins/navidrome-source/NavidromeSourceSession.h
- Modify: plugins/navidrome-source/NavidromeSourceSession.cpp
- Modify: tests/tst_NavidromeSource.cpp

Interfaces:
- Consumes: Task 3 request pipeline and TrackRef, SearchQuery, BrowseQuery.
- Produces: normalized JSON result DTOs with items, track, url, mimeType, and lyrics fields.

- [ ] Step 1: Write failing endpoint mapping tests

Add these test slots:

~~~cpp
void searchMapsSongsAlbumsAndArtists();
void browseMapsRootAndDirectoryResponses();
void resolveStreamReturnsAuthenticatedStreamDto();
void artworkUsesOptionalInterfaceAndReturnsArtworkDto();
void lyricsFetchesSongMetadataThenLyrics();
void mapsSubsonicErrorsAndMalformedJson();
void cancelsLyricsSecondStageWithoutTerminalSignal();
~~~

Use fixture JSON containing one song, album, and artist with string IDs. Assert IDs remain strings and sourceId is navidrome. Root browse must request getIndexes; non-root browse must request getMusicDirectory?id=...; stream and artwork results must not expose the password. Lyrics must hold the getSong response until the second request arrives, then return one final signal with the original public request ID.

- [ ] Step 2: Run the tests to verify they fail

Run the focused Navidrome test. Expected: mapping tests fail because the methods still emit Unsupported.

- [ ] Step 3: Implement endpoint mapping

Implement exactly:

| Session method | Endpoint | Parameters | Result |
|---|---|---|---|
| search | search3.view | query, songCount, albumCount, artistCount | normalized items |
| browse root | getIndexes.view | optional musicFolderId | normalized items |
| browse path | getMusicDirectory.view | id | normalized items |
| resolveStream | stream.view | id | stream DTO |
| fetchArtwork | getCoverArt.view | id | artwork DTO |
| fetchLyrics | getSong.view then getLyrics.view | id, then artist/title | lyrics DTO |

Parse only successful subsonic-response objects. Map error codes 40 and 41 to Authentication, 50 to Authorization, and 70 to NotFound. Map network failures to Network, malformed JSON or missing expected objects to InvalidRequest, and preserve HTTP status in SourceError.

Use a session-local metadata cache keyed by native ID to avoid repeating getSong when search or browse already supplied artist/title. The cache must not contain passwords or tokens.

fetchArtwork reached through either interface must use the same implementation. SourceManager::requestArtwork already prefers the optional interface for a plugin that advertises Artwork.

- [ ] Step 4: Run the tests to verify they pass

Run:

~~~bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/ctest --test-dir /private/tmp/quemusic-navidrome-build -R '^quemusic_navidrome_source_test$' --output-on-failure
~~~

Expected: all Navidrome mapping, error, and cancellation tests pass.

- [ ] Step 5: Commit

~~~bash
git add plugins/navidrome-source tests/tst_NavidromeSource.cpp
git commit -m "feat: implement Navidrome source operations"
~~~

### Task 5: Integrate plugin loading and root build targets

Files:
- Modify: CMakeLists.txt to add the Navidrome target to the normal application build and expose its output directory to tests.
- Modify: tests/tst_NavidromeSource.cpp
- Modify: tests/tst_PluginStartup.cpp
- Modify: docs/PLUGIN_API.md

Interfaces:
- Consumes: completed plugin target and SourceManager startup boundary.
- Produces: a root-build plugin in bin/plugins/source and startup coverage proving the Navidrome module loads.

- [ ] Step 1: Write the failing integration test

Add a startup test that loads the built Navidrome plugin directory and asserts sourceIds() contains navidrome. Add a SourceManager test that creates a session with a configured account and reaches the plugin request pipeline.

- [ ] Step 2: Run the test to verify it fails

Run:

~~~bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/cmake --build /private/tmp/quemusic-navidrome-build --target quemusic_plugin_startup_test
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/ctest --test-dir /private/tmp/quemusic-navidrome-build -R 'quemusic_plugin_startup_test|quemusic_source_manager_test' --output-on-failure
~~~

Expected: the startup test cannot find the Navidrome module until CMake integration and output dependencies are added.

- [ ] Step 3: Add CMake integration

Add add_subdirectory(plugins/navidrome-source) after the source SDK targets and outside the BUILD_TESTING block, so the normal application build bundles the module. Set its module output to the build plugins/source directory on all platforms. Pass that directory to tests using QUEMUSIC_TEST_SOURCE_PLUGIN_DIR and add a dependency on the Navidrome target.

Do not add the private Navidrome address or credentials to CMake cache entries.

- [ ] Step 4: Run the tests to verify they pass

Run the startup and source-manager tests again. Expected: the module loads, source ID is present, and existing plugin contract tests remain green.

- [ ] Step 5: Commit

~~~bash
git add CMakeLists.txt tests/tst_NavidromeSource.cpp tests/tst_PluginStartup.cpp docs/PLUGIN_API.md
git commit -m "test: integrate Navidrome plugin loading"
~~~

### Task 6: Add the opt-in real-server smoke test

Files:
- Create: tests/NavidromeSmoke.cpp
- Modify: CMakeLists.txt
- Create: docs/superpowers/runbooks/navidrome-smoke-test.md

Interfaces:
- Consumes: completed Navidrome plugin and SourceManager session API.
- Produces: executable quemusic_navidrome_smoke, built but not registered with CTest.

- [ ] Step 1: Define the smoke contract

The executable reads QUEMUSIC_NAVIDROME_URL, QUEMUSIC_NAVIDROME_USER, and QUEMUSIC_NAVIDROME_PASSWORD, and exits with a usage error if any is missing.

It calls ping, one search, root browse, stream resolution, artwork, and lyrics in sequence. It prints only operation name, success/failure, error kind, and elapsed milliseconds. It never prints environment values, request URLs, headers, tokens, salts, or response bodies.

- [ ] Step 2: Implement and build the smoke executable

Add a CMake executable linked to the source SDK, source manager, Qt Network, and Navidrome session implementation. Do not call add_test() for this target. Construct SourceAccount from environment only in process memory and clear password bytes during teardown.

- [ ] Step 3: Run a redacted local smoke test

The operator must set the three environment variables from a private local source without putting their values in the repository:

~~~bash
export QUEMUSIC_NAVIDROME_URL
export QUEMUSIC_NAVIDROME_USER
export QUEMUSIC_NAVIDROME_PASSWORD
/private/tmp/quemusic-navidrome-build/bin/quemusic_navidrome_smoke
~~~

Expected: every operation reports success or a precise server-side result without exposing credentials. If the server has no lyrics or artwork for the selected track, report that protocol result separately from reachability.

- [ ] Step 4: Commit

~~~bash
git add CMakeLists.txt tests/NavidromeSmoke.cpp docs/superpowers/runbooks/navidrome-smoke-test.md
git commit -m "test: add opt-in Navidrome smoke test"
~~~

### Task 7: Final documentation and full verification

Files:
- Modify: docs/PLUGIN_API.md
- Modify: docs/superpowers/specs/2026-08-28-navidrome-source-plugin-design.md only for implementation notes that remain accurate

Interfaces:
- Consumes: all completed plugin behavior and smoke-test command.
- Produces: user-facing setup instructions with no embedded private credentials.

- [ ] Step 1: Document account configuration and capabilities

Document exact SourceAccount keys, Subsonic token policy, capability behavior, simulated browse semantics, cancellation contract, and the opt-in smoke command with placeholders rather than real credentials.

- [ ] Step 2: Configure a clean root build

Run:

~~~bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/cmake -S . -B /private/tmp/quemusic-navidrome-build -DBUILD_TESTING=ON -DCMAKE_PREFIX_PATH=/Users/liqiang/Qt/6.11.1/macos
~~~

Expected: configuration completes with the existing qwindowkit and Crypto++ submodules available.

- [ ] Step 3: Build the complete project

Run:

~~~bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/cmake --build /private/tmp/quemusic-navidrome-build --parallel 4
~~~

Expected: QueMusic, qwindowkit, Crypto++, TagLib, QCloudMusicApi, Navidrome plugin, and tests all build successfully.

- [ ] Step 4: Run the complete test suite

Run:

~~~bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/ctest --test-dir /private/tmp/quemusic-navidrome-build --output-on-failure
~~~

Expected: zero failed tests. The opt-in smoke executable is not part of CTest.

- [ ] Step 5: Check repository safety

Run:

~~~bash
git diff --check
git status --short --branch
git grep -n -I '192\\.168\\.188\\.193' -- sdk plugins tests CMakeLists.txt docs/PLUGIN_API.md README.md
~~~

Expected: no private address or credential appears in tracked source/test files, and only intentional changes are present.

- [ ] Step 6: Commit final documentation

~~~bash
git add docs/PLUGIN_API.md docs/superpowers/specs/2026-08-28-navidrome-source-plugin-design.md
git commit -m "docs: document Navidrome source setup"
~~~
