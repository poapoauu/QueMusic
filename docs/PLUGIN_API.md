# Plugin API

QueMusic source plugins are native Qt plugins loaded at application startup by
`PluginManager` and opened through `SourceRegistry`. This document defines the
current v2 package format and ABI boundary for music source providers.

## Native package format

A native plugin is a directory, one level below a plugin root. It contains a
`manifest.json` and the shared library named by the manifest:

```text
plugins/
  navidrome/
    manifest.json
    libquemusic_navidrome_source.dylib
```

The current runtime accepts `source` packages with `runtime` set to
`native-qt`. Other category names are reserved for future lyrics, MV, GUI, and
language plugin APIs; they cannot be loaded yet.

```json
{
  "id": "org.quemusic.source.example",
  "sourceId": "example",
  "name": "Example Source",
  "version": "1.0.0",
  "category": "source",
  "runtime": "native-qt",
  "library": "libquemusic_example_source.dylib",
  "pluginApi": { "major": 1, "minHostMinor": 0 },
  "interfaces": [
    { "id": "org.quemusic.MusicSourcePlugin/2.0", "version": "2.0" }
  ],
  "runtimeRequirements": {
    "sourceSdkAbi": 2,
    "qtMajor": 6,
    "architecture": "x86_64",
    "buildKey": "Release"
  }
}
```

`id`, `sourceId`, `name`, `version`, `category`, `runtime`, `library`,
`pluginApi`, the source interface declaration, and `runtimeRequirements` are
required. `pluginApi`
must be an object containing numeric integer `major` and `minHostMinor` fields;
the latter must be non-negative. For v2, `runtimeRequirements` must contain
`sourceSdkAbi` equal to `2`, a positive integer `qtMajor`, and non-empty
`architecture` and `buildKey` strings. `library` is a
relative filename inside the package: absolute paths, `.` and `..` path
components, and directories are rejected.

The host validates this metadata before creating a `QPluginLoader` instance.
Plugin API major must be `1`; `minHostMinor` must not exceed the host's current
minor (`0`). Qt major, architecture, and build mode must match whenever the
package declares them. `QPluginLoader` remains the final native binary and ABI
compatibility gate.

## ABI and IID

- Plugin interface: `IMusicSourcePluginV2`
- Session base class: `IMusicSourceSessionV2`
- Plugin IID: `org.quemusic.MusicSourcePlugin/2.0`
- Required manifest ABI: `runtimeRequirements.sourceSdkAbi = 2`
- Optional operation interfaces: the provider interfaces in
  `sdk/source/v2/ISourceProvidersV2.h`

Plugins must be built against the same Qt major version, build key and target
architecture as the host, and against the v2 headers shipped in
`sdk/source/v2`. The host rejects a package before execution when its manifest,
IID, SDK ABI, architecture or build key is incompatible. Native Qt plugins do
not have cross-Qt or general C++ binary compatibility; rebuild them whenever
the host toolchain or SDK ABI changes.

## Required Metadata

Each plugin must implement `IMusicSourcePluginV2::descriptor()` and return a
`SourceDescriptorV2` with:

- `sourceId`: stable unique identifier for the source
- `name`: user-facing display name
- `version`: plugin implementation version
- `pluginPackageId`: manifest package identifier
- `sdkAbi`: exactly `2`
- `declaredActions`: conservative package-level action availability

## Search Paths

At startup QueMusic scans package directories from these roots:

- macOS application bundle: `QueMusic.app/Contents/PlugIns/quemusic`
- development build fallback: `QCoreApplication::applicationDirPath()/../plugins`
- user packages: `QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)/plugins`

When built from the root CMake project, bundled source package outputs remain
below `<build>/plugins`; this matches the development fallback when the
application runs from `<build>/bin`. On macOS, building `QueMusic`, or the
`quemusic_navidrome_bundle_plugin` deployment target, copies the bundled
Navidrome source library and generated manifest into
`QueMusic.app/Contents/PlugIns/quemusic/navidrome`.

The application directory is intended for bundled plugins. The app-data
directory is intended for user-installed plugins on the local machine.

## Discovery, lifecycle, and Settings

`PluginManager::discover()` reads manifests without executing plugin code.
`load()` creates a compatible Qt plugin instance. `SourceRegistry` validates
the v2 descriptor and creates one session per enabled source account. Package
discovery and session lifecycle remain C++ implementation details.

