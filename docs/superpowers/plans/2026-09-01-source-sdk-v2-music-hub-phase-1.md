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
    const auto result = qvariant_cast<PageResultV2>(spy.takeFirst().at(1));
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

The page cache key includes page, section, source scope, search text, filters and cursor. Before serialization, retain only the allowlisted media fields from `MediaItemV2`; never serialize stream URLs, request headers, secrets, cookies or tokens. `PageRepository` emits a cached result immediately, then refreshes in the background. `ArtworkCache` accepts downloaded bytes and returns a local file URL; it never stores the authenticated remote URL.

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
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: session capability descriptors, account permissions, `MediaItemV2::availableActions` and optional provider interfaces.
- Produces: `CapabilityResolver::resolve()` and explicit action methods on `MediaActionRouter` with no generic string-command fallback.

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

- [ ] **Step 5: Run focused tests**

```bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/cmake --build build/bridge --target quemusic_capability_resolver_test quemusic_media_action_router_v2_test -j2
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/ctest --test-dir build/bridge -R '(capability_resolver|media_action_router_v2)' --output-on-failure
```

Expected: precedence, provider absence, account permissions, same-source constraints and exact-instance routing pass.

- [ ] **Step 6: Commit**

```bash
git add core/music/CapabilityResolver.* core/music/MediaActionRouter.* tests/tst_CapabilityResolver.cpp tests/tst_MediaActionRouterV2.cpp CMakeLists.txt
git commit -m "feat: route media actions by effective capability"
```

### Task 7: 建立 MusicHub 和四个页面 ViewModel

**Files:**
- Create: `core/music/MusicHub.h`
- Create: `core/music/MusicHub.cpp`
- Create: `tests/tst_MusicHub.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `SourceScopeStore`, `SourceRegistry`, `PageRepository`, four `MusicPageModel` instances and `MediaActionRouter`.
- Produces: QML properties `recommendation`, `category`, `favorites`, `search`, `sourceOptions`, `selectedSourceInstanceId` and explicit refresh/search/load-more methods.

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
public:
    Q_INVOKABLE void activatePage(int pageKind);
    Q_INVOKABLE void refresh(int pageKind);
    Q_INVOKABLE void loadMore(int pageKind, const QString &sectionId);
    Q_INVOKABLE void search(const QString &text);
    Q_INVOKABLE void cancel(int pageKind);
    Q_INVOKABLE QUuid loadArtwork(const QVariantMap &media);
    Q_INVOKABLE QUuid loadLyrics(const QVariantMap &media);
signals:
    void artworkReady(QVariantMap media, QUrl localUrl);
    void lyricsReady(QVariantMap media, QString lyrics);
};
```

`sourceOptions` starts with `{sourceInstanceId: "", displayName: "全部音源", available: true}` followed by enabled registry instances. Only activated pages auto-refresh on scope changes; inactive pages refresh when first activated.

`loadArtwork()` and `loadLyrics()` delegate to `MediaAssetRepository`; the hub converts typed results to QML-safe values and never exposes authenticated remote URLs.

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

- [ ] **Step 4: Run focused tests**

```bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/cmake --build build/bridge --target quemusic_music_hub_test -j2
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/ctest --test-dir build/bridge -R music_hub --output-on-failure
```

Expected: shared scope, activation, cancellation, partial errors and source option updates pass.

- [ ] **Step 5: Commit**

```bash
git add core/music/MusicHub.* tests/tst_MusicHub.cpp CMakeLists.txt
git commit -m "feat: expose unified music hub to qml"
```

### Task 8: 用声明式 schema 重做插件设置与多实例账号管理

**Files:**
- Create: `core/settings/PluginSettingsController.h`
- Create: `core/settings/PluginSettingsController.cpp`
- Create: `components/SchemaSettingsForm.qml`
- Create: `components/SourceAccountList.qml`
- Modify: `components/PluginSettingsPanel.qml`
- Modify: `SettingsView.qml`
- Create: `tests/tst_PluginSettingsController.cpp`
- Create: `tests/tst_PluginSettingsQml.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `PluginManager`, `SourceRegistry`, `SourceAccountStore`, `SettingsSchemaV2` and secure secret storage.
- Produces: generic QML models `plugins`, `selectedPlugin`, `instances`, `settingsSections`, plus create/update/remove/enable/test/reload actions with no Navidrome-specific C++ or QML method.

- [ ] **Step 1: Write failing controller tests**

```cpp
void PluginSettingsControllerTest::secretsNeverAppearInQmlValues()
{
    controller.selectPlugin("org.quemusic.source.navidrome");
    controller.saveInstance({}, {{"serverUrl", "https://music.example"},
                                 {"username", "admin"},
                                 {"password", "secret"}});
    const QVariantMap row = controller.instances().first().toMap();
    QVERIFY(!row.contains("password"));
    QVERIFY(row.value("credentialConfigured").toBool());
}

