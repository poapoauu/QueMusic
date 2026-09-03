# QueMusic 音源 SDK v2 与统一音乐页面设计

日期：2026-09-01  
状态：已完成交互设计确认，等待规格审阅

## 1. 背景与决策

QueMusic 当前同时存在旧 `MusicApi`、整数 `songSource`、页面直接调用具体音源，以及实验性的音源 SDK v1/`MediaBridge`。这些入口的 DTO、分页和操作模型不同，使本地音乐、Navidrome 及后续音源无法稳定地共用同一套上层页面。

本设计决定：

1. 删除面向用户的“音乐源”浏览页面。
2. 所有音源统一进入“推荐、分类、收藏”三个主页面；全局搜索保留为临时结果页。
3. 默认聚合所有已启用音源，同时允许选择一个具体音源实例。
4. 建立全新的音源 SDK v2，不要求兼容当前实验性 SDK v1 ABI。
5. 以能力接口和逐对象可用性描述不同音源的操作边界。
6. 使用 `MusicHub` 作为 UI 与音源插件之间唯一的用户音乐数据入口。
7. 插件设置集中在“设置 / 插件”，采用插件列表加独立详情的主从布局。

本规格在用户音乐数据路径上取代 `2026-08-30-unified-media-bridge-design.md` 的 v1 上层设计。JS 插件仍按既有设计保留为后期方向，本阶段不实现。

## 2. 目标与非目标

### 2.1 目标

- 本地和远程音源使用一致的页面 DTO、播放队列和操作路由。
- 聚合模式能容忍单个音源失败，并明确标示内容来源。
- 插件只实现自己真正支持的能力；UI 不猜测、不伪造功能。
- 完整支持 Navidrome 的用户侧音乐浏览、播放、收藏、评分、歌单、播放上报、播放队列和书签操作。
- 原生插件能够安全地发现、加载、停用、卸载和重载。
- 一个插件可以配置多个服务器或账号实例。
- 凭据不以明文写入普通配置或日志。

### 2.2 非目标

- Navidrome 用户管理、服务器扫描、媒体库维护等管理员功能。
- 网络电台、公开分享、播客、视频和 MV；这些以后以新的可选能力加入。
- 本阶段实现 JS 插件运行时。
- 兼容 SDK v1 的第三方二进制插件。
- 本阶段允许音源插件向主 QML 引擎注入任意 QML 组件。
- 在远程音源离线时排队执行收藏、评分或歌单写操作。

## 3. 信息架构与页面行为

### 3.1 导航

主侧边栏移除“音乐源”。音乐内容入口为：

- 推荐
- 分类
- 收藏
- 全局搜索结果页（由搜索框进入，不固定占用侧边栏）

本地文件和下载属于工具页，可以继续保留，但本地媒体库的可浏览音乐内容必须同时由本地音源插件接入上述三个音乐页面。

### 3.2 共享音源范围

三个主页面和全局搜索共用一个持久化的 `SourceScope`：

```text
Aggregate
SpecificSourceInstance(sourceInstanceId)
```

默认值为 `Aggregate`。具体音源按实例展示，例如“Navidrome · 家庭服务器”，而不是只显示插件名。用户切换页面后范围保持不变。

若指定实例被停用或离线，不自动切换到别的音源；页面保留选择并说明原因。聚合模式继续显示其他可用音源，同时标示缺失实例。

### 3.3 页面结构

推荐页包含标准分区：最近播放、常听、最高评分、新增、随机及音源提供的其他推荐分区。本地插件没有推荐算法时，可用最近播放、最近加入和随机歌曲生成分区。

分类页提供流派、歌手、专辑和歌曲等标准入口。指定音源时可以追加该音源支持的原生分类，但必须映射为统一分区 DTO。

收藏页提供歌曲、专辑、歌手和歌单四个标签。歌单标签同时展示来源徽标，并支持来源允许的操作。

