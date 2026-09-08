# Source SDK v2、MusicHub 与 Navidrome 完整迁移 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 用 SDK v2 和 `MusicHub` 替换现有用户侧音源桥接，让 Navidrome 在推荐、分类、收藏和搜索页面中支持完整用户操作，并提供可安全热卸载的声明式插件设置页。

**Architecture:** 新的类型化 SDK v2 与 v1 暂时并存，Navidrome、插件加载器和页面逐步迁移后在本计划末尾删除 v1 上层入口。QML 只访问 `MusicHub`、页面 ViewModel 和通用插件设置控制器；`SourceRegistry` 持有插件租约与会话，`AggregateComposer`、`CapabilityResolver`、`MediaActionRouter` 和 `PlaybackCoordinator` 分别处理聚合、能力、写操作和播放上报。

**Tech Stack:** C++17、Qt 6.11.1（Core、Qml、Quick、Network、Multimedia、Test）、CMake/CTest、QML、macOS Keychain。

**Spec:** `docs/superpowers/specs/2026-09-01-source-sdk-v2-music-hub-design.md`

## Global Constraints

- SDK v2 IID 固定为 `org.quemusic.MusicSourcePlugin/2.0`，不承诺兼容 SDK v1 ABI。
- 默认音源范围是聚合；选择一个具体音源实例后，推荐、分类、收藏和搜索共用并持久化该范围。
- 最终能力必须取插件声明、服务器协商、账号权限和媒体对象约束四层交集。
- 插件不实现的操作不得由 UI 猜测为可用；`Unsupported` 隐藏，`Unavailable`/`Forbidden` 禁用并展示原因。
- 音源插件不得向主 QML 引擎注入任意 QML；设置页由主程序根据 `SettingsSchema` 渲染。
- 密码、令牌和 Cookie 不得写入普通配置、缓存、测试输出或日志；安全存储不可用时拒绝保存凭据，不能退回明文。
- Navidrome 管理员接口、服务器扫描、电台、分享、播客、视频、MV 和 JS 插件不在本计划范围内。
- 保留工作树内已有 macOS 本地网络、运行库、日志路径和 Navidrome 索引修复，不覆盖或回退无关修改。
- 每个任务遵循红—绿—重构循环，并只提交该任务列出的文件。

---

## File Structure

### SDK 与插件边界

- `sdk/source/v2/SourceV2Types.h/.cpp`：稳定 ID、页面 DTO、能力、错误、动作和设置值类型。
- `sdk/source/v2/IMusicSourcePluginV2.h`：插件元数据、设置描述和会话工厂。
- `sdk/source/v2/IMusicSourceSessionV2.h`：连接状态、请求取消和统一完成信号。
- `sdk/source/v2/ISourceProvidersV2.h`：页面、播放、收藏、评分、歌单、下载、队列和书签的可选接口。
- `core/plugins/PluginManifest.*`、`core/plugins/PluginManager.*`：v2 清单校验、兼容拒绝、租约和热卸载状态。

### 核心音乐域

- `core/source/SourceRegistry.h/.cpp`：v2 插件、音源实例、账号会话和租约生命周期。
- `core/music/SourceScopeStore.h/.cpp`：聚合/指定实例选择及持久化。
- `core/music/MusicPageModel.h/.cpp`：QML 页面分区模型和请求代次。
- `core/music/AggregateComposer.h/.cpp`：混排、可靠去重和聚合游标。
- `core/music/PageCache.h/.cpp`：页面元数据内存 LRU、磁盘缓存和过期标记。
- `core/music/ArtworkCache.h/.cpp`：封面文件缓存，不缓存鉴权 URL。
- `core/music/MediaAssetRepository.h/.cpp`：封面和歌词请求、取消与缓存协调。
- `core/music/PageRepository.h/.cpp`：多音源页面请求、取消、缓存状态和局部失败。
- `core/music/CapabilityResolver.h/.cpp`：四层能力交集。
- `core/music/MediaActionRouter.h/.cpp`：收藏、评分、下载、歌单、队列和书签路由。
- `core/music/PlaybackCoordinator.h/.cpp`：流地址解析、队列、播放状态和 scrobble。
- `core/music/MusicHub.h/.cpp`：QML 唯一音乐域入口及四个页面 ViewModel。

### 设置与 Navidrome

- `core/settings/PluginSettingsController.h/.cpp`：插件列表、schema 表单、账号实例和测试连接。
- `components/PluginSettingsPanel.qml`：主从式插件设置布局。
- `components/SchemaSettingsForm.qml`：通用声明式设置表单。
- `components/SourceAccountList.qml`：多实例账号列表。
- `plugins/navidrome-source/NavidromeApiClient.h/.cpp`：鉴权、HTTP、错误解析和端点调用。
- `plugins/navidrome-source/NavidromeMappers.h/.cpp`：Subsonic/OpenSubsonic JSON 到 v2 DTO。
- `plugins/navidrome-source/NavidromeSourceSession.*`：v2 provider 实现与能力协商。

### 页面与集成

- `components/SourceScopeSelector.qml`：共享聚合/指定音源选择器。
- `components/MusicSectionView.qml`：统一页面分区渲染。
- `components/MediaActionMenu.qml`：按能力显示或禁用操作。
- `pages/HomePage.qml`、`pages/PlaylistPage.qml`、`pages/FavouritePage.qml`、`pages/SearchPage.qml`：只绑定 `MusicHub` 页面模型。
- `layout/MainContent.qml`、`layout/LeftSideBar.qml`：移除音乐源页面和导航。
- `main.cpp`、`CMakeLists.txt`：组装并注册 v2 核心。

---

### Task 1: 定义 SDK v2 值类型和可选能力接口

**Files:**
- Create: `sdk/source/v2/SourceV2Types.h`
- Create: `sdk/source/v2/SourceV2Types.cpp`
- Create: `sdk/source/v2/IMusicSourcePluginV2.h`
- Create: `sdk/source/v2/IMusicSourceSessionV2.h`
- Create: `sdk/source/v2/ISourceProvidersV2.h`
- Create: `tests/tst_SourceV2Types.cpp`
- Create: `tests/tst_SourceV2Contract.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: Qt Core value types and Qt plugin metadata.
- Produces: `MediaRefV2`, `PageQueryV2`, `PageResultV2`, `CapabilitySetV2`, `ActionAvailabilityV2`, `IMusicSourcePluginV2`, `IMusicSourceSessionV2` and all optional provider interfaces used by every later task.

- [ ] **Step 1: Write failing value-type tests**

```cpp
private slots:
    void mediaIdentityIncludesSourceInstance()
    {
        const MediaRefV2 a{"navidrome", "home", "admin", MediaEntityTypeV2::Track, "42"};
        const MediaRefV2 b{"navidrome", "office", "admin", MediaEntityTypeV2::Track, "42"};
        QVERIFY(a != b);
        QCOMPARE(mediaRefV2FromVariantMap(mediaRefV2ToVariantMap(a)), a);
    }

    void availabilityPreservesReasonAndConstraints()
    {
        const ActionAvailabilityV2 value{AvailabilityV2::Forbidden,
                                         "source.permission.download",
                                         {{"sameSourceOnly", true}}};
        const auto restored = actionAvailabilityV2FromJson(actionAvailabilityV2ToJson(value));
        QCOMPARE(restored, value);
    }
```

- [ ] **Step 2: Register the two test targets and run them to verify failure**

Run:

```bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/cmake -S . -B build/bridge -DCMAKE_PREFIX_PATH=/Users/liqiang/Qt/6.11.1/macos -DBUILD_TESTING=ON
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/cmake --build build/bridge --target quemusic_source_v2_types_test quemusic_source_v2_contract_test -j2
```

Expected: compilation fails because the v2 headers and targets do not exist.

- [ ] **Step 3: Implement the exact v2 core types**

```cpp
enum class AvailabilityV2 { Unsupported, Available, Unavailable, Forbidden };
enum class MediaEntityTypeV2 { Track, Album, Artist, Playlist, Genre, Directory };
enum class MusicPageKindV2 { Recommendation, Category, Favorites, Search };
enum class SourceSessionStateV2 {
    Closed, Connecting, Ready, AuthenticationRequired, Failed, Closing
};
enum class SourceErrorKindV2 {
    Unknown, Network, Authentication, Authorization, NotFound, RateLimited,
    InvalidRequest, Unavailable, Unsupported
};
enum class PageLoadStateV2 { Idle, Loading, Ready, Empty, Failed };
enum class SourcePageLoadStateV2 { Loading, Ready, Empty, Failed };
enum class SettingsFieldTypeV2 { Text, Secret, Url, Integer, Boolean, Choice, Directory };
enum class SourceActionV2 {
    Play, Artwork, Lyrics, Download, Favorite, Unfavorite, Rating, Scrobble,
    CreatePlaylist, UpdatePlaylist, DeletePlaylist, AddPlaylistTracks,
    RemovePlaylistTracks, FetchPlayQueue, SavePlayQueue,
    FetchBookmarks, CreateBookmark, DeleteBookmark
};
enum class PageSectionKindV2 {
    RecentlyPlayed, FrequentlyPlayed, HighestRated, Newest, Random,
    Genres, Artists, Albums, Tracks, FavoriteTracks, FavoriteAlbums,
    FavoriteArtists, Playlists, SearchResults
};

struct MediaRefV2 {
    QString sourcePluginId;
    QString sourceInstanceId;
    QString accountId;
    MediaEntityTypeV2 entityType = MediaEntityTypeV2::Track;
    QString entityId;
    friend bool operator==(const MediaRefV2 &left, const MediaRefV2 &right)
    {
        return left.sourcePluginId == right.sourcePluginId
            && left.sourceInstanceId == right.sourceInstanceId
            && left.accountId == right.accountId
            && left.entityType == right.entityType
            && left.entityId == right.entityId;
    }
    friend bool operator!=(const MediaRefV2 &left, const MediaRefV2 &right)
    {
        return !(left == right);
    }
};

struct ActionAvailabilityV2 {
    AvailabilityV2 state = AvailabilityV2::Unsupported;
    QString reasonKey;
    QVariantMap constraints;
    friend bool operator==(const ActionAvailabilityV2 &left,
                           const ActionAvailabilityV2 &right)
    {
        return left.state == right.state && left.reasonKey == right.reasonKey
            && left.constraints == right.constraints;
    }
};

struct SourceIdentityV2 {
    QString sourcePluginId;
    QString sourceInstanceId;
    QString accountId;
    QString displayName;
};

struct SourceDescriptorV2 {
    QString pluginPackageId;
    QString sourceId;
    QString name;
    QString version;
    int sdkAbi = 2;
    QHash<SourceActionV2, ActionAvailabilityV2> declaredActions;
};

struct CapabilitySetV2 {
    QHash<SourceActionV2, ActionAvailabilityV2> actions;
    ActionAvailabilityV2 action(SourceActionV2 key) const
    {
        return actions.value(key);
    }
};

struct SourceScopeV2 {
    QString sourceInstanceId; // empty means Aggregate
    bool isAggregate() const { return sourceInstanceId.isEmpty(); }
};

struct PageQueryV2 {
    MusicPageKindV2 page = MusicPageKindV2::Recommendation;
    PageSectionKindV2 section = PageSectionKindV2::RecentlyPlayed;
    SourceScopeV2 scope;
    QString searchText;
    QVariantMap filters;
    QString cursor;
    int limit = 50;
};
```

Add these exact fields in the same header:

```cpp
struct MediaItemV2 {
    MediaRefV2 ref;
    QString title;
    QString subtitle;
    QStringList artists;
    QString album;
    qint64 durationMs = 0;
    QString artworkId;
    QVariantMap externalIds;
    QVariantMap metadata;
    QHash<SourceActionV2, ActionAvailabilityV2> availableActions;
};

struct PageSectionV2 {
    QString sectionId;
    QString titleKey;
    PageSectionKindV2 kind = PageSectionKindV2::Tracks;
    QString layoutHint;
    QList<MediaItemV2> items;
    QString nextCursor;
    bool hasMore = false;
};

struct SourceErrorV2 {
    SourceErrorKindV2 kind = SourceErrorKindV2::Unknown;
    QString messageKey;
    QString detail;
    std::optional<int> httpStatus;
    bool retryable = true;
};

struct SourcePageStateV2 {
    SourcePageLoadStateV2 state = SourcePageLoadStateV2::Loading;
    std::optional<SourceErrorV2> error;
};

struct PageResultV2 {
    QList<PageSectionV2> sections;
    QHash<QString, SourcePageStateV2> sourceStates;
    bool cached = false;
    bool complete = true;
};

struct SourcePageResultV2 {
    QString sourceInstanceId;
    PageResultV2 page;
    std::optional<SourceErrorV2> error;
};

struct PlaylistChangeV2 {
    QList<MediaRefV2> tracksToAdd;
    QList<int> trackIndexesToRemove;
    QString newName;
};

struct ActionResultV2 {
    SourceActionV2 action = SourceActionV2::Play;
    MediaRefV2 subject;
    QVariantMap payload;
};

struct SourceConfigurationV2 {
    QString pluginPackageId;
    QString sourceId;
    QString sourceInstanceId;
    QString accountId;
    QString displayName;
    QVariantMap parameters;
    QByteArray secret;
};

struct StreamDescriptorV2 {
    MediaRefV2 media;
    QUrl url;
    QMap<QString, QString> headers;
    QString mimeType;
    QDateTime expiresAt;
    bool seekable = true;
};

struct SettingsFieldV2 {
    QString id;
    QString labelKey;
    SettingsFieldTypeV2 type = SettingsFieldTypeV2::Text;
    bool required = false;
    bool secret = false;
    QVariant defaultValue;
    QVariantList choices;
    QVariantMap constraints;
};

struct SettingsSectionV2 {
    QString id;
    QString titleKey;
    QList<SettingsFieldV2> fields;
};
using SettingsSchemaV2 = QList<SettingsSectionV2>;
```

Register every signal-crossing type with `Q_DECLARE_METATYPE`; implement JSON/QVariant conversion only for persisted or QML-facing values.

- [ ] **Step 4: Define typed plugin and provider contracts**

```cpp
#define QUEMUSIC_MUSIC_SOURCE_PLUGIN_V2_IID "org.quemusic.MusicSourcePlugin/2.0"
#define QUEMUSIC_MUSIC_SOURCE_SDK_V2_ABI 2

class IMusicSourceSessionV2 : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    ~IMusicSourceSessionV2() override = default;
    virtual SourceIdentityV2 identity() const = 0;
    virtual SourceSessionStateV2 state() const = 0;
    virtual CapabilitySetV2 capabilities() const = 0;
    virtual QUuid open() = 0;
    virtual void close() = 0;
    virtual void cancel(const QUuid &requestId) = 0;
signals:
    void requestStarted(QUuid requestId);
    void stateChanged(SourceSessionStateV2 state);
    void capabilitiesChanged(CapabilitySetV2 capabilities);
    void pageReady(QUuid requestId, PageResultV2 result);
    void streamReady(QUuid requestId, StreamDescriptorV2 stream);
    void actionCompleted(QUuid requestId, ActionResultV2 result);
    void requestFailed(QUuid requestId, SourceErrorV2 error);
};

class IMusicSourcePluginV2 {
public:
    virtual ~IMusicSourcePluginV2() = default;
    virtual SourceDescriptorV2 descriptor() const = 0;
    virtual IMusicSourceSessionV2 *createSession(const SourceConfigurationV2 &configuration,
                                                 QObject *parent) = 0;
};
Q_DECLARE_INTERFACE(IMusicSourcePluginV2, QUEMUSIC_MUSIC_SOURCE_PLUGIN_V2_IID)

class IPageProviderV2 {
public:
    virtual ~IPageProviderV2() = default;
    virtual QUuid fetchPage(const PageQueryV2 &query) = 0;
};

class IFavoriteProviderV2 {
public:
    virtual ~IFavoriteProviderV2() = default;
    virtual QUuid setFavorite(const MediaRefV2 &media, bool favorite) = 0;
};

class IPlaybackProviderV2 {
public:
    virtual ~IPlaybackProviderV2() = default;
    virtual QUuid resolveStream(const MediaRefV2 &media) = 0;
    virtual QUuid fetchArtwork(const MediaRefV2 &media) = 0;
    virtual QUuid fetchLyrics(const MediaRefV2 &media) = 0;
};

class IRatingProviderV2 {
public:
    virtual ~IRatingProviderV2() = default;
    virtual QUuid setRating(const MediaRefV2 &media, int rating) = 0;
};

class IScrobbleProviderV2 {
public:
    virtual ~IScrobbleProviderV2() = default;
    virtual QUuid scrobble(const MediaRefV2 &media, qint64 positionMs,
                           bool submission) = 0;
};

class IPlaylistProviderV2 {
public:
    virtual ~IPlaylistProviderV2() = default;
    virtual QUuid createPlaylist(const QString &name,
                                 const QList<MediaRefV2> &tracks) = 0;
    virtual QUuid updatePlaylist(const MediaRefV2 &playlist,
                                 const PlaylistChangeV2 &change) = 0;
    virtual QUuid deletePlaylist(const MediaRefV2 &playlist) = 0;
};

class IDownloadProviderV2 {
public:
    virtual ~IDownloadProviderV2() = default;
    virtual QUuid download(const MediaRefV2 &media, const QUrl &destination) = 0;
};

class IPlayQueueProviderV2 {
public:
    virtual ~IPlayQueueProviderV2() = default;
    virtual QUuid fetchPlayQueue() = 0;
    virtual QUuid savePlayQueue(const QList<MediaRefV2> &items,
                                const MediaRefV2 &current, qint64 positionMs) = 0;
};