The manager's internal package records contain `id`, `sourceId`, `name`,
`version`, `category`, `state`, `error`, `path`, `activeLeases`, `loadable`,
`unloadable`, and `reloadable`. They are not exposed as raw QML plugin objects.
States are `discovered`, `loaded`, `failed`, and `unloaded`.

Each source session owns a package lease. Unload and reload return Busy while
any session from that package remains alive, and the Settings controls are
disabled in that state. Before unloading a package, `SourceRegistry` closes
and cancels requests, waits for terminal callbacks to drain, deletes sessions,
releases leases, destroys QML-facing models/controllers, and only then asks
`PluginManager` to unload the native library. A library must never unload while
any session, request, lease, provider pointer, or QML object can refer to it.
If unload fails, the Failed package retains its loader, cannot load a second
instance, and exposes an enabled unload retry when no lease is active.
Reloading creates a new plugin instance and reinitializes the source entry.
This is a trusted-native-code lifecycle only; there is no sandbox, signature,
permission, or cross-version ABI isolation.

## Music Hub and QML boundary

Source plugins feed the existing Recommendation, Category, Favorites and
Search pages; there is no separate source-library page. `MusicHub` exposes
page models and aggregate/single-source scope selection. `PlaybackCoordinator`
resolves a fresh stream and hands it to the native `QtPlaybackController`.
`PluginSettingsController` is the only settings facade. Operation buttons are
shown or enabled from the intersected package, server, account and item action
availability, so one source can never imply support for another source.
Plugin settings UI is generated only from `SettingsSchemaV2`; plugins cannot
provide executable QML or arbitrary UI. Native playlist mutation is restricted
to tracks from the playlist's own source instance. Cross-source playlists are
a host concern and are never forwarded as provider-native mutations.

QML must not receive source sessions, provider interfaces, raw plugin JSON,
credentials, secrets, secret references or authenticated provider responses.
All page data crosses the boundary as normalized `MediaItemV2` and
`PageSectionV2` data. Stream headers remain in the native playback path and
must never be copied into QML models or persisted.

## Deferred JavaScript runtime

JavaScript packages are designed but deliberately not implemented. Do not add
an engine, JS package loader, SDK, or runtime dependency until the project
owner explicitly requests it. The frozen design is in
[`2026-08-29-js-plugin-runtime-design.md`](superpowers/specs/2026-08-29-js-plugin-runtime-design.md).

## Navidrome source configuration

The bundled `navidrome` source plugin uses the Subsonic-compatible Navidrome
API. Its `SourceConfigurationV2` uses these plugin-defined fields:

| Field | Required value |
| --- | --- |
| `sourceId` | `navidrome` |
| `parameters[serverUrl]` | Absolute `http` or `https` server URL; a trailing `/rest` is accepted and normalized. |
| `parameters[username]` | Subsonic username. |
| `secret` | UTF-8 password bytes supplied to the session in process memory. Persisted account secrets are stored only in the platform keychain. |

The plugin exposes recommendation/category/favorites/search pages and optional
playback, artwork, lyrics, download, favorite, rating, scrobble, playlist,
play-queue, and bookmark providers. Stream and artwork operations may use
authenticated URLs containing short-lived token material, so URLs, headers,
passwords, salts, tokens, secret references, response bodies, and provider
diagnostics must be redacted from logs and must not be persisted or exposed to QML.

Every non-cancelled request emits one terminal result with its original request
ID. `cancel()` suppresses terminal signals and aborts any in-flight reply,
including the second request used by lyrics lookup. Subsonic token
authentication uses a fresh salt and `md5(password + salt)` per request; the
password, token, salt, complete authenticated URLs, and response bodies must
never be logged.

For a private-server check, build the `quemusic_navidrome_smoke` executable,
then set `QUEMUSIC_NAVIDROME_URL`, `QUEMUSIC_NAVIDROME_USER`, and
`QUEMUSIC_NAVIDROME_PASSWORD` in your own shell before running it. The command
is opt-in and is not part of CTest. It prints only operation, outcome, error
kind, and elapsed time; see
[`navidrome-smoke-test.md`](superpowers/runbooks/navidrome-smoke-test.md).

