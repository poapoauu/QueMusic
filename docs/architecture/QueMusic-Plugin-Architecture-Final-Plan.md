# QueMusic 全音源插件化改造最终落地方案

> 版本：Final Architecture Plan v1.0  
> 日期：2026-09-15  
> 目标基线：QueMusic `main` + 已完成的 `codex/unified-media-bridge` / Source SDK v2 / Original UI Adapter 工作成果  
> 核心原则：**尽量保留现有 QueMusic UI；所有音乐源（包括本地音乐）统一插件化；音源特有登录/配置/管理业务由插件提供；视觉体系由 QueMusic 统一控制。**

---

## 1. 文档目的

本文档用于把 QueMusic 当前主工程、前期已经完成的 Source SDK v2 / Navidrome / Playback / Original UI Adapter 工作，以及本轮新增需求统一收敛为一套可直接实施的最终架构。

本方案不是重新设计一套播放器 UI，也不是把每个音源做成一个独立“小应用”。最终目标是：

1. 最大限度保留 QueMusic 现有 UI、交互和视觉风格。
2. 主程序不再内置任何具体音乐平台逻辑。
3. **本地音乐、Navidrome、酷狗、网易云、Jellyfin、Subsonic 等全部是 Source Plugin。**
4. 主程序不再写死“酷狗扫码登录”“网易云扫码登录”“Navidrome 地址/账号”等平台专用 UI。
5. 插件可以提供自己的管理/登录/设置业务界面，但必须通过 QueMusic 公共 Plugin UI API 保持统一视觉。
6. 播放、队列、搜索、歌单、收藏、歌词等核心体验仍由 QueMusic 统一 UI 提供。
7. 尽量复用已经完成并通过测试的 Source SDK v2，不进行无必要的全量推翻。

---

# 2. 最终架构结论

最终 QueMusic 应从：

```text
QueMusic
├── 内置本地音乐逻辑
├── 内置酷狗 API
├── 内置网易云 API
├── MusicApi 单例
└── QML 直接操作具体音源/MediaPlayer
```

演进为：

```text
QueMusic = Music Source Plugin Host + Unified Player UI
```

即：

```text
┌──────────────────────────────────────────────────────────────┐
│                    QueMusic 原有 UI                          │
│                                                              │
│ Home / Search / Playlist / Favorite / Local-like Library     │
│ Queue / Player / Lyrics / Desktop Lyrics / Settings          │
└─────────────────────────────┬────────────────────────────────┘
                              │
                              ▼
┌──────────────────────────────────────────────────────────────┐
│                    Music UI Adapter                          │
│            OriginalUiMusicAdapter / 后续 MusicUiAdapter      │
│                                                              │
│ QML 只看到安全、稳定、统一的数据模型和动作接口               │
└─────────────────────────────┬────────────────────────────────┘
                              │
                              ▼
┌──────────────────────────────────────────────────────────────┐
│                       Source Core                            │
│                                                              │
│ SourceRegistry                                               │
│ SourceAccount / Instance Store                               │
│ CapabilityResolver                                           │
│ MediaActionRouter                                            │
│ MusicPageModel / PageRepository / Cache                      │
│ PlaybackCoordinator                                          │
│ PluginManager                                                │
│ PluginSettingsController                                     │
│ PluginUiHost                                                 │
└─────────────────────────────┬────────────────────────────────┘
                              │
            ┌─────────────────┼─────────────────┐
            ▼                 ▼                 ▼
┌──────────────────┐ ┌─────────────────┐ ┌──────────────────┐
│ Local Source     │ │ Navidrome       │ │ Kugou / Netease │
│ Plugin           │ │ Source Plugin   │ │ Source Plugin    │
│                  │ │                 │ │                  │
│ Browse/Search    │ │ Browse/Search   │ │ Browse/Search    │
│ Metadata         │ │ Playlist        │ │ Playlist         │
│ Lyrics/Artwork   │ │ Favorite        │ │ Favorite         │
│ Playback         │ │ Scrobble        │ │ Playback         │
│ Management UI    │ │ Management UI   │ │ QR Login UI      │
└──────────────────┘ └─────────────────┘ └──────────────────┘
```

---

# 3. 已确认的 QueMusic 当前主工程基线

GitHub 项目：`https://github.com/BroNekoX/QueMusic.git`

当前主工程的重要事实如下。

## 3.1 UI

QueMusic 当前采用 Qt Quick / QML，主要页面和组件包括：

```text
main.qml
SettingsView.qml
FullCenterView.qml

layout/
├── LeftSideBar.qml
├── MainContent.qml
├── PlayerControl.qml
└── PlayerMaxCenter.qml

pages/
├── HomePage.qml
├── SearchPage.qml
├── PlaylistPage.qml
├── FavouritePage.qml
├── FilePage.qml
└── DownloadPage.qml

components/
├── Style.qml
├── Options.qml
├── Playback.qml
└── 大量自研 UI 控件
```

**本方案明确保留这一套视觉和页面结构作为主 UI 基线。**

不做“为了插件化重新设计 Music Hub UI”的大规模 UI 重写。

---

## 3.2 当前在线音乐接口

当前在线逻辑集中在：

```text
api/MusicApiService.cpp/h
```

其本质是一个 QML Singleton：

```text
MusicApi
```

内部直接依赖：

```text
KugouApi
NeteaseCloudApi
```

并通过 `songSource = 0/1` 选择具体平台。

这是后续必须淘汰的核心耦合点。

---

## 3.3 当前播放链路

当前核心链路大致为：

```text
MusicApi.getMusicInfo()
        │
        ▼
MusicApi.urlplay(...)
        │
        ▼
main.qml
        │
        ▼
mainMedia.source = playurl
        │
        ▼
QMediaPlayer
```

当前 `Playback.qml` 已经承担大量统一播放控制能力，包括：

- 淡入淡出；
- 换源防爆音；
- A-B 循环；
- 睡眠定时；
- 音量；
- seek；
- 播放历史；
- 随机播放；
- Queue 控制。

这些用户体验应保留，但播放资源解析必须逐渐移到 `PlaybackCoordinator`。

---

## 3.4 当前本地音乐

本地音乐目前属于主程序内部能力，主要由：

