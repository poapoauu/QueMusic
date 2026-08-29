# Architecture

## Native PluginCore

QueMusic has one application-lifetime `PluginManager`, created after
`QGuiApplication` and before `engine.load(...)`. It owns package manifests,
`QPluginLoader` objects, and active-use leases. `SourceManager` is an adapter
over that core: it validates `IMusicSourcePlugin` descriptors, initializes
sources, and creates account-scoped sessions.

```text
QGuiApplication
  ├─ PluginManager
  │   ├─ PluginManifest
  │   ├─ QPluginLoader (while resident, including after failed unload)
  │   └─ PluginLease (one per active source session)
  └─ SourceManager
      └─ registered source descriptors and sessions
```

Plugin discovery reads external `manifest.json` files without executing their
libraries. A native package must pass package API, Qt-major, optional CPU and
build-mode checks before `QPluginLoader::instance()` runs. A loaded `source`
package must also implement `IMusicSourcePlugin`; its manifest `sourceId` must
match its returned `SourceDescriptor`.

## Runtime and ownership

`SourceManager::createSession()` acquires a `PluginLease` and parents a private
lease guard to the returned `IMusicSourceSession`. Consequently a package is
Busy while any of its sessions exist. An unused package may unload: the source
registry removes its entry as part of that state change, preventing dangling
plugin pointers. If native unload fails, the package becomes Failed but retains
its one loader and exposes an unload retry; it cannot load a second instance.
Reloading after a successful unload creates a fresh native instance and
re-registers the source.

`PluginManager` is the only plugin object exposed to QML, as the
`pluginManager` context property. It exposes a read-only package list and
Discover/Load/Unload/Reload invokables. `SourceManager`, raw plugin instances,
and sessions remain C++-only. The Settings 音源 page uses the manager to show
state, errors, and active session counts; unload and reload controls are
disabled while a package has active leases.

## Plugin roots

The manager scans:

- `QueMusic.app/Contents/PlugIns/quemusic` in a macOS application bundle;
- `<applicationDir>/../plugins` for a development build; and
- `QStandardPaths::AppDataLocation/plugins` for user-installed packages.

Each root contains package directories, each with `manifest.json` beside its
shared library. The build emits the Navidrome module and its manifest to the
development package root. On macOS, building the `QueMusic` target, or its
`quemusic_navidrome_bundle_plugin` deployment target, copies the same generated
package into the application bundle at `Contents/PlugIns/quemusic/navidrome`.

## Deliberate boundaries

The first PluginCore release supports native `source` packages only. Local
music, Kugou, NetEase, QQ, lyrics, MV, GUI, and language plugins are planned
future package categories and are not converted by this milestone. Existing
`MusicApiService`, playback wiring, and the rest of the QML data model remain
unchanged.

Native plugins are trusted in-process code. Manifest checks improve diagnostics
but do not sandbox a library, validate a signature, provide permission prompts,
or guarantee compatibility across Qt/toolchain versions.

JavaScript plugins are intentionally deferred. Their QuickJS-style isolated VM,
permissions, package integrity, and hot-reload design is recorded in
`docs/superpowers/specs/2026-08-29-js-plugin-runtime-design.md`; no JavaScript
runtime is linked or loaded today.