class IBookmarkProviderV2 {
public:
    virtual ~IBookmarkProviderV2() = default;
    virtual QUuid fetchBookmarks() = 0;
    virtual QUuid createBookmark(const MediaRefV2 &media, qint64 positionMs,
                                 const QString &comment) = 0;
    virtual QUuid deleteBookmark(const MediaRefV2 &media) = 0;
};

class IPluginSettingsProviderV2 {
public:
    virtual ~IPluginSettingsProviderV2() = default;
    virtual SettingsSchemaV2 settingsSchema() const = 0;
};

#define QUEMUSIC_PAGE_PROVIDER_V2_IID "org.quemusic.source.PageProvider/2.0"
#define QUEMUSIC_PLAYBACK_PROVIDER_V2_IID "org.quemusic.source.PlaybackProvider/2.0"
#define QUEMUSIC_FAVORITE_PROVIDER_V2_IID "org.quemusic.source.FavoriteProvider/2.0"
#define QUEMUSIC_RATING_PROVIDER_V2_IID "org.quemusic.source.RatingProvider/2.0"
#define QUEMUSIC_SCROBBLE_PROVIDER_V2_IID "org.quemusic.source.ScrobbleProvider/2.0"
#define QUEMUSIC_PLAYLIST_PROVIDER_V2_IID "org.quemusic.source.PlaylistProvider/2.0"
#define QUEMUSIC_DOWNLOAD_PROVIDER_V2_IID "org.quemusic.source.DownloadProvider/2.0"
#define QUEMUSIC_PLAY_QUEUE_PROVIDER_V2_IID "org.quemusic.source.PlayQueueProvider/2.0"
#define QUEMUSIC_BOOKMARK_PROVIDER_V2_IID "org.quemusic.source.BookmarkProvider/2.0"
#define QUEMUSIC_PLUGIN_SETTINGS_PROVIDER_V2_IID "org.quemusic.source.PluginSettingsProvider/2.0"

Q_DECLARE_INTERFACE(IPageProviderV2, QUEMUSIC_PAGE_PROVIDER_V2_IID)
Q_DECLARE_INTERFACE(IPlaybackProviderV2, QUEMUSIC_PLAYBACK_PROVIDER_V2_IID)
Q_DECLARE_INTERFACE(IFavoriteProviderV2, QUEMUSIC_FAVORITE_PROVIDER_V2_IID)
Q_DECLARE_INTERFACE(IRatingProviderV2, QUEMUSIC_RATING_PROVIDER_V2_IID)
Q_DECLARE_INTERFACE(IScrobbleProviderV2, QUEMUSIC_SCROBBLE_PROVIDER_V2_IID)
Q_DECLARE_INTERFACE(IPlaylistProviderV2, QUEMUSIC_PLAYLIST_PROVIDER_V2_IID)
Q_DECLARE_INTERFACE(IDownloadProviderV2, QUEMUSIC_DOWNLOAD_PROVIDER_V2_IID)
Q_DECLARE_INTERFACE(IPlayQueueProviderV2, QUEMUSIC_PLAY_QUEUE_PROVIDER_V2_IID)
Q_DECLARE_INTERFACE(IBookmarkProviderV2, QUEMUSIC_BOOKMARK_PROVIDER_V2_IID)
Q_DECLARE_INTERFACE(IPluginSettingsProviderV2, QUEMUSIC_PLUGIN_SETTINGS_PROVIDER_V2_IID)
```

Optional interfaces contain methods only; request lifecycle, completion and error signals all come from `IMusicSourceSessionV2`. `open()` and every asynchronous provider method must emit `requestStarted(requestId)` before any synchronous terminal signal so the host can cancel every outstanding request without wrapping provider interfaces.

- [ ] **Step 5: Run focused tests**

Run:

```bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/cmake --build build/bridge --target quemusic_source_v2_types_test quemusic_source_v2_contract_test -j2
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/ctest --test-dir build/bridge -R 'source_v2_(types|contract)' --output-on-failure
```

Expected: both tests pass.

- [ ] **Step 6: Commit**

```bash
git add sdk/source/v2 tests/tst_SourceV2Types.cpp tests/tst_SourceV2Contract.cpp CMakeLists.txt
git commit -m "feat: define source sdk v2 contracts"
```

### Task 2: 让插件清单和加载器强制执行 v2 兼容边界

**Files:**
- Modify: `core/plugins/PluginManifest.h`
- Modify: `core/plugins/PluginManifest.cpp`
- Modify: `core/plugins/PluginManager.h`
- Modify: `core/plugins/PluginManager.cpp`
- Create: `tests/fixtures/FakeV2SourcePlugin.cpp`
- Create: `tests/fixtures/FakeV2SourcePlugin.h`
- Create: `tests/fixtures/v2-manifest.json.in`
- Modify: `tests/tst_PluginManifest.cpp`
- Modify: `tests/tst_PluginManager.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `IMusicSourcePluginV2` and IID from Task 1.
- Produces: manifests exposing `sourceSdkAbi()`, `requiredQtMajor()`, `requiredArchitecture()` and `requiredBuildKey()`; `PluginManager` loads only v2 source plugins and retains lease-based unload protection.

- [ ] **Step 1: Add failing manifest and loader tests**

```cpp
void PluginManifestTest::acceptsV2SourceInterface()
{
    QString error;
    const PluginManifest manifest = PluginManifest::fromFile(v2ManifestPath(), &error);
    QVERIFY2(manifest.isValid(), qPrintable(error));
    QCOMPARE(manifest.sourceSdkAbi(), 2);
    QCOMPARE(manifest.requiredQtMajor(), QT_VERSION_MAJOR);
}

void PluginManifestTest::rejectsV1SourceInterface()
{
    QString error;
    QVERIFY(!PluginManifest::fromFile(frozenV1ManifestPath(), &error).isValid());
    QVERIFY(error.contains("source plugin interface"));
}

void PluginManagerTest::refusesUnloadWhileV2SessionLeaseExists()
{
    QVERIFY(manager.load("org.quemusic.source.fixture-v2"));
    auto lease = manager.acquire("org.quemusic.source.fixture-v2");
    QCOMPARE(manager.unload("org.quemusic.source.fixture-v2"), PluginOperationResult::Busy);
    lease = {};
    QCOMPARE(manager.unload("org.quemusic.source.fixture-v2"), PluginOperationResult::Success);
}

void PluginManagerTest::repeatedReloadDoesNotAccumulateInstancesOrLeases()
{
    QVERIFY(manager.load("org.quemusic.source.fixture-v2"));
    for (int i = 0; i < 50; ++i)
        QCOMPARE(manager.reload("org.quemusic.source.fixture-v2"),
                 PluginOperationResult::Success);
    const auto spec = manager.plugin("org.quemusic.source.fixture-v2");
    QCOMPARE(spec.activeLeases, 0);
    QCOMPARE(FakeV2SourcePlugin::liveInstances(), 1);
}
```

- [ ] **Step 2: Run plugin tests and verify the v1-only loader fails expectations**

Run:

```bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/cmake --build build/bridge --target quemusic_plugin_manifest_test quemusic_plugin_manager_test -j2
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/ctest --test-dir build/bridge -R 'plugin_(manifest|manager)' --output-on-failure
```

Expected: new assertions fail because the parser still requires `MusicSourcePlugin/1.0`.

- [ ] **Step 3: Change the manifest contract to v2**

Use this manifest shape for all v2 source packages:

```json
{
  "id": "org.quemusic.source.fixture-v2",
  "sourceId": "fixture-v2",
  "name": "Fixture V2",
  "version": "2.0.0",
  "category": "source",
  "runtime": "native-qt",
  "library": "libquemusic_fixture_v2.dylib",
  "pluginApi": { "major": 1, "minHostMinor": 0 },
  "interfaces": [{ "id": "org.quemusic.MusicSourcePlugin/2.0", "version": "2.0" }],
  "runtimeRequirements": {
    "sourceSdkAbi": 2,
    "qtMajor": 6,
    "architecture": "arm64",
    "buildKey": "Release"
  }
}
```

The configured fixture substitutes the current architecture and build mode. `PluginManifest::fromFile()` must require the v2 IID and `sourceSdkAbi == 2`; runtime checks compare Qt major, architecture and build key before `QPluginLoader::load()`.

For every source package, `sourceId` is a stable slug: reject empty values and values containing `/`. Account IDs retain their existing storage semantics and may contain `/`; constraining the plugin-owned source slug keeps `sourceId/accountId` instance IDs unambiguous without migrating account keys.

- [ ] **Step 4: Cast loaded source instances to v2 and keep precise failure states**

```cpp
if (entry->manifest.category() == PluginCategory::Source
    && qobject_cast<IMusicSourcePluginV2 *>(instance) == nullptr) {
    loader->unload();
    fail(*entry, QStringLiteral("Package does not implement IMusicSourcePluginV2"));
    return false;
}
```

Expose `busyReason` in `PluginSpec`; when a lease blocks unload, set it to `source.sessions.active` without changing the loaded state. Clear it when the final lease is released.

- [ ] **Step 5: Run focused tests**

```bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/cmake --build build/bridge --target quemusic_plugin_manifest_test quemusic_plugin_manager_test -j2
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/ctest --test-dir build/bridge -R 'plugin_(manifest|manager)' --output-on-failure
```

Expected: manifest and manager tests pass, including v1 rejection and busy unload.

- [ ] **Step 6: Commit**

```bash
git add core/plugins tests/fixtures/FakeV2SourcePlugin.* tests/fixtures/v2-manifest.json.in tests/tst_PluginManifest.cpp tests/tst_PluginManager.cpp CMakeLists.txt
git commit -m "feat: enforce source sdk v2 loading"
```

### Task 3: 建立 v2 SourceRegistry 和稳定音源实例身份

**Files:**
- Create: `core/source/SourceRegistry.h`
- Create: `core/source/SourceRegistry.cpp`
- Create: `tests/tst_SourceRegistryV2.cpp`
- Modify: `core/media/SourceAccountStore.h`
- Modify: `core/media/SourceAccountStore.cpp`
- Modify: `tests/tst_SourceAccountStore.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: v2 plugin manager, `SourceConfigurationV2`, existing `SourceAccountStore` and `ISecretStore`.
- Produces: `SourceInstanceDescriptorV2`, `SourceRegistry::enabledInstances()`, `sessionFor()`, `closeInstance()` and per-session plugin leases.

The constructor and public lifetime API are fixed as:

```cpp
explicit SourceRegistry(PluginManager *plugins, SourceAccountStore *accounts,
                        QObject *parent = nullptr);
PluginManager *pluginManager() const;
QList<SourceInstanceDescriptorV2> enabledInstances() const;
IMusicSourceSessionV2 *sessionFor(const QString &sourceInstanceId);
bool enableInstance(const QString &sourceInstanceId);
bool disableInstance(const QString &sourceInstanceId);
bool closeInstance(const QString &sourceInstanceId);
void closeAll();
```

- [ ] **Step 1: Add failing identity and lifecycle tests**

```cpp
void SourceRegistryV2Test::twoAccountsCreateDistinctInstances()
{
    saveAccount("navidrome", "home", "Home");
    saveAccount("navidrome", "office", "Office");
    const auto instances = registry.enabledInstances();
    QCOMPARE(instances.size(), 2);
    QCOMPARE(instances[0].sourceInstanceId, "navidrome/home");
    QCOMPARE(instances[1].sourceInstanceId, "navidrome/office");
}

void SourceRegistryV2Test::closingInstanceReleasesPluginLease()
{
    auto *session = registry.sessionFor("navidrome/home");
    QVERIFY(session);
    QCOMPARE(pluginManager.plugin("org.quemusic.source.navidrome").activeLeases, 1);
    QVERIFY(registry.closeInstance("navidrome/home"));
    QCOMPARE(pluginManager.plugin("org.quemusic.source.navidrome").activeLeases, 0);
}
```

- [ ] **Step 2: Run and verify failure**

```bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/cmake --build build/bridge --target quemusic_source_registry_v2_test -j2
```

Expected: target or `SourceRegistry` symbols are missing.

- [ ] **Step 3: Implement stable instance identity without rewriting persisted account keys**

```cpp
inline QString sourceInstanceId(const QString &sourceId, const QString &accountId)
{
    return sourceId + QLatin1Char('/') + accountId;
}

struct SourceInstanceDescriptorV2 {
    QString pluginPackageId;
    QString sourceId;
    QString sourceInstanceId;
    QString accountId;
    QString displayName;
    bool enabled = true;
    SourceSessionStateV2 state = SourceSessionStateV2::Closed;
};
```

Keep existing settings groups keyed by `(sourceId, accountId)` and derive `sourceInstanceId` deterministically. This avoids rewriting stored secrets while making all new DTOs instance-aware.

`sourceId` is guaranteed slash-free by `PluginManifest`; `accountId` may contain `/`. Do not split an instance ID at every slash: resolve it against stored `(sourceId, accountId)` pairs or split only at the first separator.

- [ ] **Step 4: Implement lease-owned sessions and cancellation-first shutdown**

```cpp
struct SourceRegistry::SessionEntry {
    QPointer<IMusicSourceSessionV2> session;
    PluginLease lease;
    QSet<QUuid> activeRequests;
};

bool SourceRegistry::closeInstance(const QString &instanceId)
{
    auto it = m_sessions.find(instanceId);
    if (it == m_sessions.end()) return true;
    for (const QUuid &id : std::as_const(it->activeRequests)) it->session->cancel(id);
    it->session->close();
    delete it->session;
    m_sessions.erase(it); // destroys PluginLease last
    emit instanceChanged(instanceId);
    return true;
}
```

Use `QPointer`, remove completed request IDs, and close all sessions before registry destruction. A session can only be created from an enabled account whose secret can be read.

Connect the mandatory typed `IMusicSourceSessionV2::requestStarted` signal; do not depend on an optional dynamic signal invented by a fixture. Shutdown must mark the instance as closing and remove/take its session entry before invoking plugin virtual methods, while retaining the local lease until cancellation, close and destruction return. `sessionFor()` must reject per-instance or global shutdown, and `closeAll()` must block creation while draining all entries. A returned session is borrowed and registry-owned; validate/enforce registry parentage.

External destruction violates borrowed ownership: the host has no reliable notification after an arbitrary external deleting destructor returns. Queued events are not an unwind boundary. On detecting this violation, fail closed by pinning the offending loaded package (including its loader and root instance) for process lifetime, even across PluginManager destruction. Refuse unload, reload, failure-unload and new session acquisition for that package with a restart-required diagnostic; normal registry-owned close and unrelated packages remain unaffected. Coalesce repeated pins per loaded package and remove superseded deferred-release machinery. This focused safety correction may also modify `core/plugins/PluginManager.h/.cpp` and `tests/tst_PluginManager.cpp`.

- [ ] **Step 5: Run registry and storage tests**

```bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/cmake --build build/bridge --target quemusic_source_registry_v2_test quemusic_source_account_store_test -j2
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/ctest --test-dir build/bridge -R 'source_(registry_v2|account_store)' --output-on-failure
```

Expected: distinct identities, secret retrieval, disable/close and lease release tests pass.

- [ ] **Step 6: Commit**

```bash
git add core/source/SourceRegistry.* core/media/SourceAccountStore.* tests/tst_SourceRegistryV2.cpp tests/tst_SourceAccountStore.cpp CMakeLists.txt
git commit -m "feat: add v2 source instance registry"
```

### Task 4: 建立共享音源范围和页面模型

**Files:**
- Create: `core/music/SourceScopeStore.h`
- Create: `core/music/SourceScopeStore.cpp`
- Create: `core/music/MusicPageModel.h`
- Create: `core/music/MusicPageModel.cpp`
- Create: `tests/tst_MusicPageModel.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: v2 page DTOs and `QSettings`.
- Produces: persistent `SourceScopeStore::selectedSourceInstanceId`, `MusicPageModel::beginRequest()`, `applyResult()` and `applyFailure()` used by `MusicHub`.

- [ ] **Step 1: Write failing scope and stale-result tests**

```cpp
void MusicPageModelTest::sourceScopePersistsAcrossInstances()
{
    SourceScopeStore first(&settings);
    first.setSelectedSourceInstanceId("navidrome/home");
    SourceScopeStore restored(&settings);
    QCOMPARE(restored.selectedSourceInstanceId(), "navidrome/home");
}

void MusicPageModelTest::dropsResultsFromOldGeneration()
{
    MusicPageModel model(MusicPageKindV2::Recommendation);
    const quint64 oldGeneration = model.beginRequest();
    const quint64 currentGeneration = model.beginRequest();
    QVERIFY(!model.applyResult(oldGeneration, sampleResult("old")));
    QVERIFY(model.applyResult(currentGeneration, sampleResult("current")));
    QCOMPARE(model.section(0).titleKey, "current");
}
```

- [ ] **Step 2: Run and verify failure**

```bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/cmake --build build/bridge --target quemusic_music_page_model_test -j2
```

Expected: missing model and scope-store symbols.

- [ ] **Step 3: Implement the QML-facing page state**

```cpp
class MusicPageModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(PageLoadStateV2 state READ state NOTIFY stateChanged)
    Q_PROPERTY(bool cached READ cached NOTIFY cachedChanged)
    Q_PROPERTY(QVariantList sourceStates READ sourceStates NOTIFY sourceStatesChanged)
public:
    quint64 beginRequest();
    bool applyResult(quint64 generation, const PageResultV2 &result);
    bool applyFailure(quint64 generation, const SourceErrorV2 &error);
    Q_INVOKABLE QVariantMap itemAt(int section, int item) const;
};
```

Roles are `sectionId`, `title`, `layoutHint`, `items`, `hasMore`, `loadingMore` and `error`. `beginRequest()` increments the generation, sets loading state and keeps existing cached sections visible.