```text
FolderModel
LocalMusicScanner
LocalLyricsReader
CoverHelper
TagLib
FilePage.qml
```

组成。

最终这些能力必须被收拢进：

```text
org.quemusic.source.local
```

本地音乐不再是 Source Core 里的特殊分支。

---

# 4. 前期已经完成的改造成果

以下成果来自此前的 `codex/unified-media-bridge` 工作和随本次材料提供的验证文档。

这些内容原则上**保留并继续演进，不重新发明**。

---

## 4.1 Source SDK v2 —— 已完成

已完成 Source SDK v2 基础数据和接口，包括：

- `SourceV2Types`
- `MediaRefV2`
- `SourceIdentityV2`
- `ActionAvailabilityV2`
- `IMusicSourcePluginV2`
- `IMusicSourceSessionV2`
- 多个可选 Provider Interface
- Qt metatype 注册
- Request ID 关联机制
- Source SDK v1/v2 ABI 边界

Provider 体系已经包含播放、页面、收藏、评分、Scrobble、歌单、下载、播放队列、书签、设置等方向。

### 最终决策

**继续使用 Source SDK v2。**

当前新增需求并不要求推翻 v2 ABI。

本轮新增的插件 UI 应作为：

```text
Plugin UI API 1.0
```

独立演进，不为了 UI 扩展无意义升级 Source SDK v3。

只有未来确实需要破坏性修改 Source 核心接口时，才升级 Source SDK 主 ABI。

---

## 4.2 Plugin 加载和 ABI 校验 —— 已完成

现有工作已实现：

- Plugin manifest；
- Source SDK ABI 校验；
- Qt Major 校验；
- CPU architecture 校验；
- build key 校验；
- IID 校验；
- `QPluginLoader` 前置验证；
- active lease 阻止插件错误卸载；
- Busy 状态和原因；
- v2 插件加载边界。

最终阶段已移除生产路径中的旧 v1 Source SDK 使用。

### 本轮复用

新增 Local / Kugou / Netease 等插件继续复用当前 PluginManager 和 manifest 体系。

---

## 4.3 SourceRegistry 与稳定 Source Instance Identity —— 已完成

现有 v2 已经实现了 SourceRegistry 和稳定 Source Instance Identity。

这正好满足本轮“同一插件可以配置多个音源实例”的需求。

例如：

```text
Plugin Package:
org.quemusic.source.navidrome

Source ID:
navidrome

Instances:
navidrome/home
navidrome/office
navidrome/test
```

### 最终要求

任何媒体对象都不能再仅靠：

```text
songId
hash
path
```

作为全局身份。

必须至少包含：

```text
sourceId
sourceInstanceId
accountId
entityType
entityId
```

当前 v2 的 Identity 工作应继续作为唯一标准。

---

## 4.4 MusicPageModel / Repository / Cache —— 已完成

现有改造已经建立统一音乐数据页模型和仓储/缓存层，用于：

- 推荐；
- 分类；
- 搜索；
- 收藏；
- 歌单；
- 单 Source 页面；
- 聚合页面；
- continuation/pagination；
- artwork/cache；
- source-ordered playlist；
- discovery dedup。

这些能力继续作为 Original UI Adapter 的底层数据来源。

---

## 4.5 CapabilityResolver / MediaActionRouter —— 已完成

现有 Capability 模型已经能够综合：

```text
Plugin Capability
Server Capability
Account Capability
Media Capability
```

判断某个操作是否真正可用。

已有动作覆盖包括：

- Favorite / Unfavorite；
- Playlist create/update/delete；
- Add/Remove Playlist Tracks；
- Rating；
- Scrobble；
- Download；
- Queue / Bookmark 等。

### 最终要求

UI 不允许再写：

```qml
if (source === "navidrome") ...
if (source === "local") ...
```

必须写成 Capability 驱动：

```text
canPlay
canFavorite
canAddToPlaylist
canDownload
canDelete
canScrobble
...
```

---

## 4.6 MusicHub / 页面协调层 —— 已完成

已有 MusicHub / 页面模型协调能力，包括：

- 多个稳定 `MusicPageModel`；
- source scope；
- refresh；
- search；
- cancel；
- continuation；
- retry；
- page cache / artwork cache。

即使最终 UI 恢复为原 QueMusic UI，该核心仍可继续作为统一数据协调层。

---

## 4.7 Settings Schema / Secure Storage / Settings Controller —— 已完成且必须重点复用

这一部分与本轮“插件自己管理登录和配置”高度相关。

前期已经完成：

- `SettingsSchemaV2`；
- schema validation；
- Text / URL / Directory / Boolean / Integer / Choice / Secret 等字段；
- visible/condition/action；
- `SourceAccountStore`；
- public configuration 与 secret 分离；
- named secret envelope；
- metadata-only update；
- secret rollback；
- secure backend；
- `PluginSettingsController`；
- `SchemaSettingsForm.qml`；
- `PluginSettingsPanel.qml`；
- settings action/probe；
- directory adapter；
- capability/state 显示。

### 最终决策

这套 Schema Settings 不废弃。

它将成为插件管理 UI 的**第一层标准机制**。

也就是：

```text
80% 普通配置 -> Settings Schema 自动渲染
20% 特殊业务 -> Custom Plugin QML
```

Navidrome 的普通服务器地址、用户名、密码、播放质量等，优先继续使用 Schema。

二维码登录、复杂服务器发现、账号绑定等标准 Schema 无法表达的功能，才使用 Custom QML。

---

## 4.8 Navidrome Source v2 —— 已完成核心能力

现有工作已经完成 Navidrome v2 Source 的大量核心能力，包括：

- v2 handshake；
- Browse/Search；
- Album/Artist/Track/Playlist mapping；
- Favorites；
- Playlist operations；
- Rating；
- Queue；
- Bookmark；
- Scrobble；
- Artwork；
- Lyrics；
- Download；
- Playback stream descriptor；
- Capability negotiation；
- 生命周期和 cancellation；
- Source identity 修正。

当前这部分应继续作为第一个远程 Source 的基准实现。

---

## 4.9 PlaybackCoordinator —— 已完成

现有工作已经建立统一播放协调层：

