# QueMusic JavaScript Plugin Runtime Design

## Status

Designed and intentionally deferred on 2026-08-29. Do not add a JavaScript
engine, JS package loader, SDK, or runtime dependency until the project owner
explicitly requests implementation.

## Purpose

Provide a future ABI-independent runtime for online music sources, lyrics, MV
providers, and integrations. It complements native Qt plugins, which remain
appropriate for local media, playback engines, and deep Qt integration.

## Songloft-inspired model

Borrow the concepts from Songloft: one isolated QuickJS VM per plugin, serial
task scheduling, manifest permissions, package integrity validation, hot reload,
host bridges, health checks, and a TypeScript development toolchain. Do not copy
its Go, Flutter, WebView, or WebF implementation.

## Package and API

```text
quemusic-plugin-navidrome-1.0.0.qmpkg
  manifest.json
  main.mjs
  i18n/
  assets/
  qml/                 optional; restart-required
```

```js
export function activate(context) {
  return {
    source: {
      async search(query) {},
      async browse(query) {},
      async resolveStream(track) {},
      async fetchArtwork(track) {},
      async fetchLyrics(track) {}
    },
    deactivate() {}
  };
}
```

The runtime will use the same package identity and lifecycle model as the
native PluginManager. A `JsSourcePlugin` converts JSON request IDs and DTOs
between JS services and the source contract, so QML does not depend on runtime.

## Permissions and safety

The initial facade is limited to manifest-approved HTTP origins, namespaced
storage, redacted logging, translations, and scoped account authentication.
It excludes Node modules, shell execution, arbitrary filesystem access, raw
sockets, and remote code loading. In-process JS is not a security sandbox:
only trusted, explicitly installed packages may run. An untrusted tier, if
needed, must be a separate process with JSON-RPC.

## Lifecycle

For a hot-reloadable source, lyrics, or MV package: stop new work, cancel host
requests, invoke `deactivate()` with a deadline, destroy its engine, and
reload its module. Repeated activation or health-check failures quarantine the
plugin. GUI and language assets never hot unload in V1; they stage for restart.

## Trigger and order

Implement only on an explicit owner request, after native PluginCore is stable:

1. Select an embedded engine following license and platform review; QuickJS is
   preferred, while QJSEngine must prove module, interruption, and async bridge
   support before selection.
2. Add a test-only command plugin and minimal host bridge.
3. Add TypeScript declarations, builder, and scaffolder.
4. Convert one online source only after timeout, cancellation, update, and
   unload tests pass.
