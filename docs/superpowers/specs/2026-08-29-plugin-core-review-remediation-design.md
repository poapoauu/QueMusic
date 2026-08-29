# PluginCore Review Remediation Design

## Status

Accepted on 2026-08-29 after independent review of the native PluginCore
branch. This document scopes corrective work only; it does not add JavaScript
plugins or new plugin categories.

## Goals

- Do not execute two native packages that claim the same `sourceId`.
- Do not report a package as loaded when its source descriptor validation or
  initialization fails.
- Keep the source registry and package loader state safe across unload,
  reload, and manager destruction.
- Enforce that a package library resolves inside its package directory.
- Deploy the bundled Navidrome package to the macOS bundle root that startup
  scans.

## Loader and source state

`PluginManager` remains the owner of every `QPluginLoader`. Discovery rejects
duplicate package IDs and duplicate source IDs before a library is created.

For a source package, `SourceManager` validates its descriptor and runs
`initialize()`. If either action fails, it asks PluginManager to transition the
package to `Failed`, retain the supplied error, and release the just-created
library. A package with no retained loader can later be retried. A failed
unload is different: the entry retains its loader and instance, remains
unavailable for a second load, and returns failure on reload until an unload
succeeds.

The source registry synchronizes with package state. Loaded packages register
one source; failed or unloaded packages remove it. `SourceManager` stores the
injected manager as a `QPointer` and clears package-backed registrations if the
manager is destroyed. It refuses package-backed session creation without a live
manager.

## Package boundary

After syntactic relative-path validation, manifest parsing resolves the package
directory and library through canonical paths. The library must exist, be a
file, and have a canonical path below the canonical package root. A symlink to
an external library therefore fails discovery before `QPluginLoader` can run
it.

## Deployment

The development package uses the configuration-safe path
`<build>/plugins/navidrome/$<CONFIG>`. On macOS, the
`quemusic_navidrome_bundle_plugin` deployment target depends on the Navidrome
module and generated manifest, then copies both to:

```text
QueMusic.app/Contents/PlugIns/quemusic/navidrome/
```

QueMusic depends on this deployment target. This is the exact bundled root
selected by `defaultSourcePluginSearchPaths()`.

## Tests and verification

Tests are added before each corrective implementation and must cover:

- duplicate `sourceId` discovery rejection before either library is loaded;
- descriptor and initialization failure becoming a visible failed package;
- failed unload refusing a second loader/reload;
- manager destruction making package-backed source use safely unavailable;
- symlink escape rejection; and
- macOS package output path and module/manifest-only incremental refresh when
  running on macOS.

The complete default build and full CTest suite must pass after all changes.