On macOS, `SourceAccountStore` writes only allowlisted non-sensitive metadata
and a keychain reference to QSettings; the secret itself is stored through
Security.framework. On unsupported platforms secret persistence explicitly
reports unavailable and must never fall back to plaintext settings storage.

## Legacy provider migration boundary

Navidrome is the first v2 native source plugin. NetEase, Kugou, QQ and local
files must each gain a v2 adapter or native plugin before joining aggregate
pages. New UI work must use only the v2 music-hub boundary.

## Capability Rules

Actions advertise what a session can do and are fail-closed. Current keys come
from `SourceActionV2`:

- `Play`, `Artwork`, `Lyrics`, `Download`
- `Favorite`, `Unfavorite`, `Rating`, `Scrobble`
- `CreatePlaylist`, `UpdatePlaylist`, `DeletePlaylist`
- `AddPlaylistTracks`, `RemovePlaylistTracks`
- `FetchPlayQueue`, `SavePlayQueue`
- `FetchBookmarks`, `CreateBookmark`, `DeleteBookmark`

Do not mark an action `Available` unless the matching optional provider
interface exists and can complete it. Effective availability is the
intersection of four layers: descriptor declaration, negotiated server support,
account authorization, and item-specific availability. Unknown constraints,
missing layers, and missing interfaces fail closed as unsupported.
`Unsupported`, `Unavailable`, and `Forbidden` are distinct UI states.

## Trusted Native-Code Model

Source plugins are trusted native code loaded into the QueMusic process. They
have the same process privileges as the application, can use the provided
network access manager, and can crash or compromise the process if malicious or
incorrect. There is currently no sandbox, signature validation, permission
prompt, or ABI isolation layer. Only install plugins from trusted sources.

## Runtime Contract

- `createSession(const SourceConfigurationV2 &, QObject *parent)` creates a per-account
  session owned by the provided parent.
- `SourceConfigurationV2` keeps its identity fields (`sourceId`, `sourceInstanceId`,
  `accountId`, and
  `displayName`) first, then provides plugin-defined connection data through
  `parameters` (`QVariantMap`) and sensitive bytes through `secret`
  (`QByteArray`). Plugins must document the parameter keys they accept and must
  never log `secret` or derived credentials.
- `IMusicSourceSessionV2` owns lifecycle (`open`, `close`, `cancel`) and state.
- Page, playback, favorites, rating, scrobble, playlist, download, play-queue,
  bookmark and settings behavior is exposed only through the matching optional
  provider interface.
- Every asynchronous call emits `requestStarted` before exactly one terminal
  `pageReady`, `streamReady`, `actionCompleted`, `settingsActionCompleted` or
  `requestFailed`, unless cancelled.

## Minimal Plugin Skeleton

The skeleton is declaration-only: method bodies are intentionally omitted.
Every real request method must return a request ID and later, asynchronously,
emit `requestStarted` and exactly one matching terminal signal unless that
request was cancelled. Returning a fresh UUID without scheduling those signals
violates the contract. `descriptor()` and runtime capabilities must mark only
the actions implemented by the provider interfaces below as available.

```cpp
#include "v2/IMusicSourcePluginV2.h"
#include "v2/ISourceProvidersV2.h"

class ExampleSession final : public IMusicSourceSessionV2,
                             public IPageProviderV2,
                             public IPlaybackProviderV2 {
    Q_OBJECT
    Q_INTERFACES(IPageProviderV2 IPlaybackProviderV2)

public:
    SourceIdentityV2 identity() const override;
    SourceSessionStateV2 state() const override;
    CapabilitySetV2 capabilities() const override;
    QUuid open() override;
    void close() override;
    void cancel(const QUuid &) override;
    QUuid fetchPage(const PageQueryV2 &) override;
    QUuid resolveStream(const MediaRefV2 &) override;
    QUuid fetchArtwork(const MediaRefV2 &) override;
    QUuid fetchLyrics(const MediaRefV2 &) override;
};

class ExampleSourcePlugin final : public QObject, public IMusicSourcePluginV2 {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QUEMUSIC_MUSIC_SOURCE_PLUGIN_V2_IID)
    Q_INTERFACES(IMusicSourcePluginV2)

public:
    SourceDescriptorV2 descriptor() const override;
    IMusicSourceSessionV2 *createSession(const SourceConfigurationV2 &configuration,
                                         QObject *parent) override;
};
```