```text
MediaRef
   │
   ▼
PlaybackCoordinator
   │
   ▼
Source Playback Provider
   │
   ▼
StreamDescriptor
   │
   ▼
PlaybackSink
```

并具备：

- request correlation；
- generation fencing；
- provider lifecycle；
- scrobble timing；
- cancellation；
- playback ownership；
- stream validation。

该层应成为所有 Source（包括 Local）的唯一播放资源解析入口。

---

## 4.10 QtPlaybackController —— 已完成

已有 `QtPlaybackController` 封装 `QMediaPlayer + QAudioOutput`，并通过私有 backend 和 generation fence 避免旧回调污染新播放。

### 已知限制

当前 Qt backend 对带自定义 HTTP Header 的播放描述符支持有限，现有实现会拒绝 non-empty headers。

因此最终架构仍保留：

```text
PlaybackDescriptor.headers
```

但第一阶段插件需要优先返回：

- file URL；
- 可直接访问 URL；
- 临时签名 URL；
- URL 内认证形式（仅短生命周期、不可持久化）。

未来需要 Header Streaming 时，再增加新的网络流式 PlaybackBackend，而不是把认证逻辑重新塞回 UI。

---

## 4.11 PlaybackControlsAdapter —— 已完成

已有安全的 QML Playback Adapter，可将 QtPlaybackController 状态暴露给 QML，同时保持 null-safe。

继续保留。

---

## 4.12 OriginalUiMusicAdapter —— 已完成

2026-09-12 阶段已经恢复原 QueMusic 音乐 UI，并用安全 Adapter 把 Source v2 数据映射回原 UI。

已完成：

- 推荐页面 Adapter；
- 收藏；
- 搜索；
- Detail；
- Queue；
- Player；
- Capability 控制；
- 私有完整 identity lookup；
- QML 不直接拿完整敏感 Source Map；
- nullable-safe；
- 缺 Adapter 时不产生 `undefined` / `TypeError`。

验证结果：

```text
Full applicable CTest: 35/35 passed
Navidrome isolated source test: 1/1 passed
git diff --check: passed
```

### 当前必须修正的一点

当时最终报告仍然保留：

```text
legacy/local playback keeps its existing path
```

本轮最终方案明确：

> 这只能作为迁移期兼容路径，不能作为最终架构。

最终本地音乐也必须经过：

```text
Local Source Plugin
→ Source SDK v2
→ PlaybackCoordinator
→ QtPlaybackController
```

---

# 5. 新增核心需求：所有音源全部插件化

从现在开始建立硬性原则：

> **QueMusic Core 不拥有“音乐源”。QueMusic Core 只拥有 Source Plugin Host。**

因此以下能力全部移出 Core：

```text
KugouApi
NeteaseCloudApi
LocalMusicScanner 的 Source 业务层
平台登录逻辑
平台 Cookie/Token 业务
平台 Playlist/Favorite 业务
平台服务器配置
```

最终 Source 列表可以是：

```text
org.quemusic.source.local
org.quemusic.source.navidrome
org.quemusic.source.kugou
org.quemusic.source.netease
org.quemusic.source.jellyfin
org.quemusic.source.subsonic
...
```

主程序只根据 manifest + SourceRegistry 动态发现它们。

---

# 6. 本地音乐插件化

## 6.1 插件身份

建议：

```text
Package ID: org.quemusic.source.local
Source ID: local
Default Instance ID: local/default
Account ID: default
```

Local Plugin 随 QueMusic 默认发行，但架构上仍是标准 Source Plugin。

可以设计为：

- 默认启用；
- 用户可以禁用；
- 是否允许物理卸载由发行策略决定；
- Core 不能对它做任何特殊 API 分支。

---

## 6.2 Local Plugin 复用现有代码

迁移而不是重写：

```text
现有 FolderModel              → Local Source Browse/Library
现有 LocalMusicScanner        → Local Source Scanner
现有 LocalLyricsReader        → Lyrics Provider
现有 CoverHelper / TagLib     → Metadata/Artwork Provider
本地 path                     → Playback Descriptor(file://)
```

第一阶段 Local `entityId` 可以采用规范化 canonical file URL/path。

后续如果需要支持“文件移动后仍保持同一媒体 ID”，再引入 Local Library DB UUID；不作为本轮插件化阻塞条件。

---

## 6.3 Local Plugin Capability

初期建议支持：

```text
Browse
Search
Play
Artwork
Lyrics
Metadata
Library
```

可选：

```text
Favorite
Playlist
DeleteFile
Rescan
WatchDirectories
```

其中 `Rescan / WatchDirectories / Directory configuration` 属于管理 UI，而不是主播放 UI。

---

# 7. 插件管理 UI：最终模型

用户提出的关键问题是正确的：

> 如果酷狗插件已经卸载，主程序里仍然写死一个“酷狗扫码登录”页面，那么它并没有真正插件化。

因此最终必须做到：

```text
主程序不包含任何具体 Source 的登录/设置页面。
```

---

# 8. 插件 UI 采用“两级模型”

## 8.1 Level 1：Settings Schema 自动渲染

优先使用已经完成的：

```text
SettingsSchemaV2
PluginSettingsController
SchemaSettingsForm.qml
PluginSettingsPanel.qml
```

适用：

- Server URL；
- Username；
- Password；
- Token；
- 文件夹路径；
- Checkbox；
- 下拉选项；
- 数字设置；
- 简单“测试连接” Action；
- 简单状态。

例如 Navidrome 完全可以先用 Schema 完成：

```text
服务器地址
用户名
密码
转码策略
默认音质
Scrobble 开关
测试连接
```

这类 UI 100% 由 QueMusic 渲染，因此视觉天然统一。

---

## 8.2 Level 2：Custom Plugin Management QML

只有 Schema 无法合理表达时才使用。

典型场景：

- 酷狗二维码登录；
- 网易云二维码登录；
- OAuth 浏览器回调；
- PIN 登录；
- 局域网服务器发现；
- 多阶段初始化；
- 特殊同步状态；
- 复杂账户绑定。

插件提供：

```text
ManagementPage.qml
```

但不能任意设计视觉。

它必须使用：

```qml
import QueMusic.PluginUI 1.0
```

---

# 9. QueMusic Plugin UI API 1.0

这是本轮新增加的独立公共 API。

