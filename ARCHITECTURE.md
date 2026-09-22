# Architecture

## Native PluginCore

QueMusic creates one application-lifetime `PluginManager` before loading QML.
It discovers package manifests, owns `QPluginLoader` instances and tracks active
leases. Source packages expose `IMusicSourcePluginV2`; account-scoped sessions
derive from `IMusicSourceSessionV2` and implement only the optional provider
interfaces for operations they actually support.

```text
QGuiApplication
  ├─ PluginManager
  │   ├─ PluginManifest
  │   ├─ QPluginLoader
  │   └─ PluginLease
  ├─ SourceRegistry
  │   └─ account-scoped IMusicSourceSessionV2 instances
  ├─ MusicRepository
  ├─ MusicHub
  ├─ PlaybackCoordinator -> QtPlaybackController
  └─ PluginSettingsController
```

Discovery reads `manifest.json` without executing plugin code. A v2 native
package must declare source SDK ABI 2, Qt major, architecture and build key.
Those requirements and the `org.quemusic.MusicSourcePlugin/2.0` IID are checked
before the plugin is accepted. Native plugins must be rebuilt when the Qt
toolchain or source SDK ABI changes.

## Runtime and ownership

`SourceRegistry` creates one session for each enabled source configuration and
owns its package lease. A package therefore cannot unload while one of its
sessions is active. The registry closes and releases sessions before unload or
application shutdown, preventing QObjects or function pointers from outliving
their plugin library.

Sessions own lifecycle and asynchronous request state. Each request emits
`requestStarted` before exactly one terminal result unless cancelled. Optional
provider interfaces divide page, playback, favorites, ratings, scrobbling,
playlist, download, play-queue, bookmark and settings operations. Effective
availability is the fail-closed intersection of plugin, server, account and
item capability layers.

## QML boundary and page flow

There is no source-library page. Every source contributes normalized sections
to Recommendation, Category, Favorites and Search. The user can select an
aggregate scope or a single source instance.

```text
source plugins -> SourceRegistry -> MusicRepository -> MusicHub -> page models -> QML
                                            |
                                            +-> PlaybackCoordinator
                                                -> QtPlaybackController
```

QML receives only the high-level objects `musicHub`, `playbackCoordinator`,
`playbackController`, `pluginSettings` and the package list exposed by
`pluginManager`. It never receives source sessions, provider interfaces, raw
provider JSON, credentials, secret references or authenticated responses.
`MusicHub` normalizes page sections; `PlaybackCoordinator` resolves fresh
streams and keeps request headers in C++; `PluginSettingsController` supplies
schema-driven per-plugin configuration and capability boundaries.

The legacy local and original online-provider queue remains isolated behind
`LegacyQueueController` while those providers are migrated. It cannot carry v2
media references or provider payloads.

## Plugin roots

The manager scans:

- `QueMusic.app/Contents/PlugIns/quemusic` in a macOS bundle;
- `<applicationDir>/../plugins` for development builds; and
- `QStandardPaths::AppDataLocation/plugins` for user-installed packages.

Each root contains package directories with `manifest.json` beside the shared
library. The build emits Navidrome to the development package root and copies
it into `Contents/PlugIns/quemusic/navidrome` for the macOS application bundle.

## Security and deferred runtimes

Native plugins are trusted in-process code. Manifest checks improve diagnostics
but do not sandbox libraries, validate signatures or provide permission
isolation. On macOS, source secrets are stored in Keychain; QSettings contains
only allowlisted metadata and secret references. Unsupported platforms report
secure storage unavailable instead of persisting plaintext credentials.

JavaScript plugins remain deliberately deferred. The frozen design is in
`docs/superpowers/specs/2026-08-29-js-plugin-runtime-design.md`; no JavaScript
plugin runtime is linked today.

## Migration boundary

Navidrome is the first complete v2 source. Local files, NetEase, Kugou and QQ
remain on their existing paths until each has a v2 adapter or native plugin.
New pages and operations must use the v2 hub and capability model; the removed
v1 media bridge is not a supported integration point.