`applyResult()` upserts incoming sections by stable `sectionId`; it never clears sibling sections completed in the same generation. Add `finishGeneration(generation, expectedSections)` to derive Ready/Empty/Failed only after all expected sections have completed.

- [ ] **Step 4: Persist aggregate as an empty instance ID**

```cpp
void SourceScopeStore::setSelectedSourceInstanceId(const QString &value)
{
    if (m_selected == value) return;
    m_selected = value;
    m_settings->setValue("MusicHub/selectedSourceInstanceId", value);
    emit selectedSourceInstanceIdChanged();
}
```

An empty value means aggregate. Do not persist localized labels or array indexes.

- [ ] **Step 5: Run focused tests**

```bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/cmake --build build/bridge --target quemusic_music_page_model_test -j2
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/ctest --test-dir build/bridge -R music_page_model --output-on-failure
```

Expected: persistence, generation, cached-state and per-source error tests pass.

- [ ] **Step 6: Commit**

```bash
git add core/music/SourceScopeStore.* core/music/MusicPageModel.* tests/tst_MusicPageModel.cpp CMakeLists.txt
git commit -m "feat: add shared source scope and page models"
```

### Task 5: 实现多音源页面仓库与聚合器

**Files:**
- Create: `core/music/AggregateComposer.h`
- Create: `core/music/AggregateComposer.cpp`
- Create: `core/music/PageRepository.h`
- Create: `core/music/PageRepository.cpp`
- Create: `core/music/PageCache.h`
- Create: `core/music/PageCache.cpp`
- Create: `core/music/ArtworkCache.h`
- Create: `core/music/ArtworkCache.cpp`
- Create: `core/music/MediaAssetRepository.h`
- Create: `core/music/MediaAssetRepository.cpp`
- Create: `tests/tst_AggregateComposer.cpp`
- Create: `tests/tst_PageRepository.cpp`
- Create: `tests/tst_MusicCaches.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `SourceRegistry`, `IPageProviderV2`, `PageQueryV2` and `MusicPageModel` generation IDs.
- Produces: `AggregateComposer::compose()` and `PageRepository::requestPage()` with opaque aggregate cursors, cancellation and partial-success results.

`PageRepository` is constructed as `PageRepository(SourceRegistry *, AggregateComposer *, QObject *)` and emits `pageReady(QUuid, quint64, PageResultV2)` or `pageFailed(QUuid, quint64, SourceErrorV2)`.

- [ ] **Step 1: Write failing aggregation tests**

```cpp
void AggregateComposerTest::roundRobinsSourcesWithinSameSection()
{
    const auto result = composer.compose({sourcePage("home", {"h1", "h2"}),
                                          sourcePage("office", {"o1", "o2"})}, 4);
    QCOMPARE(ids(result.sections[0]), QStringList({"h1", "o1", "h2", "o2"}));
}

void AggregateComposerTest::deduplicatesOnlyWithReliableExternalId()
{
    auto a = track("home", "a", {{"isrc", "CN-A01-24-00001"}});
    auto b = track("office", "b", {{"isrc", "CN-A01-24-00001"}});
    auto c = track("office", "c", {{"title", a.title}});
    QCOMPARE(composer.composeItems({{a}, {b, c}}, 10).size(), 2);
}

void PageRepositoryTest::returnsPartialSuccessWhenOneSourceFails()
{
    requestAggregate();
    failSource("office", SourceErrorKindV2::Network);
    succeedSource("home", samplePage());
    QTRY_COMPARE(spy.count(), 1);
    const auto result = qvariant_cast<PageResultV2>(spy.takeFirst().at(2));
    QCOMPARE(result.sections.size(), 1);
    QCOMPARE(result.sourceStates.value("office").state, SourcePageLoadStateV2::Failed);
}

void MusicCachesTest::pageCacheNeverSerializesTransientOrSecretFields()
{
    PageResultV2 page = pageContainingMetadata({{"streamUrl", "https://secret/url"},
                                                {"password", "secret"},
                                                {"genre", "Jazz"}});
    cache.store(cacheKey(), page, now());
    const QByteArray disk = readCacheFile(cache.filePath(cacheKey()));
    QVERIFY(!disk.contains("streamUrl"));
    QVERIFY(!disk.contains("password"));
    QVERIFY(disk.contains("Jazz"));
}

void MusicCachesTest::stalePageIsReturnedAndMarkedCached()
{
    cache.store(cacheKey(), samplePage(), now().addSecs(-3600));
    const auto value = cache.lookup(cacheKey(), now(), std::chrono::minutes(5));
    QVERIFY(value.has_value());
    QVERIFY(value->cached);
    QVERIFY(value->stale);
}

void MusicCachesTest::artworkRepositoryReturnsLocalCachedUrlOnSecondRequest()
{
    const QUuid first = assets.requestArtwork(trackRef("42"));
    completeArtwork(first, QByteArray("image-bytes"), "image/jpeg");
    QTRY_COMPARE(artworkSpy.count(), 1);
    const QUuid second = assets.requestArtwork(trackRef("42"));
    QTRY_COMPARE(artworkSpy.count(), 2);
    QCOMPARE(provider.artworkRequestCount(), 1);
    QVERIFY(lastArtworkUrl(artworkSpy).isLocalFile());
}
```

- [ ] **Step 2: Run tests and verify failure**

```bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/cmake --build build/bridge --target quemusic_aggregate_composer_test quemusic_page_repository_test quemusic_music_caches_test -j2
```

Expected: the new classes are missing.

- [ ] **Step 3: Implement deterministic composition and opaque cursor encoding**

```cpp
struct AggregateCursorState {
    QHash<QString, QString> sourceCursors;
    QSet<QString> exhaustedSources;
    int nextSource = 0;
};

PageResultV2 AggregateComposer::compose(const QList<SourcePageResultV2> &inputs,
                                        int requestedLimit) const;
QString AggregateComposer::encodeCursor(const AggregateCursorState &state) const;
std::optional<AggregateCursorState> AggregateComposer::decodeCursor(const QString &cursor) const;
```

Encode compact JSON with URL-safe base64. Reject malformed cursors as `InvalidRequest`; do not expose individual provider cursors to QML. Deduplication keys are `isrc`, then `musicBrainzRecordingId`; absent reliable IDs never collapse rows.

- [ ] **Step 4: Implement repository fan-out, cancellation and partial completion**

```cpp
QUuid PageRepository::requestPage(const PageQueryV2 &query, quint64 generation)
{
    const QUuid groupId = QUuid::createUuid();
    const auto instanceIds = resolveScope(query.scope);
    PendingGroup group{query, generation, instanceIds.size()};
    m_groups.insert(groupId, group);
    for (const QString &instanceId : instanceIds)
        dispatchProvider(groupId, instanceId, queryForInstance(query, instanceId));
    return groupId;
}
```

Track `(groupId, instanceId, providerRequestId)`. Emit a result after every source completes or fails; when at least one succeeds, return `PageResultV2` with per-source failures instead of a page-level failure. `cancel(groupId)` cancels every outstanding provider request.

- [ ] **Step 5: Implement stale-while-refresh page and artwork caches**

```cpp
class PageCache final {
public:
    std::optional<CachedPageV2> lookup(const PageCacheKeyV2 &key, QDateTime now,
                                       std::chrono::seconds maxAge) const;
    bool store(const PageCacheKeyV2 &key, const PageResultV2 &page, QDateTime storedAt);
    void invalidateSource(const QString &sourceInstanceId);
};

class ArtworkCache final {
public:
    std::optional<QUrl> lookup(const QString &sourceInstanceId,
                               const QString &artworkId) const;
    QUrl store(const QString &sourceInstanceId, const QString &artworkId,
               const QByteArray &bytes, const QString &mimeType);
};

class MediaAssetRepository final : public QObject {
    Q_OBJECT
public:
    MediaAssetRepository(SourceRegistry *sources, ArtworkCache *artwork,
                         QObject *parent = nullptr);
    QUuid requestArtwork(const MediaRefV2 &media);
    QUuid requestLyrics(const MediaRefV2 &media);
    void cancel(const QUuid &requestId);
signals:
    void artworkReady(QUuid requestId, MediaRefV2 media, QUrl localUrl);
    void lyricsReady(QUuid requestId, MediaRefV2 media, QString lyrics);
    void failed(QUuid requestId, SourceErrorV2 error);
};
```

The page cache key includes page, section, source scope, search text, filters and cursor. Use a bounded memory LRU with access-order promotion, source invalidation and disk fallback. Before serialization, retain only the allowlisted media fields from `MediaItemV2`; never serialize stream URLs, request headers, secrets, cookies or tokens. Preserve validated action restrictions in both live results and cached DTOs, including `sameSourceOnly` (boolean) and `maxBitrate` (number). If a constraint cannot safely survive the allowlist, make that action `Unavailable` with a host-owned reason rather than dropping its restriction while retaining `Available` (Ruling 11). `PageRepository` emits a cached result immediately, then refreshes in the background. `ArtworkCache` accepts downloaded bytes and returns a local file URL; it never stores the authenticated remote URL.

A valid provider terminal may contain several sections, including Task10's three favorites sections. One non-cached complete PageResult counts as one query, regardless of rendered section count. Keep each returned section's overflow and continuation independent; accept and test this shape without weakening malformed-cursor or source-identity checks.

**Section continuation contract (Ruling 12):** Initial queries can produce several standard sections; every section receives an independent opaque token bound to that section kind and the page/scope/search/filter/limit identity. Task7 loadMore sends the clicked section's kind and token. Continuation provider requests/terminals concern only that requested section, even if the initial response was a multi-section bundle. Retain independent per-source buffers/cursors for each initial section; consuming one section must not advance another. Test initial three-section output, one-query completion accounting, interleaved continuation of two siblings, crossing a buffered/remote boundary, and rejection of a token reused for another section.

`MediaAssetRepository` routes by `sourceInstanceId`, returns cached artwork immediately, stores only downloaded bytes, and tracks provider request IDs so page changes and plugin unload can cancel them. Lyrics remain in memory for the current session unless a later cache policy explicitly permits persistence.

- [ ] **Step 6: Run focused tests**

```bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/cmake --build build/bridge --target quemusic_aggregate_composer_test quemusic_page_repository_test quemusic_music_caches_test -j2
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/ctest --test-dir build/bridge -R '(aggregate_composer|page_repository|music_caches)' --output-on-failure
```

Expected: mixed success, full failure, cancellation, pagination and deterministic ordering tests pass.

- [ ] **Step 7: Commit**

```bash
git add core/music/AggregateComposer.* core/music/PageRepository.* core/music/PageCache.* core/music/ArtworkCache.* core/music/MediaAssetRepository.* tests/tst_AggregateComposer.cpp tests/tst_PageRepository.cpp tests/tst_MusicCaches.cpp CMakeLists.txt
git commit -m "feat: aggregate page feeds across sources"
```

### Task 6: 实现能力解析和类型化操作路由

**Files:**
- Create: `core/music/CapabilityResolver.h`
- Create: `core/music/CapabilityResolver.cpp`
- Create: `core/music/MediaActionRouter.h`
- Create: `core/music/MediaActionRouter.cpp`
- Create: `tests/tst_CapabilityResolver.cpp`
- Create: `tests/tst_MediaActionRouterV2.cpp`
- Modify: `sdk/source/v2/SourceV2Types.h`
- Modify: `sdk/source/v2/SourceV2Types.cpp`
- Modify: `tests/tst_SourceV2Types.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: session capability descriptors, account permissions, `MediaItemV2::availableActions` and optional provider interfaces.
- Produces: `CapabilityResolver::resolve()` and explicit action methods on `MediaActionRouter` with no generic string-command fallback.

**Contract correction (Ruling 10):** Task1's single mutable `CapabilitySetV2::actions` map cannot represent the two negotiated layers. In this task replace it with `serverActions` and `accountActions`, both keyed by `SourceActionV2`, add `serverAction()` and `accountAction()`, and retain `action()` only as a derived intersection accessor. Missing server/account entries yield `Unavailable` with explicit unknown-capability/unknown-permission reason keys, not a grant. Plugin/media omission remains `Unsupported`. Introduce one pure SDK intersection helper reused by `CapabilitySetV2::action()` and `CapabilityResolver`, so state precedence and constraint merging cannot diverge. Adapt the exact existing v2 consumers/tests; do not add a compatibility actions map. This unreleased v2 contract may change as the user authorized.

The router obtains server/account layers from the live session, not editable account settings or cached QML values. A supplied item action can further restrict but never grant plugin/server/account capability. Validate the complete owning identity before dispatch; any compound playlist change must satisfy each requested action and same-instance membership. Unknown negotiated permissions must fail before provider invocation, and tests must distinguish two accounts on the same plugin. Constructor wiring, synchronous provider completion and returned host request-ID correlation must be documented for Task7.

Media/playlist map arguments mean the full item shape returned by `MusicPageModel::itemAt()` (`ref` and `availableActions`), not a bare ref (Ruling 13). Parse that actual shape in router tests. Current model action keys are decimal enum strings and availability states are integers; do not assume named/string values or silently grant missing media actions. `createPlaylist(sourceInstanceId, name)` is the explicitly object-free operation. Task13 callers pass the complete item so per-object restrictions reach the resolver; a later presentation mapping must be deliberate and tested.

- [ ] **Step 1: Write failing four-layer capability tests**

```cpp
void CapabilityResolverTest::forbiddenAccountOverridesDeclaredSupport()
{
    const auto result = resolver.resolve(SourceActionV2::Download,
                                         availablePlugin(), availableServer(),
                                         forbiddenAccount("source.permission.download"),
                                         availableMedia());
    QCOMPARE(result.state, AvailabilityV2::Forbidden);
    QCOMPARE(result.reasonKey, "source.permission.download");
}

void CapabilityResolverTest::sameSourceConstraintRejectsForeignPlaylistItem()
{
    const auto result = resolver.canAddToPlaylist(navidromePlaylist("home"),
                                                   localTrack("local/default"));
    QCOMPARE(result.state, AvailabilityV2::Unsupported);
    QVERIFY(result.constraints.value("sameSourceOnly").toBool());
}
```

- [ ] **Step 2: Write failing router tests**

```cpp
void MediaActionRouterV2Test::favoriteRoutesToOwningInstance()
{
    router.setFavorite(track("navidrome/home", "42"), true);
    QCOMPARE(homeFavoriteProvider.calls(), 1);
    QCOMPARE(officeFavoriteProvider.calls(), 0);
}

void MediaActionRouterV2Test::unsupportedActionFailsWithoutCallingProvider()
{
    router.download(trackWithoutDownload(), destination);
    QTRY_COMPARE(failureSpy.count(), 1);
    QCOMPARE(downloadProvider.calls(), 0);
}
```

- [ ] **Step 3: Run and verify failure**

```bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/cmake --build build/bridge --target quemusic_capability_resolver_test quemusic_media_action_router_v2_test -j2
```

Expected: missing resolver and router targets.

- [ ] **Step 4: Implement precedence and typed routing**

```cpp
ActionAvailabilityV2 CapabilityResolver::resolve(
    SourceActionV2 action,
    const ActionAvailabilityV2 &plugin,
    const ActionAvailabilityV2 &server,
    const ActionAvailabilityV2 &account,
    const ActionAvailabilityV2 &media) const;

class MediaActionRouter final : public QObject {
    Q_OBJECT
public:
    Q_INVOKABLE QUuid setFavorite(const QVariantMap &media, bool favorite);
    Q_INVOKABLE QUuid setRating(const QVariantMap &media, int rating);
    Q_INVOKABLE QUuid download(const QVariantMap &media, const QUrl &destination);
    Q_INVOKABLE QUuid createPlaylist(const QString &sourceInstanceId, const QString &name);
    Q_INVOKABLE QUuid updatePlaylist(const QVariantMap &playlist, const QVariantMap &change);
    Q_INVOKABLE QUuid deletePlaylist(const QVariantMap &playlist);
    Q_INVOKABLE QUuid setBookmark(const QVariantMap &media, qint64 positionMs);
signals:
    void actionSucceeded(QUuid requestId, QVariantMap result);
    void actionFailed(QUuid requestId, QVariantMap error);
};
```

Precedence is `Unsupported` first when a required provider interface is absent, then `Forbidden`, then `Unavailable`, then `Available`. Preserve the most specific non-empty reason and merge constraints without weakening an earlier restriction.

Add tests for independently reported server/account states, missing account grants, derived `CapabilitySetV2::action()`, conflict-safe constraint merging and live capability changes after a page item was created. If an unknown constraint conflict cannot be interpreted safely, return `Unavailable` with a reason rather than choose a permissive value.

- [ ] **Step 5: Run focused tests**

```bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/cmake --build build/bridge --target quemusic_capability_resolver_test quemusic_media_action_router_v2_test -j2
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/ctest --test-dir build/bridge -R '(capability_resolver|media_action_router_v2)' --output-on-failure
```

Expected: precedence, provider absence, account permissions, same-source constraints and exact-instance routing pass.

- [ ] **Step 6: Commit**

```bash
git add core/music/CapabilityResolver.* core/music/MediaActionRouter.* sdk/source/v2/SourceV2Types.* tests/tst_SourceV2Types.cpp tests/tst_CapabilityResolver.cpp tests/tst_MediaActionRouterV2.cpp
# Stage only this task's CMake hunks; preserve unrelated worktree changes.
git commit -m "feat: route media actions by effective capability"
```

### Task 7: 建立 MusicHub 和四个页面 ViewModel