建议模块：

```text
QueMusic.PluginUI 1.0
```

## 9.1 公共基础组件

第一版建议至少提供：

```text
PluginPage
PluginScrollPage
PluginSection
PluginGroup

PluginLabel
PluginDescription
PluginSeparator

PluginButton
PluginPrimaryButton
PluginDangerButton
PluginIconButton

PluginTextField
PluginPasswordField
PluginNumberField
PluginUrlField
PluginDirectoryField

PluginSwitch
PluginCheckBox
PluginComboBox

PluginStatus
PluginBadge
PluginBusyIndicator
PluginErrorState
PluginEmptyState

PluginAccountCard
PluginServerCard
PluginQrCode
PluginQrLogin
```

插件开发者不需要自己处理：

- 字体；
- 字号；
- 颜色；
- 圆角；
- hover；
- pressed；
- disabled；
- 间距；
- 阴影；
- 动画；
- 深浅主题。

---

## 9.2 PluginTheme

插件不能直接依赖内部 `Style.qml`。

应增加稳定公共桥：

```text
内部：Style.qml
       │
       ▼
公共：PluginTheme
       │
       ▼
Plugin UI Kit
       │
       ▼
插件 Management QML
```

PluginTheme 只暴露**语义 token**，例如：

```text
background
surface
surfaceHover
primary
textPrimary
textSecondary
border
success
warning
danger

spacingSmall
spacingMedium
spacingLarge

radiusSmall
radiusMedium
radiusLarge

fontCaption
fontBody
fontTitle
```

禁止插件依赖：

```text
Style.themes.xxx
QButton
QCard
Options
mainMedia
window
MusicApi
```

这些属于 QueMusic Private API。

---

# 10. Plugin UI Contract

## 10.1 不将 UI 塞进 Source 核心接口

不要改成：

```cpp
class IMusicSourcePluginV2 {
    virtual QQuickItem *settingsPage() = 0;
};
```

这会污染 Source SDK。

建议使用独立可选 Provider：

```text
ISourceManagementUiProvider
```

其 ABI 属于：

```text
Plugin UI API 1.0
```

而不是 Source SDK ABI。

---

## 10.2 推荐描述结构

概念上：

```cpp
struct ManagementUiDescriptor {
    QString uiApiVersion;       // "1.0"
    QUrl componentUrl;          // qml/ManagementPage.qml
    bool supportsCreate;
    bool supportsEdit;
};
```

插件可选实现：

```cpp
class ISourceManagementUiProvider
{
public:
    virtual ManagementUiDescriptor managementUi() const = 0;
};
```

如果插件没有此 Provider：

```text
Host -> 使用 Settings Schema
```

如果插件同时提供 Schema + Custom UI：

```text
Custom UI 可以嵌入 Schema Form，
或者由 Custom UI 负责特殊区块、Schema 负责普通配置。
```

---

# 11. PluginUiContext

自定义 QML 不应该获得整个 QueMusic 全局上下文。

Host 只提供一个明确的：

```text
PluginUiContext
```

建议包含：

```text
pluginPackageId
sourceId
sourceInstanceId
accountId
mode(Create/Edit)

backend        // 插件自有 QObject UI backend
settings       // Host 提供的安全 settings bridge
capabilities   // 管理动作状态
host           // 极小的 Host UI service
```

不提供：

```text
mainMedia
playListModel
MusicApi
window
AccountManager
内部 Style
其它 Source 对象
```

说明：因为 Source Plugin 本身是 native C++ 动态库，Plugin UI API 的隔离目标主要是**架构隔离和兼容性**，不是把 native 插件当作恶意代码进行安全沙箱。

---

# 12. 插件认证与 Secret 管理

插件可以控制认证流程，但**Secret 的持久化继续由 Host 管理**。

例如 Navidrome：

```text
Management UI
   │
   ▼
Navidrome UI Backend
   │
   ▼
PluginSettingsController / SourceAccountStore
   │
   ▼
Secure Credential Backend
```

二维码登录：

```text
KugouManagement.qml
     │
     ▼
KugouUiBackend.requestQr()
     │
     ▼
二维码展示
     │
     ▼
扫码成功 -> Backend 获得 Cookie/Token
     │
     ▼
Host Secure Credential Store
```

严禁：

```qml
Settings {
    property string cookie
    property string password
}
```

敏感值不能落入普通 QML Settings、日志、普通 metadata 或展示模型。

---

# 13. Settings 页面最终布局

保留此前已经认可的：

> **插件列表 + 独立详情**

推荐最终结构：

```text
┌──────────────────────────────────────────────────────────┐
│ 设置 > 音源插件                                          │
├──────────────────┬───────────────────────────────────────┤
│ 本地音乐         │ 本地音乐                              │
│ Navidrome        │                                       │
│   家庭服务器     │ [Plugin Management Content]           │
│   测试服务器     │                                       │
│ 酷狗音乐         │                                       │
│ 网易云音乐       │                                       │
│                  │                                       │
│ + 添加音源       │                                       │
└──────────────────┴───────────────────────────────────────┘
```

Host 负责：

- 页面 Header；
- 左侧列表；
- 插件 Icon；
- Instance 名称；
- Missing/Disabled/Error 状态；
- 页面 Margin；
- Scroll 容器；
- Theme；
- 删除/禁用 Instance 的标准确认。

插件只负责右侧业务 Content。

---

# 14. 插件卸载后的 UI 行为

必须从架构上解决“插件卸载但登录 UI 还留着”的问题。

规则：

```text
Management UI 的存在 = Plugin 当前已加载并声明 UI Provider
```

所以插件卸载以后：

```text
不存在 Custom QML
不存在平台登录按钮
不存在平台专用 UI
```

但 Source Instance 配置建议默认**保留**，显示统一 Host Missing Plugin 页面：

```text
家庭音乐服务器

状态：所需插件未安装
插件：org.quemusic.source.navidrome

[删除此音源配置]
```

重新安装对应插件后，原 Instance 可以恢复。

用户显式选择“删除配置及凭据”时，再清除 metadata 和 secret。

---

# 15. Plugin Package 推荐结构

