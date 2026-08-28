# Navidrome Source Plugin Design

**Status:** Draft for review  
**Date:** 2026-08-28  
**Branch:** `codex/navidrome-source-plugin`

## 1. Objective

Add the first real NAS-oriented source plugin to QueMusic. The plugin targets
Navidrome through its Subsonic-compatible REST API and proves that the source
SDK can support a remote music library without coupling the host application to
Navidrome-specific HTTP code.

The first implementation is a backend/plugin milestone. It must be loadable by
`SourceManager`, usable by a caller that supplies a `SourceAccount`, and fully
testable without requiring a live server. QML account management and migration
of the existing Kugou/Netease services remain separate follow-up work.

## 2. Scope

### Included

- A native Qt plugin at `plugins/navidrome-source`.
- Subsonic JSON requests using protocol version `1.16.1`.
- Token authentication using a per-request random salt.
- Account configuration for server URL, username, and password.
- Connection/authentication through `ping`.
- Search through `search3`.
- Tag-based and simulated-directory browsing through `getIndexes` and
  `getMusicDirectory`.
- Audio stream URL resolution through `stream`.
- Artwork URL resolution through the optional Artwork capability and
  `getCoverArt`.
- Lyrics retrieval through `getSong` followed by `getLyrics`, when song
  metadata is needed to identify the lyrics.
- Async success, failure, and cancellation behavior following the source SDK.
- Unit tests with a local `QTcpServer` fixture.
- A documented manual smoke test against a user-provided private Navidrome
  instance. Credentials must never be committed or printed.

### Excluded from this milestone

- Playlist read/write, favourites, ratings, scrobbling, downloads, transcoding,
  video, and server scan control.
- Folder browsing as a physical filesystem tree. Navidrome documents that its
  folder endpoints expose a simulated tag-based tree instead.
- QML settings pages, account persistence, credential vault integration, and
  source selection UI.
- Changes to the existing Kugou or Netease implementations.
- Any change to the existing v1 virtual-function order or plugin IID.

Navidrome's compatibility documentation lists `search3`, `stream`,
`getCoverArt`, and `getLyrics` as supported endpoints, while noting that
folder browsing is simulated rather than a physical folder hierarchy:
<https://www.navidrome.org/docs/developers/subsonic-api/>.

## 3. Source SDK account boundary

`SourceAccount` currently contains identity fields only. The plugin needs
runtime connection data, so append the following fields to the end of the
struct:

```cpp
QVariantMap parameters;
QByteArray secret;
```

The Navidrome plugin interprets:

| Field | Key/value | Rule |
|---|---|---|
| `parameters` | `serverUrl` | Required absolute `http` or `https` URL; `/rest` is normalized by the plugin. |
| `parameters` | `username` | Required Subsonic username. |
| `secret` | raw UTF-8 password bytes | Required for token authentication; never included in logs or result DTOs. |

The three existing identity fields remain first and aggregate initialization
with only those fields remains valid. No virtual interface is changed. The
account fields are an additive SDK data contract and will be documented as
required input for account-aware plugins. The host-side persistence and secure
storage policy is deliberately deferred; this milestone only defines the
in-memory handoff needed by a plugin session.

Subsonic's recommended authentication is `t = md5(password + salt)` with a
fresh salt for each request, alongside `u`, `v`, `c`, and `f` parameters:
<https://www.subsonic.org/pages/api.jsp>.

## 4. Plugin architecture

### Files and targets

The plugin will follow the existing test plugin layout:

```text
plugins/navidrome-source/
├── CMakeLists.txt
├── NavidromeSourcePlugin.h/.cpp
├── NavidromeSourceSession.h/.cpp
└── plugin.json
```

The target links to `quemusic_source_sdk`, Qt Core, and Qt Network, and is
emitted to the normal `plugins/source` build directory. Its descriptor is:

- id: `navidrome`
- protocol: `subsonic`
- SDK version: `1.0`
- capabilities: `Search | Browse | StreamAudio | Artwork | Lyrics`

The plugin's `initialize()` receives and stores the host-provided network
manager. Each `createSession()` call copies the account configuration into an
independent session owned by the caller's parent.

### Request pipeline

Each public session method follows this pipeline:

1. Validate account data and operation-specific identifiers.
2. Generate a request ID and a random salt of at least six characters.
3. Construct the `/rest/<endpoint>.view` URL with JSON output and authentication
   parameters.
4. Issue the request through the shared `QNetworkAccessManager`.
5. Track the reply and operation state by request ID.
6. Parse HTTP and Subsonic response status.
7. Emit exactly one terminal `requestSucceeded` or `requestFailed` signal,
   unless the request was cancelled.
8. Remove reply/state tracking and securely clear transient token material.

No nested event loop or synchronous network request is allowed. Cancellation
must abort all replies associated with the public request ID, including the
second-stage lyrics request.