void PluginSettingsControllerTest::unloadReportsActiveSessionReason()
{
    openSession("navidrome/home");
    QVERIFY(!controller.unloadPlugin("org.quemusic.source.navidrome"));
    QCOMPARE(controller.lastErrorKey(), "source.sessions.active");
}


void PluginSettingsControllerTest::testConnectionUsesEphemeralSession()
{
    const QUuid request = controller.testConnection(
        {{"serverUrl", "https://music.example"},
         {"username", "admin"}, {"password", "secret"}});
    QVERIFY(!request.isNull());
    completeEphemeralOpen(SourceSessionStateV2::Ready);
    QTRY_COMPARE(connectionSpy.count(), 1);
    QCOMPARE(registry.liveSessionCount(), 0);
}
```

- [ ] **Step 2: Write failing QML layout assertions**

```cpp
QVERIFY(findObject(root, "pluginMasterList"));
QVERIFY(findObject(root, "pluginDetailPane"));
QVERIFY(findObject(root, "schemaSettingsForm"));
QVERIFY(!findObject(root, "navidromeConfigAction"));
```

- [ ] **Step 3: Run and verify failure**

```bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/cmake --build build/bridge --target quemusic_plugin_settings_controller_test quemusic_plugin_settings_qml_test -j2
```

Expected: generic controller and schema form are missing; old Navidrome-specific action still exists.

- [ ] **Step 4: Implement schema-driven form data and secret separation**

```cpp
QVariantList PluginSettingsController::settingsSections() const
{
    auto *settings = qobject_cast<IPluginSettingsProviderV2 *>(selectedPluginObject());
    if (settings == nullptr) return {};
    return settingsSchemaToVariantList(settings->settingsSchema(),
                                       m_draftNonSecretValues,
                                       m_credentialConfigured);
}

bool PluginSettingsController::saveInstance(const QString &instanceId,
                                             const QVariantMap &draft)
{
    const auto split = splitSettingsBySecretFlag(currentSchema(), draft);
    if (!validateSettings(currentSchema(), split.nonSecret, split.secret)) return false;
    return m_accountStore->upsert(toSourceAccount(instanceId, split), true, &m_lastError);
}
```

The QVariant model contains field ID, label key, type, required, choices, current non-secret value and `credentialConfigured`. It never contains an existing secret. An empty secret during edit preserves the credential reference.

`testConnection(draft)` creates a non-persisted session, waits for `Ready`, `AuthenticationRequired` or `Failed`, emits a sanitized result, then closes and destroys that session and releases its lease. It never writes the draft credential.

- [ ] **Step 5: Replace the card list with the approved A layout**

```qml
Row {
    anchors.fill: parent
    spacing: 16
    ListView {
        id: pluginMasterList
        objectName: "pluginMasterList"
        width: 260
        model: pluginSettings.plugins
    }
    Column {
        id: pluginDetailPane
        objectName: "pluginDetailPane"
        width: parent.width - pluginMasterList.width - 16
        SourceAccountList { model: pluginSettings.instances }
        SchemaSettingsForm {
            id: schemaForm
            objectName: "schemaSettingsForm"
            sections: pluginSettings.settingsSections
        }
    }
}
```

Remove `navidromeAccountDialog`, `createNavidromeAccount`, `updateNavidromeAccount` and all `modelData.id === "navidrome"` branches from QML. Keep common load, unload and reload actions in the detail header.

- [ ] **Step 6: Run focused tests**

```bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/cmake --build build/bridge --target quemusic_plugin_settings_controller_test quemusic_plugin_settings_qml_test -j2
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/ctest --test-dir build/bridge -R plugin_settings --output-on-failure
```

Expected: schema rendering, validation, secret redaction, multiple instances and busy unload tests pass offscreen.

- [ ] **Step 7: Commit**

```bash
git add core/settings components/PluginSettingsPanel.qml components/SchemaSettingsForm.qml components/SourceAccountList.qml SettingsView.qml tests/tst_PluginSettingsController.cpp tests/tst_PluginSettingsQml.cpp CMakeLists.txt
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
- Modify: `plugins/navidrome-source/NavidromeSourceSession.h`
- Modify: `plugins/navidrome-source/NavidromeSourceSession.cpp`
- Modify: `plugins/navidrome-source/CMakeLists.txt`
- Modify: `tests/tst_NavidromeSource.cpp`