```text
plugins/
└── org.quemusic.source.navidrome/
    ├── plugin.json
    ├── quemusic_source_navidrome.dll
    ├── qml/
    │   ├── ManagementPage.qml
    │   ├── LoginSection.qml
    │   └── ServerStatus.qml
    └── resources/
        └── navidrome.svg
```

Local：

```text
plugins/
└── org.quemusic.source.local/
    ├── plugin.json
    ├── quemusic_source_local.dll
    ├── qml/
    │   └── ManagementPage.qml   # 可选；简单设置也可全用 Schema
    └── resources/
        └── local.svg
```

酷狗：

```text
plugins/
└── org.quemusic.source.kugou/
    ├── plugin.json
    ├── quemusic_source_kugou.dll
    ├── qml/
    │   ├── ManagementPage.qml
    │   └── QrLogin.qml
    └── resources/
```

---

# 16. Manifest 扩展建议

在现有 plugin manifest 基础上增加 Plugin UI 信息，而不改变 Source SDK v2 ABI。

示例：

```json
{
  "id": "org.quemusic.source.navidrome",
  "name": "Navidrome",
  "version": "1.0.0",
  "sourceSdkAbi": 2,
  "pluginUiApi": "1.0",
  "sourceId": "navidrome",
  "instances": {
    "multiple": true
  },
  "ui": {
    "management": "qml/ManagementPage.qml"
  }
}
```

注意：

- manifest 里的 UI 字段是声明信息；
- runtime 仍要通过 Provider/兼容性验证；
- Component 必须位于插件自己的允许目录/resource namespace；
- Host 检查 Plugin UI API 版本。

---

# 17. 主音乐 UI 与插件 Management UI 的边界

这是最终方案必须坚持的边界。

## 插件可以拥有

```text
登录
登出
账号状态
服务器地址
服务器发现
API Token
Cookie / OAuth / QR 流程
转码配置
插件特殊同步
扫描目录
重新扫描
插件高级设置
```

## 插件不能接管

```text
QueMusic 首页
搜索结果视觉
统一歌曲列表
统一歌单详情
统一专辑详情
统一歌手详情
收藏页面
播放队列
底部播放器
沉浸歌词
桌面歌词
播放核心 UI
```

这些仍然由 QueMusic 统一实现。

也就是说：

```text
Plugin = Data + Capability + Action + Management UI
Host   = Music Experience UI + Playback UX
```

---

# 18. OriginalUiMusicAdapter 的最终定位

前期已经完成的 `OriginalUiMusicAdapter` 不应删除。

它就是此次“保留原 UI”战略的关键隔离层。

建议最终结构：

```text
QML 原页面
    │
    ▼
OriginalUiMusicAdapter
    │
    ├── presentation models
    ├── dynamic source scopes
    ├── capability state
    ├── action methods
    └── private opaque identity lookup
    │
    ▼
MusicHub / Source Core
```

QML 公开 Model Role 应只包含显示需要的字段，例如：

```text
title
artist
album
artwork
duration
badge
favorite
capability flags
opaqueKey
```

完整 Source Identity、认证信息、完整 provider DTO 不应作为公共 QML role 暴露。

---

# 19. 原 UI 的动态音源改造

目前主 UI 仍存在类似：

```text
MusicApi.songSource == 0
MusicApi.songSource == 1
```

最终必须替换为：

```text
Source Scope / Source Instance Model
```

例如：

```text
全部音源
本地音乐
家庭 Navidrome
酷狗音乐
```

这个列表完全由 SourceRegistry 生成。

插件卸载时选项自动消失或变 Missing 状态，不需要修改 QML 源码。

---

# 20. Queue 最终改造

当前 QueueModel 和 Playback 仍偏向：

```text
name
path
songer
source
```

最终 QueueItem 必须升级为稳定媒体身份：

```text
QueueItem
├── sourceId
├── sourceInstanceId
├── accountId
├── entityType
├── entityId
├── title
├── artist
├── album
├── artwork
└── duration
```

禁止把长期播放 URL 作为 Queue 的主身份。

原因：

- URL 可能过期；
- URL 可能带短期 Token；
- URL 可能需要重新签名；
- Local path 与 Remote URL 语义不同；
- Queue/History 必须长期持久化。

播放时：

```text
Queue MediaRef
      │
      ▼
PlaybackCoordinator
      │
      ▼
对应 Source Session
      │
      ▼
resolve playback
      │
      ▼
Transient StreamDescriptor
```

---

# 21. Playback 最终统一路径

最终不能再存在：

```text
Local -> mainMedia old path
Remote -> PlaybackCoordinator
```

必须统一为：

```text
                  MediaRef
                     │
                     ▼
              PlaybackCoordinator
                     │
            ┌────────┴────────┐
            ▼                 ▼
       Local Plugin      Remote Plugin
            │                 │
            └────────┬────────┘
                     ▼
              StreamDescriptor
                     │
                     ▼
             QtPlaybackController
                     │
                     ▼
                QMediaPlayer
```

Local 的 descriptor：

```text
file:///D:/Music/a.flac
```

Navidrome：

```text
https://server/rest/stream?...临时认证...
```

播放后端不关心它是什么 Source。

---

# 22. 酷狗 / 网易云迁移原则

当前：

```text
MusicApiService
├── KugouApi
└── NeteaseCloudApi
```

最终：

```text
plugins/kugou-source/
plugins/netease-source/
```

每个插件自己负责：

- 网络协议；
- 平台 DTO mapping；
- 登录；
- QR 轮询；
- Cookie/Token 生命周期；
- Search/Browse；
- Playlist/Favorite；
- Playback descriptor；
- Lyrics/Artwork；
- 特殊平台能力。

主程序不能再包含：

```text
KugouLogin.qml
NeteaseLogin.qml
平台二维码逻辑
平台颜色判断
source == 0 / source == 1
```

---

# 23. Navidrome 最终补充工作

Navidrome 数据 Source 本身已经完成较多。

本轮主要补：

1. 使用现有 Settings Schema 作为普通配置 UI；
2. 如需要复杂 server discovery，再增加 Custom Management QML；
3. 支持多 Instance；
4. 从 Settings 中直接显示连接状态/账号状态；
5. 完成真实 GUI smoke；
6. 完成真实服务器的非破坏性连接验证；
7. 修改型 playlist/favorite round-trip 必须显式使用测试账户/测试数据。

