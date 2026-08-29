# QueMusic NAS-first Plugin Architecture

## Status

Draft for review.

## 1. Goal

Evolve QueMusic into a NAS-first desktop music client whose music sources are
replaceable Qt plugins. The first target is a stable local/NAS playback flow;
Kugou and NetEase Cloud Music should eventually use the same source boundary as
Navidrome and other NAS services.

The design reuses the plugin and playback boundaries already implemented in the
author's `soundwave` project, while adapting its Songloft-specific application
and data code to QueMusic's domain.

## 2. Current context

QueMusic currently exposes most online-source behavior through
`MusicApiService`, with direct Kugou, NetEase Cloud Music, and legacy JavaScript
implementations. Local playback is driven from QML through Qt Multimedia.

The reusable `soundwave` seams are:

- `PluginManager` and its persistent `QPluginLoader` lifecycle;
- `IAppPlugin`, `IPagePlugin`, `IPlaybackPlugin`, and `PluginContext`;
- `IPlaybackEngine`, `PlaybackEngineManager`, and `PlaybackEngineFacade`;
- Repository → Application → ViewModel layering and its QtTest coverage.

`soundwave`'s Songloft API client, authentication model, domain `Song` type,
QML pages, and Songloft-specific endpoints are not portable as-is.

## 3. Goals and non-goals

### Goals

- Load trusted music-source plugins at runtime on macOS, Windows, and Linux.
- Present one normalized catalog/playback model to QML.
- Support per-source capabilities instead of assuming every source supports
  every feature.
- Make Navidrome/OpenSubsonic the first NAS source plugin.
- Migrate Kugou and NetEase behind the same source boundary without changing
  the user-facing player flow.
- Preserve a replaceable playback-engine boundary so Qt Multimedia and a future
  MPV engine can coexist during migration.
- Keep authentication, network access, caching, and error mapping out of QML.

### Non-goals for the first milestone

- No plugin marketplace or remote plugin installation.
- No arbitrary plugin-provided QML replacing core pages.
- No automatic execution of untrusted plugin binaries.
- No MV implementation in the Navidrome plugin; video is an optional capability.
- No immediate rewrite of every existing `MusicApiService` endpoint.
- No forced adoption of QCoro or C++20 in the existing QueMusic build until the
  compatibility cost is measured.

## 4. Architecture

```text
QML
  ↓
Presentation ViewModels
  ↓
Application use cases
  ├── SourceManager / SourceSession
  ├── PlaybackController
  ├── Cache / Download / Lyrics services
  └── Unified domain models
  ↓
Source SDK contracts                 Playback SDK contracts
  ↓                                   ↓
Local / Navidrome / Feiniu /         Qt Multimedia / MPV / future engines
Kugou / NetEase plugins
```

The core owns queue behavior, source selection, caching, playback state, and
the UI-facing models. A source plugin owns only protocol-specific work:
authentication, catalog queries, metadata mapping, stream resolution, and
optional source features.

The two plugin categories remain separate:

- `IMusicSourcePlugin`: where the media comes from;
- `IPlaybackPlugin`: how a resolved `MediaSource` is decoded and played.

## 5. Music-source plugin contract

The new SDK should live in a small, dependency-light public contract area,
separate from `MusicApiService` and QML. The initial contract is conceptually:

```text
IMusicSourcePlugin
├── descriptor()
├── capabilities()
└── createSession(SourceAccount, SourceContext)

IMusicSourceSession
├── connect / disconnect / authenticate
├── search(SearchQuery)
├── browse(BrowseQuery)
├── resolveStream(TrackRef)
├── fetchArtwork(TrackRef) [retained MusicSourcePlugin/1.0 ABI slot]
├── fetchLyrics(TrackRef)
├── read/write playlists when supported
└── fetchVideo(TrackRef) when supported

Optional capability interfaces are additive and do not remove or reorder the
published base vtable. `IMusicSourceArtworkSession` repeats
`fetchArtwork(const TrackRef &)` under its own IID for explicit capability
discovery. Other new optional capabilities follow the same independently
versioned pattern.
```

The exact C++ declarations will be chosen during implementation, but the
following constraints are fixed:

- The interface is versioned with a Qt IID such as
  `org.quemusic.MusicSourcePlugin/1.0`.
- Plugin metadata declares a stable source ID, display name, version, protocol,
  supported capabilities, and minimum SDK version.
- Request results use request IDs and asynchronous signals. Plugins must not
  block the GUI thread.
- Transport and metadata payloads use stable SDK types or JSON DTOs, not
  private QueMusic models or QML objects.
- `TrackRef` contains both `sourceId` and provider-native ID.
- `StreamDescriptor` may contain URL, headers, MIME type, expiry, seekability,
  and media kind; a stream is not represented by a URL alone.
- Source errors are normalized into categories such as authentication,
  network, permission, not-found, rate-limit, unsupported, and parse failure.

### ABI compatibility decision

The published `IMusicSourceSession` contract for
`org.quemusic.MusicSourcePlugin/1.0` has the virtual order `search`, `browse`,
`resolveStream`, `fetchArtwork`, `fetchLyrics`, `cancel`. That order is
immutable. In particular, the legacy `fetchArtwork(const TrackRef &)` slot is
retained at its original position between `resolveStream` and `fetchLyrics`;
removing it while retaining IID 1.0 would break existing plugin binaries.

`IMusicSourceArtworkSession`, IID
`org.quemusic.MusicSourceArtworkSession/1.0`, is added alongside the base
session for new capability discovery. A newly written Artwork provider should
inherit both interfaces and declare `Q_INTERFACES(IMusicSourceArtworkSession)`;
one matching `fetchArtwork` override satisfies both contracts.

The host gates all artwork calls on `SourceCapability::Artwork`. If the flag is
absent, it does not use an optional artwork interface even when one is exposed.
If the flag is present, it prefers the optional interface and otherwise falls
back to the retained v1 base slot so legacy Artwork-advertising plugins remain
usable. This preserves binary compatibility while allowing the optional
interface to evolve independently. Any breaking change to the base source or
playback contracts requires a new major IID; old IID semantics are never
silently changed.

### Capabilities

The first capability set is:

```text
Search
Browse
StreamAudio
StreamVideo
Artwork
Lyrics
PlaylistRead
PlaylistWrite
Favorites
Download
Scrobble
```

The UI checks capabilities exposed by the active source. It must not branch on
provider names such as `sourceId == "navidrome"` for normal feature behavior.

## 6. Plugin loading and lifecycle

`SourceManager` will adapt the `soundwave` `PluginManager` pattern:

1. Discover plugins in the application plugin directory and user plugin
   directory.
2. Inspect Qt metadata before loading when possible.
3. Verify IID, SDK version, source ID, and duplicate IDs.
4. Load the plugin with `QPluginLoader` and retain the loader until application
   exit.
5. Register descriptors and capabilities.
6. Isolate load/initialization failures so one broken plugin does not prevent
   the application from starting.

Plugins are trusted native code and are not sandboxed. The first release will
ship built-in plugins and document manual installation; remote installation
and signature verification are separate future work.

The host and plugins must use a compatible Qt/toolchain ABI. The SDK IID gets a
new major version for breaking changes; old IID semantics are not silently
changed.

## 7. Authentication and host services

Credentials and tokens belong to the host account/session layer, not QML.
Plugins receive a narrow `SourceContext` for host services such as:

- network access or a host-owned request helper;
- secure credential storage;
- logging;
- cache paths and cancellation.

Plugins must not access the main window, QML engine internals, or unrelated
source accounts. Source-specific credentials are namespaced by source ID and
account ID.

Navidrome authentication will target the Subsonic/OpenSubsonic API. The plugin
will initially implement server connection, login, library browsing, search,
artwork, audio stream resolution, and read-only playlists where supported.

## 8. Playback migration

The `soundwave` playback abstractions are reusable, but QueMusic currently
drives Qt Multimedia directly from QML. Migration will therefore be staged:

1. Introduce a core playback interface and adapter for the existing Qt
   Multimedia path.
2. Move player controls and playback state behind a C++ controller/ViewModel.
3. Add the `soundwave`-style MPV plugin as an optional playback backend.
4. Add video/MV output only after the audio path and capability negotiation are
   stable.

No source plugin may call a concrete playback engine. It returns a
`StreamDescriptor`; the core resolves the suitable engine from capabilities.