**Files:**
- Create: `core/music/MusicHub.h`
- Create: `core/music/MusicHub.cpp`
- Create: `tests/tst_MusicHub.cpp`
- Modify: `core/music/MusicPageModel.h`
- Modify: `core/music/MusicPageModel.cpp`
- Modify: `tests/tst_MusicPageModel.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `SourceScopeStore`, `SourceRegistry`, `PageRepository`, four `MusicPageModel` instances and `MediaActionRouter`.
- Produces: QML properties `recommendation`, `category`, `favorites`, `searchResults`, `sourceOptions`, `selectedSourceInstanceId` and explicit refresh/search/load-more methods.

Use this constructor throughout later tasks:

```cpp
explicit MusicHub(SourceRegistry *sources, SourceScopeStore *scope,
                  QSettings *cacheSettings, QObject *parent = nullptr);
```

- [ ] **Step 1: Write failing shared-scope and request tests**

```cpp
void MusicHubTest::scopeChangeRefreshesAllActivatedPages()
{
    hub.activatePage(MusicPageKindV2::Recommendation);
    hub.activatePage(MusicPageKindV2::Favorites);
    hub.setSelectedSourceInstanceId("navidrome/home");
    QCOMPARE(repository.lastQuery(MusicPageKindV2::Recommendation).scope.sourceInstanceId,
             "navidrome/home");
    QCOMPARE(repository.lastQuery(MusicPageKindV2::Favorites).scope.sourceInstanceId,
             "navidrome/home");
}

void MusicHubTest::searchUsesSameScope()
{
    hub.setSelectedSourceInstanceId("navidrome/home");
    hub.search("Miles Davis");
    QCOMPARE(repository.lastQuery(MusicPageKindV2::Search).searchText, "Miles Davis");
    QCOMPARE(repository.lastQuery(MusicPageKindV2::Search).scope.sourceInstanceId,
             "navidrome/home");
}

void MusicHubTest::disabledSelectedSourceIsPreservedAndExplained()
{
    hub.activatePage(MusicPageKindV2::Recommendation);
    hub.setSelectedSourceInstanceId("navidrome/home");
    registry.disableInstance("navidrome/home");
    QCOMPARE(hub.selectedSourceInstanceId(), "navidrome/home");
    QCOMPARE(hub.recommendation()->state(), PageLoadStateV2::Failed);
    QCOMPARE(hub.recommendation()->error().messageKey, "source.instance.disabled");
}
```

- [ ] **Step 2: Run and verify failure**

```bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/cmake --build build/bridge --target quemusic_music_hub_test -j2
```

Expected: `MusicHub` is missing.

- [ ] **Step 3: Implement the QML boundary**

```cpp
class MusicHub final : public QObject {
    Q_OBJECT
    Q_PROPERTY(MusicPageModel *recommendation READ recommendation CONSTANT)
    Q_PROPERTY(MusicPageModel *category READ category CONSTANT)
    Q_PROPERTY(MusicPageModel *favorites READ favorites CONSTANT)
    Q_PROPERTY(MusicPageModel *searchResults READ searchResults CONSTANT)
    Q_PROPERTY(QVariantList sourceOptions READ sourceOptions NOTIFY sourceOptionsChanged)
    Q_PROPERTY(QString selectedSourceInstanceId READ selectedSourceInstanceId
               WRITE setSelectedSourceInstanceId NOTIFY selectedSourceInstanceIdChanged)
    Q_PROPERTY(MediaActionRouter *actions READ actions CONSTANT)
    Q_PROPERTY(QVariantMap categoryContext READ categoryContext NOTIFY categoryContextChanged)
    Q_PROPERTY(bool canNavigateBack READ canNavigateBack NOTIFY categoryContextChanged)
public:
    QVariantMap categoryContext() const;
    bool canNavigateBack() const;
    Q_INVOKABLE void activatePage(int pageKind);
    Q_INVOKABLE void refresh(int pageKind);
    Q_INVOKABLE void loadMore(int pageKind, const QString &sectionId);
    Q_INVOKABLE void search(const QString &text);
    Q_INVOKABLE void cancel(int pageKind);
    Q_INVOKABLE void retrySection(int pageKind, const QString &sectionId);
    Q_INVOKABLE bool browse(const QVariantMap &item);
    Q_INVOKABLE bool navigateBack();
    Q_INVOKABLE QUuid loadArtwork(const QVariantMap &media);
    Q_INVOKABLE QUuid loadLyrics(const QVariantMap &media);
    Q_INVOKABLE void cancelAsset(const QUuid &requestId);
signals:
    void artworkReady(QUuid requestId, QVariantMap media, QUrl localUrl);
    void lyricsReady(QUuid requestId, QVariantMap media, QString lyrics);
    void assetFailed(QUuid requestId, QVariantMap error);
    void categoryContextChanged();
};
```

`sourceOptions` starts with `{sourceInstanceId: "", displayName: "全部音源", available: true}` followed by enabled registry instances. Only activated pages auto-refresh on scope changes; inactive pages refresh when first activated.

If the selected instance is disabled or removed, retain a non-available option for that exact ID with a host-owned reason key. Do not silently select aggregate or leave the selector blank. A shared scope change invalidates old-context rows in all four stable model objects immediately; only activated pages issue new requests. Same-query refresh may retain cached rows. Changed search text or drill-down filters must not display rows from the old query as if they belong to the new one.

`loadArtwork()` and `loadLyrics()` accept the five-field MediaRef map (the item's `ref`, unlike full-item action/browse inputs) and delegate to `MediaAssetRepository`; the hub converts typed results to QML-safe values and never exposes authenticated remote URLs. Malformed references get deferred, correlated host failures; use the returned request ID to cancel or match the terminal signal.

Use an explicit standard-section map so page composition is deterministic:

```cpp
QList<PageSectionKindV2> MusicHub::sectionsForPage(MusicPageKindV2 page)
{
    switch (page) {
    case MusicPageKindV2::Recommendation:
        return {PageSectionKindV2::RecentlyPlayed,
                PageSectionKindV2::FrequentlyPlayed,
                PageSectionKindV2::HighestRated,
                PageSectionKindV2::Newest,
                PageSectionKindV2::Random};
    case MusicPageKindV2::Category:
        return {PageSectionKindV2::Genres, PageSectionKindV2::Artists,
                PageSectionKindV2::Albums, PageSectionKindV2::Tracks};
    case MusicPageKindV2::Favorites:
        return {PageSectionKindV2::FavoriteTracks,
                PageSectionKindV2::FavoriteAlbums,
                PageSectionKindV2::FavoriteArtists,
                PageSectionKindV2::Playlists};
    case MusicPageKindV2::Search:
        return {PageSectionKindV2::SearchResults};
    }
    return {};
}
```

`refresh(page)` starts one generation, dispatches one repository request per listed section, and merges each completed section by `sectionId` without erasing successful sibling sections. The page reaches `Ready` when every section has completed and at least one has data; all-section failure becomes `Failed`.

`loadMore(pageKind, sectionId)` uses that rendered section's kind and opaque token, preserving the originating page, selected scope, search text, filters and limit (Ruling 12). Never reuse a sibling's token or the initial bundle's section kind for a different clicked section. Each continuation updates only its target section and leaves sibling rows and continuation positions unchanged.

**Model lifecycle integration (Ruling 14):** Extend `MusicPageModel` with narrowly scoped, generation-checked reset, cancellation and per-section update APIs. Keep all existing Task4 full-refresh contracts. A section continuation appends to its existing items, updates only its cursor/loading/error state, and cannot prune siblings. A one-section retry replaces only that section; a failed continuation retains prior items and its retryable cursor. Permit independent section requests but suppress duplicate requests for the same section. On page cancellation invalidate in-flight callbacks, clear all loading indicators, retain accepted rows, and settle to `Ready` when rows contain items or `Idle` otherwise; do not synthesize a successful empty result. Cancelled asset IDs likewise suppress queued terminal notifications, without cancelling another delegate's request. Include request IDs in asset signals so identical-media delegates can correlate independently.

**Browse context (Ruling 15):** Add observable `categoryContext` (`item` full map and `sourceInstanceId`, empty at root) and `canNavigateBack` properties. `browse(item)` validates all MediaRef identity fields against the owning registry instance before changing state. Album -> Category/Tracks with `albumId`; artist -> Category/Albums with `artistId`; playlist -> Category/Tracks with `playlistId`; genre -> Category/Tracks with `genre` using its typed entity ID. A detail request uses the entity's own instance even in aggregate mode, without changing the shared selectedSourceInstanceId. Track/unsupported directory or malformed/foreign references return false without changing context. Keep a small in-memory category query-context stack: `navigateBack()` restores the previous query and refreshes it, returning false at root. Global scope changes clear this stack and return Category to its root before fetching; never transfer a source-owned filter to another source. No new top-level detail page or plugin-specific host code.

When the shared scope names a specific instance, reject `browse(item)` from a different instance without changing context, even if that other instance exists. This also rejects a late click from an old delegate after a scope change; aggregate mode accepts any otherwise-valid owning instance. Add this case to the browse tests.

Add focused tests with controlled fake providers for: changed scope/search dropping old rows without changing model pointers; load-more appending only its section after full refresh has finished; two independent sibling tokens; duplicate-terminal deduplication; cancellation clearing page/section loading and ignoring late callbacks; retry replacing only its target; disabled-selected option retention; two same-media asset IDs with only one cancelled; and source-bound album/artist/playlist/genre browse plus back navigation without global scope mutation. Example assertions use real returned provider/repository IDs, not guessed generation counts:

```cpp
auto *const stableCategory = hub.category();
hub.setSelectedSourceInstanceId("");
QVERIFY(hub.browse(albumItemFromHome));
QCOMPARE(hub.selectedSourceInstanceId(), QString());
QCOMPARE(hub.categoryContext().value("sourceInstanceId").toString(), "navidrome/home");
QVERIFY(hub.canNavigateBack());
QVERIFY(hub.navigateBack());
QCOMPARE(hub.category(), stableCategory);
QVERIFY(hub.categoryContext().isEmpty());
```

- [ ] **Step 4: Run focused tests**

```bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/cmake --build build/bridge --target quemusic_music_hub_test -j2
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/ctest --test-dir build/bridge -R music_hub --output-on-failure
```

Expected: shared scope, activation, cancellation, partial errors and source option updates pass.

- [ ] **Step 5: Commit**

```bash
git add core/music/MusicHub.* core/music/MusicPageModel.* tests/tst_MusicHub.cpp tests/tst_MusicPageModel.cpp
# Stage only Task7 CMake hunks; preserve pre-existing user edits.
git commit -m "feat: expose unified music hub to qml"
```

### Task 8: 用声明式 schema 重做插件设置与多实例账号管理

#### Task 8a: 通用配置与命名凭据的存储前置（先独立实现和审查）

Task8 分为 8a 存储、8b 控制器/会话生命周期、8c QML 三个顺序审查单元；不改 Task9–14 编号。只有三个单元都完成才记 Task8 complete。下面旧控制器示例的 legacy upsert 调用仅为历史草图，必须由本节的 v2 保存路径替代；不得直接照抄。8b/8c 在派发前补齐显示条件、声明动作、Registry 重配置通知和确切 QML 接口，不属于8a。

**8a Files (exclusive write scope):**
- Create: `sdk/source/v2/SourceSecretsV2.h`, `sdk/source/v2/SourceSecretsV2.cpp`
- Create: `core/settings/SourceSettingsValidation.h`, `core/settings/SourceSettingsValidation.cpp`
- Modify: `core/media/SourceAccountStore.h`, `core/media/SourceAccountStore.cpp`
- Create: `tests/tst_SourceSettingsStorage.cpp`
- Modify: `tests/tst_SourceAccountStore.cpp` only if required for directly related legacy regression coverage
- Modify: `CMakeLists.txt` only scoped source/link additions and new `quemusic_source_settings_storage_test` target

**8a contracts:**
1. Retain legacy `upsert(SourceAccount,...)` restrictions (only string serverUrl/username parameters). Add a distinctly named schema-aware v2 saving API; do not permit arbitrary QVariantMap persistence through the old API.
2. V2 input includes package ID, source/account identity, display name, enabled state, schema and draft. Derive instance ID from sourceId/accountId; reject empty package/account/source and slash-containing source. Match and preserve existing encoded/legacy record groups and account/credential identities. Metadata record v2 stores pluginPackageId and positive configuration version separately from record format version. Existing record v1 remains readable, no eager migration.
3. Pure validation/splitting in host code checks unique safe section/field IDs (ASCII letters/digits/dot/underscore/hyphen, nonempty, no slash), recognized field enums, exact primitive value types, required fields, choices and constraints. Text/Secret/Url/Directory take QString; Boolean takes bool; Integer takes finite integral numeric value in the JSON safe-integer range (not bool/numeric string). Choice uses declared nonempty scalar choices of QString/bool/safe integer with exact type category matching. URL must be absolute http(s), have host and no embedded userinfo; Directory is a path string, no filesystem access in validation. Allow min/max for Integer and minLength/maxLength/pattern for string fields; invalid bounds/patterns, unknown constraints, unknown draft fields and nonprimitive values fail closed with host-owned error keys, never value-bearing diagnostics. Do not persist secret defaults. Ordinary missing fields on edit retain stored values; create may use valid non-secret defaults.
4. `type == Secret` is always secret regardless of flag. Other `secret == true` fields must be string-valued field types (Text/Url/Directory); unsupported combinations reject the schema. Values classified secret never enter ordinary parameters. Require the controller to serialize only validated public schema metadata later; no QML here.
5. Add pure SDK named-secret envelope helpers using `QMap<QString,QByteArray>`: prefix `QueMusic.SourceSecrets/2\n` followed by compact JSON object mapping safe field IDs to canonical base64 strings. Strict decode rejects malformed prefix payload, invalid keys/base64, nonstring JSON values, duplicate keys and oversize envelope (>1MiB). Duplicate JSON keys must not silently win. Empty envelope/map is valid. This is an encoding, NOT encryption; bytes only belong in the existing secure store or C++ session configuration. No provider signature change. Legacy raw secret input is distinguished explicitly, never treated as an envelope after malformed-marker detection. Helpers must be independent of application classes.
6. New v2 saves write nonempty supplied string secret values as UTF-8 bytes into that envelope via ISecretStore. On editing, omitted or empty secret fields preserve previous credentials; a metadata-only edit keeps the EXACT secret reference and makes ZERO secure-store read/write/remove calls. Partial secret edits read/merge securely in C++ and rotate only after all validation. Existing raw secret can map to the sole declared secret field; multiple fields + legacy raw requires explicit re-entry of all required credentials (do not guess a destination). If all declared credentials are replaced, do not require reading the previous secret. Never leak raw/backend error detail to v2 callers.
7. Metadata v2 may store configuredSecretFieldIds, never values, enabling per-field indicators without reading credentials. A v1 reference can be preserved on metadata-only editing when exactly one secret field gives unambiguous required-field/indicator semantics; do not migrate/rewrite bytes on such edit. Persist an explicit raw-vs-envelope format marker when preserving raw under record v2, so future partial edits know how to interpret it. Incompatible schema changes to existing secret IDs/format must return a host-owned re-entry-required error, not silently drop or expose credentials.
8. New accounts whose schema has no configured/required secrets work with nullptr or unavailable secure store; record v2 permits empty reference. Read/sourceAccount, setEnabled and remove must support those accounts without invoking secure store. V1's existing requirement for reference remains unchanged. Accounts requiring credentials still fail safely when secure backend is absent.
9. Validate all draft/schema before storage IO. Persist metadata and credential reference with existing rollback guarantees; preserve old metadata/reference if writing new secret or metadata fails, clean up new secrets on failure; retain established old-secret cleanup failure handling. Removal restores metadata on secure cleanup failure. Do not widen QSettings path injection or accidentally rewrite another source/account. Tests use temporary INI and fake stores only, never user's Keychain/settings/NAS.
10. Host validation outputs contain separated public parameters and only newly supplied secret updates, with explicit preservation intent; no QObject or live plugin pointer retention. Document final API for Task8b/9 consumers in report. Existing SourceRegistry can continue passing SourceAccount.secret bytes unchanged; Task9 explicitly decodes named credentials with SDK helper and handles single-field legacy raw.

**8a TDD and verification:**
- First add failing tests for generic directory/bool/integer/choice roundtrip, invalid/unknown/secret-looking draft rejection, Secret-type flag mismatch safety, missing required fields, schema/constraint validation, envelope roundtrip/malformed/duplicate-key/size guards, no-secret account lifecycle without backend, two-secret creation/partial-update preservation, empty-edit same reference + zero secure calls, legacy raw single-field preservation/multi-field ambiguity, and write/cleanup failure rollback.
- Ensure preexisting legacy arbitrary-parameter rejection and encoded slash-bearing account IDs still pass. Inspect actual existing fake/test conventions and reuse them where appropriate.
- Run new focused target plus account-store and source-registry targets selected by actual CTest names; run full build and full suite once when implementation is green. Use Qt/CMake already configured in build/bridge, timeouts30–45, local fixtures only. Local-listener tests may need scoped sandbox escalation.
- Self-review and commit only owned files/hunks. Never stage all CMake/user changes.
- Report RED/GREEN commands, exact output summaries, API/wire contract, changed paths, commit IDs and concerns to controller. Do not implement controller, schema visibility/actions, Registry refresh API, QML, Navidrome changes or app registration in8a.

#### Task 8b: 通用设置控制器、临时会话与配置失效通知

本节取代旧 Task8 的控制器伪代码；8a 已完成。仅实现本节，不修改 QML 或应用入口。Task8c 消费以下明确接口。所有新原生 ABI 仍属未发布 v2，不增加兼容层。

**Files:**
- Create: `core/settings/PluginSettingsController.h/.cpp` (public snapshots/selection/CRUD/plugin actions)
- Create: `core/settings/PluginSettingsOperation.h/.cpp` (owned ephemeral open/action lifecycle)
- Create: `core/settings/SettingsSchemaPresentation.h/.cpp` (validate/detach/serialize public schema and visibility)
- Modify: `core/settings/SourceSettingsValidation.h/.cpp` (typed visibility/action schema validation)
- Modify: `core/media/SourceAccountStore.h/.cpp` (shared nonpersisting draft configuration preparation)
- Modify: `core/source/SourceRegistry.h/.cpp` (post-save/remove invalidation and package identity check)
- Modify: `sdk/source/v2/SourceV2Types.h`, `sdk/source/v2/ISourceProvidersV2.h`, `sdk/source/v2/IMusicSourceSessionV2.h`
- Create: `tests/fixtures/SettingsV2FixturePlugin.h/.cpp` (buildable local native plugin with controllable synchronous/asynchronous behavior)
- Create: `tests/tst_PluginSettingsController.cpp`, `tests/tst_PluginSettingsOperation.cpp`
- Modify: `tests/tst_SourceSettingsStorage.cpp`, `tests/tst_SourceRegistryV2.cpp`, `tests/tst_SourceV2Contract.cpp`
- Modify: `CMakeLists.txt` only owned new sources/link/test/fixture hunks
- Modify narrowly: `core/plugins/PluginManager.h/.cpp` and `tests/tst_PluginManager.cpp` for the Ruling19 callable-lease lifetime prerequisite below; no unrelated manager refactor.
- No QML, no real Navidrome/Admin calls or system Keychain/user settings access in tests.

**Ruling19 lifecycle prerequisite:** A safely reproduced test showed that manager destruction unloads a root even while a valid callable lease survives. The lease must retain its loaded package/root independently of the manager facade until normal owned cleanup and the last callable lease complete. Normal healthy leases must not permanently pin packages; ordinary unload/reload controls remain functional. Provide lease-level permanent pin capability for the existing R7 ownership violation when the manager has already disappeared (including manager deletion inside a factory before it returns a foreign-parent session). A permanent pin must remain keyed to the library and survive replacement managers/process teardown as before. Avoid release callbacks touching a partly destroyed manager, preserve reentrant acquire/release notification safety, and cover manager-before-lease destruction plus ordinary last-lease cleanup and manager-gone foreign ownership with safe regressions. Do not invoke unmapped plugin code in RED tests. This is a bounded prerequisite fix, not a relaxation of dependency-loss cancellation or ownership guarantees.

**Consumes:** PluginManager acquire/pluginInstance/spec/notifications; SourceRegistry borrowed sessions; SourceAccountStore saveValidatedV2(SourceAccountSaveV2, error), storedAccount/accounts; SDK named-secret helpers; existing schema validator. Field IDs/source IDs/account IDs and named/raw wire retain Task8a semantics. Current SourceRegistry enabledInstances() includes disabled descriptors; sessionFor() creates/opens, so do not call it to display settings.

**Typed SDK extensions:**
```cpp
enum class SettingsComparisonV2 { Equal, NotEqual };
struct SettingsVisibilityConditionV2 {
    QString fieldId;
    SettingsComparisonV2 comparison = SettingsComparisonV2::Equal;
    QVariant value;
};
// Append std::optional<SettingsVisibilityConditionV2> visibleWhen to SettingsFieldV2.
struct SettingsActionDescriptorV2 {
    QString id;
    QString labelKey;
    bool requiresConfirmation = false;
};
// Append QList<SettingsActionDescriptorV2> actions to SettingsSectionV2.
struct SettingsActionCapabilitiesV2 {
    QHash<QString, ActionAvailabilityV2> serverActions;
    QHash<QString, ActionAvailabilityV2> accountActions;
};
class ISettingsActionProviderV2 {
public:
    virtual ~ISettingsActionProviderV2() = default;
    virtual SettingsActionCapabilitiesV2 settingsCapabilities() const = 0;
    virtual QUuid runSettingsAction(const QString &actionId) = 0;
};
// IID org.quemusic.source.SettingsActionProvider/2.0; Q_DECLARE_INTERFACE.
// Add session signal: void settingsActionCompleted(QUuid requestId, QString actionId);
```
Keep optional plugin settingsSchema() interface independent. Settings actions are nonmedia: intersect declared action + serverActions + accountActions (no fabricated media layer). Missing declaration is Unsupported; missing runtime layer is Unavailable; preserve standard severity precedence; any constraints are unsupported for this action interface and fail closed. Map errors/reasons to host-owned keys, not arbitrary provider strings. No SourceActionV2 admin enum or arbitrary payload. Settings actions are only infrastructure tested with a fake diagnostic action; actual server administration remains excluded. Registry tracks the new terminal signal when present just like actionCompleted.

**Visibility rules:** validate whole schema before use, unique action IDs across sections using same safe ID syntax. Conditions reference an existing nonsecret field other than itself, use a known comparison enum and a primitive compatible with the target field/choice category; reject condition dependency cycles. Evaluate comparisons using merged public draft/previous/default values, not field visibility recursively. Missing comparison target value means invisible for both operators. Required is enforced only when visible; explicit supplied values (even hidden) still undergo type/constraint validation, and hidden existing valid values/credentials remain preserved. Do not drop hidden fields from storage or treat visibility as authorization. Empty secret input remains preservation. Extend the shared validator, not a second controller validator. Required-secret resolution in store must use evaluated visibility too, including ambiguous legacy raw re-entry.

**C++ draft configuration (zero writes):**
```cpp
std::optional<SourceConfigurationV2> SourceAccountStore::configurationForDraftV2(
    const SourceAccountSaveV2 &request, QString *error = nullptr);
