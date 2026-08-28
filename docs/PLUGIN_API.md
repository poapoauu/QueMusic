# Plugin API

QueMusic source plugins are native Qt plugins loaded at application startup by
`SourceManager`. This document defines the current ABI boundary for music source
providers.

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
also rejected if it does not expose the required IID, has an empty source id or
display name, or if another loaded plugin already uses the same source id.

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

At startup QueMusic loads source plugins from exactly these directories:

- `QCoreApplication::applicationDirPath()/plugins/source`
- `QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)/plugins/source`

The application directory is intended for bundled plugins. The app-data
directory is intended for user-installed plugins on the local machine.

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
- `IMusicSourceSession` handles async provider work and must implement:
  `search`, `browse`, `resolveStream`, `fetchLyrics`, and `cancel`.
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
  `expiresAt`, `video`, and `seekable`). `Artwork` pairs the
  `SourceCapability::Artwork` flag with `IMusicSourceArtworkSession::fetchArtwork`,
  which completes with a JSON DTO containing `track`, `url`, and `mimeType`.
  Unsupported operations emit `requestFailed` with `SourceErrorKind::Unsupported`.

## Minimal Plugin Skeleton

```cpp
#include "IMusicSourceArtworkSession.h"
#include "IMusicSourcePlugin.h"

class ExampleSession final : public IMusicSourceSession,
                             public IMusicSourceArtworkSession {
    Q_OBJECT
    Q_INTERFACES(IMusicSourceArtworkSession)

public:
    using IMusicSourceSession::IMusicSourceSession;

    QUuid search(const SearchQuery &) override { return QUuid::createUuid(); }
    QUuid browse(const BrowseQuery &) override { return QUuid::createUuid(); }
    QUuid resolveStream(const TrackRef &) override { return QUuid::createUuid(); }
    QUuid fetchLyrics(const TrackRef &) override { return QUuid::createUuid(); }
    void cancel(const QUuid &) override {}

    QUuid fetchArtwork(const TrackRef &) override { return QUuid::createUuid(); }
};

class ExampleSourcePlugin final : public QObject, public IMusicSourcePlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QUEMUSIC_MUSIC_SOURCE_PLUGIN_IID)
    Q_INTERFACES(IMusicSourcePlugin)

public:
    SourceDescriptor descriptor() const override
    {
        return {
            QStringLiteral("example.source"),
            QStringLiteral("Example Source"),
            QStringLiteral("1.0.0"),
            QStringLiteral("example"),
            QStringLiteral("1.0"),
            SourceCapability::Artwork
        };
    }

    bool initialize(SourcePluginContext &context) override
    {
        return context.network != nullptr;
    }

    IMusicSourceSession *createSession(const SourceAccount &account, QObject *parent) override
    {
        Q_UNUSED(account);
        return new ExampleSession(parent);
    }
};
```