全局搜索统一搜索歌曲、专辑、歌手和歌单，遵循当前 `SourceScope`。

### 3.4 来源与歌单边界

每个媒体条目都显示来源徽标。详情或操作菜单可以进一步显示账号和服务器实例。

音源原生歌单只能接收该音源和账号允许的歌曲。例如本地歌曲不能加入 Navidrome 原生歌单。QueMusic 聚合歌单由核心库保存，可以引用多个音源的 `MediaRef`；它不是一个新的音源，也不尝试回写为单个服务器的原生歌单。

## 4. 核心架构

```text
QML Pages
  └─ Recommendation / Category / Favorites / Search ViewModel
       └─ MusicHub
            ├─ SourceRegistry
            ├─ CapabilityResolver
            ├─ PageRepository
            ├─ AggregateComposer
            ├─ MediaActionRouter
            ├─ PlaybackCoordinator
            └─ PluginSettingsHost
                 └─ SDK v2 source sessions
                      ├─ Navidrome
                      ├─ Local Music
                      └─ Future source plugins
```

### 4.1 MusicHub

`MusicHub` 是 QML 可见的唯一音乐域入口，负责：

- 维护共享 `SourceScope`。
- 创建页面请求并返回统一页面结果。
- 聚合多个音源、处理分页和局部失败。
- 根据 `MediaRef` 路由播放和写操作。
- 暴露动作的最终可用性及不可用原因。

QML 不再访问 `MusicApi`、整数 `songSource`、`MediaBridge` 或具体插件对象。

### 4.2 组件职责

- `SourceRegistry`：插件发现、兼容检查、实例和会话生命周期。
- `CapabilityResolver`：合并插件、服务器、账号和媒体对象四层能力。
- `PageRepository`：页面查询、缓存、刷新和请求取消。
- `AggregateComposer`：分区合并、轮询混排、可靠去重和聚合游标。
- `MediaActionRouter`：收藏、评分、下载、歌单和书签等操作路由。
- `PlaybackCoordinator`：统一播放解析、队列、播放上报和当前媒体状态。
- `PluginSettingsHost`：主从式设置 UI、设置描述渲染、凭据与诊断。

当前 `MediaBridge` 中可复用的请求协调和播放逻辑迁入上述组件。迁移完成后删除旧桥接入口，避免两套上层 API 长期共存。

## 5. SDK v2 合同

### 5.1 ABI 与装载检查

SDK v2 使用新的 Qt 插件 IID，例如 `org.quemusic.IMusicSourcePlugin/2.0`。插件清单至少声明：

- 插件 ID 和版本
- SDK ABI 版本
- Qt 主版本和构建兼容键
- 支持的平台与架构
- 声明能力

加载器必须在实例化插件前拒绝 SDK ABI、Qt 主版本、平台或架构不兼容的动态库，并给出可理解的错误。v2 不承诺加载 v1 二进制插件。

### 5.2 基础对象

`IMusicSourcePluginV2` 只负责插件级元数据、设置描述和创建会话。`IMusicSourceSessionV2` 负责一个具体音源实例的连接状态、能力协商、取消请求和关闭。

`IMusicSourceSessionV2` 还必须统一发出类型化 `requestStarted(requestId)` 信号。`open()` 和所有可选 provider 的异步方法都必须在任何同步完成或失败信号之前先发出该信号；核心据此记录并在关闭、切换范围或卸载插件前取消全部未完成请求。仅返回请求 ID 而不发出该信号属于不符合 v2 契约。

功能以可选的类型化接口提供：

- `IPageProvider`
- `IPlaybackProvider`
- `IFavoriteProvider`
- `IRatingProvider`
- `IScrobbleProvider`
- `IPlaylistProvider`
- `IDownloadProvider`
- `IPlayQueueProvider`
- `IBookmarkProvider`
- `IPluginSettingsProvider`