## 5. Endpoint and DTO mapping

The plugin returns host-neutral JSON objects rather than exposing raw
Navidrome response shapes as a permanent host contract.

### Authentication

- `ping.view`: validates connectivity and credentials.
- Subsonic error code `40` maps to `Authentication`.
- Error code `50` maps to `Authorization`.
- Error code `70` maps to `NotFound`.
- Missing/invalid JSON or other protocol failures map to `InvalidRequest` or
  `Unknown` with the HTTP status preserved when available.

### Search

`search(query)` calls `search3` with `query`, `songCount`, `albumCount`, and
`artistCount` derived from the requested limit. The normalized result contains
an `items` array. Each item includes:

```json
{
  "kind": "track|album|artist",
  "id": "server-native-id",
  "title": "...",
  "artist": "...",
  "album": "...",
  "duration": 0,
  "coverArtId": "..."
}
```

Only fields present in the server response are emitted; the plugin does not
invent IDs or convert Navidrome's string IDs to integers.

### Browse

- Empty/root path calls `getIndexes`.
- A non-empty native directory path calls `getMusicDirectory` with its ID.
- The normalized result contains `items`, where directories have `kind`
  `artist|album|directory` and songs have `kind` `track`.
- `BrowseQuery::cursor` is reserved for future pagination and is not sent until
  the host contract defines cursor semantics for this endpoint.

### Stream

`resolveStream(track)` calls `stream` with the track's native ID. It returns a
JSON `StreamDescriptor`-compatible object containing the original `track`, the
authenticated stream `url`, optional `mimeType`, and `seekable: true` unless
the response proves otherwise. The password is never included in the URL;
the URL may contain the token and salt required by the server.

### Artwork

`fetchArtwork(track)` calls `getCoverArt` with the track native ID and returns a
JSON object containing `track`, authenticated `url`, and `mimeType` when known.
The plugin implements both the retained v1 base method and
`IMusicSourceArtworkSession`, declaring the optional interface with
`Q_INTERFACES`.

### Lyrics

`fetchLyrics(track)` first calls `getSong` when artist/title metadata is not
already cached for the track, then calls `getLyrics` with the resolved artist
and title. The normalized result preserves plain and synchronized lyric data
when available and includes the original `track`. An empty lyrics payload is a
successful response with no lines, not a network failure.

## 6. Error handling and security

- Non-2xx HTTP responses become `Network` unless the body contains a recognized
  Subsonic authentication, authorization, or not-found error.
- JSON protocol responses with `status: failed` are never treated as success.
- Invalid base URLs, missing account fields, malformed JSON, and missing
  required response objects produce actionable error messages without secrets.
- Redirects are not followed across schemes without an explicit Qt network
  policy decision.
- Passwords, tokens, salts, complete authenticated URLs, and response headers
  containing credentials are excluded from logs and test failure messages.
- The plugin accepts private HTTP endpoints for NAS deployments and HTTPS for
  protected deployments; transport security remains the user's server/network
  responsibility until the host adds a policy setting.

## 7. Testing strategy

### Automated tests

Use a local `QTcpServer` fixture so tests are deterministic and do not require
the private Navidrome instance. The fixture will assert:

- endpoint path, HTTP method, required parameters, and JSON format;
- username, client name, protocol version, and fresh token/salt shape;
- search, browse, stream, artwork, and two-stage lyrics mapping;
- Subsonic error-code mapping and malformed responses;
- cancellation before response and during the lyrics second stage;
- plugin metadata, capability flags, optional Artwork cast, and SDK loading.

Tests must not contain the real server address or credentials.

### Manual integration smoke test

After automated tests pass, run an opt-in local command against the user-provided
Navidrome server using credentials supplied through environment variables or an
untracked local configuration. The smoke test will perform `ping`, one search,
one browse, one stream URL resolution, artwork, and lyrics. Its output will
record only endpoint names, status, and elapsed time; it must redact all query
credentials and response secrets.

## 8. Build and acceptance criteria

The feature is accepted when all of the following are true:

1. The plugin target builds as part of the root CMake project with the existing
   Qt toolchain.
2. The plugin loads through `SourceManager` and advertises only implemented
   capabilities.
3. All automated tests pass without a live server.
4. The manual smoke test succeeds against the configured Navidrome instance,
   or its exact network/server failure is documented without weakening the
   automated test result.
5. `git diff --check` is clean and no credential, local IP, or private config
   is committed.
6. Existing source SDK ABI tests and local lyrics tests remain green.

## 9. Follow-up work

After this plugin backend is stable, the next separate milestone should add a
host-side source account manager and QML settings UI. That layer will define
secure persistence, account editing, source selection, and how a generic source
session feeds the existing player models. Playlist/favourite/scrobble support
can then be added as independent capabilities rather than expanding this first
plugin's scope.
