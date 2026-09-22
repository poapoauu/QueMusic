# Original UI / Unified Source Adapter Design

## Goal

Keep QueMusic's existing user-facing music UI visually and behaviorally consistent
with upstream commit `a63511d`, while retaining the source SDK v2 plugin runtime,
Navidrome integration, aggregate/specific-source selection, capability boundaries,
and unified playback pipeline.

This correction treats the existing QML pages as consumers of data. Source plugins
and source-specific protocols must not determine page structure or visual design.

## Scope boundary

### Restore and preserve

The following music-shell surfaces use their upstream `a63511d` structure, layout,
navigation, animations, styling, and reusable controls as the visual baseline:

- `layout/LeftSideBar.qml`
- `layout/MainContent.qml`
- `pages/HomePage.qml`
- `pages/PlaylistPage.qml`
- `pages/FavouritePage.qml`
- `pages/SearchPage.qml`
- the corresponding music-shell wiring in `main.qml`

Necessary data bindings may be added, but they must not replace page composition,
remove existing tabs or controls, change navigation order, or introduce a parallel
visual language.

### Retain

- Source SDK v2 interfaces and v2-only plugin loading.
- `MusicHub`, repositories, caches, source registry, capability resolver, action
  router, playback coordinator, and Qt playback controller.
- Navidrome providers and user-side API operations.
- Generic plugin-management and plugin-settings UI previously requested by the
  project owner. It remains a settings concern and must not become a replacement
  music-library page.
- The removal of the standalone “音乐源” browsing/search page. Music from all
  sources appears through the normal 推荐、分类、收藏 and search experiences.

## Architecture

### Compatibility presentation adapter

Add a host-owned presentation adapter between `MusicHub` and the original QML
models. It translates `MediaItemV2`, sections, loading/error state, continuation,
and source identity into the exact role names and invokable operations expected by
the original controls.

The adapter is not a second source API. It may only:

1. request page/search data from `MusicHub`;
2. expose stable, QML-safe presentation roles;
3. retain the complete typed v2 item privately for later actions;
4. forward play, favorite, playlist and other actions to the existing action router
   or playback coordinator;
5. map typed errors and capability reasons into existing empty/error affordances.

It must never construct authenticated URLs in QML, call Navidrome directly, infer
unsupported operations, or persist plugin secrets.

### Source scope

Aggregate or specific-source selection remains a host-level state. It is presented
inside an existing toolbar/control area using the project's original control style,
not as a redesigned page header. The selected scope applies consistently to 推荐、
分类、收藏 and search requests. Source attribution may be shown only where the
existing component has a compatible secondary-text or menu location.

### Capability boundary

Every item keeps its originating source instance, account, entity identity and
capability snapshot. Menus expose only operations supported by that item and source.
Mixed-source selections use the intersection of supported operations. Unsupported
operations are hidden or disabled with the existing UI convention; the adapter must
not emulate one source's operation with another source.

### Playback and queue

Original click and queue gestures remain unchanged from the user's perspective.
Internally they forward the full v2 identity to `PlaybackCoordinator`; transient
stream URLs and headers remain below QML. Existing local queue visuals stay in
place, while queue entries retain private source identity so later playback routes
to the correct plugin.

## Migration approach

1. Capture structural baselines for the six upstream QML surfaces and their key
   object hierarchy/navigation semantics.
2. Restore those surfaces from `a63511d` as the visual baseline.
3. Introduce adapter contracts and tests before reconnecting live data.
4. Replace legacy `MusicApi`/model calls at page event boundaries one flow at a
   time: 推荐, 分类, 收藏, search, then playback/actions.
5. Keep legacy local/download behavior operational until equivalent local-source
   plugin support is ready; do not remove visible features merely to complete the
   Navidrome path.

## Error handling

- A failing source does not blank successful aggregate results from other sources.
- A specific-source failure uses the existing page's empty/error presentation and
  provides a retry operation.
- Authentication/configuration failures direct the user to that plugin's settings
  page without adding a music-source library page.
- Stale page, artwork, action, and playback responses are rejected by request or
  generation identity.
- User-visible messages contain no credentials, authenticated URLs, headers, or
  server response bodies.

## Verification

### Automated

- Structural QML regression tests compare required navigation entries, page/tab
  hierarchy, key object names, and original control types against the upstream
  baseline.
- Adapter unit tests cover role mapping, aggregate and specific scope, continuation,
  source identity preservation, partial failure, and capability intersection.
- Interaction tests cover original click/menu signals routing the complete v2 item
  to action and playback layers.
- Existing source SDK, plugin manager, Navidrome, repository/cache, MusicHub and
  playback tests remain green.
- Startup tests reject reintroduction of the old v1 source bridge.

### Manual

At 1140×720 and the minimum 810×540 window size:

- compare 推荐、分类、收藏、搜索、侧栏 and transitions with upstream;
- confirm no clipping, overlap, blank white replacement surface, or extra source
  library navigation item;
- switch between aggregate and Navidrome-specific scope;
- load pages, paginate, open details, play a track, seek, stop, and advance queue;
- verify supported Navidrome actions are available and unsupported operations remain
  visibly bounded;
- verify local/download pages retain their original appearance and behavior.

Real-server mutation checks use a designated test item and the guarded smoke tool;
ordinary GUI validation starts read-only.

## Non-goals

- Redesigning or modernizing the existing visual language.
- Replacing original controls with generic MusicHub section cards.
- Restoring source SDK v1, `MediaBridge`, `SourceManager`, or the standalone source
  library page.
- Implementing JS plugins or converting every legacy local/cloud source in this
  correction pass.