插件未实现某接口，即不具有该类能力。不得用字符串命令和无约束 `QVariantMap` 替代核心音乐操作接口。

### 5.3 统一值类型

核心 DTO 为不可依赖插件生命周期的值对象，至少包括：

```text
MediaRef
  sourcePluginId
  sourceInstanceId
  accountId
  entityType
  entityId

MediaItem
  ref, title, subtitle, artists, album, duration
  artworkRef, metadata, availableActions

PageQuery
  pageKind, sectionKind, sourceScope, filters, cursor, limit

PageSection
  sectionId, title, layoutHint, items, nextCursor

PageResult
  sections, sourceStates, cacheState, completionState
```

聚合分页游标是核心生成的不透明值，内部保存各音源游标、耗尽状态和轮询位置。UI 和插件都不能解析该值。

### 5.4 异步与线程规则

- UI 线程不得执行阻塞网络或磁盘操作。
- 每个异步请求携带请求 ID、页面代次和取消令牌。
- 每个异步请求必须先通过会话的 `requestStarted` 信号登记，再允许发出完成或失败信号。
- 切换页面、音源范围或卸载插件时必须取消相关请求。
- 迟到结果若代次不匹配必须丢弃。
- 插件返回纯 DTO；不得把插件拥有的模型或对象树直接交给 QML。

## 6. 能力与操作边界

### 6.1 可用性状态

每个动作使用以下状态：

```text
Unsupported  音源不支持
Available    当前可以执行
Unavailable  理论支持但暂时不可用
Forbidden    当前账号无权限
```

`ActionAvailability` 同时包含本地化原因键和结构化约束，例如“只允许同一音源实例的歌曲”。

最终可用性为以下四层的交集：

```text
插件声明 ∩ 服务器协商 ∩ 账号权限 ∩ 当前媒体对象约束
```

### 6.2 UI 规则

- 普通操作若为 `Unsupported`，从菜单隐藏。
- 用户预期存在但状态为 `Unavailable` 或 `Forbidden` 时，控件保留并禁用，同时显示原因。
- 音源专属操作放在“来自 <音源> 的操作”分组中。
- 多选操作按音源实例和能力分组；不得静默跳过不支持项目。
- 乐观更新只用于可回滚的收藏和评分。服务器失败时恢复旧值并显示错误。
- 歌单创建、更新、增删歌曲和删除必须等待服务端确认后更新 UI。

## 7. 聚合、去重与缓存

### 7.1 聚合

相同标准分区按类型合并，并以轮询方式从各音源取项，避免单一音源占满页面。来源特有分区可以独立保留，并清楚标明来源。

只在存在可靠的 ISRC、MusicBrainz ID 或等价稳定标识时跨音源折叠重复条目。仅凭标题、歌手和时长相似不得自动去重；不能可靠确认时保留多条并显示来源。

### 7.2 缓存

- 页面元数据使用内存 LRU 和磁盘缓存，并采用先显示缓存、后台刷新的方式。
- 封面使用独立磁盘缓存。
- 缓存键包含音源实例、账号、页面类型、过滤条件和分页位置。
- 临时播放 URL、密码、令牌和 Cookie 不写入普通缓存。
- 远程音源离线时可以展示标记为“缓存”的旧内容，但写操作立即失败并说明原因。

## 8. 插件设置与账号模型

### 8.1 设置 UI

“设置 / 插件”采用左侧插件列表、右侧插件详情的主从布局。主程序统一展示：

- 加载状态、版本和兼容性
- 启用、停用和重载
- 账号或服务器实例
- 权限与能力列表
- 诊断、脱敏日志和连接测试

插件通过声明式 `SettingsSchema` 提供文本、密码、URL、数字、开关、选择、目录、字段校验、显示条件和插件动作。主程序使用统一控件渲染独立详情页。

首版不允许音源插件向主 QML 引擎注入任意设置组件，以免 QML 对象或类型注册阻止动态库安全卸载。复杂 UI 插件能力另行设计。

