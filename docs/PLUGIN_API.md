# Plugin API

QueMusic source plugins are native Qt plugins loaded at application startup by
`PluginManager` and adapted by `SourceManager`. This document defines the
current package format and ABI boundary for music source providers.

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
    { "id": "org.quemusic.MusicSourcePlugin/1.0", "version": "1.0" }
  ],
  "runtimeRequirements": { "qtMajor": 6 }
}
```

`id`, `sourceId`, `name`, `version`, `category`, `runtime`, `library`,
`pluginApi`, and the source interface declaration are required. `pluginApi`
must be an object containing numeric integer `major` and `minHostMinor` fields;
the latter must be non-negative. `runtimeRequirements` may be absent, but when
present it must be an object: `qtMajor` is a numeric integer greater than or
equal to `1`, and `architecture` and `buildMode` are non-empty strings whenever
present. `library` is a
relative filename inside the package: absolute paths, `.` and `..` path
components, and directories are rejected.

The host validates this metadata before creating a `QPluginLoader` instance.
Plugin API major must be `1`; `minHostMinor` must not exceed the host's current
minor (`0`). Qt major, architecture, and build mode must match whenever the
package declares them. `QPluginLoader` remains the final native binary and ABI
compatibility gate.

## ABI and IID

- Interface: `IMusicSourcePlugin`
- Session base class: `IMusicSourceSession`
- Optional artwork interface: `IMusicSourceArtworkSession`
- Plugin IID: `org.quemusic.MusicSourcePlugin/1.0`
- Artwork IID: `org.quemusic.MusicSourceArtworkSession/1.0`
- SDK version field: `SourceDescriptor::sdkVersion` (exactly `1.0` for IID 1.0)

Plugins must be built against the same Qt major version and the QueMusic source
SDK headers shipped in `sdk/source`. IID 1.0 has one authoritative compatibility
policy: `sdkVersion` must exactly equal `1.0`; empty, older, newer, or differently
formatted values are rejected before `initialize()` runs. A future compatible
version must be added intentionally to the host policy with tests. A plugin is
also rejected if it does not expose the required IID or has an empty source id
or display name. Packages with duplicate manifest `sourceId` values are
rejected during discovery, before a loader is created for the duplicate.

`IMusicSourceSession` is already published with this exact virtual order:
`search`, `browse`, `resolveStream`, `fetchArtwork`, `fetchLyrics`, `cancel`.
The legacy `fetchArtwork(const TrackRef &)` slot is retained permanently for
`MusicSourcePlugin/1.0` binary compatibility. `IMusicSourceArtworkSession` does
not replace or remove that slot; it is an additive, independently versioned
interface for explicit runtime capability discovery.

## Required Metadata

Each plugin must implement `IMusicSourcePlugin::descriptor()` and return a
`SourceDescriptor` with:

- `id`: stable unique identifier for the source
- `name`: user-facing display name
- `version`: plugin implementation version
- `protocol`: provider family or protocol label such as `subsonic`
- `sdkVersion`: the plugin SDK contract version it targets
- `capabilities`: a `SourceCapabilities` bitmask

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
`load()` creates a compatible Qt plugin instance, and `SourceManager` then
validates its descriptor, initializes it, and adds its source ID to the source
registry. Package discovery and lifecycle remain C++-only implementation
details for the unified media bridge.

The manager's internal package records contain `id`, `sourceId`, `name`,
`version`, `category`, `state`, `error`, `path`, `activeLeases`, `loadable`,
`unloadable`, and `reloadable`. They are not exposed as raw QML plugin objects.
States are `discovered`, `loaded`, `failed`, and `unloaded`.

Each source session owns a package lease. Unload and reload return Busy while
any session from that package remains alive, and the Settings controls are
disabled in that state. When an unused source package unloads, `SourceManager`
removes the corresponding source registry entry before it can be used again.
If unload fails, the Failed package retains its loader, cannot load a second
instance, and exposes an enabled unload retry when no lease is active.
Reloading creates a new plugin instance and reinitializes the source entry.
This is a trusted-native-code lifecycle only; there is no sandbox, signature,
permission, or cross-version ABI isolation.

## Unified Media Bridge and QML boundary

`MediaBridge` is the only QML-facing boundary for plugin-backed media. QML
must never receive `PluginManager`, `SourceManager`, `SourceSessionRegistry`,
`IMusicSourceSession`, raw plugin `QObject`s, raw provider JSON, credentials,
or secret references. It receives fixed-role `MediaListModel` rows only:
`sourceId`, `accountId`, `nativeId`, `kind`, `title`, `subtitle`, `artists`,
`albumTitle`, `durationMs`, `artworkUrl`, `playable`, `container`, and the
non-sensitive `extra` map. These roles are a host contract; source-specific
payload fields must be normalized before crossing it.

`mediaBridge.accountController` is the narrow account-management facade. It
may expose the source/account identifiers, display name, enabled state,
server URL, username, and loaded-source descriptors needed by the UI. It must
not return a password, token, secret, secret reference, authenticated URL, or
provider response.

Bridge playback is routed through `QueueWiring`. The component allowlists the
normalized queue fields and preserves the bridge flag plus serialized
`MediaId`; it then calls `MediaBridge::play()` so each bridge item resolves a
fresh stream before playback. `QueueWiring` must not pass provider payloads,
stream URLs, request headers, or source/session objects through QML.

The current QML `MediaPlayer` path supports URL-authenticated streams only.
If a resolved stream contains any request headers, `MediaBridge` fails that
playback action with `Unsupported`; it does not silently discard headers or
attempt unauthenticated playback. A source that requires headers needs a
native playback backend before it can be played.

## Deferred JavaScript runtime

JavaScript packages are designed but deliberately not implemented. Do not add
an engine, JS package loader, SDK, or runtime dependency until the project
owner explicitly requests it. The frozen design is in
[`2026-08-29-js-plugin-runtime-design.md`](superpowers/specs/2026-08-29-js-plugin-runtime-design.md).

## Navidrome source configuration

The bundled `navidrome` source plugin uses the Subsonic-compatible Navidrome
API. Construct its `SourceAccount` with these plugin-defined fields:

| Field | Required value |
| --- | --- |
| `sourceId` | `navidrome` |
| `parameters[serverUrl]` | Absolute `http` or `https` server URL; a trailing `/rest` is accepted and normalized. |
| `parameters[username]` | Subsonic username. |
| `secret` | UTF-8 password bytes supplied to the session in process memory. Persisted account secrets are stored only in the platform keychain. |

The plugin advertises `Search`, `Browse`, `StreamAudio`, `Artwork`, and
`Lyrics`. Root browse is Navidrome's simulated tag-based view (`getIndexes`),
while non-root browse uses the server's simulated music-directory endpoint;
neither is a physical NAS filesystem browser. Stream and artwork operations
return authenticated URLs. Those URLs can contain short-lived token material,
so callers must not log or persist them.

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

Navidrome is the first provider using the native source-plugin and
`MediaBridge` path. Existing NetEase, Kugou, QQ, and local-file behavior stays
behind `MusicApiService` until that individual provider has a complete bridge
adapter or native source plugin: normalized search/browse results, account and
session lifecycle, artwork/lyrics/stream handling, queue re-resolution, and
regression coverage. This milestone does not remove or bulk-migrate legacy
providers.

## Capability Rules

Capabilities advertise what a session can do. They are declarative and should
match real behavior. Current flags come from `SourceCapability`:

- `Search`
- `Browse`
- `StreamAudio`
- `StreamVideo`
- `Artwork`
- `Lyrics`
- `PlaylistRead`
- `PlaylistWrite`
- `Favorites`
- `Download`
- `Scrobble`

Do not claim a capability unless the session can perform that operation and emit
either `requestSucceeded` or `requestFailed` for it.

Artwork dispatch is metadata-gated and follows this policy:

- Without `SourceCapability::Artwork`, the host does not call either artwork
  interface, even if the session exposes `IMusicSourceArtworkSession`.
- With `SourceCapability::Artwork`, the host prefers
  `IMusicSourceArtworkSession::fetchArtwork` when that interface is present.
- A v1 session that advertises Artwork but does not expose the optional
  interface remains supported through the retained
  `IMusicSourceSession::fetchArtwork` slot.
- Newly written plugins that advertise Artwork should implement
  `IMusicSourceArtworkSession`, declare it with `Q_INTERFACES`, and keep the
  required v1 base override. One `fetchArtwork` override satisfies both
  interfaces when their signatures match.

## Trusted Native-Code Model

Source plugins are trusted native code loaded into the QueMusic process. They
have the same process privileges as the application, can use the provided
network access manager, and can crash or compromise the process if malicious or
incorrect. There is currently no sandbox, signature validation, permission
prompt, or ABI isolation layer. Only install plugins from trusted sources.

## Runtime Contract

- `initialize(SourcePluginContext &context)` runs once when the plugin is loaded.
- `createSession(const SourceAccount &, QObject *parent)` creates a per-account
  session owned by the provided parent.
- `SourceAccount` keeps its identity fields (`sourceId`, `accountId`, and
  `displayName`) first, then provides plugin-defined connection data through
  `parameters` (`QVariantMap`) and sensitive bytes through `secret`
  (`QByteArray`). Plugins must document the parameter keys they accept and must
  never log `secret` or derived credentials.
- `IMusicSourceSession` handles async provider work and must implement:
  `search`, `browse`, `resolveStream`, the retained legacy `fetchArtwork`,
  `fetchLyrics`, and `cancel`, in that virtual order.
- `IMusicSourceArtworkSession` is optional. Sessions that advertise
  `SourceCapability::Artwork` should also implement
  `org.quemusic.MusicSourceArtworkSession/1.0`, declare
  `Q_INTERFACES(IMusicSourceArtworkSession)`, and be discoverable with
  `qobject_cast<IMusicSourceArtworkSession *>(session)`.
- A session reports results with `requestSucceeded`, failures with
  `requestFailed`, and auth state changes with `authenticationChanged`.
- Every advertised capability must complete a non-cancelled request. For this
  contract, `StreamAudio` completes `resolveStream` with a JSON
  `StreamDescriptor` DTO (`track`, `url`, optional `headers`, `mimeType`,
  `expiresAt`, `video`, and `seekable`). `Artwork` pairs the capability flag
  with optional-interface dispatch when available and the retained v1 base
  fallback otherwise; either path completes with a JSON DTO containing
  `track`, `url`, and `mimeType`. Unsupported operations emit `requestFailed`
  with `SourceErrorKind::Unsupported`.

## Minimal Plugin Skeleton

The skeleton is declaration-only: method bodies are intentionally omitted.
Every real request method must return a request ID and later, asynchronously,
emit exactly one matching `requestSucceeded` or `requestFailed` signal unless
that request was cancelled. Returning a fresh UUID without scheduling a terminal
signal violates the contract. `descriptor()` must include
`SourceCapability::Artwork` when using the artwork interface below.

```cpp
#include "IMusicSourceArtworkSession.h"
#include "IMusicSourcePlugin.h"

class ExampleSession final : public IMusicSourceSession,
                             public IMusicSourceArtworkSession {
    Q_OBJECT
    Q_INTERFACES(IMusicSourceArtworkSession)

public:
    using IMusicSourceSession::IMusicSourceSession;

    QUuid search(const SearchQuery &) override;
    QUuid browse(const BrowseQuery &) override;
    QUuid resolveStream(const TrackRef &) override;
    QUuid fetchArtwork(const TrackRef &) override;
    QUuid fetchLyrics(const TrackRef &) override;
    void cancel(const QUuid &) override;
};

class ExampleSourcePlugin final : public QObject, public IMusicSourcePlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QUEMUSIC_MUSIC_SOURCE_PLUGIN_IID)
    Q_INTERFACES(IMusicSourcePlugin)

public:
    SourceDescriptor descriptor() const override;
    bool initialize(SourcePluginContext &context) override;
    IMusicSourceSession *createSession(const SourceAccount &account,
                                       QObject *parent) override;
};
```