bool SourceRegistry::configurationChanged(const QString &sourceInstanceId);
```
configurationForDraftV2 shares validation, previous-record identity checks, secret-format/schema evolution and merge with saveValidatedV2. Refactor internal helpers rather than copy the existing transaction body into controller. Resolve unchanged existing bytes only for a probe that needs them; return new-envelope bytes for updates. It must perform ZERO QSettings writes/sync/removes and ZERO secret writes/removes, including errors; saving still retains Task8a's metadata-only zero secure reads guarantee. No temporary save/delete trick. New no-secret drafts work without secret backend. No controller stores or returns SourceConfigurationV2/secretUpdates to QML.

configurationChanged validates reversible source/account instance syntax, invalidates in-flight creation, closes any old session before releasing its lease, and emits instanceChanged even if no session/account exists (removed-account notification). It does not create a replacement or toggle enabled state. Guard reentrant registry destruction/creation during cancellation/close/notification; no new session using old configuration may escape. sessionFor must reject a stored nonempty pluginPackageId that differs from the selected descriptor/loader package; legacy empty-package resolution retains existing behavior. Controller calls configurationChanged only after successful save/remove, so failed saves preserve currently running sessions. Enable/disable uses existing registry authority.

**Public controller API (QObject, injected dependencies):**
```cpp
PluginSettingsController(PluginManager *, SourceRegistry *, SourceAccountStore *,
                         QObject *parent = nullptr);
Q_INVOKABLE bool selectPlugin(const QString &packageId);
Q_INVOKABLE bool selectInstance(const QString &instanceId); // empty = new draft
Q_INVOKABLE bool setDraftValues(const QVariantMap &publicDraft); // replace public draft overrides
Q_INVOKABLE bool setDirectoryField(const QString &fieldId, const QUrl &localFolder);
Q_INVOKABLE bool saveInstance(const QString &displayName, const QVariantMap &secretDraft);
Q_INVOKABLE bool removeInstance(const QString &instanceId);
Q_INVOKABLE bool setInstanceEnabled(const QString &instanceId, bool enabled);
Q_INVOKABLE QUuid testConnection(const QVariantMap &secretDraft);
Q_INVOKABLE QUuid runSettingsAction(const QString &actionId,
                                  const QVariantMap &secretDraft, bool confirmed = false);
Q_INVOKABLE void cancelOperation();
Q_INVOKABLE void discoverPlugins();
Q_INVOKABLE bool loadPlugin(const QString &packageId);
Q_INVOKABLE bool unloadPlugin(const QString &packageId);
Q_INVOKABLE bool reloadPlugin(const QString &packageId);
// Properties (NOTIFY, never CONSTANT except truly stable dependencies):
// QVariantList plugins, instances, settingsSections, settingsActions, sourceCapabilities;
// QVariantMap selectedPlugin;
// QString selectedPluginId, selectedInstanceId, lastErrorKey;
// bool busy;
// Signals:
void connectionTestFinished(QUuid requestId, QVariantMap result);
void settingsActionFinished(QUuid requestId, QVariantMap result);
void draftReset(); // QML clears transient password controls on selection/save/reset
```
Only a selected loaded valid v2 settings-provider can create/save/test a draft. Unsupported/no-schema/nonmusic packages still appear safely in plugin list with common load/unload controls and host reason. selectPlugin resets selection/draft and cancels old work; selectInstance accepts only that package's source/account (legacy source matching allowed), rejects foreign account/unknown ID, empty starts a fresh stable UUID-backed draft account identity. Creation and connection test use that draft identity, but only explicit save persists it. Existing account IDs never editable implicitly. Successful save selects saved instance and resets draft/temporary password controls; failed save keeps public overrides. setDraftValues accepts only declared nonsecret fields, validates partial values without requiring omitted fields, cancels outstanding work and clears stale probe permissions on change. secretDraft accepts only declared secret fields, is used only during explicit save/test/action invocation, never stored in public snapshots, signaled or logged. Passed empty secrets preserve. Plugin/instance removal or unloaded state invalidates stale selection data safely.

setDirectoryField accepts only a declared nonsecret Directory field and a nonempty local-file QUrl, converts with QUrl::toLocalFile (not string prefix chopping), then updates that one override through the same draft path. Reject remote URLs/other field types without changing draft. It does not inspect or modify the filesystem. This gives QML FolderDialog a platform-correct path boundary; include URL percent/space handling and rejected remote input tests.

**Snapshot allowlists:**
- plugins / selectedPlugin: id, sourceId, name, version, state (stable lowercase manager-state string), activeLeases, loadable/unloadable/reloadable, settingsAvailable, reasonKey. Construct field-by-field; no raw manager error/busyReason/path passthrough.
- instances: sourceInstanceId, accountId, displayName, enabled, state, credentialConfigured, configuredSecretFieldIds; include selected unavailable state safely. No credentialReference, secretFormat, bytes or unrestricted parameters.
- settingsSections: id,titleKey,fields; each field id,labelKey,type(integer enum),required,secret,visible,choices,constraints,credentialConfigured; value only for nonsecret fields. No defaultValue/raw schema/draft/secretUpdates.
- settingsActions: id,labelKey,requiresConfirmation,state(integer AvailabilityV2),reasonKey. Before explicit probe, missing runtime information stays Unavailable; unsupported optional interface remains Unsupported once determined. Refresh grants only from a same-draft probe, clear on edits/selection/config/plugin changes; dispatch always reopens and rechecks live permission, never trusts cached UI grant.
- results: connection {success:bool,state:int,reasonKey:QString}, action {success:bool,actionId:QString,state:int,reasonKey:QString}; all returned reason strings host-owned, no backend detail/URL/credentials. Actual returned actionId must match pending declared ID.
- State enums: connection and instance state use SourceSessionStateV2; action state uses AvailabilityV2. Successful action is Available, unknown/missing runtime permission Unavailable, explicit denial Forbidden. Keep these domains distinct in consumers.
- sourceCapabilities (source-level MUSIC operation diagnostics, separate from nonmedia settingsActions): rows {action:int SourceActionV2,pluginState:int,serverState:int,accountState:int,state:int,reasonKey:QString}. Copy descriptor.declaredActions under the schema/descriptor lease; probe reads session.capabilities() while alive. Compute source-level intersection with the shared SDK helper; no fabricated media permission. Before probe or after any draft/selection/config invalidation, server/account are unknown Unavailable. Iterate known music actions, reject invalid enum keys, and use host-owned reasons only. This read-only summary is NOT authority for media dispatch; UI must label it source-level and retain Task6/13 per-media rechecks. Include missing/denied roles, invalidation and no-probe-network-on-view tests. No new session should be created merely to read this property.

Validate/detach schema under a short lease: copy QString contents (not merely shared QStringLiteral data), primitive QVariant contents, lists/maps/condition strings into host-owned storage. Do not retain secret defaults. Release viewing lease after detached snapshots; merely viewing settings must not permanently prevent unload. Invalid schema becomes settingsUnavailable with host reason, not permissive empty schema.

Lease acquisition/release itself can emit manager notifications. Avoid recursive or perpetual schema reload loops: cache detached schema for the same loaded plugin object/load generation, refresh lease-count/status snapshots separately, and only notify changed public values. Coalesce reentrant rebuilds; invalidate schema when that loaded object disappears/reloads. Include a stable-event-loop/lease-zero regression after selecting a loaded plugin.

**Ephemeral operation lifecycle:**
PluginSettingsOperation owns its factory-created session and PluginLease independently of Registry. Retain callable lease/operation state across reentrant factory/open/provider callbacks, even controller destruction. Never parent the session to an owner that can vanish inside its method stack; defer teardown until invocation unwinds. Validate returned session identity/source/account and requested parent/ownership; refuse mismatches. Public calls allocate a host UUID and defer all terminal public signals until after it is returned; one terminal at most. open() and action calls must have observed matching requestStarted before accepting return UUID or inline terminal state/result. Capture started IDs in invocation-local lifetime independent of controller; never cancel a fabricated/unstarted returned ID. Handle nested/foreign request signals without claiming them as the operation's request.

Existing Ruling7 applies to foreign-owned factory results and unexpected external destruction of operation-owned sessions: if the external destructor unwind cannot be proven, permanently pin that offending package/root until process exit, including manager destruction. A zero-delay timer is not an unwind proof (nested event loops can execute it inside the destructor). Never close/delete foreign-owned sessions. Normal owned cleanup disconnects its unexpected-destruction observer and must retain ordinary successful unload behavior. Add nested-loop metadata-only RED coverage and isolated-process pin tests; do not deliberately unload an executing plugin merely to demonstrate the failure.
After open Ready: probe snapshots only sanitized capabilities then closes; action verifies current declared/server/account intersection and confirmation before invoking optional provider. Auth/failed states and requestFailed are sanitized terminals; unsupported/nonready action cannot execute. One active operation per controller, generation cancellation on explicit cancel, selection/public draft/config changes, dependency loss and superseding request. Default operation deadline 15000ms (inject a smaller timeout into internal helper in tests, not via QML); timeout is Unavailable with host reason. Late/wrong-ID/duplicate results ignored. Close/destroy session before lease release on every terminal/cancel/destruction path; don't delete borrowed Registry sessions. Unload/reload while registered-session/settings leases are active refuses with host-owned busy reason, never force-closes unrelated playback. Controller cancellation may leave a temporary lease until an in-progress provider invocation safely returns; do not force unload that window.

**TDD sequence and acceptance (concrete scenarios):**
- [ ] Add failing storage tests: configurationForDraftV2 merges two secrets with no writes; raw sole password preserved; new no-secret draft returns config with no backend; invalid/missing-required input has no IO; save still zero reads for metadata-only; visibility changes required state without losing hidden stored values; secret condition target/cycle/type/unknown action schema rejected.
- [ ] Add failing registry tests: notify with no session and after account removal; live old session destroyed then next session uses new config; reentrant creation invalidated; stored foreign package cannot create; owner deletion inside close does not continue dereferencing registry.
- [ ] Add failing controller/operation tests using a real local plugin fixture and QPointer/event counters, not module-static counters: generic two-instance CRUD, disabled account, unknown/foreign IDs, no-schema package, public-only nested snapshots/no secret defaults, caller-supplied secret in publicDraft rejected, existing credential never echoed, test draft never persisted, failed save does not close live session, notification reaches source selector observer, selected plugin unload releases detached schema lease.
- [ ] Cover inline Ready/failure/open returning unstartedID, wrong-ID and duplicate action results, delayed cancellation/timeout, missing/false account grant, unsupported provider, unsupported constraints, confirmation required, live grant revoked after UI probe, controller deletion during create/open/run, unload attempts inside provider invocation and normal unload after final cleanup. Assert session closed/destroyed before activeLeases drops. Observe real behavior, no tautological expected-value fixtures.
```cpp
const auto oldMetadata = settings.allKeys();
const auto id = controller.testConnection({{"password", "draft-only"}});
QVERIFY(!id.isNull());
QCOMPARE(done.count(), 0); // public completion must be deferred
QTRY_COMPARE(done.count(), 1);
QCOMPARE(settings.allKeys(), oldMetadata);
QCOMPARE(secrets.writeCount, 0);
QCOMPARE(registryObserver.liveSessions(), 0); // use actual fixture-owned counters, not an invented Registry API
QCOMPARE(manager.plugin(packageId).activeLeases, 0);
```
- [ ] Build/run new focused targets `quemusic_plugin_settings_controller_test`, `quemusic_plugin_settings_operation_test` plus existing storage/registry/SDK contract tests; record behavioral RED before implementation.
- [ ] Implement the three focused host units, shared storage preparation and typed SDK additions; run focused GREEN throughout.
- [ ] Full build and CTest once after final GREEN (Qt6.11.1 configured build/bridge; cmake/ctest at /Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin; timeout40, local fixture escalation only). Existing40legacyQMLwarnings are baseline, record precisely; don't suppress them.
- [ ] Self-review, scoped commit (never all CMake/user files), report final API, changed paths, tests/commands/results and concerns. Controller then independent review. No GUI/main.cpp registration or real NAS call in this task.

#### Task 8c: 通用设置界面（仅在8b独立审查通过后执行）

**Files:**
- Create: `components/SchemaSettingsForm.qml`
- Create: `components/SourceAccountList.qml`
- Create: `components/PluginSettingsText.js` (shared host-owned translated labels/statuses, no plugin-ID branching)
- Modify: `components/PluginSettingsPanel.qml`
- Modify: `SettingsView.qml`
- Create: `tests/tst_PluginSettingsQml.cpp`
- Modify narrowly: `tests/tst_MediaBridgeQml.cpp` (the two existing plugin-panel tests and their declarations/helpers)
- Modify: `CMakeLists.txt` (new QML test/resource/link hunks only)
- Do not modify controller/storage/manager implementation, main.cpp, unrelated login dialogs or shared scroll components.

**Interfaces:**
- Consumes: Task8b `PluginSettingsController` via nullable injected `controller` property. Properties: plugins, selectedPlugin, instances, settingsSections, settingsActions, sourceCapabilities, selectedPluginId, selectedInstanceId, lastErrorKey, busy; `snapshotsChanged`/`draftReset` and connection/action finished signals. No direct PluginManager, Registry, account-store or plugin-object calls in the form.
- Produces: approved master/detail plugin settings, generic multi-instance form/actions/diagnostics, safe null-controller placeholder until Task14 registers the real controller.
- SettingsView binds `controller: typeof pluginSettingsController !== "undefined" ? pluginSettingsController : null`; remove only old Navidrome settings dialog/helper and configure signal wiring. Preserve unrelated KuGou/NetEase login UI.

- [ ] **Step 1: Write failing production-QML behavior tests**

```cpp
// Use the actual QML panel and an injected QObject controller double with
// Task8b's exact properties/signatures; record real user-triggered calls.
// Isolate QSettings path/organization/application before creating QQmlEngine.
private slots:
    void nullControllerShowsUnavailable();
    void selectionAndMultiInstanceCrud();
    void schemaTypesVisibilityAndCredentialPlaceholder();
    void invalidVisibleDraftBlocksStaleSubmission();
    void secretsClearOnSelectionCancelAndSuccessfulSave();
    void connectionTestRetainsUnsavedPasswordOnlyInInput();
    void actionConfirmationAndAvailability();
    void musicDiagnosticsAreNotMediaPermission();
    void directorySelectionUsesLocalUrlAdapter();
    void lifecycleBusyAndNoSchemaStates();
    void responsiveGeometryAndTheme_data();
    void responsiveGeometryAndTheme();