---

# 24. 代码目录最终建议

不要求一次性移动所有旧文件，但最终结构建议为：

```text
QueMusic/
│
├── app/
│   ├── adapters/
│   │   ├── OriginalUiMusicAdapter.*
│   │   └── ...
│   └── composition/
│
├── core/
│   ├── source/
│   │   ├── SourceRegistry.*
│   │   ├── SourceAccountStore.*
│   │   ├── SourceScopeStore.*
│   │   └── ...
│   ├── music/
│   │   ├── MusicHub.*
│   │   ├── MusicPageModel.*
│   │   ├── PageRepository.*
│   │   ├── CapabilityResolver.*
│   │   └── MediaActionRouter.*
│   ├── playback/
│   │   ├── PlaybackCoordinator.*
│   │   └── QtPlaybackController.*
│   └── plugin-ui/
│       ├── PluginUiHost.*
│       ├── PluginUiContext.*
│       └── PluginUiRegistry.*
│
├── sdk/
│   ├── source/v2/
│   └── plugin-ui/v1/
│
├── qml/
│   ├── internal/        # QueMusic 私有 UI
│   └── plugin-ui/       # QueMusic.PluginUI 1.0 公共组件
│
├── plugins/
│   ├── local-source/
│   ├── navidrome-source/
│   ├── kugou-source/
│   └── netease-source/
│
└── tests/
```

现有项目的 QML 路径不需要第一阶段强制全部迁移到 `qml/`；目录整理放在功能稳定之后，避免无意义大 diff。

---

# 25. 迁移实施顺序

以下顺序按风险和依赖设计，建议严格执行。

---

## Phase 0：固定当前代码基线

### 目标

把此前已经完成但尚未 merge/push 的成果安全固化下来。

### 工作

1. 确认本地 `codex/unified-media-bridge` 分支仍存在；
2. 创建备份 tag/branch；
3. 与最新 QueMusic `main` 做一次受控 rebase/merge；
4. 重新跑当前 35/35 测试；
5. Navidrome isolated test；
6. `git diff --check`；
7. 不在此阶段增加新功能。

### 出口条件

```text
现有 Source v2 + Original UI Adapter 能在当前 main 基线上稳定编译测试。
```

> 如果本地历史分支已经不存在，仅剩本次导出的文档/diff，则先根据 diff 恢复代码，不应直接重新从零设计。

---

## Phase 1：冻结 Source SDK v2，定义 Plugin UI API 1.0

### 目标

不破坏现有 Source SDK v2。

### 新增

```text
sdk/plugin-ui/v1/
PluginUiTypes
ISourceManagementUiProvider
PluginUiContext contract
pluginUiApi manifest validation
```

### 同时建立

```text
QueMusic.PluginUI 1.0
PluginTheme
PluginPage
PluginSection
基础表单控件
状态控件
QR 控件
```

### 出口条件

一个 Test Source Plugin 能动态加载自己的 `ManagementPage.qml`，并自动跟随 QueMusic Light/Dark/Theme。

---

## Phase 2：把现有 Plugin Settings 系统升级为“两级 UI”

### 复用

```text
PluginSettingsController
SchemaSettingsForm
PluginSettingsPanel
SourceAccountStore
SettingsSchemaV2
```

### 行为

```text
有 Custom UI Provider
    -> Host 加载 Custom Management QML

没有 Custom UI Provider
    -> Host 自动渲染 SchemaSettingsForm
```

允许 Custom QML 内嵌标准 Schema 区块。

### 出口条件

- 简单 Test Plugin：只用 Schema；
- QR Test Plugin：使用 Custom UI；
- 两者视觉一致；
- 卸载插件后 Custom UI 自动消失。

---

## Phase 3：实现 Local Source Plugin

### 目标

把本地音乐变成第一个完整验证统一架构的 Source。

### 迁移

```text
FolderModel / LocalMusicScanner
LocalLyricsReader
CoverHelper / TagLib
```

### 初期 Capability

```text
Browse
Search
Play
Artwork
Lyrics
Metadata
```

### Management Settings

```text
音乐目录
自动扫描
监听变更
重新扫描
忽略目录
```

优先使用 Settings Schema。

### 出口条件

现有 `FilePage` 可以完全从 OriginalUiMusicAdapter 获取本地音乐，不再直接使用旧 FolderModel 作为业务数据源。

---

## Phase 4：Queue / History 统一 MediaRef Identity

### 目标

移除 path/hash/source-int 作为主身份。

### 工作

- QueueModel 新增 v2 identity fields；
- History 使用相同 identity；
- persistence migration；
- old queue JSON 向新结构兼容一次；
- invalid/missing source item 安全显示。

### 出口条件

Local + Navidrome 可以同时存在同一 Queue，重启后能正确恢复 identity。

---

## Phase 5：本地播放迁入 PlaybackCoordinator

### 目标

消灭长期 legacy/local playback path。

### 结果

```text
所有播放 -> PlaybackCoordinator
```

### 出口条件

代码扫描不得再出现“local 特殊绕过 coordinator”的生产播放路径。

---

## Phase 6：原 UI 动态 Source 化

### 工作

替换：

```text
songSource int
source == 0/1
固定酷狗/网易 UI
```

改成：

```text
SourceRegistry/SourceScopeModel
```

### 页面

- Home；
- Search；
- Playlist；
- Favorites；
- File/Library；
- source selector；
- Queue；
- Player。

### 出口条件

新增一个 Source Plugin 不需要修改这些页面的 source switch/case。

---

## Phase 7：酷狗插件迁移 + QR Management UI

### 数据层

迁移现有 KugouApi 到：

```text
plugins/kugou-source/
```

### UI

酷狗二维码和账号状态完全由插件 Management QML 提供。

UI 必须使用：

```qml
import QueMusic.PluginUI 1.0
```

### 出口条件

删除/禁用 Kugou Plugin 后：

- Source 列表不再出现可用酷狗；
- 登录 UI 不出现；
- 主程序 QML 不含 Kugou Login 实现。

---

## Phase 8：网易云插件迁移

同 Phase 7。

完成后：

```text
MusicApiService 不再承担任何具体在线平台数据业务。
```

