# Architecture

## Runtime Boundary

QueMusic now initializes source plugin discovery once during application
startup. After `QGuiApplication` exists and before `engine.load(...)`, the app
creates exactly one `SourceManager` owned for the lifetime of the application.
That manager:

- adds the bundled plugin search root under `applicationDirPath()/plugins/source`
- adds the user plugin search root under
  `QStandardPaths::AppDataLocation/plugins/source`
- logs `sourceLoadFailed` through Qt warnings so the existing `LogManager`
  message handler records loader problems
- calls `loadAll()` immediately to populate the in-process source registry

This boundary is intentionally C++-only. `SourceManager`, source sessions, and
plugin instances are not exposed as direct QML context properties. The existing
QML-facing objects, playback path, and `MusicApiService` singleton continue to
define the active user-facing runtime.

## Ownership Model

- `QGuiApplication` owns the application-lifetime `SourceManager`
- `SourceManager` owns plugin loaders and keeps loaded plugin instances alive
- individual source sessions are created later per account and are owned by the
  caller-provided parent

This keeps discovery global while session state remains explicit and scoped.

## Deliberate Non-Goals

This startup integration does not:

- replace or modify `MusicApiService`
- change existing QML context properties
- reroute local lyrics behavior
- alter playback or playback plugin wiring
- add plugin sandboxing, permission prompts, or signature verification
- solve Issue #12 or touch `meshgradient`

Those changes belong to later, separately reviewable tasks once source adapters
and compatibility layers exist.