```

Use generic fixture IDs, all seven field types, long labels, two accounts, missing/forbidden capabilities, a no-schema plugin and a secret-flagged Text field. Simulate edits/clicks on production controls; assert actual call arguments/counts and state changes, not source-text-only scans. Include rejection of an invalid URL after an earlier valid draft: Save/Test/Run must remain disabled and no stale configuration submitted. Connection test must not cause a save call. Password may remain only in its private input while editing the same draft after Test; clear on selection, Cancel, successful Save, draftReset, unload/controller replacement or form destruction. Never initialize secret input from a snapshot or copy it into public draft/models.
In this step also register `quemusic_plugin_settings_qml_test` in CMake with the production QML resources under `/QueMusic`, Qt Quick/Qml/Test links and generated module import path so Step3 reaches behavioral assertions rather than failing because the target or imports are absent.

- [ ] **Step 2: Write failing QML layout assertions**

```cpp
QVERIFY(root->findChild<QQuickItem *>("pluginMasterList"));
QVERIFY(root->findChild<QQuickItem *>("pluginDetailPane"));
QVERIFY(root->findChild<QQuickItem *>("schemaSettingsForm"));
QVERIFY(!root->findChild<QObject *>("navidromeConfigAction"));
```

Render with QQuickWindow/offscreen at 1200x800 and 480x800 in isolated dark/light settings. Check mapped scene rectangles: positive widths within panel, no header/notice/form/action overlap, wrapped long text, independent master/details scrolling. Test null/empty/loaded/busy plugin states. Explicitly bind surface/text/control colors to Style.themes so platform palette cannot produce the former white content/black label defect. Capture test-only screenshots if needed; do not launch the user's app or change their theme.

- [ ] **Step 3: Run and verify failure**

```bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/cmake --build build/bridge --target quemusic_plugin_settings_qml_test -j2
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/ctest --test-dir build/bridge -R '^quemusic_plugin_settings_qml_test$' --output-on-failure --timeout 40
```

Expected behavioral RED: old panel lacks generic selection/form/actions/layout or still exposes its Navidrome-only setup path. Compilation errors alone are not RED evidence.

- [ ] **Step 4: Implement generic form, draft synchronization and private secret submission**

```qml
// Panel consumes controller; the form owns only public raw overrides and
// private input controls, never a stored credential or public secret map.
function synchronizePublicDraft() {
    if (!controller) return false
    controller.cancelOperation()
    return controller.setDraftValues(publicDraft)
}
// Actual Save handler: first synchronize and stop if it fails; collect new
// password-control input into a local map only for this explicit call:
// controller.saveInstance(displayNameInput.text, localSecretDraft)
// Test: controller.testConnection(localSecretDraft), never save/delete.
// Action: controller.runSettingsAction(actionId, localSecretDraft, confirmed).
```

Render type values Text0, Secret1, Url2, Integer3, Boolean4, Choice5, Directory6; secret flag overrides any text presentation. Map Text/Url/Directory to text controls (Directory also has FolderDialog), Secret or secret-flagged Text to a password-mode TextField, Integer to SpinBox, Boolean to Switch and Choice to ComboBox. Only local-folder acceptance calls controller.setDirectoryField(fieldId, selectedFolder); do not strip file URL prefixes in JS. After acceptance mirror the resulting public value without discarding other edits. Fields bind visible, required, choices, constraints and configured-secret placeholder; hidden fields do not lose edits/stored values. User-edit signals (textEdited/valueModified/activated/clicked), not programmatic valueChanged, update public overrides. Track failed synchronization separately from backend status; disable all submissions while invalid, and retain visible edits on failed Save. A status-only snapshot notification must not reset edits or duplicate operations.

SourceAccountList consumes instances and selectedInstanceId, emits selection/new/remove/enable intents to the panel; new calls selectInstance(""). Confirm account removal before removeInstance. Selected unavailable accounts remain visible with reason, never silently switch. Refresh and common lifecycle buttons call discoverPlugins/loadPlugin/unloadPlugin/reloadPlugin; respect exact public flags and busy state, no forced unload.

Settings action states use AvailabilityV2 (Unsupported0 hidden, Available1 enabled, Unavailable2/Forbidden3 disabled with translated reason). Required confirmation opens a host dialog and calls runSettingsAction only after acceptance; controller rechecks permission. Connection/instance state uses distinct SourceSessionStateV2 values; never infer success via truthiness. Display sourceCapabilities as read-only source-level diagnostics, with unknown runtime layers labeled not checked and an explicit per-media restriction disclaimer; opening settings must not auto-probe. Use PluginSettingsText.js for host-owned qsTr status/action labels, qsTrId labelKey with safe fallback and common reason translation; unknown keys show generic unavailable text, no raw provider error/URL/secret data. No plugin-specific conditionals or injected QML.

- [ ] **Step 5: Replace the card list with the approved A layout**

```qml
readonly property bool compact: width < 760
property var controller: null
// Desktop: bounded master ListView left, independent detail ScrollView right.
// Compact: bounded master above details. Clamp widths to available space.
// Stable objectNames: pluginMasterList, pluginDetailPane, schemaSettingsForm,
// pluginPanelHeader, pluginPanelNotice; header buttons use wrapping Flow.
```

Details contain name/version/state, common lifecycle controls, instances, schema sections, explicit Save/Test/Cancel, nonmedia settings actions and source-level capability diagnostics. Preserve settings-page containX/standWidth integration while clamping narrow geometry. Do not keep an old Navidrome dialog as a fallback. Update only the two plugin-panel legacy tests to this generic contract; no unrelated test/UI rewrites.

- [ ] **Step 6: Run focused tests**

```bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/cmake --build build/bridge --target quemusic_plugin_settings_qml_test quemusic_media_bridge_qml_test -j2
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/ctest --test-dir build/bridge -R 'plugin_settings|media_bridge_qml' --output-on-failure --timeout 40
```

Expected: production-QML behavior, credential separation, capability boundaries, multiple instances and desktop/compact dark/light geometry pass offscreen. Register new resources under /QueMusic, include the JS helper, reuse generated module import path. Isolate settings before engine creation; no system Keychain/NAS access. After focused GREEN run full build/full CTest once, report new versus existing QML warnings explicitly without suppressing them.

- [ ] **Step 7: Commit**

```bash
git add components/PluginSettingsPanel.qml components/SchemaSettingsForm.qml components/SourceAccountList.qml components/PluginSettingsText.js SettingsView.qml tests/tst_PluginSettingsQml.cpp
# Stage only this task's CMake and legacy plugin-panel test hunks after review.
git commit -m "feat: add schema-driven plugin settings"
```

### Task 9: 将 Navidrome 插件迁移到 v2 并协商实际能力

**Files:**
- Create: `plugins/navidrome-source/NavidromeApiClient.h`
- Create: `plugins/navidrome-source/NavidromeApiClient.cpp`
- Modify: `plugins/navidrome-source/NavidromeSourcePlugin.h`
- Modify: `plugins/navidrome-source/NavidromeSourcePlugin.cpp`
- Modify: `plugins/navidrome-source/NavidromeSourceSession.h`
- Modify: `plugins/navidrome-source/NavidromeSourceSession.cpp`
- Modify: `plugins/navidrome-source/manifest.json.in`
- Modify: `plugins/navidrome-source/plugin.json`
- Modify: `plugins/navidrome-source/CMakeLists.txt`
- Modify: `tests/tst_NavidromeSource.cpp`

**Interfaces:**
- Consumes: SDK v2 plugin/session contracts and existing salted-token authentication behavior.
- Produces: a v2 Navidrome plugin, `NavidromeApiClient::get()`, negotiated capabilities, generic settings schema and an opened session ready for later page/action providers.

- [ ] **Step 1: Add failing manifest, schema and capability-negotiation tests**

```cpp
void NavidromeSourceTest::pluginAdvertisesV2AndGenericSettings()
{
    NavidromeSourcePlugin plugin;
    QCOMPARE(plugin.descriptor().sdkAbi, 2);
    const auto schema = plugin.settingsSchema();
    QCOMPARE(field(schema, "serverUrl").type, SettingsFieldTypeV2::Url);
    QVERIFY(field(schema, "password").secret);
}

void NavidromeSourceTest::openNegotiatesExtensionsAfterPing()
{
    server.enqueueJson("/rest/ping.view", okResponse());
    server.enqueueJson("/rest/getOpenSubsonicExtensions.view",
                       extensionsResponse({"lyrics", "songLyrics"}));
    server.enqueueJson("/rest/getUser.view", currentUserRolesResponse());
    session.open();
    QTRY_COMPARE(session.state(), SourceSessionStateV2::Ready);
    QVERIFY(session.capabilities().action(SourceActionV2::Lyrics).state
            == AvailabilityV2::Available);
    QVERIFY(server.requests().at(0).url.query().contains("u=admin"));
    QVERIFY(!server.requests().at(0).url.query().contains("p=admin"));
}

void NavidromeSourceTest::subsonicAuthorizationErrorIsTypedAndRedacted()
{
    server.enqueueJson("/rest/download.view",
                       subsonicError(50, "User is not authorized"));
    NavidromeApiClient client(configuration(), &network);
    QSignalSpy clientFailure(&client, &NavidromeApiClient::failed);
    client.get("authorizationProbe", "download", QUrlQuery("id=42"));
    QTRY_COMPARE(clientFailure.count(), 1);
    const auto error = qvariant_cast<SourceErrorV2>(clientFailure.takeFirst().at(1));
    QCOMPARE(error.kind, SourceErrorKindV2::Authorization);
    QVERIFY(!error.detail.contains("admin"));
}
```

- [ ] **Step 2: Run and verify failure**

```bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/cmake --build build/bridge --target quemusic_navidrome_source_test -j2
```

Expected: plugin still implements v1 and has no settings schema or negotiated capability state.

- [ ] **Step 3: Extract authenticated transport without changing credential behavior**

```cpp
class NavidromeApiClient final : public QObject {
    Q_OBJECT
public:
    explicit NavidromeApiClient(SourceConfigurationV2 configuration,
                                QNetworkAccessManager *network, QObject *parent = nullptr);
    QUuid get(const QString &operation, const QString &endpoint, QUrlQuery query = {});
    void cancel(const QUuid &requestId);
signals:
    void succeeded(QUuid requestId, QString operation, QJsonObject subsonicResponse);
    void failed(QUuid requestId, SourceErrorV2 error);
};
```

For every request generate a fresh random salt, compute the existing Subsonic token, append `u`, `t`, `s`, `v=1.16.1`, `c=QueMusic` and `f=json`, and redact `t`, `s`, passwords and cookies from errors and logs. Keep base URLs with a path prefix working.

**Credential input from Task8a:** Decode the shared SDK named-secret envelope in C++ and obtain the schema-declared `password` bytes before computing the token; do not hash the whole envelope as if it were a password. Explicitly support existing single-field legacy raw credentials without rewriting their stored record merely on open. Malformed/unknown envelope formats or missing required named credentials fail before sending a network request, with a host-safe configuration/authentication reason. No credential/envelope bytes in settings models, logs, query error strings or ordinary account parameters. Add fixtures proving legacy raw and named password produce equivalent authentication tokens for the same salt, plus malformed/missing-field early rejection. Use Task8a's SDK helpers rather than duplicate the wire parser.

Parse Subsonic error code 40 as authentication, code 50 as authorization, code 70 as not found and unsupported endpoint responses as unsupported. Preserve HTTP status separately and expose only sanitized detail.

- [ ] **Step 4: Implement v2 plugin metadata, settings schema and open handshake**

```cpp
SettingsSchemaV2 NavidromeSourcePlugin::settingsSchema() const
{
    return {{"connection", "settings.connection", {
        urlField("serverUrl", "settings.serverUrl", true),
        textField("username", "settings.username", true),
        secretField("password", "settings.password", true),
        choiceField("quality", "settings.quality", {"original", "transcoded"}, "original")
    }}};
}

QUuid NavidromeSourceSession::open()
{
    setState(SourceSessionStateV2::Connecting);
    return m_client->get("ping", "ping");
}
```

After successful ping, request `getOpenSubsonicExtensions`. A 404/unsupported extension response does not fail the whole session; it yields the conservative Subsonic capability set. Authentication failure sets `AuthenticationRequired`, network failure sets `Failed`, and success sets `Ready` and emits `capabilitiesChanged`.

**Account negotiation (Ruling 10):** Also request `getUser` with `username` equal to the currently configured user. Fill `CapabilitySetV2::serverActions` from protocol/server negotiation and `accountActions` independently from verified current-user roles. This read does not require administrator privileges; never query other users or add user-management UI. Do not persist role grants into ordinary editable account parameters. Unsupported/malformed role responses leave role-dependent actions `Unavailable` without discarding otherwise usable read-page functionality; genuine authentication errors still enter `AuthenticationRequired`. Map only verified endpoint semantics, not similarly named roles. Reconnect clears both maps before negotiation. Include fake-server tests for own-user request parameters, role false, missing/invalid fields, unsupported getUser, authentication errors and independent action availability. Reference: https://opensubsonic.netlify.app/docs/endpoints/getuser/ and https://opensubsonic.netlify.app/docs/responses/user/ .

**Role mapping clarification (Ruling 19):** Map only the four role fields whose protocol descriptions directly match v2 actions: `streamRole` gates Play, `coverArtRole` gates Artwork, `downloadRole` gates Download, and `playlistRole` gates Create/Update/DeletePlaylist plus Add/RemovePlaylistTracks. Do not map `commentRole` to Rating or `scrobblingEnabled` to Scrobble; neither is an authorization statement for those actions. After a valid own-user response, declared/server-supported actions with no dedicated role field may receive an explicit account Available entry and rely on Task11's per-action authorization-error downgrade; before that response, or when it is unsupported/malformed, those entries remain Unavailable. False or missing typed values for the four mapped roles stay Unavailable. This makes the plan's Lyrics availability assertion possible without inventing a lyrics role, while preserving fail-closed behavior for role-gated actions.

- [ ] **Step 5: Update both Qt and package manifests to v2**

Set `Q_PLUGIN_METADATA` IID to `org.quemusic.MusicSourcePlugin/2.0`, Qt metadata `sdkAbi` to `2`, and package manifest interface/version to `2.0` with `runtimeRequirements.sourceSdkAbi = 2`.

Declare `Q_INTERFACES(IMusicSourcePluginV2 IPluginSettingsProviderV2)` on `NavidromeSourcePlugin`; the base source-plugin interface remains independent of settings UI.

- [ ] **Step 6: Run focused tests**

```bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/cmake --build build/bridge --target quemusic_navidrome_source_test -j2
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/ctest --test-dir build/bridge -R navidrome_source --output-on-failure
```

Expected: ping, extension fallback, authentication, path prefix, cancellation and secret-redaction tests pass.

- [ ] **Step 7: Commit**

```bash
git add plugins/navidrome-source tests/tst_NavidromeSource.cpp
git commit -m "feat: migrate navidrome plugin to source sdk v2"
```

### Task 10: 实现 Navidrome 页面、搜索和媒体解析

**Files:**
- Create: `plugins/navidrome-source/NavidromeMappers.h`
- Create: `plugins/navidrome-source/NavidromeMappers.cpp`
- Modify: `plugins/navidrome-source/NavidromeApiClient.h`
- Modify: `plugins/navidrome-source/NavidromeApiClient.cpp`
- Modify: `plugins/navidrome-source/NavidromeSourcePlugin.cpp`
- Modify: `plugins/navidrome-source/NavidromeSourceSession.h`
- Modify: `plugins/navidrome-source/NavidromeSourceSession.cpp`
- Modify: `plugins/navidrome-source/CMakeLists.txt`
- Modify: `core/music/AggregateComposer.h`
- Modify: `core/music/AggregateComposer.cpp`
- Modify: `core/music/PageCache.cpp`
- Modify: `core/music/PageRepository.cpp`
- Modify: `tests/tst_AggregateComposer.cpp`
- Modify: `tests/tst_PageRepository.cpp`
- Modify: `tests/tst_MusicCaches.cpp`
- Modify: `tests/tst_NavidromeSource.cpp`

**Interfaces:**
- Consumes: `IPageProviderV2`, `IPlaybackProviderV2`, `IDownloadProviderV2`, Navidrome transport and v2 DTOs.
- Produces: recommendation/category/favorites/search pages, stream/artwork/lyrics results, atomic local downloads, and source-ordered native-playlist composition.

- [ ] **Step 1: Add table-driven failing endpoint mapping tests**

```cpp
void NavidromeSourceTest::pageEndpoint_data()
{
    QTest::addColumn<PageSectionKindV2>("section");
    QTest::addColumn<QString>("endpoint");
    QTest::addColumn<QString>("type");
    QTest::newRow("random") << PageSectionKindV2::Random << "getAlbumList2" << "random";
    QTest::newRow("newest") << PageSectionKindV2::Newest << "getAlbumList2" << "newest";
    QTest::newRow("recent") << PageSectionKindV2::RecentlyPlayed << "getAlbumList2" << "recent";
    QTest::newRow("frequent") << PageSectionKindV2::FrequentlyPlayed << "getAlbumList2" << "frequent";
    QTest::newRow("highest") << PageSectionKindV2::HighestRated << "getAlbumList2" << "highest";
    QTest::newRow("genres") << PageSectionKindV2::Genres << "getGenres" << "";
    QTest::newRow("artists") << PageSectionKindV2::Artists << "getArtists" << "";
}