---

## Phase 9：删除 Legacy MusicApi / Local 特殊路径

当 Local + Navidrome + Kugou/Netease 的目标能力全部经过 v2 后，删除：

```text
MusicApiService 平台聚合职责
KugouApi Core 引用
NeteaseCloudApi Core 引用
songSource int 平台选择
平台登录页面
legacy local playback branch
```

如果 `MusicApiService` 还临时存在，它只能是兼容 Adapter，并设置明确删除期限。

---

## Phase 10：SDK 发布与第三方开发者支持

输出：

```text
docs/SOURCE_PLUGIN_API_V2.md
docs/PLUGIN_UI_API_V1.md
docs/PLUGIN_MANIFEST.md
docs/PLUGIN_DEVELOPMENT_GUIDE.md
examples/source-plugin-minimal/
examples/source-plugin-custom-ui/
```

并提供：

- CMake helper；
- Plugin packaging helper；
- ABI checker；
- manifest validator；
- UI linter；
- Test Host。

---

# 26. UI 风格统一的强制规则

以下规则应写入 Plugin UI 开发规范。

## 必须

1. Custom UI 必须 `import QueMusic.PluginUI 1.x`；
2. 主页面使用 `PluginPage`；
3. 表单使用 Plugin UI Kit；
4. Color/spacing/radius/font 使用 Host token；
5. 支持 Light/Dark；
6. 支持 QueMusic 自定义主题；
7. 支持 DPI 缩放；
8. 不得假设固定窗口尺寸。

## 禁止

插件管理 QML 不得直接依赖：

```text
Style.qml
Options.qml
main.qml
mainMedia
MusicApi
内部 QButton/QCard
其它 Source Plugin
私有 C++ context property
```

## 建议 CI 检查

对 Plugin QML 做静态扫描，拒绝典型私有 import/identifier。

这不是安全沙箱，而是 API 稳定性检查。

---

# 27. 状态与错误模型

主程序只展示统一状态：

```text
Ready
Loading
AuthenticationRequired
Disabled
Unavailable
MissingPlugin
ConfigurationInvalid
Error
```

不要在 Core 写：

```text
“酷狗二维码已过期”
“Navidrome 服务器密码错误”
```

这类业务细节属于插件 Management UI。

主音乐页面只需要知道：

```text
该 Source 是否可用
该 Action 是否可执行
是否需要用户去 Settings 完成配置
```

---

# 28. 安全与隐私要求

继续继承此前已经完成的安全边界：

1. Secret 不进入普通 QML Presentation Model；
2. Secret 不进入日志；
3. authenticated playback URL 不持久化；
4. Header/Token 不进入 Queue；
5. 插件错误消息通过 Host-owned message key 映射，避免泄露服务器细节；
6. SourceAccountStore 继续负责 credential transaction；
7. QR 登录得到的 Cookie/Token 只能由 C++ backend 写入 secure store；
8. Plugin Management QML 不直接访问 secure backend。

---

# 29. 测试策略

必须维持“架构 contract test + 实现 test + UI smoke”三层测试。

## 29.1 Source SDK / Plugin Contract

至少覆盖：

- ABI/IID；
- manifest；
- Source identity；
- Plugin unload lease；
- Capability；
- action correlation；
- cancellation；
- Provider mismatch；
- plugin missing。

---

## 29.2 Plugin UI API

新增：

```text
quemusic_plugin_ui_contract_test
quemusic_plugin_ui_qml_test
quemusic_plugin_ui_theme_test
quemusic_plugin_ui_unload_test
```

覆盖：

- UI API version；
- custom QML 加载；
- null backend；
- plugin unload；
- instance switching；
- Light/Dark；
- theme update；
- invalid component；
- missing plugin；
- 禁止 private import 的 lint。

---

## 29.3 Local Source

新增：

```text
quemusic_local_source_test
```

覆盖：

- scan；
- browse；
- search；
- metadata；
- artwork；
- lyrics；
- playback descriptor；
- missing file；
- source settings；
- rescan。

---

## 29.4 Playback

至少验证：

```text
Local Source -> Coordinator -> QtPlaybackController
Navidrome -> Coordinator -> QtPlaybackController
```

避免只测试 plugin provider 不测试最终 host path。

---

## 29.5 Original UI

继续保持：

- QML offscreen smoke；
- no undefined/TypeError；
- nullable adapter；
- dynamic source selector；
- capability buttons；
- queue mixed source；
- plugin uninstall；
- original layout structure regression。

---

# 30. 最终验收门槛

项目只有同时满足以下条件，才能声明“全音源插件化完成”。

## Architecture

- [ ] Core 不含任何具体音乐平台 API 业务；
- [ ] Local Music 是 Source Plugin；
- [ ] Navidrome 是 Source Plugin；
- [ ] Kugou/Netease 如保留，也必须是 Source Plugin；
- [ ] 不存在 `source == 0/1` 平台逻辑；
- [ ] 不存在平台专用 Login QML 在主程序中；
- [ ] 所有播放经过 PlaybackCoordinator；
- [ ] Queue 使用稳定 MediaRef identity。

## Plugin Management

- [ ] Schema Settings 可工作；
- [ ] Custom Management QML 可工作；
- [ ] Plugin UI API Theme 跟随；
- [ ] 插件卸载后其 UI 自动消失；
- [ ] Missing Plugin 使用 Host 通用页面；
- [ ] Secret 安全存储。

## UX

- [ ] 原 QueMusic 主 UI 结构基本保持；
- [ ] 搜索、推荐、收藏、歌单、播放队列仍是统一体验；
- [ ] Local/Remote 对用户操作模型一致；
- [ ] 插件设置页视觉统一。

## Tests

- [ ] 全 CTest green；
- [ ] Navidrome isolated test green；
- [ ] Local Source test green；
- [ ] Plugin UI tests green；
- [ ] GUI smoke green；
- [ ] `git diff --check` green；
- [ ] 生产代码平台硬编码扫描 clean。

---

# 31. 明确不做的事情

为了防止改造失控，本轮不应：