### 8.2 多实例与凭据

插件 ID、音源实例 ID 和账号 ID 分开：

```text
pluginId:         org.quemusic.navidrome
sourceInstanceId: navidrome/home-server
accountId:        stable-user-id
```

一个插件可以拥有多个实例。非敏感配置按插件命名空间保存并带配置版本。密码、访问令牌和 Cookie 通过 `ICredentialStore` 写入系统安全凭据库；普通配置只保存凭据引用。秘密不得暴露给 QML，也不得写入日志。

`sourceId` 是插件声明的稳定 slug，必须非空且不得包含 `/`；`accountId` 保持现有存储语义并允许包含 `/`。因此 `sourceInstanceId = sourceId + "/" + accountId` 可逆且不会改写已有账号或凭据键。

原生插件与主程序运行在同一进程，因此属于受信任代码；凭据存储不能把原生插件变成安全沙箱。

### 8.3 热重载

热重载必须按顺序执行：

```text
阻止新请求
→ 取消网络请求
→ 页面释放模型与句柄
→ 停止播放和后台任务
→ 销毁账号会话
→ 销毁插件对象
→ 验证无存活引用
→ 卸载动态库
→ 重新发现并加载
```

若仍有对象、请求、播放或设置动作占用插件，卸载必须失败并报告占用原因；不得强制卸载。

会话由 `SourceRegistry` 独占管理，返回给调用方的指针仅供借用，不得直接销毁。若检测到外部代码破坏此所有权约定，无法证明外部析构栈已经退出时，必须保留该插件包的动态库与根对象至进程退出（包括插件管理器先被销毁的情况），拒绝该包的卸载、重载和新会话获取，并提供“需要重启”的诊断。此保护仅影响违规插件包，不改变正常会话关闭及其他插件的热重载行为。

## 9. Navidrome 用户功能映射

连接阶段调用 `ping`，并结合服务器版本和 OpenSubsonic 扩展列表协商最终能力。理论上存在但当前服务器未提供的端点标记为 `Unavailable` 或 `Unsupported`，不能直接显示为可用。

### 9.1 页面与浏览

- 推荐和专辑列表：`getAlbumList2`，使用 random、newest、recent、frequent、highest、starred 等服务端支持类型。
- 收藏：`getStarred2`。
- 流派、歌手、专辑和歌曲：`getGenres`、`getArtists`、`getArtist`、`getAlbum`、`getSong`。
- 搜索：`search3`。

### 9.2 媒体与用户动作

- 播放：`stream`。
- 封面：`getCoverArt`。
- 歌词：优先使用协商可用的 `getLyricsBySongId`，否则使用兼容端点；均不可用时隐藏歌词能力。
- 下载：`download`。
- 收藏和取消收藏：`star`、`unstar`。
- 评分：`setRating`。
- 播放上报：`scrobble`。

`PlaybackCoordinator` 在开始播放时发送非提交事件；播放达到歌曲时长 50% 或四分钟（先到者）时发送一次提交事件。未知时长时在四分钟提交。每个队列项只提交一次。

### 9.3 歌单与状态同步

- 歌单读取：`getPlaylists`、`getPlaylist`。
- 创建：`createPlaylist`。
- 重命名、添加和移除歌曲：`updatePlaylist`。
- 删除：`deletePlaylist`。
- 播放队列：`getPlayQueue`、`savePlayQueue`。
- 书签：`getBookmarks`、`createBookmark`、`deleteBookmark`。

所有写操作使用服务端返回结果更新本地状态。Navidrome 原生歌单只接受同一 Navidrome 实例允许的歌曲。

## 10. 错误处理

- 所有音源失败：显示页面级错误和重试入口。
- 单个分区失败：保留其他分区，仅重试失败分区。
- 聚合中的单个音源失败：展示其他音源，同时标记失败实例和原因。
- 单个用户操作失败：在操作位置提示并按需要回滚。
- 插件失败：区分发现失败、ABI 不兼容、配置无效、认证失败、服务器不可达和运行时错误。
- 日志必须过滤密码、令牌、Cookie 以及 Navidrome 鉴权查询参数。

