# QueMusic Native Plugin Core Design

## Status

Approved for implementation on 2026-08-29. JavaScript plugins are explicitly
out of implementation scope for this milestone; their frozen follow-up design
is in `docs/superpowers/specs/2026-08-29-js-plugin-runtime-design.md`.

## Goal

Replace source-only, process-lifetime plugin discovery with a small native Qt
plugin core that can discover directory-packaged plugins, validate metadata,
load them through `QPluginLoader`, expose their state to QML, and unload them
when no active client lease remains.

## Constraints

- C++17 and Qt 6 only; add no third-party plugin framework.
- Native plugins are trusted local code. The host validates compatibility but
  cannot sandbox a loaded shared library.
- Existing source SDK v1 may be used as an adapter during this milestone, but
  compatibility is not a hard constraint. If a clean PluginCore boundary
  requires a breaking change, introduce a new major source API and migrate all
  in-tree plugins in the same change.
- The initial native runtime supports `source` packages only. `lyrics`,
  `mv`, `gui`, and `language` reserve category identifiers.
- A source plugin cannot unload while a session created by that plugin exists.
- The application and user plugin roots are both scanned. On macOS, the app
  root is `QueMusic.app/Contents/PlugIns/quemusic`; the development fallback is
  `<applicationDir>/../plugins`.

## Package format

Each plugin is a directory containing `manifest.json` and exactly one library
named by its `library` field.

```text
plugins/
  navidrome/
    manifest.json
    libquemusic_navidrome_source.dylib
```

```json
{
  "id": "org.quemusic.source.navidrome",
  "name": "Navidrome",
  "version": "1.0.0",
  "category": "source",
  "runtime": "native-qt",
  "library": "libquemusic_navidrome_source.dylib",
  "pluginApi": { "major": 1, "minHostMinor": 0 },
  "interfaces": [{ "id": "org.quemusic.MusicSourcePlugin/1.0", "version": "1.0" }],
  "runtimeRequirements": { "qtMajor": 6, "architecture": "arm64", "buildMode": "Debug" }
}
```

The host rejects duplicate package IDs, duplicate source IDs, malformed
manifests, a library escaping the package directory, unsupported category or
runtime, and incompatible Qt major or CPU architecture before calling
`QPluginLoader::instance()`. The manifest and loaded plugin metadata must agree
on identity and source interface.

## Core model and lifecycle

```text
PluginManager
  ├─ PluginManifest       parsed package manifest
  ├─ PluginSpec           immutable identity + mutable state/error
  ├─ QPluginLoader        retained only while Loaded
  └─ PluginLease          shared active-use token

SourceManager
  └─ SourcePluginAdapter  source descriptor/session bridge
```

`PluginState` is `Discovered`, `Loaded`, `Failed`, or `Unloaded`.
`discover()` parses manifests without executing code; `load()` verifies
requirements, creates the instance, verifies the declared category interface,
and retains its loader. `acquire()` produces a `PluginLease` only for a
loaded package. `unload()` returns Busy while leases exist and otherwise
destroys the instance and calls `QPluginLoader::unload()`. `reload()` is
unload then load.

`SourceManager::createSession()` owns a lease guard parented to the returned
session, so its library stays loaded for the session lifetime. It may preserve
the current source SDK as an adapter, but can migrate it as one in-tree change
when an adapter becomes misleading or unsafe.

## QML and settings

The application publishes the manager as `pluginManager`. Its `plugins`
property lists `id`, `name`, `version`, `category`, `state`, `error`,
`path`, `activeLeases`, and `reloadable`. The existing Settings plugin
placeholder becomes controls for discover, load, unload, and reload. Busy
sources show their active use count and cannot be unloaded.

## Compatibility and deployment

No `QLibrary::PreventUnloadHint` is set. Native plugins need the host's Qt
major, architecture, compatible toolchain/ABI, and applicable build mode.
Manifest checks provide actionable diagnostics; `QPluginLoader` is the final
binary-compatibility gate. The CMake build places a manifest beside every
MODULE and later deployment copies packages into the bundle plugin root.

## Out of scope

- JavaScript engine, remote marketplace, installation, signing, and updater.
- Conversion of local files, Kugou, NetEase, QQ, lyrics, or MV to plugins.
- Dynamic QML type registration or live language reload.