1. 为插件化重新设计整个 QueMusic 主 UI；
2. 让每个 Source 自己实现搜索/列表/播放器主界面；
3. 允许插件直接访问 QueMusic 私有 QML 全局对象；
4. 为 Custom UI 推翻已经完成的 Settings Schema；
5. 为当前新增需求直接推翻 Source SDK v2 做 v3；
6. 在 Local Plugin 完成前大量迁移第三方在线 Source；
7. 把播放 URL 当作持久身份；
8. 把 Cookie/Token 放进 Queue/Model/普通 Settings；
9. 一开始做大规模目录重排；
10. 在功能稳定前冻结第三方公共 SDK。

---

# 32. 最终技术决策摘要

| 项目 | 最终决策 |
|---|---|
| 主 UI | 保留 QueMusic 当前 UI 和设计语言 |
| Source SDK | 继续 v2，不无意义升级 v3 |
| 所有音源插件化 | 是，包括 Local |
| Local Music | `org.quemusic.source.local` |
| Source Instance | 使用现有稳定 v2 Identity |
| 主 UI 数据边界 | `OriginalUiMusicAdapter` |
| Action | `CapabilityResolver + MediaActionRouter` |
| 播放 | `PlaybackCoordinator + QtPlaybackController` |
| Queue | MediaRef identity，不持久化播放 URL |
| 普通插件设置 | `SettingsSchemaV2 + SchemaSettingsForm` |
| 特殊插件 UI | Custom Management QML |
| UI API | 新增 `QueMusic.PluginUI 1.0` |
| Theme | `Style -> PluginTheme -> PluginUI Components` |
| Secret | Host `SourceAccountStore` / secure backend |
| 酷狗扫码 | Kugou Plugin 自己实现 Management UI |
| Navidrome 登录 | Navidrome Plugin Schema/Management UI |
| 插件卸载 | UI 自动消失，Instance 可保留 Missing 状态 |
| 主播放器 UI | 插件不得接管 |
| 第三方扩展 | 后期开放稳定 Source SDK v2 + Plugin UI API v1 |

---

# 33. 对现有成果的处理矩阵

| 已有内容 | 处理 |
|---|---|
| Source SDK v2 | **保留** |
| PluginManager v2 loading | **保留** |
| SourceRegistry | **保留** |
| Source Instance identity | **保留** |
| CapabilityResolver | **保留** |
| MediaActionRouter | **保留** |
| MusicPageModel / Repository / Cache | **保留** |
| MusicHub | **保留为 Core 数据协调层** |
| SettingsSchemaV2 | **保留并作为 Plugin UI Level 1** |
| SourceAccountStore | **保留** |
| PluginSettingsController | **保留并扩展** |
| SchemaSettingsForm.qml | **保留** |
| PluginSettingsPanel.qml | **保留并扩展 PluginUiHost** |
| Navidrome v2 | **保留** |
| PlaybackCoordinator | **保留** |
| QtPlaybackController | **保留** |
| PlaybackControlsAdapter | **保留** |
| OriginalUiMusicAdapter | **保留，是核心适配层** |
| 原 QueMusic Music UI | **保留** |
| legacy/local direct playback | **迁移后删除** |
| MusicApiService | **过渡期兼容，最终删除平台聚合职责** |
| KugouApi Core 内置 | **迁移到插件** |
| NeteaseCloudApi Core 内置 | **迁移到插件** |
| FolderModel 直接作为 UI Source | **迁移到 Local Plugin** |
| 主程序平台登录 UI | **删除** |
| Plugin custom management UI | **新增** |
| QueMusic.PluginUI 1.0 | **新增** |

---

# 34. 推荐的第一个实际开发 Sprint

为了最快形成闭环，第一个 Sprint 不建议先碰酷狗/网易。

建议只做：

```text
1. 恢复/固化当前 Source v2 + Original UI 分支
2. 新建 Plugin UI API 1.0 最小版本
3. PluginSettingsPanel 支持 Custom Management QML
4. 写一个 Test QR Plugin 验证自定义 UI
5. 创建 Local Source Plugin 骨架
6. 把本地 Browse + Play 先接进去
```

第一 Sprint 完成后，应该能够看到：

```text
设置 -> 音源插件

本地音乐
  -> 统一风格的目录设置

测试二维码插件
  -> 插件自己提供 QR 页面，但看起来仍像 QueMusic
```

同时主界面的本地歌曲开始通过 Source SDK v2 出现并播放。

这一步一旦成功，后续 Navidrome/Kugou/Netease 都只是“实现不同 Source Provider 和 Management UI”，核心架构不再变化。

---

# 35. 最终定义

本次改造完成后的 QueMusic 应被定义为：

> **一个保持自身统一音乐体验和视觉设计的跨平台 Music Source Plugin Host。QueMusic 核心负责统一浏览、搜索、收藏、歌单、队列、播放和歌词体验；所有媒体来源——包括本地文件——都通过 Source Plugin 提供数据、能力和播放资源。每个插件可以拥有独立的认证、配置和管理业务 UI，但必须通过 QueMusic Plugin UI API 使用统一组件和 Theme，从而做到“业务可扩展、视觉不割裂、主程序不感知具体平台”。**

这应作为后续架构评审、代码 Review 和新插件设计的最高层原则。

---

# 36. 参考基线

本方案综合了：

1. QueMusic GitHub 当前 `main` 工程结构；
2. `2026-08-27-quemusic-plugin-foundation`；
3. `2026-08-28-source-sdk-v1-optional-artwork`；
4. `2026-08-30-unified-media-bridge`；
5. `2026-09-01-source-sdk-v2-music-hub-phase-1`；
6. `2026-09-12-original-ui-data-adapter`；
7. 本轮新增需求：Local 也插件化、Source 独立 Management UI、UI 风格统一。

其中当前最重要的实现基线是：

```text
2026-09-12 Original UI Data Adapter
+
Source SDK v2 Core
```

最终报告记录的最近验证状态为：

```text
CTest: 35/35 passed
Navidrome isolated source test: 1/1 passed
git diff --check: passed
```

但当时：

- 分支尚未 merge；
- 尚未 push；
- real Navidrome GUI smoke 未完成；
- 修改型 playlist/favorite round-trip 未完成；
- macOS offscreen + QWindowKit 不能视为 GUI smoke 成功。

这些内容必须在 Phase 0 / 后续真实集成验证中重新确认。