**Interfaces:**
- Consumes: `IPageProviderV2`, `IPlaybackProviderV2`, `IDownloadProviderV2`, Navidrome transport and v2 DTOs.
- Produces: recommendation/category/favorites/search pages plus stream, artwork, lyrics and download descriptors.

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

Mappers contain no network or session state. Every item receives stable `MediaRefV2`, source badge metadata, server IDs, duration, cover art reference, reliable external IDs and per-object action candidates.

- [ ] **Step 5: Implement endpoint dispatch and pagination**

Map standard sections to `getAlbumList2`, `getGenres`, `getArtists`, `getArtist`, `getAlbum`, `getSong`, `getSongsByGenre`, `getStarred2` and `search3`. `PageQueryV2.filters` uses the mutually exclusive keys `artistId`, `albumId`, `songId` or `genre` for drill-down. A top-level `Tracks` request without one of these filters returns typed `Unsupported` rather than inventing a server-wide song order. Translate offset-based endpoints to opaque provider cursors encoded as decimal offsets; reject negative or malformed cursors. Use `size=query.limit` and cap the accepted limit to `1..500`.

- [ ] **Step 6: Implement media providers**

`resolveStream` uses `stream`; artwork uses `getCoverArt`; lyrics prefer `getLyricsBySongId` only when negotiated and otherwise use the compatible lyric endpoint; download uses `download`. Stream/download URLs are emitted transiently and never inserted into page metadata or disk cache.

- [ ] **Step 7: Run focused tests**

```bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/cmake --build build/bridge --target quemusic_navidrome_source_test -j2
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/ctest --test-dir build/bridge -R navidrome_source --output-on-failure
```

Expected: all endpoint, mapper, pagination, lyrics fallback, stream, artwork and download tests pass.

- [ ] **Step 8: Commit**

```bash
git add plugins/navidrome-source/NavidromeMappers.* plugins/navidrome-source/NavidromeSourceSession.* plugins/navidrome-source/CMakeLists.txt tests/tst_NavidromeSource.cpp
git commit -m "feat: expose navidrome page feeds and media"
```

### Task 11: 补齐 Navidrome 收藏、评分、歌单、队列和书签

**Files:**
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
    session.addPlaylistTracks(playlistRef("p1"), {localTrack("local/default", "l1")});
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

For `star`/`unstar`, map track IDs to `id`, album IDs to `albumId`, and artist IDs to `artistId`. Expose rating only for tracks. Encode repeated playlist additions as `songIdToAdd`, repeated removals as `songIndexToRemove`, and queue items as repeated `id` parameters in preserved order.

When the server returns an authorization error for a typed action, change only that action to `Forbidden` with reason `source.permission.<action>`, emit `capabilitiesChanged`, and keep unrelated actions available. Reconnect resets negotiated/account capability state.

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

Do not mutate page models from the plugin. `MediaActionRouter` applies the confirmed result, except favorite/rating optimistic state which it rolls back on `requestFailed`.

- [ ] **Step 6: Run focused tests**

```bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/cmake --build build/bridge --target quemusic_navidrome_source_test quemusic_media_action_router_v2_test -j2
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/ctest --test-dir build/bridge -R '(navidrome_source|media_action_router_v2)' --output-on-failure
```

Expected: all mutations, validation, same-instance constraints, server failures and optimistic rollback pass.

- [ ] **Step 7: Commit**

```bash
git add plugins/navidrome-source/NavidromeSourceSession.* plugins/navidrome-source/NavidromeMappers.* tests/tst_NavidromeSource.cpp
git commit -m "feat: complete navidrome user operations"
```

### Task 12: 建立统一播放协调器和 Navidrome scrobble

**Files:**
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
    onTriggered: musicHub.actions.download(model.ref, downloadDestination)
}
```

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