void NavidromeSourceTest::drillDownUsesTypedFilterEndpoint_data()
{
    QTest::addColumn<QString>("filterKey");
    QTest::addColumn<QString>("endpoint");
    QTest::newRow("artist") << "artistId" << "getArtist";
    QTest::newRow("album") << "albumId" << "getAlbum";
    QTest::newRow("song") << "songId" << "getSong";
    QTest::newRow("genre") << "genre" << "getSongsByGenre";
    QTest::newRow("playlist") << "playlistId" << "getPlaylist";
}

void NavidromeSourceTest::favoritesMapsSongsAlbumsAndArtists()
{
    server.enqueueJson("/rest/getStarred2.view", starredResponse());
    const QUuid id = session.fetchPage(favoritesQuery());
    QTRY_COMPARE(pageSpy.count(), 1);
    const auto page = qvariant_cast<PageResultV2>(pageSpy.takeFirst().at(1));
    QCOMPARE(page.sections.size(), 3);
    QCOMPARE(page.sections[0].items[0].ref.sourceInstanceId, "navidrome/home");
}

void NavidromeSourceTest::playlistTracksCarryAbsoluteOccurrenceMetadata()
{
    server.enqueueJson("/rest/getPlaylist.view", playlistWithDuplicateSongs());
    const QUuid id = session.fetchPage(playlistQuery("p1", 1, "1"));
    QTRY_COMPARE(pageSpy.count(), 1);
    const auto page = qvariant_cast<PageResultV2>(pageSpy.takeFirst().at(1));
    QCOMPARE(page.sections[0].items[0].metadata.value("playlistId").toString(), "p1");
    QCOMPARE(page.sections[0].items[0].metadata.value("playlistIndex").toInt(), 1);
}
```

- [ ] **Step 2: Add failing media-resolution tests**

```cpp
void NavidromeSourceTest::streamDescriptorUsesAuthenticatedUrlWithoutPersistingIt()
{
    const QUuid id = session.resolveStream(trackRef("42"));
    QTRY_COMPARE(streamSpy.count(), 1);
    const auto stream = qvariant_cast<StreamDescriptorV2>(streamSpy.takeFirst().at(1));
    QVERIFY(stream.url.path().endsWith("/rest/stream.view"));
    QCOMPARE(QUrlQuery(stream.url).queryItemValue("id"), "42");
}

void NavidromeSourceTest::lyricsUsesNegotiatedEndpoint()
{
    enableExtension("songLyrics");
    server.enqueueJson("/rest/getLyricsBySongId.view", lyricLinesResponse());
    session.fetchLyrics(trackRef("42"));
    QTRY_COMPARE(actionSpy.count(), 1);
    QCOMPARE(lastOperation(actionSpy), SourceActionV2::Lyrics);
}

void NavidromeSourceTest::downloadWritesRequestedLocalFileAtomically()
{
    server.enqueueBytes("/rest/download.view", QByteArrayLiteral("audio-bytes"));
    const QUrl destination = newLocalDestination();
    session.download(trackRef("42"), destination);
    QTRY_COMPARE(actionSpy.count(), 1);
    QCOMPARE(lastAction(actionSpy).payload.value("destination").toUrl(), destination);
    QCOMPARE(readFile(destination), QByteArrayLiteral("audio-bytes"));
}
```

- [ ] **Step 3: Run and verify failure**

```bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/cmake --build build/bridge --target quemusic_navidrome_source_test -j2
```

Expected: page and v2 media provider methods are missing.

- [ ] **Step 4: Implement pure JSON mappers**

```cpp
namespace NavidromeMappers {
MediaItemV2 song(const QJsonObject &value, const SourceIdentityV2 &source);
MediaItemV2 album(const QJsonObject &value, const SourceIdentityV2 &source);
MediaItemV2 artist(const QJsonObject &value, const SourceIdentityV2 &source);
MediaItemV2 playlist(const QJsonObject &value, const SourceIdentityV2 &source);
PageSectionV2 albums(PageSectionKindV2 kind, const QJsonObject &response,
                     const SourceIdentityV2 &source);
QList<PageSectionV2> starred(const QJsonObject &response,
                             const SourceIdentityV2 &source);
QList<PageSectionV2> search(const QJsonObject &response,
                            const SourceIdentityV2 &source);
}
```

Mappers contain no network or session state. Every item receives stable `MediaRefV2`, source badge metadata, server IDs, duration, cover art reference, reliable external IDs and per-object action candidates. Tracks mapped from `getPlaylist` additionally receive only `metadata.playlistId` as a non-empty string and `metadata.playlistIndex` as the absolute zero-based integer position in the server list. Do not use `trackNumber` or a rendered row index as playlist position.

- [ ] **Step 5: Implement endpoint dispatch and pagination**

Map standard sections to `getAlbumList2`, `getGenres`, `getArtists`, `getArtist`, `getAlbum`, `getSong`, `getSongsByGenre`, `getPlaylist`, `getStarred2` and `search3`. `PageQueryV2.filters` uses the mutually exclusive keys `artistId`, `albumId`, `songId`, `playlistId` or `genre` for drill-down. `artistId` returns Albums; album/playlist/genre drill-down returns Tracks, preserving native playlist order and duplicates. A top-level `Tracks` request without one of these filters returns typed `Unsupported` rather than inventing a server-wide song order. Translate offset-based endpoints to opaque provider cursors encoded as decimal offsets; reject negative or malformed cursors. Use `size=query.limit` and cap the accepted limit to `1..500`.

`getPlaylist` returns a native ordered collection. Slice its entry array by the validated decimal offset and limit, set each returned occurrence's absolute `playlistIndex`, and expose the next absolute offset only when entries remain. A `playlistId` query requires one concrete `sourceInstanceId` matching the session; reject aggregate or foreign scope before transport.

Extend the host with a typed source-ordered composition mode selected only for a specific-source `playlistId` query. It accepts exactly one source, preserves input order and repeated reliable IDs in the initial result and every continuation, and never carries discovery-page `seenIds` into that cursor. `PageRepository` rejects aggregate `playlistId` before provider dispatch, treats validated `playlistId` as cacheable, and uses this mode for composition. Discovery queries keep the existing round-robin/reliable-ID deduplication unchanged. Add composer and repository regressions for duplicate ISRC/MBID occurrences, provider order across continuation and zero provider calls for aggregate playlist queries.

Extend `PageCache` live/disk sanitization with exactly two native-playlist fields: non-empty string `playlistId` and integral `playlistIndex` in `0..INT_MAX`. Invalid types, negative/fractional/out-of-range indexes and all unknown metadata remain dropped. Add live sanitization and disk round-trip tests proving valid occurrence metadata survives without widening the allowlist.

An initial favorites query may return its three standard sections as required above. A continuation query carries the clicked section kind and that section's provider cursor; return only that requested section on continuation (Ruling 12). Keep cursors/offsets section-specific even when a shared underlying endpoint supplies several lists. A plugin must adapt whole-response transport into this typed per-section continuation contract.

- [ ] **Step 6: Implement media providers**

Implement `IPageProviderV2`, `IPlaybackProviderV2` and `IDownloadProviderV2` on the session and declare only the corresponding implemented actions (`Play`, `Artwork`, `Lyrics`, `Download`) in the plugin descriptor; Task11 actions remain absent/`Unsupported` until their provider interfaces exist. `resolveStream` uses `stream` and emits only a transient `StreamDescriptorV2`; it is never inserted into page metadata or disk cache. Artwork uses bounded binary `getCoverArt` bytes and the existing exact `ActionResultV2` keys `bytes: QByteArray` and `mimeType: QString`. Lyrics prefer `getLyricsBySongId` only when negotiated and otherwise use the compatible endpoint, emitting exact `lyrics: QString`.

Extend `NavidromeApiClient` with bounded binary response support for artwork and streaming-to-file support for downloads without exposing authenticated URLs. `download(media, destination)` accepts only an absolute local file URL whose parent directory already exists and whose target does not exist. Stream `download` into a uniquely named temporary file in that same directory and atomically rename without overwriting; do not buffer the full song in memory. On cancel, HTTP/Subsonic failure, disk error, session close or destruction, abort transport and remove every partial/temp file. Success is emitted only after the final rename and uses `ActionResultV2{Download, exact requested media, {"destination": exact requested QUrl}}`. Add fake-server tests for content, exact payload, no authenticated URL in action/page/cache data, existing/remote/relative destination early rejection with zero requests, destination race/no-overwrite behavior, cancellation/failure/teardown cleanup, and no terminal success before the file is committed.

- [ ] **Step 7: Run focused tests**

```bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/cmake --build build/bridge --target quemusic_navidrome_source_test -j2
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/ctest --test-dir build/bridge -R navidrome_source --output-on-failure
```

Expected: all endpoint, mapper, pagination, lyrics fallback, stream, artwork, atomic-download, native-playlist order/duplicate and cache-sanitization tests pass.

- [ ] **Step 8: Commit**

```bash
git add plugins/navidrome-source/NavidromeMappers.* plugins/navidrome-source/NavidromeApiClient.* plugins/navidrome-source/NavidromeSourcePlugin.cpp plugins/navidrome-source/NavidromeSourceSession.* plugins/navidrome-source/CMakeLists.txt core/music/AggregateComposer.* core/music/PageCache.cpp core/music/PageRepository.cpp tests/tst_AggregateComposer.cpp tests/tst_PageRepository.cpp tests/tst_MusicCaches.cpp tests/tst_NavidromeSource.cpp
git commit -m "feat: expose navidrome page feeds and media"
```

### Task 11: 补齐 Navidrome 收藏、评分、歌单、队列和书签

**Files:**
- Modify: `plugins/navidrome-source/NavidromeSourcePlugin.cpp`
- Modify: `tests/NavidromeSmoke.cpp` (capability assertions only)
- Modify: `plugins/navidrome-source/NavidromeSourceSession.h`
- Modify: `plugins/navidrome-source/NavidromeSourceSession.cpp`
- Modify: `plugins/navidrome-source/NavidromeMappers.h`
- Modify: `plugins/navidrome-source/NavidromeMappers.cpp`
- Modify: `tests/tst_NavidromeSource.cpp`

**Interfaces:**
- Consumes: all typed write-provider interfaces and capability descriptors.
- Produces: server-confirmed favorite/rating/playlist/play-queue/bookmark mutations with exact request-to-result correlation.

- [ ] **Step 1: Add failing data-driven action tests**

```cpp
void NavidromeSourceTest::simpleActionEndpoint_data()
{
    QTest::addColumn<SourceActionV2>("action");
    QTest::addColumn<QString>("endpoint");
    QTest::newRow("star") << SourceActionV2::Favorite << "star";
    QTest::newRow("unstar") << SourceActionV2::Unfavorite << "unstar";
    QTest::newRow("rating") << SourceActionV2::Rating << "setRating";
    QTest::newRow("delete-playlist") << SourceActionV2::DeletePlaylist << "deletePlaylist";
    QTest::newRow("save-queue") << SourceActionV2::SavePlayQueue << "savePlayQueue";
    QTest::newRow("bookmark") << SourceActionV2::CreateBookmark << "createBookmark";
}
```

- [ ] **Step 2: Add failing playlist constraint and mutation tests**

```cpp
void NavidromeSourceTest::rejectsForeignTrackBeforePlaylistRequest()
{
    PlaylistChangeV2 change;
    change.tracksToAdd = {localTrack("local/default", "l1")};
    session.updatePlaylist(playlistRef("p1"), change);
    QTRY_COMPARE(failureSpy.count(), 1);
    QCOMPARE(server.requests().size(), 0);
    QCOMPARE(lastError(failureSpy).kind, SourceErrorKindV2::Unsupported);
}

void NavidromeSourceTest::updatePlaylistUsesIndexedRemovalAndSongIds()
{
    server.enqueueJson("/rest/updatePlaylist.view", okResponse());
    session.updatePlaylist(playlistRef("p1"),
                           PlaylistChangeV2{{trackRef("42")}, {2}, "Renamed"});
    QTRY_COMPARE(actionSpy.count(), 1);
    const QUrlQuery query(server.requests().first().url);
    QCOMPARE(query.queryItemValue("name"), "Renamed");
    QVERIFY(query.allQueryItemValues("songIdToAdd").contains("42"));
    QVERIFY(query.allQueryItemValues("songIndexToRemove").contains("2"));
}