错误对象使用稳定错误码、本地化消息键、可安全展示的详情、是否可重试和相关音源实例，不直接把底层异常文本作为唯一 UI 信息。

## 11. 迁移顺序

1. 建立 SDK v2 值类型、可选能力接口、兼容检查和测试插件。
2. 建立 `MusicHub`、能力解析、页面仓库、聚合器和动作路由。
3. 改造推荐、分类、收藏和搜索 ViewModel，加入共享音源选择器并移除音乐源页面。
4. 将 Navidrome 迁移到 v2，完成本规格列出的全部用户功能和设置页。
5. 将本地音乐迁移为 v2 插件，完成本地分类、收藏、推荐和播放。
6. 将网易云、酷狗、QQ 等旧音源逐个迁移为插件。
7. 删除旧 `MusicApi` 页面耦合、整数 `songSource` 和旧 `MediaBridge` 上层入口。
8. 完成热重载、诊断、文档和全量回归验证。

迁移期间允许使用内部适配器维持可编译状态，但适配器不得成为公开 SDK，也不得在最终架构中保留。

由于整体迁移跨越核心 SDK、页面和多个音源，实施不作为一个不可分割的大批次进行。规格批准后按以下可独立验收的计划拆分：

1. SDK v2、`MusicHub`、页面骨架、插件设置宿主与 Navidrome 完整迁移。
2. 本地音乐插件迁移与 QueMusic 聚合歌单。
3. 网易云、酷狗、QQ 等旧音源插件化。
4. 旧入口清理、热重载强化、性能和完整回归。

第一个实施计划完成后，应用必须已经能够仅依靠 SDK v2 在三个主页面中使用 Navidrome；后续计划不得要求推翻第一阶段的公开合同。

## 12. 测试与验收

### 12.1 自动测试

- SDK 契约测试覆盖发现、兼容拒绝、能力声明、请求取消、会话关闭和卸载。
- `MusicHub` 测试覆盖指定音源、聚合、混排、分页、可靠去重和部分失败。
- 能力测试覆盖四层能力交集、对象约束和多选分组。
- Navidrome 模拟 HTTP 测试覆盖第 9 节全部端点、无权限、未支持端点、认证失败和异常响应。
- QML 测试覆盖共享范围持久化、页面状态、操作隐藏、禁用原因和来源徽标。
- 热重载测试反复加载和卸载插件，验证无存活会话、请求、模型或 QML 对象。
- 现有测试和新增测试必须在完整构建中全部通过。

### 12.2 内网集成与手工验收

可选集成测试通过环境变量连接用户提供的 Navidrome 服务；地址和凭据不写入仓库、测试输出或日志。

最终手工验收至少包括：

1. 侧边栏不再出现“音乐源”。
2. 推荐、分类、收藏和搜索都能在聚合与指定 Navidrome 实例之间切换。
3. 单一音源故障不会清空聚合页面。
4. UI 根据实际能力正确隐藏或禁用操作并说明原因。
5. Navidrome 浏览、播放、封面、歌词、下载、收藏、评分、播放上报、歌单、播放队列和书签均可操作。
6. 本地歌曲和 Navidrome 歌曲可同时出现在聚合页面，且来源和歌单边界清晰。
7. Navidrome 插件可以在不崩溃、不残留对象的情况下停用、卸载和重载。
8. 完整构建、CTest、签名检查及实际 GUI 播放验证通过。

## 13. 后续扩展点

SDK v2 以后可以通过新的可选接口增加 MV、视频、同步歌词、外部元数据、语言包和 UI 插件。新增能力不得修改或重新解释已有接口语义；需要破坏性变更时提升 SDK ABI 主版本。
