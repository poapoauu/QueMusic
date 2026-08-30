# Unified Media Bridge Design

## Status

Proposed on 2026-08-30. This design requires user review before implementation.

## Goal

Provide one stable UI-facing media contract for every music source while keeping
protocol, authentication, and provider-specific parsing inside source plugins
or temporary legacy adapters.

## Scope

The first implementation supports the complete UI flow for plugin-backed
sources: search, library browsing, artist, album, playlist, directory,
artwork, lyrics, stream resolution, playback queueing, request state, account
selection, and retryable errors. Navidrome is the first real source.

The existing `MusicApiService` remains in place during the first implementation.
Its NetEase, Kugou, QQ, and other legacy provider code is migrated gradually
behind bridge adapters or native source plugins. Local files are also a future
source plugin; they are not converted in this milestone.

## Architecture

```text
QML pages and playback queue
        |
        | MediaItem / MediaListModel / request state
        v
MediaBridge
  |- SourceSessionRegistry
  |- Media normalization and pagination
  |- artwork and lyrics dispatch
  |- stream resolution and PlaybackEntry creation
        |
        v
SourceManager and native PluginManager
        |
        v
Source plugin session or legacy source adapter
```

Only `MediaBridge` is exposed to QML for plugin-backed media. QML does not
receive `SourceManager`, a plugin object, `IMusicSourceSession`, or a raw
provider response. The playback engine receives a `PlaybackEntry`, not a
provider-specific URL or identifier.

### Components

#### `MediaItem` and `MediaId`

`MediaId` is the stable identity used for cache keys, favourites, queue items,
and bridge requests:

```text
sourceId / accountId / nativeId / kind
```

`kind` distinguishes track, album, artist, playlist, directory, and other
container types. `MediaItem` supplies the UI-facing title, subtitle, artists,
album title, duration in milliseconds, artwork reference, item capabilities,
and an extensible non-sensitive `extra` map. Provider JSON field names do not
cross this boundary.

#### `MediaListModel` and `MediaPage`

`MediaListModel` is a QAbstractListModel with stable QML roles for `MediaItem`.
It owns a request state (`Idle`, `Loading`, `Ready`, `Empty`, or `Failed`), an
error category, retry metadata, and paging state. `MediaPage` contains a
normalized item list plus an opaque bridge-owned `nextCursor` and `hasMore`.
Source-specific page numbers or tokens never appear in QML.

#### `SourceSessionRegistry`

The registry owns one source session per enabled `sourceId + accountId` pair.
It creates sessions through `SourceManager`, reuses them for bridge requests,
cancels outstanding work before removal, and releases leases before an account
is disabled, a plugin is unloaded, or the application exits.

#### `MediaBridge`

`MediaBridge` owns the registry and maps every source-session result to a
normalized model. It is the QML API for:

```text
search(sourceScope, keyword, page)
browse(MediaId container, page)
open(MediaId item)
enqueue(MediaItem)
play(MediaItem)
loadArtwork(MediaId)
loadLyrics(MediaId)
retry(requestId)
```

`sourceScope` can select a single account, all accounts for a source, or all
enabled sources. Results retain their origin in `MediaId`, so a mixed-source
search remains safe to enqueue and play.

#### `PlaybackEntry`

Before playback, `MediaBridge` asks the owning session to resolve a stream and
creates a short-lived `PlaybackEntry`: stable `MediaId`, temporary stream URL,
request headers, expiration time, artwork reference, and lyrics reference.
The bridge never writes temporary authenticated URLs or request headers to
persistent storage.

## Account and Secret Handling

An account record contains source ID, account ID, display name, enabled state,
and non-sensitive parameters such as a Navidrome server URL. Passwords, tokens,
and equivalent credentials are stored only through the system keychain. The
settings file contains a keychain reference, never the secret itself.

## Lifecycle and Errors

Every bridge request has a UUID. The bridge maps source errors to localized
categories: network, authentication, authorization, not found, rate limited,
invalid request, unavailable, unsupported, and unknown. QML observes model
state and error category rather than plugin signals. Retry uses the recorded
request intent only when the request is safe to repeat.

On account disable, plugin unload, or shutdown, the bridge cancels pending
requests, releases playback/session leases, clears the affected model state,
and then destroys the session. An unavailable or unloaded source is presented
as a retryable source state rather than a dangling QML object.

## Migration Plan

1. Add the bridge foundation and normalize Navidrome sessions.
2. Add a Music Sources UI for account configuration, enabling, and health.
3. Move plugin-backed search and library browsing pages to bridge models.
4. Route bridge-created `PlaybackEntry` values into the existing playback
   queue and engine.
5. Wrap current `MusicApiService` providers behind bridge adapters, one source
   at a time.
6. Convert local files into a source plugin using the same contract.

The legacy online pages remain functional until their matching adapter is
complete. No mass rewrite or removal occurs in the bridge foundation milestone.

## Acceptance Criteria

- A Navidrome account can be configured without persisting its password in a
  settings file.
- Search, browse, artwork, lyrics, and stream resolution are represented by
  bridge models and actions, not raw plugin objects.
- An item obtained from a Navidrome search can be queued and handed to the
  playback engine through a `PlaybackEntry`.
- Loading, empty, failed, and retry states are testable independently.
- Existing `MusicApiService` UI remains functional during the first rollout.
- Plugin unloading or account removal does not leave sessions, requests, or
  QML references alive.

## Constraints

- Continue using native Qt plugins and the existing Source SDK v1 contract.
- Do not introduce a heavyweight plugin framework or a JavaScript runtime in
  this milestone.
- Preserve the current macOS package deployment and CMake 3.16 compatibility.
- Keep source credentials out of repository files, test output, and ordinary
  settings storage.