void NavidromeSourceTest::forbiddenResponseDowngradesThatActionCapability()
{
    server.enqueueJson("/rest/download.view",
                       subsonicError(50, "User is not authorized"));
    session.download(trackRef("42"), temporaryDestination());
    QTRY_COMPARE(capabilitiesSpy.count(), 1);
    QCOMPARE(session.capabilities().action(SourceActionV2::Download).state,
             AvailabilityV2::Forbidden);
}
```

- [ ] **Step 3: Run and verify failure**

```bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/cmake --build build/bridge --target quemusic_navidrome_source_test -j2
```

Expected: typed mutation methods are absent.

- [ ] **Step 4: Implement every user-side mutation with source checks**

Implement `star`, `unstar`, `setRating`, `getPlaylists`, `getPlaylist`, `createPlaylist`, `updatePlaylist`, `deletePlaylist`, `getPlayQueue`, `savePlayQueue`, `getBookmarks`, `createBookmark` and `deleteBookmark`. Before network dispatch, require every `MediaRefV2.sourceInstanceId` to match the session instance. Validate ratings as `0..5`, bookmark positions as non-negative, playlist names as non-empty after trimming, and removal indexes as non-negative.

Task11 alignment: reuse Task10's existing `getPlaylist` detail path; implement `getPlaylists` through `fetchPage` for the Playlists section. Use the actual typed provider signatures in `ISourceProvidersV2.h`, not endpoint names as additional public APIs. Validate complete plugin/instance/account identity and supported entity types before transport. Preserve synchronous requestStarted and cancellation/close/reentrant semantics. Scrobble remains Task12 and Unsupported in Task11. Declare new actions only alongside their provider implementation; adjust the smoke's capability assertions accordingly. UI/host routing for queue/bookmark fetch remains Task12/13, not part of this plugin task.

For a compound playlist update, preserve which sub-actions (rename, additions, removals) were exercised; on endpoint authorization denial downgrade only those attempted account actions, retaining unrelated entries and serverActions. Names omitted by an empty `change.newName` mean no rename; reject a nonempty whitespace-only name. Queue order and duplicates must survive encoding and decoding. Empty queue saves omit current; a nonempty current must belong to its queue. Fetch results must contain allowlisted typed media refs/items and positions, never raw server JSON or authenticated URLs. Existing Task6 mutation payload keys and types remain binding. Record the exact queue/bookmark payload contract in the implementation report for subsequent typed host integration.

For `star`/`unstar`, map track IDs to `id`, album IDs to `albumId`, and artist IDs to `artistId`. Expose rating only for tracks. Encode repeated playlist additions as `songIdToAdd`, repeated removals as `songIndexToRemove`, and queue items as repeated `id` parameters in preserved order.

When the server returns an authorization error for a typed action, change only that action in `CapabilitySetV2::accountActions` to `Forbidden` with reason `source.permission.<action>`, emit `capabilitiesChanged`, and keep unrelated actions and the server-support layer unchanged. The derived `action()` accessor must reflect the denial. Reconnect resets both negotiated maps.

- [ ] **Step 5: Emit server-confirmed typed results**

```cpp
void NavidromeSourceSession::completeMutation(const PendingRequest &pending,
                                              const QJsonObject &response)
{
    ActionResultV2 result;
    result.action = pending.action;
    result.subject = pending.subject;
    result.payload = NavidromeMappers::mutationResult(pending.action, response);
    emit actionCompleted(pending.requestId, result);
}
```

Do not mutate page models from the plugin. `MediaActionRouter` validates and emits the confirmed result using Task6's documented allowlisted payload contract; it owns no page model. Task13's MusicHub/UI integration applies confirmed results and, if favorite/rating use optimistic state, owns the previous value and request-correlated rollback. Do not introduce a circular router-to-hub dependency in this plugin task.

- [ ] **Step 6: Run focused tests**

```bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/cmake --build build/bridge --target quemusic_navidrome_source_test quemusic_media_action_router_v2_test -j2
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/ctest --test-dir build/bridge -R '(navidrome_source|media_action_router_v2)' --output-on-failure
```

Expected: all mutations, validation, same-instance constraints, server failures and confirmed-result routing pass. UI reconciliation and any optimistic rollback are tested in Task13, not by mutating host models from this plugin task.

- [ ] **Step 7: Commit**

```bash
git add plugins/navidrome-source/NavidromeSourceSession.* plugins/navidrome-source/NavidromeMappers.* tests/tst_NavidromeSource.cpp
git commit -m "feat: complete navidrome user operations"
```

### Task 12: 建立统一播放协调器和 Navidrome scrobble

执行拆分：先完成音源身份一致性修复，再实现 Navidrome scrobble provider，最后实现协调器及 QML 接口。前两个子项通过不代表 Task12 完成。应用 composition root 在 Task14 组装；在该接线及实际 GUI 验证完成前，不宣称新播放链路已用于运行中的应用。

The provider slice also updates `NavidromeMappers.cpp`: mapped Track items explicitly grant Scrobble, while albums/artists/directories do not. This is required by the host's plugin/server/account/media capability intersection; descriptor availability alone is insufficient.

Navidrome provider contract: implement `IScrobbleProviderV2::scrobble(media, positionMs, submission)` and declare Scrobble only with the implementation. Accept only a nonempty Track ref owned by the session and a nonnegative position. Use `scrobble` with `id` and explicit `submission=true/false`. Do not send playback position as `time`: that optional protocol field means Unix epoch milliseconds, not position; omit it in this phase. On server-confirmed success return `ActionResultV2` with Scrobble, exact subject and allowlisted `{positionMs, submission}` payload. Reuse requestStarted/cancel/close handling and per-action authorization downgrade; do not gate on `scrobblingEnabled` or unrelated roles. Tests must cover both wire values, invalid/cross-owner input with no transport, server failure, cancellation and capability downgrade. Protocol reference: https://opensubsonic.netlify.app/docs/endpoints/scrobble/ (verified 2026-09-08).

**Files:**
- Modify: `plugins/navidrome-source/NavidromeSourceSession.h`
- Modify: `plugins/navidrome-source/NavidromeSourceSession.cpp`
- Modify: `plugins/navidrome-source/NavidromeSourcePlugin.cpp`
- Modify: `tests/tst_NavidromeSource.cpp`
- Modify if required for capability assertions: `tests/NavidromeSmoke.cpp`
- Modify: `plugins/navidrome-source/NavidromeMappers.cpp`
- Create: `core/music/PlaybackCoordinator.h`
- Create: `core/music/PlaybackCoordinator.cpp`
- Create: `tests/tst_PlaybackCoordinator.cpp`
- Modify: `layout/PlayerControl.qml`
- Modify: `main.qml`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `IPlaybackProviderV2`, `IScrobbleProviderV2`, `SourceRegistry`, existing player and queue signals.
- Produces: `play()`, `enqueue()`, `reportPosition()`, `reportStopped()` and one-shot start/submission scrobble behavior.

- [ ] **Step 1: Write failing threshold and one-shot tests**

```cpp
void PlaybackCoordinatorTest::startsAndSubmitsAtHalfDuration()
{
    coordinator.play(trackWithDuration(180000));
    resolveStream();
    QCOMPARE(scrobbler.startedCalls(), 1);
    coordinator.reportPosition(89999);
    QCOMPARE(scrobbler.submittedCalls(), 0);
    coordinator.reportPosition(90000);
    QCOMPARE(scrobbler.submittedCalls(), 1);
    coordinator.reportPosition(120000);
    QCOMPARE(scrobbler.submittedCalls(), 1);
}

void PlaybackCoordinatorTest::unknownDurationSubmitsAtFourMinutes()
{
    coordinator.play(trackWithDuration(0));
    resolveStream();
    coordinator.reportPosition(239999);
    QCOMPARE(scrobbler.submittedCalls(), 0);
    coordinator.reportPosition(240000);
    QCOMPARE(scrobbler.submittedCalls(), 1);
}
```

- [ ] **Step 2: Run and verify failure**

```bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/cmake --build build/bridge --target quemusic_playback_coordinator_test -j2
```

Expected: coordinator target is missing.

- [ ] **Step 3: Implement playback ownership and scrobble threshold**

```cpp
qint64 PlaybackCoordinator::submissionThresholdMs(qint64 durationMs)
{
    return durationMs > 0 ? qMin(durationMs / 2, 240000LL) : 240000LL;
}

void PlaybackCoordinator::reportPosition(qint64 positionMs)
{
    if (!m_current.has_value() || m_current->submitted) return;
    if (positionMs < submissionThresholdMs(m_current->item.durationMs)) return;
    m_current->submitted = true;
    scrobble(m_current->item.ref, true, positionMs);
}
```

Emit the non-submission scrobble once after stream resolution and before playback starts. Cancel unresolved stream requests on track changes. Queue entries store `MediaRefV2`, not expiring stream URLs.

- [ ] **Step 4: Replace QML source branching with coordinator calls**

`PlayerControl.qml` sends queue item, position, stop and end events to `playbackCoordinator`. Remove integer-source branches from the touched play/enqueue path. Keep unrelated visual and shortcut behavior unchanged.

- [ ] **Step 5: Run focused tests**

```bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/cmake --build build/bridge --target quemusic_playback_coordinator_test -j2
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/ctest --test-dir build/bridge -R playback_coordinator --output-on-failure
```

Expected: stream cancellation, start event, 50%/four-minute submission, duplicate prevention and unsupported-scrobble behavior pass.

- [ ] **Step 6: Commit**

```bash
git add core/music/PlaybackCoordinator.* tests/tst_PlaybackCoordinator.cpp layout/PlayerControl.qml main.qml CMakeLists.txt
git commit -m "feat: centralize source playback and scrobble"
```

### Task 13: 把推荐、分类、收藏和搜索切换到 MusicHub

**Files:**
- Create: `components/SourceScopeSelector.qml`
- Create: `components/MusicSectionView.qml`
- Create: `components/MediaActionMenu.qml`
- Modify: `pages/HomePage.qml`
- Modify: `pages/PlaylistPage.qml`
- Modify: `pages/FavouritePage.qml`
- Modify: `pages/SearchPage.qml`
- Modify: `layout/MainContent.qml`
- Modify: `layout/LeftSideBar.qml`
- Delete: `pages/SourceLibraryPage.qml`
- Create: `tests/tst_MusicHubQml.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: QML-visible `MusicHub`, `MusicPageModel`, `MediaActionRouter` and `PlaybackCoordinator`.
- Produces: three main music pages and search using the same source selector, clear source badges, action boundaries and partial-error presentation.

- [ ] **Step 1: Write failing offscreen navigation and binding tests**

```cpp
void MusicHubQmlTest::sidebarHasNoSourceLibraryEntry()
{
    auto root = load("layout/LeftSideBar.qml");
    QVERIFY(!findObject(root.get(), "sourceLibraryChoice"));
}

void MusicHubQmlTest::allMusicPagesExposeSharedSourceSelector()
{
    for (const QString &page : {"HomePage.qml", "PlaylistPage.qml",
                                "FavouritePage.qml", "SearchPage.qml"}) {
        auto root = load("pages/" + page);
        auto *selector = findObject(root.get(), "sourceScopeSelector");
        QVERIFY2(selector, qPrintable(page));
        QCOMPARE(selector->property("modelObject").value<QObject *>(), musicHub);
    }
}

void MusicHubQmlTest::forbiddenActionIsVisibleAndDisabled()
{
    auto menu = loadActionMenu(forbiddenDownloadItem());
    auto *download = findObject(menu.get(), "downloadAction");
    QVERIFY(download->property("visible").toBool());
    QVERIFY(!download->property("enabled").toBool());
    QCOMPARE(download->property("reasonKey").toString(), "source.permission.download");
}
```

- [ ] **Step 2: Run and verify failure**

```bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/cmake --build build/bridge --target quemusic_music_hub_qml_test -j2
```

Expected: new components do not exist and source-library navigation is still present.

- [ ] **Step 3: Implement the shared selector and section view**

```qml
ComboBox {
    id: selector
    objectName: "sourceScopeSelector"
    required property var modelObject
    model: modelObject.sourceOptions
    textRole: "displayName"
    valueRole: "sourceInstanceId"
    onActivated: modelObject.selectedSourceInstanceId = currentValue
    Component.onCompleted: currentIndex = indexOfValue(modelObject.selectedSourceInstanceId)
}
```

`MusicSectionView` renders section title, source-aware item cards/rows, load-more state and section retry. Each item always carries a source badge in aggregate mode; specified-source mode may reduce badge prominence but retains it in details.

When a visible item has an `artworkId` but no local `artworkUrl`, `MusicSectionView` calls `musicHub.loadArtwork(model.ref)` once and updates the delegate only from `musicHub.artworkReady`. Delegate destruction cancels outstanding asset requests through the page controller.

- [ ] **Step 4: Implement capability-aware action menus**

```qml
MenuItem {
    id: downloadAction
    objectName: "downloadAction"
    property string reasonKey: model.availableActions.download.reasonKey || ""
    visible: model.availableActions.download.state !== "unsupported"
    enabled: model.availableActions.download.state === "available"
    text: enabled ? qsTr("下载") : qsTr("下载 · %1").arg(reasonText(reasonKey))
    onTriggered: musicHub.actions.download(model, downloadDestination)
}
```

The named/string availability fields above illustrate presentation rules, not the current model wire shape. Task4's actual map uses decimal action keys and integer states; bind through an explicit enum/presentation mapping and test the real data shape. Pass the whole item to the typed router (Ruling 13), preserving the media restriction layer.

Apply the same state rule to favorite, rating, add-to-playlist, remove-from-playlist and bookmark actions. Source-specific actions are grouped under a heading containing `sourceDisplayName`.

- [ ] **Step 5: Replace page data sources and remove source-library routing**

Bind Home to `musicHub.recommendation`, PlaylistPage to `musicHub.category`, FavouritePage to `musicHub.favorites`, and SearchPage to `musicHub.searchResults`. Call `musicHub.activatePage()` when each loader becomes active. Remove page index 7, `configureSourceRequested`, the “音乐源” sidebar choice and `SourceLibraryPage.qml` from QML resources.

- [ ] **Step 6: Run focused QML tests**

```bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/cmake --build build/bridge --target quemusic_music_hub_qml_test -j2
QT_QPA_PLATFORM=offscreen /Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/ctest --test-dir build/bridge -R music_hub_qml --output-on-failure
```

Expected: navigation removal, shared selection, partial errors, source badges and operation states pass.

- [ ] **Step 7: Commit**

```bash
git add components/SourceScopeSelector.qml components/MusicSectionView.qml components/MediaActionMenu.qml pages/HomePage.qml pages/PlaylistPage.qml pages/FavouritePage.qml pages/SearchPage.qml layout/MainContent.qml layout/LeftSideBar.qml tests/tst_MusicHubQml.cpp CMakeLists.txt
git rm pages/SourceLibraryPage.qml
git commit -m "feat: unify music pages on music hub"
```

### Task 14: 组装应用、移除 v1 上层入口并完成全量验证

**Files:**
- Modify: `core/source/SourceStartup.h`
- Modify: `core/source/SourceStartup.cpp`
- Modify: `main.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/NavidromeSmoke.cpp`
- Modify: `docs/PLUGIN_API.md`
- Modify: `docs/superpowers/runbooks/navidrome-smoke-test.md`
- Delete: `core/media/MediaBridge.h`
- Delete: `core/media/MediaBridge.cpp`
- Delete: `core/media/SourceAccountController.h`
- Delete: `core/media/SourceAccountController.cpp`
- Delete: `core/media/SourceSessionRegistry.h`
- Delete: `core/media/SourceSessionRegistry.cpp`
- Delete: `core/source/SourceManager.h`
- Delete: `core/source/SourceManager.cpp`
- Delete: `sdk/source/IMusicSourceArtworkSession.h`
- Delete: `sdk/source/IMusicSourcePlugin.h`
- Delete: `sdk/source/IMusicSourceSession.h`
- Delete: `sdk/source/SourcePluginContext.h`
- Delete: `sdk/source/SourceTypes.h`
- Delete: `sdk/source/SourceTypes.cpp`
- Delete: obsolete v1 bridge/session/ABI tests and frozen-v1 fixture targets from `CMakeLists.txt`

**Interfaces:**
- Consumes: every v2 component from Tasks 1–13.
- Produces: one application composition root exposing `musicHub`, `playbackCoordinator` and `pluginSettings`; no QML-visible `mediaBridge` or v1 source session.

- [ ] **Step 1: Add a failing startup integration assertion**

In the QML startup test, require these context objects and reject the old one:

```cpp
QVERIFY(contextProperty("musicHub").value<QObject *>());
QVERIFY(contextProperty("playbackCoordinator").value<QObject *>());
QVERIFY(contextProperty("pluginSettings").value<QObject *>());
QVERIFY(!contextProperty("mediaBridge").isValid());
```

- [ ] **Step 2: Run startup and QML tests to verify failure**

```bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/cmake --build build/bridge --target quemusic_plugin_startup_test quemusic_music_hub_qml_test -j2
```

Expected: the composition root still exports `mediaBridge` and lacks one or more v2 objects.

- [ ] **Step 3: Build one owned composition root in `main.cpp`**

```cpp
SourceRegistry *sources = initializeSourceStartupBoundary(application, engine,
                                                           &sourceAccountStore);
SourceScopeStore sourceScope(sourceAccountSettings, &engine);
MusicHub musicHub(sources, &sourceScope, sourceAccountSettings, &engine);
PlaybackCoordinator playbackCoordinator(sources, &engine);
PluginSettingsController pluginSettings(sources->pluginManager(), sources,
                                        &sourceAccountStore, &engine);

engine.rootContext()->setContextProperty("musicHub", &musicHub);
engine.rootContext()->setContextProperty("playbackCoordinator", &playbackCoordinator);
engine.rootContext()->setContextProperty("pluginSettings", &pluginSettings);
```

Declare objects in dependency order so destruction occurs in reverse: QML engine releases pages first, then controllers, sessions, leases and finally plugin manager. Connect application shutdown to `SourceRegistry::closeAll()` before engine destruction.

- [ ] **Step 4: Remove v1 libraries, tests and source page artifacts**

Delete the listed v1 files only after no target includes them. Remove the old `quemusic_media_bridge`, v1 session-registry, v1 source-manager, frozen ABI and v1 fixture targets. Keep `SourceAccountStore`, `MacKeychainSecretStore` and any media value/model code still used by non-source legacy UI until its later migration plan.

Run:

```bash
rg -n 'MediaBridge|IMusicSourceSession([^V]|$)|MusicSourcePlugin/1\.0|SourceLibraryPage|sourceLibraryPage' --glob '!docs/superpowers/**' .
```

Expected: no production or active-test matches. Historical specifications may still mention v1.

- [ ] **Step 5: Update plugin API and smoke-test documentation**

Document the v2 IID, manifest requirements, optional provider interfaces, four-layer capability rule, schema-only settings UI, same-source native playlist restriction, credential redaction and unload sequence. Update the smoke test to read URL, username and password from environment variables and to exercise ping, recommendation, stream resolution, favorite round-trip on a designated test item only when explicitly enabled, and playlist CRUD on a temporary test playlist.

- [ ] **Step 6: Run the complete build and automated test suite**

```bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/cmake -S . -B build/bridge -DCMAKE_PREFIX_PATH=/Users/liqiang/Qt/6.11.1/macos -DBUILD_TESTING=ON
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/cmake --build build/bridge --target QueMusic -j2
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/ctest --test-dir build/bridge --output-on-failure
```

Expected: application and all tests build; CTest reports zero failures.

- [ ] **Step 7: Verify the macOS bundle and plugin packaging**

```bash
codesign --verify --deep --strict build/bridge/bin/QueMusic.app
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/ctest --test-dir build/bridge -R 'macos_(local_network_usage|runtime_libraries|bundle_plugin)' --output-on-failure
```

Expected: code-sign verification succeeds and all macOS packaging tests pass.

- [ ] **Step 8: Run opt-in Navidrome and GUI smoke tests**

With credentials supplied only through the current shell environment:

```bash
build/bridge/bin/quemusic_navidrome_smoke
open build/bridge/bin/QueMusic.app
```

Expected: the smoke executable connects without printing secrets; the GUI shows no “音乐源” navigation, defaults to “全部音源”, can select the configured Navidrome instance, loads all three pages, plays a track and exposes only negotiated actions. Creating and deleting a temporary playlist succeeds.

- [ ] **Step 9: Commit**

```bash
git add core/source/SourceStartup.* main.cpp CMakeLists.txt tests/NavidromeSmoke.cpp docs/PLUGIN_API.md docs/superpowers/runbooks/navidrome-smoke-test.md
git add -u core/media core/source sdk/source tests CMakeLists.txt
git commit -m "refactor: complete source sdk v2 migration"
```

---

## Completion Gate

Do not mark this plan complete until all of the following are true:

- The source-library page and navigation are absent.
- `musicHub` is the only QML music-source data boundary.
- Navidrome supports every endpoint group listed in the specification with negotiated capabilities.
- Unsupported, unavailable and forbidden actions are visually distinct and tested.
- Multiple Navidrome instances are addressable by stable source-instance IDs.
- Secrets remain outside QML, ordinary configuration, cache and logs.
- Repeated plugin unload/reload has no active session, request, lease or QML object leak.
- Complete CTest, macOS packaging checks, codesign verification and manual playback smoke tests pass.