## 9. Migration phases

### Phase A — SDK and host foundation

- Add QueMusic plugin contracts and metadata schema.
- Port/adapt `PluginManager`, persistent loader ownership, and failure signals.
- Add fake plugin and loader contract tests.
- Keep the current `MusicApiService` behavior working through an adapter.

### Phase B — Local source

- Wrap local file discovery and the existing local lyrics behavior as a source
  adapter.
- Normalize local tracks into the shared model.
- Preserve the existing local file UI during the first migration.

### Phase C — Navidrome source

- Implement the first NAS plugin against the Subsonic/OpenSubsonic API.
- Add connection/account settings and health checks.
- Implement browse, search, artwork, audio playback, and capability reporting.
- Test with a fake HTTP server; do not require a live NAS in CI.

### Phase D — Existing online sources

- Wrap current Kugou and NetEase implementations as source adapters.
- Keep their protocol and parsing code inside their respective plugins.
- Remove direct provider branching from core use cases after parity tests pass.

### Phase E — Optional capabilities

- Add lyrics, downloads, favorites, playlists, and MV independently.
- Add Feiniu only after its supported API/protocol and authentication behavior
  are documented.

## 10. Proposed layout

```text
sdk/
  source/
    IMusicSourcePlugin.h
    IMusicSourceSession.h
    IMusicSourceArtworkSession.h
    SourceTypes.h
core/
  source/
    SourceManager.*
    SourceAccount.*
  models/
    Track.*
    Album.*
    Artist.*
    Playlist.*
  playback/
plugins/
  source/local/
  source/navidrome/
  source/kugou/
  source/netease/
```

The exact placement may follow the existing QueMusic directories, but the
source SDK must not depend on QML or provider-specific classes.

## 11. License and third-party requirements

The author confirms that `soundwave` application code is original. Before
publishing copied code, `soundwave` should receive an explicit license, with
Apache-2.0 recommended for the author's code if all copyright is held by the
author. Qt, QCoro, libmpv, and other dependencies retain their own licenses.

QueMusic's existing Apache-2.0 files, AGPL-3.0 meshgradient component, and
third-party notices remain separate. New plugin SDK and source-plugin code
must not be placed inside the AGPL meshgradient component, and each imported
component must retain its copyright and license notices.

## 12. Testing and acceptance criteria

- Plugin metadata and IID validation tests.
- Duplicate source ID and unsupported SDK version tests.
- Plugin load failure does not stop unrelated sources from loading.
- Fake source contract tests cover success, authentication failure, network
  failure, cancellation, and expired stream URLs.
- Navidrome adapter tests use recorded/fake JSON responses and verify mapping
  to unified models.
- Playback tests verify that source plugins return descriptors and never depend
  on a concrete engine.
- Optional capability interfaces are versioned independently from the base
  source session. A frozen MusicSourcePlugin/1.0 binary built with the legacy
  artwork slot remains loadable and dispatches trailing `fetchLyrics` and
  `cancel` calls correctly through the current host.
- Artwork tests cover all metadata/interface combinations used by policy:
  advertised optional interface, advertised legacy-base fallback, and an
  unadvertised optional interface that the host must not call.
- Existing QueMusic build and local-lyrics tests remain green.
- No QML code performs source HTTP requests or token handling.

## 13. Decisions requested before implementation

The recommended first implementation is:

1. Keep QueMusic's current Qt baseline and asynchronous style; do not make
   QCoro/C++20 a prerequisite yet.
2. Port the `soundwave` plugin lifecycle and playback boundary concepts first.
3. Add a separate `IMusicSourcePlugin` contract rather than extending
   `MusicApiService`.
4. Implement Local and Navidrome source adapters before migrating Kugou and
   NetEase.
5. Keep the Issue #12 branch independent from this architecture branch.

The approved ABI decision is to keep the complete published
`IMusicSourceSession` v1 virtual order, including its legacy artwork slot, and
add the separately versioned `IMusicSourceArtworkSession` interface described
above. The optional interface is additive; it is not an ABI-preserving reason
to remove the v1 slot.

This design is ready for review. Implementation should begin only after the
architecture and first milestone are approved.
