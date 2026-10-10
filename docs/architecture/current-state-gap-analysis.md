# QueMusic 当前状态与最终插件架构差距分析

> 状态：原始审计保留如下；实施顺序已确认，隔离开发分支已完成 Phase 1–4 的阶段实现与本机验收，尚未合并
> 原始审计日期：2026-09-17；进展复核：2026-10-03
> 设计基线：[`QueMusic-Plugin-Architecture-Final-Plan.md`](./QueMusic-Plugin-Architecture-Final-Plan.md)

当前进展说明：本文件第 1–4 节的分支拓扑和 40/40 测试数是 **2026-09-17 的历史快照**，不再代表当前开发分支。`codex/plugin-ui-api-v1` 在原有 v2 上完成 Plugin UI API、Plugin Settings、Local Source Plugin，以及 Phase 4 的不透明身份、统一队列/历史持久化和恢复。2026-10-03 本机 Debug 全量构建及 CTest **69/69** 通过；Phase 4 证据见[验收记录](phase-4-queue-history-identity-record.md)。Phase 5/6、其余平台迁移及跨平台发行验证仍未完成，不能将此视为最终架构完成。

## 1. 结论摘要

本次检查的最重要结论是：**此前完成的 Source SDK v2 / Source Registry / Capability / Action Router / Navidrome / PlaybackCoordinator / Original UI Data Adapter 改造确实存在，不应重新实现；但它们尚未位于当前检出分支。**

仓库当前存在两个必须区分的状态：

| 范围 | 分支 / 提交 | 真实状态 |
| --- | --- | --- |
| 当前检出目录 | `codex/issue-12-local-lyrics` / `c76afd3` | 仍以 Source SDK v1、`SourceManager`、`PlaybackEngineManager` 和 Legacy `MusicApi`/本地特殊路径为主 |
| 已完成改造分支 | `codex/unified-media-bridge` / `b94e181` | 比当前 HEAD 直接领先 96 个提交；包含 Source SDK v2、`SourceRegistry`、Capability、Action Router、`PlaybackCoordinator`、Navidrome v2、Settings Schema 和 `OriginalUiMusicAdapter` |

`git rev-list --left-right --count HEAD...codex/unified-media-bridge` 的结果为 `0 96`，说明当前 HEAD 是该改造分支的祖先。从提交拓扑看可直接快进集成；但两个工作区都有未提交内容，集成前必须先隔离和确认这些改动，不能把它们意外带入架构迁移。

因此正确起点不是在当前分支重写 v2，而是：

1. 保护现有未提交改动；
2. 将 `codex/unified-media-bridge` 的 96 个既有提交集成到当前开发线；
3. 在干净、可复现的构建目录中复验；
4. 以现有 Source SDK v2 为稳定核心，独立增加 `QueMusic.PluginUI 1.0`；
5. 再迁移 Local、队列、播放和遗留平台代码。

本次没有开始大规模实现，只新增了架构基线副本和本 Gap Analysis。

## 2. 审计范围与验证结果

本次逐项检查了：

- 仓库目录与两条相关分支的提交拓扑；
- 主工程及 Navidrome 插件的 CMake 目标、打包和测试配置；
- Source SDK v1 与 v2；
- `PluginManager`、`SourceRegistry`、`SourceInstance` 身份和生命周期；
- Capability、Action Router、页面 Repository/Cache、`MusicHub`；
- `PlaybackCoordinator`、`QtPlaybackController` 和现存 Legacy 播放路径；
- Navidrome Source v2；
- `OriginalUiMusicAdapter` 与主要 QML 页面；
- Settings Schema、插件设置控制器、账户和 Secret 存储；
- 本地音乐扫描、播放、歌词和 UI 路径；
- 主程序中的网易云、酷狗登录和平台专用 UI；
- Local/Kugou/Netease Source Plugin 以及 Plugin UI API 是否存在。

自动验证结果：

- 当前检出分支：构建目录中的 12/12 项 CTest 通过；
- `codex/unified-media-bridge`：先发现原 `build/` 陈旧、部分测试可执行文件缺失；完成增量构建后，当前实际源码的 40/40 项 CTest 通过；
- 高级分支主应用和 Navidrome 插件均能完成构建。

这证明高级分支中的 v2 基线是可复用的工作成果，但不等于最终架构已经完成。40/40 只覆盖当前已有测试，不覆盖尚不存在的 Plugin UI API、Local Source Plugin、队列持久化迁移、所有 Legacy 路径删除和完整的人工 UI 回归。

## 3. 当前真实架构

### 3.1 当前检出分支

当前检出分支仍是第一代插件桥接架构：

- Source SDK 为 `sdk/source/IMusicSourcePlugin.h`、`IMusicSourceSession.h`、`SourceTypes.h`；
- Host 使用 `core/source/SourceManager.*` 管理 Source；
- 播放仍以 `core/playback/PlaybackEngineManager.*`、QML `MediaPlayer` 和 `MusicApiService` 为主；
- Navidrome 是 v1 插件形态；
- 本地音乐通过 `FolderModel`/目录模型和 QML 特殊分支直接播放；
- 网易云、酷狗实现和账号 UI 位于主程序。

这个状态不能作为下一阶段重新开发的基础，否则会重复已经完成的 96 个提交。

### 3.2 已完成改造分支

`codex/unified-media-bridge` 的主干数据链路已经形成：

```text
Plugin package
  -> PluginManager（manifest / ABI / Qt / arch / build-key 校验）
  -> IMusicSourcePluginV2
  -> SourceRegistry（多 SourceInstance、session lease、生命周期隔离）
  -> MusicHub / Repository / Cache / CapabilityResolver / MediaActionRouter
  -> OriginalUiMusicAdapter 或新的通用 QML 组件
  -> PlaybackCoordinator
  -> IPlaybackProviderV2.resolvePlayback()
  -> QtPlaybackController / QMediaPlayer
```

同时仍并存另一套 Legacy 链路：

```text
Home/Search/Playlist/Favourite/File 等旧 QML
  -> MusicApi / FolderModel / AccountManager / source 整数
  -> 旧 playListModel（name/path/songer/source）
  -> QML MediaPlayer 直接设置 URL 或本地 path
```

所以高级分支是“v2 核心已完成、生产 UI 和本地/平台旧路径仍在过渡”的双轨架构，而不是最终全插件化架构。

### 3.3 启动与依赖注入现状

高级分支的 `main.cpp` 已组装：

- `PluginManager` / `SourceRegistry`；
- `MusicHub`；
- `MediaActionRouter`；
- `PlaybackCoordinator` 与 `QtPlaybackController`；
- `OriginalUiMusicAdapter`；
- `PluginSettingsController`。

但它同时仍实例化并暴露：

- `FolderModel`；
- 本地歌曲和收藏相关模型；
- `AccountManager`；
- `MusicApiService` 的共享账号依赖。

这说明依赖注入入口已经具备迁移基础，但 Composition Root 尚未切断 Legacy 依赖。

## 4. 与最终方案逐项对照

### 4.1 已完成，可直接保留

以下成果应在集成后直接保留，不得回退或重写：

| 能力 | 现状 | 保留理由 |
| --- | --- | --- |
| Source SDK v2 | `sdk/source/v2/` 已定义插件、会话、Provider、类型和 Secret 处理 | 已建立清晰的数据/行为契约，符合继续演进原则 |
| 插件加载与 ABI 防线 | `PluginManager` 检查 manifest、API/ABI、Qt major、架构、build key、IID 和真实接口类型 | 可阻止错误插件进入运行期；活跃 lease 也会阻止卸载 |
| SourceRegistry | `core/source/SourceRegistry.*` 管理会话、启停、配置变化和生命周期 fencing | 已承担 Host 到 SourceInstance 的统一入口 |
| Plugin 与 SourceInstance 分离 | `SourceInstanceDescriptorV2` 含 package、source、instance、account 等身份；账户存储支持多实例 | 已具备“一插件多实例”的核心能力 |
| 页面数据层 | `MusicPageModel`、`PageRepository`、`PageCache`、`AggregateComposer`、`MediaAssetRepository` | 可继续作为通用页面和缓存层 |
| CapabilityResolver | 已综合插件、服务端、账号和媒体可用性 | 已符合按能力驱动 UI/动作的方向 |
| MusicHub | 已提供推荐、分类、收藏、搜索、Source 选择和页面导航协调 | 可作为主音乐 UI 的统一门面 |
| PlaybackCoordinator | 队列项保留稳定 `MediaRefV2`/展示信息，在播放时解析资源，并包含 generation fencing/scrobble | 已符合“插件解析、Core 播放”和不过早持久化 URL 的原则 |
| QtPlaybackController | 作为 `PlaybackSink` 封装 `QMediaPlayer`/`QAudioOutput` | Core 播放器边界已经成立 |
| OriginalUiMusicAdapter | 将 v2 数据映射给现有 QueMusic 列表模型、播放和常用动作 | 是“保留现有 UI 和交互”的正确迁移层 |
| Settings Schema 基础 | 已有字段类型、分组、校验、展示转换、控制器和 QML 表单 | 普通设置由 Host 渲染的基础已完成 |
| 账户/Secret 分离 | `SourceAccountStore` 将公开参数与 Secret 引用分离，macOS 使用 Keychain | 符合不在普通配置中明文保存密码的原则 |
| Navidrome Source v2 核心 | 已实现页面、播放、下载、收藏、评分、scrobble、播放列表、队列、书签等 Provider | 应以补齐和打磨为主，不应重新写插件 |

### 4.2 已完成但需要调整

| 能力 | 当前不足 | 调整方向 |
| --- | --- | --- |
| Source 身份字段 | `SourceIdentityV2`/`MediaRefV2` 中仍使用名为 `sourcePluginId` 的字段，而注释/语义实际接近 `sourceId` | 在不破坏 v2 ABI 的前提下明确语义；优先新增访问器、别名或下一兼容版本迁移，禁止直接改布局 |
| Action Router | 已覆盖收藏、评分、下载、播放列表、书签等主要动作，但 Provider 与 Router 暴露面仍不完全一致 | 逐项补齐真正需要由 Host 触发的动作，并保持 capability gate；不要把平台业务塞回 Host |
| Settings 系统 | Schema、账户和操作控制器已完成，但只有自动表单，没有独立 Custom Management UI 合约 | 在现有设置系统上增加二级 UI，不重写 schema 层 |
| Secret Store | macOS 有 Keychain；其他平台当前可能退化为不可用实现 | 为 Windows/Linux 提供系统安全存储或明确的不可用 UX 与验收策略 |
| QtPlaybackController | 当前拒绝带 HTTP headers 的流，只接受本地文件和普通 HTTP(S) URL | 若目标插件需要 header/cookie 流，需扩展 Core 播放能力或定义安全的临时资源代理；不能让插件绕过 Core 自行播放 |
| OriginalUiMusicAdapter | 已覆盖主要列表与动作，但旧页面仍保留 `MusicApi` fallback，适配器尚未成为唯一数据入口 | 分页迁移并删除 fallback；保留视觉组件和交互，不保留平台耦合 |
| 动态 Source UI | `MusicHub`/Adapter 已能提供 Source 选项和当前实例，但部分 QML 仍使用 `source == -1` 或平台整数 | 统一绑定 `sourceInstanceId`/`MediaRefV2`，删除枚举式平台判断 |
| Navidrome | 核心 Provider 完成，仍需最终 UI、错误状态、设置/管理入口和真实服务器回归 | 作为首个标准样板插件完成端到端验收 |
| 测试基线 | 高级分支现有 40 项测试通过，但历史文档中的测试数量已过时，且缺少新增架构阶段的测试 | 每阶段增量建立 contract、QML import、迁移、卸载和端到端测试 |

### 4.3 尚未实现

| 目标 | 证据与结论 |
| --- | --- |
| `QueMusic.PluginUI 1.0` | 仓库中没有独立的 Plugin UI SDK、QML module、稳定 UI Kit、Theme Token、Host/Context/Validator |
| 两级插件设置 UI | 只有 Settings Schema 自动表单；没有由 manifest/descriptor 声明、由 Host 安全加载的 Custom Management Page |
| Local Source Plugin | 没有 `plugins/local-source/`，本地音乐仍是 `FolderModel` + QML 特殊路径 |
| Local 多实例 | 尚不能把多个本地目录/媒体库建模为标准 SourceInstance |
| 队列/历史稳定身份持久化 | v2 `PlaybackCoordinator` 内存队列方向正确，但生产 UI 仍有旧 `{name,path,songer,source}` 队列；未见完成的持久化迁移与恢复策略 |
| 全 Source 统一播放 | 在线 v2 可走 Coordinator；本地和部分 Legacy QML 仍直接给 `MediaPlayer.source` 赋值 |
| Kugou Source Plugin | 平台 API 和 QR 登录仍在 Host，没有独立插件包和 Management UI |
| Netease Source Plugin | 平台 API 和登录/账号逻辑仍在 Host，没有独立插件包 |
| 插件缺失/卸载后的完整 UX | 有生命周期保护基础，但没有最终的缺失插件页、Custom UI 卸载处理和持久化引用恢复体验 |
| Plugin UI 第三方开发文档与 CI | 尚无独立版本化 SDK、示例插件、import allowlist/lint 和兼容矩阵 |

### 4.4 与最终架构冲突，需要迁移或删除

| 冲突项 | 当前表现 | 最终处理 |
| --- | --- | --- |
| Host 内平台专用登录 UI | `SettingsView.qml` 仍含网易云/酷狗账号卡、QR 对话框和平台分支 | 迁入各自插件的 Settings Schema 或 Custom Management UI；Host 只保留通用插件容器 |
| Host 内平台 API | `api/NeteaseCloudApi.*`、`api/KugouApi.*`、`MusicApiService.*` 仍被主程序编译 | 功能迁入对应插件后删除主程序依赖；如 `MusicApiService` 仍有通用能力则拆分为平台无关服务，而不是保留大杂烩 |
| Legacy Local 特殊路径 | `FolderModel`、`FilePage.qml`、`main.qml::playLocalSong()` 直接处理本地路径和播放 | Local Plugin 接管扫描/媒体身份/资源解析后删除特殊分支 |
| Legacy 播放分支 | `PlayerControl.qml` 以 `source == -1` 判断本地，否则调用 `MusicApi.getMusicInfo()` | 全部改为 `PlaybackCoordinator`，QML 只发播放意图 |
| Legacy 队列模型 | 队列项保存 URL/path 和平台整数 | 迁移为稳定 `MediaRefV2` + 展示快照，播放时解析 URL |
| 主 UI 依赖平台枚举 | Home/Search/Playlist/Favourite 等页面仍有 `songSource`、平台整数与 `MusicApi` fallback | 通过 Adapter/MusicHub/Capability 动态驱动，删除平台 switch |
| Host 私有 QML 作为插件依赖 | 目前尚无插件 UI API，若直接让插件 import `components/` 会形成隐式私有 ABI | 必须先发布独立 `QueMusic.PluginUI 1.0`；插件禁止 import 主程序私有路径 |
| Source SDK v1 残留 | 当前检出分支仍有 v1；高级分支已经删除旧 v1 应用桥 | 集成高级分支后以 v2 为唯一 Source 核心；不恢复 v1 双轨长期共存 |

## 5. 最终架构原则确认

以下原则作为后续重构的硬约束，结论均为“确认并接受”：

1. **尽量保留 QueMusic 现有 UI 和交互。** 迁移数据来源、身份和动作分发，不以重做整套 UI 为目标。
2. **所有音乐来源最终都是 Source Plugin，包括 Local。** Host 不再保留永久的 Local 特殊身份。
3. **主程序不包含具体平台的登录、账号或专用配置 UI。** Host 只负责通用插件管理、Schema 渲染和 Custom UI 容器。
4. **插件拥有认证、账号、服务器和特殊管理业务。** Host 提供 Secret、网络、导航和生命周期能力，但不理解平台协议。
5. **普通设置优先使用 Settings Schema。** 只有二维码登录、设备授权、复杂同步/诊断等业务使用 Custom UI。
6. **复杂 UI 使用独立版本化 `QueMusic.PluginUI 1.0`。** 它不进入 Source SDK v2，也不改变 Source 的核心数据契约。
7. **Plugin UI 只能使用稳定 UI Kit / Theme Token。** 禁止依赖主程序私有 QML 组件、对象名、上下文属性或页面结构。
8. **Local Music 必须成为标准 Source Plugin。** 旧 `FolderModel`/路径直播放只允许作为迁移期代码。
9. **所有 Source 最终统一经过 `PlaybackCoordinator`。** 不允许 Local、Kugou、Netease 或插件 Custom UI 绕过 Core 播放。
10. **Source Plugin 解析媒体资源，Core 实际播放。** URL、headers、到期时间等通过受控播放描述传递。
11. **Plugin 与 SourceInstance 必须区分并支持多实例。** 现有 v2 身份和账户存储继续沿用。
12. **Source SDK v2 冻结演进，不推翻重做。** 修正优先采用兼容扩展；Plugin UI 独立版本化。

## 6. 推荐实施顺序

以下顺序总体遵循最终方案，但将“先整合已存在的 v2 分支”设为实际 Phase 0。

### Phase 0：收敛并固定真实 v2 基线

工作：

- 分别保护当前目录和高级工作树的未提交内容；
- 将 `codex/unified-media-bridge` 的 96 个提交快进集成到当前开发线；
- 审核高级工作树中未提交的 CMake、日志、存储路径相关改动，单独决定保留、拆分或放弃；
- 使用全新构建目录执行 configure、build、CTest；
- 记录 Source SDK v2 ABI、Qt/编译器/架构兼容矩阵和测试清单。

出口：当前开发线包含既有 v2 成果；无意外混入的工作树改动；干净构建通过；不再从 v1 开始实现。

### Phase 1：冻结 Source SDK v2，定义 Plugin UI API 1.0

工作：新增独立 `sdk/plugin-ui/v1` 和 QML module；定义 `PluginUiDescriptor`、`PluginUiContext`、稳定 UI Kit、Theme Token、导航/通知/Secret 能力以及 import allowlist。只做公开契约和最小示例，不改变 Source SDK v2 的 ABI 布局。

出口：第三方测试插件能只依赖公开 SDK/QML module 编译和加载；禁止 import 主程序私有 QML；主题切换可用。

### Phase 2：将 Plugin Settings 升级为两级 UI

工作：在现有 `PluginSettingsController`、`SchemaSettingsForm` 上增加 Custom Management Page 的声明、加载、生命周期、错误和插件缺失处理。Schema 仍是默认路径。

出口：无 Custom UI 的插件只显示 Schema；有 Custom UI 的插件可在受控容器中打开；禁用/卸载插件会安全关闭页面并保留可解释状态。

### Phase 3：实现 Local Source Plugin

工作：把目录配置、扫描、媒体身份、封面/歌词入口和本地资源解析迁入 `plugins/local-source`；一个或多个媒体目录集合可建模为独立 SourceInstance。初期保留原 FilePage 的视觉结构，通过 Adapter 接入。

出口：Local 可由 SourceRegistry 创建、启停和移除实例；扫描结果产生稳定 MediaRef；主 UI 不再直接依赖路径作为媒体身份。

进展（2026-09-29，当前开发分支）：Phase 3-A 插件/Provider/多实例/播放闭环和 Phase 3-B 本地目录 Adapter 分支、非破坏性旧目录迁移已完成阶段验收；macOS Debug/x86_64 全量构建及 64/64 CTest 通过。详见 [Phase 3 验收记录](phase-3-local-source-plugin-record.md)。但“我的文件夹”自建集合仍是 Legacy 路径，真实应用主题截图和 Windows/Linux 未验收；不得据此把整个 FilePage 或最终架构标为完成。

### Phase 4：统一 Queue / History 身份与持久化

工作：将旧队列结构迁移为 `MediaRefV2` + 展示快照；定义 schema version、旧数据迁移、插件缺失/实例删除/账号变化后的恢复策略；历史记录使用同一身份模型。

出口：队列持久化中没有长期保存的可过期 URL；重启后可恢复；缺失 SourceInstance 显示为不可播放而不崩溃；旧队列可迁移或安全丢弃并告知用户。

进展（2026-10-03，隔离开发分支）：不透明 Local ID、旧缓存隔离、Host Codec/Store、Coordinator 恢复、启动与原 UI 队列模式拆分均已落地；包含真实 Local 插件与 Navidrome 身份夹具的阶段集成测试及全量 CTest 69/69 通过。旧 QML 内存队列没有可自动扫描的生产 JSON 来源，因此只提供显式旧文件导入、备份和拒绝项提示，不伪造路径到 SourceInstance 的映射。见 [Phase 4 验收记录](phase-4-queue-history-identity-record.md)。Phase 5/6 仍未完成。

### Phase 5：本地播放全部进入 PlaybackCoordinator

工作：Local Plugin 的 `IPlaybackProviderV2` 解析 `file://` 或受控本地资源；删除 `main.qml`、`FilePage.qml`、`PlayerControl.qml` 的本地直播放分支。

2026-10-04 入口复核：Local Plugin 的不透明 Track ID → 受控 `file://` 解析及 Adapter → Coordinator 路径已经存在，不能重做。既有 `LegacyLocalFolderMigration` 只迁移 `folders.type='local'` 目录配置，不迁移 `folders.type='my'` 的 `songs.path`，且后者允许导入插件扫描根目录之外的任意文件。现已新增独立旧身份认领扩展、候选实例消歧服务、`LegacyCollectionMigration` 和显式确认入口：用户可先查看旧歌曲数量、选择 Local 实例，再备份并逐项保存认领结果或待重试状态。页面显示逐首状态；已认领歌曲通过可选媒体项查找扩展取得插件提供的播放动作，再进入 Coordinator。后续入口清理已移除 `main.qml` 的本地文件直播放函数、“我的文件夹”未迁移歌曲的直播放/路径入队、“下载管理”的路径直播放/入队，并拒绝旧队列 `source == -1` 切歌。真实 Local 双实例、临时 WAV、快速切歌、解析取消与实例禁用前解析的集成断言已补齐；测试 Sink 不等同真实音频设备验收。旧记录与下载文件列表继续可见，但未经 Local 插件认领的文件暂不能从这些页面直接播放；不会由 Host 猜测实例或自动扩大扫描目录。真实用户数据库与完整 Phase 5 全链路验收仍待完成，旧在线平台 fallback 属 Phase 6。详情见 [Phase 5 迁移设计](phase-5-local-playback-migration-design.md)。

出口：在线与本地均只通过 Coordinator -> PlaybackSink；QML 不再直接解析音源或设置媒体 URL；切歌、取消和实例禁用遵守 generation/lifecycle fencing。

### Phase 6：原 UI 动态 Source 化并清除 fallback

工作：逐页将 Home/Search/Playlist/Favourite/File/Download、播放器、桌面歌词和动作菜单改为 Adapter/MusicHub/Capability 驱动；Source 选择使用实例 ID，不再使用平台整数。

2026-10-04 首个页面切片：`SearchPage.qml` 已保留原搜索标签、结果列表和详情层，但删除页面内所有 `MusicApi` 搜索/播放/收藏、固定平台选项及旧歌单弹窗分支。窗口搜索框、搜索按钮与历史记录三个入口也只调用 `OriginalUiMusicAdapter.search()`。Source 下拉列表按运行时 `sourceOptions` 展示，以 `selectedSourceInstanceId` 选择；新增第三方实例无需改页面代码，禁用实例不能被选中。Home、Playlist、Favourite、Player 等仍有旧平台 fallback，不能据此宣称 Phase 6 完成。详见 [Phase 6 页面迁移记录](phase-6-original-ui-source-migration.md)。

随后 Home 的来源选择、每日推荐与分类入口已改为 Adapter/Capability 路径，并主动激活推荐和分类页模型；“上次听到”、私人漫游/雷达、热门歌单及旧详情仍是待迁移项。首页未完全动态化，也没有为旧平台路径构造虚假的 SourceInstance 身份。

“上次听到”现已改用统一队列历史的展示快照；仅当原 occurrence 仍在队列且来源可用时允许通过 Coordinator 重播，移除或禁用后保持可见但不可重播。Home 不再读取 `Options.lastSongs` 或用旧平台 hash/source 入队。私人漫游/雷达、热门歌单及其详情仍待迁移；全页/全阶段验收尚未完成。

Home 的“我的收藏歌单”现显示 Adapter 已加载的歌单数并激活收藏页；分类详情弹窗删除了不可达的旧 `MusicApi.musicPlaylists` 回退，浏览需通过 Adapter Capability。私人漫游/雷达与热门歌单仍是旧平台入口，后续需可信的插件推荐/歌单来源，不能复用不相符的分类数据冒充。

收藏页歌曲、歌单和歌单详情的页面级旧平台回退已移除，Adapter 缺失时仅显示空态；播放、入队、浏览、取消收藏和批量操作走 Capability/Adapter。旧收藏数据没有删除，但尚未映射到插件身份，页面不再展示无法安全操作的旧条目；后续如需恢复应设计明确的认领/迁移流程。原“关注歌手”和“历史记录”标签仍是占位，不能将其计入已完成的插件功能；页面级代码清理也不代表 Phase 6 全部完成。

分类页来源下拉框已去固定平台选项和整数写入；Adapter 列表动作加入逐行 Capability，刷新经 Host Adapter 转交 MusicHub。歌曲标签页的固定地区标签已改为插件 Genre 展示项，旧新歌请求、hash 音质解析、下载菜单和路径队列回退已删除，无 Adapter 时为空。歌单标签页现只展示插件 Playlist 实体，使用 Adapter 浏览和收藏，已删除平台歌单菜单、请求、分页、路径身份收藏和旧详情入口；分类首页补上 v2 标准 Playlists 查询。歌手标签现只展示插件 Artist，原圆形卡片经 Adapter 浏览专辑及歌曲，已删除平台地区筛选和歌手请求。分类详情关闭后恢复分类首页；Host 新增 `closeCategoryBrowse()` / `resetCategoryNavigation()`。排行榜只识别插件 Playlist 的可选 `metadata.collectionKind="chart"` 白名单标记，复用标准浏览与分页；当前 Navidrome/Local 没有公共榜单，显示空态。旧榜单请求、平台徽标和路径队列详情已删除，该页不再直接引用 MusicApi。Source SDK v2 结构与 ABI 不变；旧 Host/缓存忽略可选标记，分类查询数由四变五。分类页的歌曲独立筛选、详情逐级返回和加载/错误交互仍待完善，不能宣称全部交互验收完成。

2026-10-08 后续切片已完成上述歌曲独立筛选、详情逐级返回及通用加载/错误交互：`categorySongs` 只投影 Track 并复用展示身份，Tracks 分区定向分页与重试复用现有 Host 能力；`categoryBack()` 复用 MusicHub 导航栈，安全标题/封面不暴露 ref。Unsupported-only 查询显示空态，网络失败可重试。SDK 毫秒在 Adapter 转为原列表秒数，共享列表时长随行数据持续更新。本次仅演进 Host API，Source SDK v2 与 Plugin UI API ABI 不变。主程序构建及五个 Adapter/QML/结构测试套件通过。多实例同类分区状态仍只投影首个匹配分区，网格精细分页状态、真实音频与手工视觉验收仍待完善。

上述首分区限制随后已在分类歌曲/详情修正：Host 展示模型提供 `paginationSectionIds` 与 `retrySectionIds`，聚合所有匹配分区的状态，经现有 MusicHub 接口分别分页/重试。耗尽、无游标、加载中或失败分区不重复翻页；非 Unsupported 错误只重试对应未加载分区，聚合错误不暴露插件诊断。分类歌曲/详情不再被共享列表的旧 MusicApi 加载状态阻塞。旧单分区属性保留，Source SDK v2 和 Plugin UI API ABI 不变。歌手/歌单/榜单网格以及 Search/Favourite 的精细分区分页、真实多实例音频与手工视觉仍是后续验收项。

分类页歌手、歌单和榜单随后也完成了定向分页切片：新增 Host `categoryArtists` / `categoryPlaylists` / `categoryCharts` 展示模型，复用已有动作身份。歌手只请求 Artists 分区；普通歌单与榜单共用标准 Playlists 查询/游标，只在展示层区分，未虚构榜单专用能力。删除三个标签通用空 sectionId 的“更多”请求，接入分区加载、重试、耗尽和空态；首批没有榜单但仍有 Playlists 游标时允许继续加载。歌单列表不再受旧 MusicApi 加载状态阻塞。主程序构建及六个相关测试套件通过；新增的是 Host 属性，Source SDK v2/Plugin UI API ABI 不变。其他页面分区状态、剩余旧平台入口和真实多实例音频/手工视觉仍待迁移与验收。

搜索四标签、收藏两标签及其详情随后完成共享分区分页切片：Host 聚合所有匹配分区的分页/重试状态，QListView 通过默认关闭的 `sourcePaging` 增量接入，开启后不再读取最后一行状态或受旧 MusicApi 加载状态阻塞。根页与详情保持原页码及动作身份，空结果仍可定向重试；模型重置和 Adapter 移除时的按钮回调补上空模型保护。Host API 兼容风险：搜索/收藏模型聚合 error 已改为 sectionId -> failed 布尔状态，旧私有诊断读取者需同步调整；Source SDK v2、Source DTO 与 Plugin UI API ABI 不变。Home、File/Download、播放器/歌词等剩余入口及真实音频、实例卸载与手工视觉验收仍待推进。

Home 每日推荐随后接入五类标准推荐分区聚合及共享 `sourcePaging`，删除空 sectionId 的通用更多请求；空结果可定向重试，旧 MusicApi 加载状态不再阻塞插件分页。Home 分类详情支持 Capability 控制的 Track 播放、入队、收藏及非 Track 浏览，关闭后恢复分类首页；空模型/Adapter 回调安全，不显示无可信数据的曲目计数和旧菜单。Host `recommendSongs.error` 同样改为 sectionId -> failed 通用状态，旧私有诊断读取者需同步调整；Source SDK v2/Plugin UI API ABI 不变。主程序构建及六个相关套件通过。私人漫游/雷达、热门歌单及其旧详情仍待迁移，未用 Random 或普通分类歌单冒充；Home 与 Phase 6 均未完成全部验收。

2026-10-09，File 目录增量切片：保留既有独立目录浏览和旧歌曲认领流程；目录根/内容改用所有 Tracks 分区的聚合分页与重试身份，空分区也可继续，失败分区单独从第一页重新查询并替换，不刷新健康实例。无来源的全页失败仍可全页刷新，Unsupported-only 显示空态。目录行/占位错误改为失败布尔标记，移除原始诊断；行动作加入 Capability 和空模型保护。新增 Host `retryDirectorySection()`，QML 与 Host 展示契约需同步更新；Source SDK v2/Plugin UI API ABI 不变。File 旧集合、下载业务、播放器/歌词剩余旧分支及真实音频/手工视觉验收仍待推进，不能据此宣称 Phase 6 完成。

目录切片验收：主程序构建及九个目录/Adapter/MusicHub/QML/结构套件通过，新增测试覆盖健康实例不被重试、重复请求拒绝、失败分区替换、Unsupported 残留游标与过期回调、无行分区状态、错误脱敏和空依赖动作安全；真实设备音频、完整实例卸载与手工视觉验收未执行。

播放器标题/歌手菜单的搜索入口已改为现有 Adapter 搜索，不再写入旧平台搜索模型，保留原菜单与结果页跳转；缺少 Adapter/空文本时拒绝请求。歌词、当前封面及部分播放控制也已按下述切片迁移；播放器收藏及剩余平台入口尚未统一。

当前播放歌词的 Host 契约已增量接入：Adapter 提供仅 time/text 的行、状态与失败重试，经既有 MusicHub 资源仓库取得插件文本，按当前 generation/媒体身份/请求 ID 拒绝旧结果；停播、禁用及销毁清理。纯 LRC 解析提取为共享无 IO 工具，Host 不发现或读取本地歌词。Source SDK v2/Plugin UI API 不变；全屏/桌面歌词 UI 已绑定该契约，翻译/逐字信息不能由旧平台补全。

全屏与桌面歌词随后接入 Host 当前歌词和统一时钟；Source 模式缺歌词、停播或缺 Adapter 不会使用旧平台行/翻译。兼容输入只由 main.qml 在真实旧平台播放时显式启用，播放链路切换停止另一播放器。保留原布局/动画，补齐失败重试、末行保护和 PlainText 渲染；纯桥接测试和结构检查不代替真实歌词动画/音频验收。收藏及剩余旧入口仍待迁移。

当前播放封面随后接入 Adapter 的 `currentCover`：沿用现有 MusicHub/MediaAssetRepository，只接受本地缓存 URL，并验证当前 generation、媒体身份及请求 ID。主窗口封面/预览、全屏唱片/背景图片和桌面播放器共享同一展示地址；Source 模式无结果/停播/缺 Adapter 时显示默认图，不回退旧播放器封面或旧取色渲染图。歌词重试不会取消或重复请求健康封面。动态取色已按下述切片收口；资源缓存未新增内容解码验证，真实视觉与音频仍待验收。收藏与其他旧入口未完成。Source SDK v2 与 Plugin UI API ABI 均不变。

动态取色随后绑定当前缓存封面，支持本地与 qrc 默认图读取而不启动网络请求；旧网络取色结果和重入观察者不能覆盖新封面的颜色/渲染图。全屏 Mesh 背景为网络响应和后台图片处理增加 generation fencing，切换到 Source 封面后拒绝旧任务。取色逐像素读取先统一 ARGB32 格式，避免灰度/索引图片格式导致错误访问。新增图片展示测试覆盖这些异步边界，尚不代替真实 GPU 动画/视觉验收。

桌面播放器随后移除直接 mainMedia 读写，播放/暂停、时间、可跳转状态与进度统一经过 Host 展示/控制桥接；底部播放器时间与进度复用同一桥接。Source 停播或控制 Adapter 缺失时不启动旧播放器；上一首/下一首/随机动作也不切回旧队列，用户可显式从队列选择 Source 恢复。Source 模式倍速仍作用于 Core；真正启动旧平台播放才切回兼容模式。收藏、下载、Windows SMTC、频谱及其他平台专用信息尚未完整迁移，不能宣称播放器已完全统一。

当前播放收藏的 Host 契约已增量接入：Coordinator 在播放/权限事件中缓存经过 Plugin/Server/Account/Media 交集及 Provider 检查的收藏权限，非 QML 的 `currentActionItem()` 提供当前队列项动作快照。Adapter 的 `currentFavorite` 只公开 canFavorite/canUnfavorite/state/pending/failed 和不含音源身份的播放 token，`setCurrentFavorite()` 沿用 Router 复核，按请求 ID/generation/完整媒体身份隔离结果并去重重入点击。初始状态 unknown，只有插件返回 bool 才标记最后确认收藏/未收藏；不从旧收藏数据库或历史记录推断。原按钮已接入：已知状态单击切换，未知状态用原 QMenu 显式选择操作，无权限/pending/停播/缺 Adapter 禁用；菜单保存 token，切歌后关闭且旧动作拒绝。legacy 数据库只在显式兼容模式使用。SDK 仍没有统一读取收藏状态的 Provider，不能把最后确认结果视为跨重播或外部修改后的权威读状态。Source SDK v2/Plugin UI API ABI 不变；新增 Host C++ API 需与展示层同步构建。

Windows SMTC 随后接入统一播放桥接：Source 输入仅控制 Core，元数据仅提取当前 title/artists/album 与本地缓存封面，AppMediaId 使用 opaque occurrence，不读旧队列或分享 SourceRef/播放资源。停播清理信息/时间线并禁用控制，缺 Adapter 或服务不可用不回退；旧 mainMedia 迟到信号不能覆盖 Source 系统状态。Core 状态、进度、队列和封面变化同步系统展示，原生封面读取失败也清除旧缩略图。主程序构建和九个联合套件通过，但 macOS 只验证 QML 服务边界，Windows WinRT 分支编译、系统浮层/物理键/进度/图片权限仍待真实 Windows 验收。Source SDK v2、Plugin UI API 与 SMTC API 不变，下载、频谱与详情/选项等剩余入口仍未完全统一。

播放器音乐详情随后接入当前展示投影，仅保留 title/artist/album/sourceLabel 和显式 legacy 字段，不复制 SourceRef、资源或任意 metadata。Source 第一行显示来源，v2 未提供的文件名/日期/格式为未知；长度读取 Core 时钟，切歌更新，停播/无当前项清空，旧信息只在显式兼容模式可见。主程序构建和九个播放/UI 套件通过，详情弹窗采用实际 QML 逻辑与轻量容器夹具，真实视觉仍待验收。下载按钮、播放选项与频谱等剩余路径尚未统一；SDK ABI 不变。

当前下载按钮随后封堵 Source 停播后的旧 hash 回退：sticky Source 模式始终禁用并在 handler 拒绝，显式 legacy 才允许原在线下载，越界和 Legacy Local 也拒绝。主程序构建及七个相关套件通过；实际按钮测试确认 Source 活跃/停播均零旧队列读取和零旧 API 调用。这并非 Source 下载功能完成：Router 已有 Download Provider 路由，但当前动作快照、Adapter 当前下载 API、用户目标文件选择及覆盖保护仍需接入，不能用旧路径或泄露播放资源替代。

当前 Source 下载的 Host 契约随后增量接入：Coordinator 缓存 Provider 和四层交集的 Download 权限，Adapter currentDownload 只展示 canDownload/pending/failed/completed/token，downloadCurrent 必须携带当前播放 token 和用户选择的新本地目标；请求及结果按完整身份、代次、目标和 ID 隔离。Host 拒绝已有文件/目录、远程/相对/畸形地址、悬空链接及无效父目录，并在 pending 通知后再检查；不自动创建目录或覆盖。Router 与 Navidrome 现有提交保护继续复用，第三方插件仍需自行确保提交期不覆盖。主程序构建和四个联合套件通过，保存 UI 尚未接入，该状态不是跨曲目下载任务管理器，真实字节/服务器和视觉验收仍待完成。Source SDK v2/Plugin UI API ABI 不变，Host API 需同步构建。

原下载按钮随后接入 Host SaveFile 选择：Source 由 currentDownload 权限/token/pending 启用，打开时捕获令牌，确认前在 QML 与 Host 两端复核；取消不提交，切歌/停播/降权/移除 Adapter 关闭旧选择，旧确认拒绝且不回退 legacy。对话框要求新文件，已有目标仍拒绝覆盖；不猜测 SDK 未提供的文件名/扩展名。主程序构建及八个联合套件通过，包括原按钮/保存 handlers 的 QML 夹具与既有 Navidrome 本机 HTTP 字节/清理回归，真实 OS 保存对话框与服务器仍待验收。当前 Source 下载未写入旧 DownloadModel，跨曲目任务展示/进度/取消/下载页统一尚未实现；不能据此宣称 Download 或 Phase 6 整体完成。

出口：视觉和核心交互回归通过；QML 中不再存在 `source == -1`、Netease/Kugou switch 或 `MusicApi` 音源 fallback；无能力动作自动隐藏/禁用。

### Phase 7：Navidrome 样板插件收口

工作：补齐真实服务器场景、错误/空态、设置与管理 UI、header 认证策略、多个服务器实例和卸载恢复体验。它应成为第三方插件参考实现。

出口：至少两个 Navidrome 实例可并存；账号/Secret 隔离；浏览、搜索、播放、常用动作和错误恢复通过端到端验收。

### Phase 8：迁移 Kugou Source Plugin

工作：API、账号、二维码登录、轮询和特殊管理页全部进入插件；Host 仅承载公开 Plugin UI。

出口：主程序不含 Kugou 登录/UI/API；插件禁用或卸载不影响其他 Source；二维码流程通过安全和生命周期测试。

### Phase 9：迁移 Netease Source Plugin

工作与验收对齐 Kugou；复用 Plugin UI API，不为单一平台增加 Host 特例。

出口：主程序不含 Netease 登录/UI/API；多账号/多实例可用；原 UI 通过通用能力访问。

### Phase 10：删除 Legacy MusicApi / Local / 平台特殊路径

工作：在所有替代路径和迁移测试通过后，删除 Legacy C++ API、账号注入、路径直播放、平台枚举和无引用 QML；清理 CMake、资源和依赖。

出口：生产目标不再编译 `NeteaseCloudApi`、`KugouApi`、Legacy Local 模型或平台专用 Host UI；架构扫描和运行时日志均无旧入口。

### Phase 11：SDK 发布与第三方支持

工作：发布 Source SDK v2 与 Plugin UI 1.0 的版本/兼容矩阵、示例插件、打包模板、CI 校验和迁移指南。

出口：示例插件可在仓库外构建、打包、安装；ABI/API 和 QML import 兼容测试可自动运行。

## 7. 预计修改、新增与删除的主要文件

以下是实施范围预估，不代表现在立即创建或删除。

### 7.1 优先保留并修改

- `CMakeLists.txt`：接入 Plugin UI module、Local/Kugou/Netease 插件目标、测试和最终 Legacy 清理；
- `main.cpp`：最终只装配通用 Host 服务，移除平台账号和 Local 特殊模型注入；
- `core/plugins/PluginManifest.*`、`PluginManager.*`：增加可选 UI descriptor/资源校验，不改变 Source v2 核心；
- `core/source/SourceRegistry.*`：仅做兼容扩展和 Local/卸载场景验证；
- `core/music/PlaybackCoordinator.*`：加入持久化队列接入、缺失实例状态和必要的资源策略；
- `core/music/OriginalUiMusicAdapter.*`、`MusicHub.*`、`MediaActionRouter.*`、`CapabilityResolver.*`：补齐原 UI 动态化和动作覆盖；
- `core/settings/PluginSettingsController.*`、`PluginSettingsOperation.*`、`SettingsSchemaPresentation.*`；
- `components/PluginSettingsPanel.qml`、`SchemaSettingsForm.qml`、`SourceAccountList.qml`；
- `SettingsView.qml`、`main.qml`、`layout/PlayerControl.qml`、`layout/MainContent.qml`、`layout/LeftSideBar.qml`；
- `pages/HomePage.qml`、`SearchPage.qml`、`PlaylistPage.qml`、`FavouritePage.qml`、`FilePage.qml`、`DownloadPage.qml`；
- `plugins/navidrome-source/*` 与其 manifest；
- `sdk/source/v2/*`：原则上冻结，只允许 ABI 安全的澄清或追加。

### 7.2 预计新增

- `sdk/plugin-ui/v1/`：C++ 公共类型、Context 接口、版本和兼容声明；
- `qml/QueMusic/PluginUI/` 或等价独立 module：公开 UI Kit、Theme Token、`qmldir`；
- `core/plugin-ui/PluginUiHost.*`、`PluginUiContextImpl.*`、`PluginUiValidator.*`；
- `plugins/local-source/`：插件、会话、扫描器、数据库/索引、播放 Provider、设置 schema、测试；
- `core/music/QueueStore.*`、`HistoryStore.*` 或等价的版本化持久层；
- `plugins/kugou-source/`；
- `plugins/netease-source/`；
- `examples/source-plugin/`、`examples/plugin-ui/` 和 SDK contract tests。

具体文件名可在阶段设计时微调，但职责边界不能改变。

### 7.3 最终预计删除或移出主程序

- `api/NeteaseCloudApi.*`；
- `api/KugouApi.*`；
- `api/MusicApiService.*` 中的平台聚合职责；若存在真正通用职责，应先拆分再删除；
- `cpp/AccountManager.*` 中的平台账号职责；
- `cpp/FolderModel.*` 作为生产 Local Source 特殊入口；
- QML 中 Netease/Kugou QR、平台账号卡、平台整数 switch、Local 直播放和 `MusicApi` fallback；
- `components/LegacyQueueController.qml` 及旧队列结构；
- Source SDK v1 / `SourceManager` 残留（高级分支已基本完成此删除，不应恢复）。

所有删除必须延后到替代路径、数据迁移和回归测试通过之后。

## 8. ABI / API / 数据兼容风险

| 风险 | 影响 | 控制措施 |
| --- | --- | --- |
| 直接修改 Source v2 结构体布局或虚函数表 | 已编译插件无法加载或发生未定义行为 | v2 冻结；新增能力优先用可选 Provider、descriptor 扩展或新接口版本；持续运行 ABI fixture tests |
| `sourcePluginId` 名称与 `sourceId` 语义不一致 | 第三方插件误用、持久化键不稳定 | 先文档化和提供兼容访问器；如需重命名，使用序列化别名/迁移，不原地破坏字段 |
| Plugin UI QML import 成为隐式 ABI | Host 私有组件一改即破坏插件 | 只允许 `QueMusic.PluginUI 1.0` 和 Qt allowlist；CI 扫描禁止私有 import |
| Qt minor/编译器/架构/build key 差异 | 二进制插件被拒绝或崩溃 | 延续 manifest/runtime 校验；发布兼容矩阵和构建模板 |
| SourceInstance/账户 ID 变化 | 收藏、队列、历史、缓存失联 | 明确定义稳定 ID 生成与账户迁移；禁止使用显示名或索引作为持久键 |
| 旧队列持久化含 URL/path/平台整数 | 升级后不可恢复或错误路由 | 引入 schema version 和一次性迁移；URL 不作为稳定身份；失败项保留可解释占位 |
| 插件禁用/卸载时仍有页面、请求或播放 lease | use-after-free、崩溃、悬挂 UI | 延续 Registry/PluginManager lease；Plugin UI Host 增加页面关闭、请求取消和 context 失效 fencing |
| 带 header/cookie 的流 | `QtPlaybackController` 当前明确拒绝 | 在 Core 设计受控支持方案并测试日志脱敏；禁止插件自行启动播放器绕过策略 |
| Secret 跨平台能力不完整 | Windows/Linux 无法安全保存账号 | 使用 OS Secret Store；不可用时明确拒绝持久化并提供可理解错误，绝不明文降级 |
| Local 路径和数据库迁移 | 扫描结果重复、收藏/历史丢失、移动文件失效 | 定义 Local stable ID、路径规范化、重扫/移动策略和备份；用真实媒体 fixture 测试 |
| UI 行为回归 | 保留现有 UI 的目标被数据层迁移破坏 | 建立页面截图/结构/键鼠交互回归；逐页切换，不做大爆炸式重写 |
| 高级工作树未提交改动 | 集成时混入与本次架构无关的存储/日志改动 | Phase 0 单独审查、提交或暂存；以提交边界而非工作树整体复制 |

## 9. 分阶段测试与验收条件

### Phase 0

- 新构建目录从零 configure/build 成功；
- 全量 CTest 通过，测试数量和名称固化到基线记录；
- `git merge-base`/提交图确认没有重做或丢失 v2 提交；
- Source SDK v1 不再被生产目标使用；
- 工作树中的无关改动有明确归属，没有被架构提交夹带。

### Phase 1

- C++ Plugin UI API contract/ABI 测试；
- QML `import QueMusic.PluginUI 1.0` 成功；
- 浅色/深色、缩放、字体、禁用态和键盘焦点测试；
- import allowlist/lint 拒绝主程序私有组件；
- 示例插件在仓库外只依赖公开 SDK 构建成功。

### Phase 2

- Schema-only、Custom-only、两者兼有、无设置四种插件测试；
- Custom UI 加载失败、版本不兼容、插件缺失时显示通用错误页；
- 禁用/卸载插件时页面关闭、Context 失效、异步回调被抑制；
- Secret 不出现在 QML 普通属性、日志和非安全配置文件中。

### Phase 3

- 单目录、多目录、多实例扫描测试；
- 重扫、重复文件、删除/移动、权限拒绝、不可读文件和空库测试；
- metadata、封面、歌词和稳定 MediaRef 测试；
- Local 插件可安装、启停、删实例和重建；
- FilePage 外观和核心操作与当前版本对照回归。

### Phase 4

- 队列/历史序列化 round-trip 和 schema version 测试；
- Legacy `{path/source}` 数据迁移测试；
- 应用重启恢复、插件缺失、实例删除、账号变化、媒体消失测试；
- 持久化内容不包含临时 token、Secret 或可过期播放 URL。

### Phase 5

- Local/HTTP/HTTPS 播放统一进入 Coordinator 的 contract 测试；
- resolve 失败、过期资源、快速切歌、取消、禁用实例、卸载插件测试；
- 播放日志脱敏测试；
- 静态扫描确认生产 QML 不再直接为音源设置 `MediaPlayer.source`。

### Phase 6

- Home/Search/Playlist/Favourite/File/Download 页面结构和截图回归；
- 动态 SourceInstance 列表、多实例切换、聚合/单源筛选测试；
- capability 缺失时动作隐藏/禁用并有一致提示；
- 静态扫描确认无平台整数、平台名称 switch 和 `MusicApi` fallback；
- 键盘、鼠标、窗口缩放和桌面歌词基本交互验收。

### Phase 7

- Navidrome mock contract tests 全通过；
- 至少一个真实 Navidrome 服务器的人工 smoke；
- 两个服务器实例并存、Secret 隔离、断网、401、服务器升级和重新认证测试；
- 浏览、搜索、播放、收藏/评分/播放列表等 capability-driven 行为测试。

### Phase 8 / Phase 9

- Kugou/Netease 的 API 和认证 contract tests；
- QR/设备授权超时、取消、重复扫码、插件禁用/卸载测试；
- 多账号/多实例和 Secret 隔离；
- 主程序二进制和 QML 资源中不再包含平台登录实现；
- 不支持的动作由 capability 控制，不由 Host 平台判断控制。

### Phase 10

- 删除 Legacy 文件后从零构建和全量测试；
- CMake/source/resource 扫描无 `NeteaseCloudApi`、`KugouApi`、Legacy Local、旧 queue controller 和平台 QR Host UI；
- 应用启动、插件发现、Local、Navidrome、Kugou、Netease 和缺失插件场景全部 smoke；
- 包体检查确认平台专用库只随对应插件交付。

### Phase 11

- SDK 示例在支持的 OS/Qt/编译器矩阵中构建；
- manifest、包结构、ABI、QML import 和主题合规 CI 全通过；
- 第三方开发文档从空目录开始可复现一个可安装、可配置、可播放的示例 Source；
- Source SDK v2 与 Plugin UI 1.x 的兼容策略和弃用周期公开。

### 后续增量：跨曲目下载会话记录

Adapter 后续新增 downloadTasks 安全投影，独立于当前播放 token 保存 Router 已分发下载的 pending/completed/failed 状态。切歌/停播/Coordinator 销毁不抹去任务结果，Router 销毁与实例失效使进行中记录失败；完整身份、请求和目标仅保留在私有进行中记录。展示只含 opaque taskId、标题/歌手/来源、目标文件名和通用状态；相同进行中目标 URL 去重，终态移除不取消、不删文件。四个 Host/播放/路由/队列联合套件通过。下载页接入、取消、真实字节进度与跨启动恢复仍待实现，真实服务器和原生视觉未验收；Source SDK v2 与 Plugin UI API ABI 不变。

### 后续增量：原下载页 Source 会话展示

DownloadPage 后续由 MainContent 持续绑定 Adapter，在原下载中/已下载页签分别展示安全的进行中与失败/完成记录。终态移除不删除文件，进行中 handler 再拒绝；缺 Adapter 清空 Source 区，旧任务与旧目录文件作为明确的兼容区保留。Source 下载无需旧固定目录扫描，完成后仍必须由 Local 插件明确导入，禁止路径直播放与自动扩张根目录。主程序构建和八个相关套件通过，实际 QML 夹具覆盖分类/页签/移除/缺 Adapter/纯文本。取消、进度、跨启动恢复与真实服务器/原生视觉验收仍未完成，SDK v2 与 Plugin UI API ABI 不变。

### 后续增量：Source 下载任务取消

Router 后续增加 Host-only cancelDownload，Adapter 通过 opaque taskId 查找私有请求，下载页提供进行中取消与取消终态展示。复用 v2 session.cancel 和既有延后/租约保护，拒绝陌生、非下载、已完成或重复请求；迟到成功不能覆盖取消，关闭实例等生命周期失败优先。切歌后的取消不影响当前曲目/另一实例，取消与移除记录均不由 Host 删除用户文件；当前曲目可重新选择新目标提交。主程序构建和九个联合套件通过，包括取消回调销毁 Router、卸载 Busy、实例关闭、文件保留及实际 QML 状态保护。Source SDK v2/Plugin UI API ABI 未改；真实服务器、原生 UI、字节进度、跨启动记录与安全重试仍未验收/实现，Phase 6 继续推进播放器剩余入口迁移。

### 后续增量：播放器选项路径隔离

实际代码进一步确认旧自动播放/音质/自定义设备只改旧播放器与旧设置，Source Core 独立构造默认 QAudioOutput，尚无自定义设备 API；间距补偿/EQ 也是旧占位。Source 模式现隐藏这些旧/未实现选项，默认设备行明确禁用，bindings 和 handlers 双重阻止旧配置读写；兼容模式保留。倍速改为统一展示桥接，Source 只走 Core 全局设置、缺控制器拒绝，严格验证原 0.1–4.0 范围和预设索引。主程序构建及十二个相关套件通过，实际选项 QML/真实桥接夹具验证停播、程序化触发、无效输入、缺 Core 和显式 legacy 隔离。Source 自定义设备、自动播放策略、EQ/gap compensation 仍需独立 Core 实现；插件专属音质仍应交给 Settings Schema/Plugin UI。真实设备/音频/弹窗视觉未验收，SDK v2 与 Plugin UI API ABI 不变，后续继续频谱与剩余入口迁移。

### 后续增量：频谱安全管线与旧播放器隔离

已完成有界、可失效的 Host-private PCM/FFT 分析器以及 `GetWave` 生命周期修复。原 UI 频谱改由安全数据适配器投影，Source 模式不再绑定旧播放器、不再读取旧波形；当前 Source 空频谱是明确的迁移中状态，Core PCM 接入仍待下一切片完成。

验收：主程序构建与 7 项联合回归通过，覆盖四种 PCM、有限几何输出、队列上界、旧任务隔离、销毁与重新绑定。未执行物理音频和 GPU 视觉验收。无需改变 SDK v2 或插件 ABI；新增 Host 库需同步构建。

### 后续增量：Source 的 Core 频谱接入已完成

上一增量中的 Source 空频谱过渡状态已由 Core 自身的解码 PCM 接入替代。QML 只消费有界几何路径与显示开关；原频谱布局、原 `waveDisplay` 设置及明确的 Legacy 分区保留。暂停/停止/结束/错误、切曲、seek、禁用和适配器替换有清空与代次隔离测试，旧后台任务不复用到新播放。

主程序构建及 7 项联合回归通过，包括实际 main Binding + 真实 Core 门面接线；未做物理音频、真实服务器或 GPU 手工验收。Source SDK v2 / 插件 ABI 不变；Host 门面新增内部 API，需同步重建应用和相关测试。当前下一项是桌面悬浮播放卡的安全展示/动作迁移，不能将其旧播放器入口视为已完成。

### 后续增量：桌面悬浮卡统一展示/播放入口

`DesktopSpot` 已移除 `mainMedia` 与旧标题直连，改读安全适配器展示及 typed transport，Source 空态不会读写 Legacy。保留布局/动画，标题按纯文本，拖动与范围切换有实际 Slider/Binding 回归；主程序构建及 7 项联合回归通过。视觉控件为测试 stand-in，尚无原生桌面/GPU 手工证明。

普通 `DesktopPlayerWindow` 的标题/歌手仍读旧 window 字段，且它和底栏存在相同范围变化风险，作为下一有界切片继续处理；不能据此删除其他仍在使用的 Legacy 业务。

### 后续增量：普通桌面元数据与两处进度范围已收敛

普通桌面播放器的标题/歌手已接安全 details，并在 Source 空态清空、按纯文本显示。它与原底栏的真实 Slider/Binding 均验证 duration 变化、拖动及 Source/Legacy 切换，修复范围恢复而 position 未变时进度不能恢复的问题。主程序构建及 7 项联合回归通过，尚无真实音频或原生窗口/GPU 手工验收。

剩余差距：底栏标题/歌手及搜索菜单仍消费兼容 window 字段；main 的兼容状态维护及平台专用业务并未因此完成清理。后续先统一这些显示/搜索入口，再继续页面和平台插件化；不删除尚在使用的 Legacy 行为。Source SDK v2 和插件 ABI 不变。

### 后续增量：底栏元数据与搜索菜单已统一

上一增量中底栏标题/歌手的显示与搜索差距已收敛到安全 details；Source 空态清空，旧菜单快照失效后不会搜索上一首。保留原菜单及多歌手拆分，通用菜单标签按纯文本显示。实际 Text/MouseArea/解析与搜索函数/处理器、真实 MenuItem/Text 有 offscreen 测试；主程序构建及 7 项联合回归通过。

main 可变兼容字段和明确 Legacy 收藏仍在使用；其他消费者、平台业务和页面迁移未整体完成。后续继续按原顺序审计/迁移，不能直接全删 Legacy。SDK v2 / Source 插件 ABI 不变；真实音频及原生窗口/GPU 手工验收仍待执行。

### 后续增量：最大化与桌面歌词剩余标题展示已迁移

最大化标题/歌手、歌词模式组合标题及桌面歌词控制栏已不读取可变兼容 window 字段。统一通过安全展示桥接投影并以纯文本显示；Source 空态、缺失单侧元数据及迟到 Legacy 数据有实际 Text/桥接回归。主程序构建及 7 项联合回归通过，未做 GPU/原生窗口/真实音频验收；SDK v2 与 Plugin UI API 不变。

核对到 main 仍有无调用方的 Navidrome 账号编辑函数/Loader 待处理标记，且其调用的旧设置页方法已不存在；下一切片移除该残留，保留已经存在的通用插件设置导航。

### 后续增量：失效 Navidrome 主窗口账号入口已删除

已移除无调用方且指向不存在方法的 Navidrome 账号打开函数、待加载标记和 Loader 专用分支。通用插件/实例设置入口保留，并以实际 main 函数/Loader 处理器测试加载前后、最新选择、空实例及不重放；主程序构建和 original playback、UI structure、plugin settings QML、management UI session 共 4 项回归通过。

这是失效入口清理，不代表仍在使用的网易/酷狗 Host 登录 UI 已迁移。下一已识别差距为 Source 向 Legacy 标题字段的复制、关闭时混写旧 lastSongs 及封面查看旧标题读取；需先修正这些边界再删除兼容状态。

### 后续增量：Source 与 Legacy 元数据及关闭记录已隔离

Source 已不再复制标题/歌手到 main 的兼容字段，关闭时也不读取旧队列或改写旧 lastSongs；Source 停播后的模式边界继续保持。Source 的持久化沿用既有 QueueHistoryStore，显式 Legacy 的保存/最小化及收藏业务未删除。封面查看改读安全标题，导出名限制为有界单文件名，并在异步抓图前固定目标。

主程序构建及 9 项联合回归通过，包含真实 main 处理器、Source 零 Legacy 读取、显式 Legacy 记录、Unicode/路径/保留名与异步目标快照测试。图片测试不实际写盘，尚无原生视觉或真实音频验收；SDK v2 / Source 插件 ABI / Plugin UI API 不变。封面写入失败提示和异步图像生命周期仍待处理，平台专用登录 UI 与其余页面仍按既定顺序迁移。

### 后续增量：封面导出结果与异步视图边界已补齐

封面保存已检查捕获提交及写盘结果，空目录、图像不可用、失败/异常不再误报成功；重复保存受 pending 约束，失败后可主动重试。源变化或重新打开推进视图代次，异步完成时拒绝过期/不再 Ready 的结果，不将新封面写到旧请求目标。原交互及目标快照保留，没有自动重试/删除或新覆盖策略。

主程序构建及 9 项联合回归通过，新增实际生产处理器的失败/重复/往返失效/同源重开/重试测试。测试使用受控捕获与写盘替身，尚未执行真实图片写入、macOS 权限及 GPU/窗口捕获验收；没有改变 SDK v2 / Source 插件 ABI / Plugin UI API。下一项扩大构建/回归核验，之后继续剩余页面与平台插件化，不把局部修正等同于最终架构完成。

### 后续增量：全目标构建与 71 项回归检查点（2026-10-09）

代码基线 `23b3315` 在 macOS / Qt 6.11.1 的既有 `build-phase3` 完成全目标增量构建，全部 71 项 CTest 通过（141.85 秒），测试后无意外工作树改动。回归涵盖 Source SDK v2、独立 Plugin UI、Settings Schema、Plugin/Instance、Capability/Router、Coordinator/Core、频谱、原 UI、队列历史、Local 与 Navidrome；包含 macOS 插件包增量同步、移动后 Local bundle 测试。

这不替代从零构建、跨 OS/ABI 矩阵、真实服务器/物理播放/原生视觉与封面实际写盘验收。当前下一开发切片是首页私人漫游/雷达和热门歌单的通用 Source 表达及入口迁移；保留原卡片视觉，不猜造 v2 功能或回退旧路径冒充 Source。网易/酷狗平台专用 Host UI、其余页面 Legacy 业务与最终删除阶段仍有工作，Phase 6 未整体完成，最终架构基线与既定迁移顺序不变。

### 后续增量：首页热门歌单入口已统一 Source 数据（2026-10-10）

首页原热门歌单卡片现复用已实现的 Category / Playlists 与安全 categoryPlaylists，保留布局/动画，按纯文本显示；名称为“音源歌单”，不猜造热门排名、播放量或曲目数。浏览/详情播放、入队、收藏及分页统一经 Adapter，独占的旧歌单窗口及 hash/path/Legacy 收藏分支已删除。模型缺失不回退，空/加载/失败及分区 cursor/重试有通用展示与保护。

主程序构建及 9 项联合回归通过，实际 QML 页面覆盖分类/能力/详情动作/分页/失败/无 Adapter/纯文本和零旧歌单调用；SDK v2 / Source 插件 ABI / Plugin UI API 不变。私人漫游/雷达尚无明确契约，其兼容代码仍保留，不宣称已插件化。下一切片是共享详情容器的旧平台字段、纯文本标题及范围变化失效；真实服务器/原生视觉与 Phase 6 完整验收仍待完成。

### 后续增量：共享详情容器与首页 Source 上下文已隔离

共享 AnimatorWindow 删除未使用的旧 songSource/MusicApi 绑定，标题以纯文本显示；Host 私有 resetView 立即隐藏、取消动画、关闭 Loader 并清空展示。首页范围变化或 Adapter 替换/移除失效 Source 推荐/分类详情，导航失效关闭详情，嵌套导航更新标题；上下文切换不会误清新 Adapter 导航，正常关闭仍清当前导航。动画中重新打开取消旧关闭动作，防止新详情被旧完成动作隐藏。

主程序及相关资源构建、10 项联合回归通过，包含生产容器/页面与延后销毁、父页面恢复、Source 范围、替换/移除、嵌套标题、重开及正常关闭测试；不等同原生视觉或真实服务器验收。SDK v2 / Source 插件 ABI / Plugin UI API 不变，该容器不公开给插件。下一项全目标核验，然后继续私人漫游/雷达明确契约与入口边界，其 Legacy 与平台登录迁移仍未完成。

### 后续增量：首页迁移后的全量回归检查点（2026-10-10）

代码基线 `b017ac5` 在 macOS / Qt 6.11.1 的现有 build-phase3 完成全目标增量构建与全部 71 项 CTest（114.89 秒），测试后无意外工作树改动。共享容器及首页歌单/详情的最新资源已参与原播放、操作、本地目录和完整插件/SDK/Core/Source 包装回归。没有修改 SDK v2 / Source 插件 ABI / Plugin UI API。

已完成首页旧热门歌单的通用数据/动作迁移和 Source 详情上下文边界；尚未完成私人漫游/雷达、平台账号 UI 及 Phase 6 整体迁移。下一步先明确私人发现能力的可选声明、查询及旧 Provider 不支持行为，避免将 Random 或未知过滤器被忽略后的普通列表冒充私人业务，再迁移相关入口。真实服务器/音频/视觉、从零及跨平台 ABI 验收仍待完成。

### 后续增量：私人发现可选扩展与旧插件兼容边界（2026-10-10）

已新增独立版本的 Session `IDiscoveryProviderV1`，区分私人漫游/雷达与普通 Random，并由 PageRepository 在逐账号 Availability 允许时分发。Host selector 非法组合被拒绝，绝不进入旧 fetchPage；结果仅接受该实例/音源/账号的 Track 分区，私人页面不持久缓存，续页重验权限，调用期持独立插件租约。Source SDK v2 / Plugin UI API 的现有 ABI 未变，新增公共扩展头随 PluginSdk 安装。

v2/新扩展契约、安装后外部编译、PageRepository 与 AggregateComposer 共 5 项联合回归通过；详见 `source-discovery-v1.md`。下一步接入 Host 私有发现模型、展示状态与原 Home 卡片/详情，验证范围、权限、卸载和代次；尚无真实平台插件实现私人服务，不能把契约测试宣称为服务完成。平台登录迁移、Phase 6 整体及最终 Legacy 删除仍未完成。

### 后续增量：私人发现 Host 模型及权限生命周期

MusicHub / OriginalUiMusicAdapter 已接入两个独立私人模型，复用共享实例范围、代次、取消和分区游标；普通推荐与分类模型互不混用。Registry 当前会话能力变化/退休使旧行、在途请求与缓冲游标失效；仅激活的私人模型按最新范围刷新，关闭后不继续后台查询。展示只保留安全角色、Host 错误分区状态和私有 key；播放/入队/收藏使用原 Coordinator / Router 并验证真实完整项。

主程序构建及 7 项相关契约/模型/注册/外部安装回归通过，含混合不支持实例续页、空/无权限/失败状态、旧行拒绝、迟到取消与会话退休。下一项生产 Home 卡片和详情迁移；真实私人服务尚无插件实现，其余平台账号 UI 和最终删除阶段仍待后续，不改变最终设计基线或 SDK v2 ABI。

### 后续增量：首页私人卡片与详情已接入 Source

Home 私人漫游/雷达已复用独立发现模型与安全动作，保留原卡片和动画；首页不再含 MusicApi、旧队列 path/音质和 Legacy 收藏引用。缺扩展明确显示不支持，不使用 Random 或平台 fallback。生产 QML 测试覆盖原卡片/按钮、状态、游标/重试、能力拒绝、关闭、范围/Adapter 变化及回调重入，不向替换 Adapter 派发旧请求或清其私人模型。

首次全量回归暴露目录控制器对 lease / Busy 通知无条件刷新而循环取消新请求；已按真实包加载状态过滤通知，保留调用期安全租约，并增加目录边界回归。该问题不是私人服务未实现导致的正常不支持态。

修复后 macOS / Qt 6.11.1 的现有 build-phase3 全目标增量构建成功，全部 73 项 CTest 通过（123.13 秒）；之前两项目录失败已在完整重跑中消除。未完成从零构建、跨 OS/ABI、真实私人服务/音频/原生视觉验收。

这关闭的是 Host 入口迁移差距；当前 Local / Navidrome 未实现私人发现，Kugou / Netease Source 服务、认证 UI 及最终 Legacy 删除仍未完成。SDK v2 / Source 插件 ABI / Plugin UI API 不变，下一项补齐收藏页关注歌手已有 v2 分区的投影与 UI；历史可信恢复能力和实例失效继续按原计划验收。

## 10. 当前工作树注意事项

审计时发现的未提交内容如下，均不应被本次文档工作覆盖：

- 当前目录：`ThirdParty/qwindowkit` 有修改，`.ui-screenshots/` 未跟踪；
- 高级工作树：`CMakeLists.txt`、`cpp/AccountManager.cpp`、`cpp/LogManager.*` 有修改，并有 AppStoragePaths、macOS 校验和相关测试等未跟踪文件。

这些内容可能有价值，但与 96 个已提交的 v2 成果不是同一事实层级。Phase 0 必须先确认其所有者和目的，再决定独立提交、暂存还是放弃；不得通过复制整个工作树来集成架构。

## 11. 建议的确认点

建议确认上述实施顺序，尤其是以下三个决策后再进入编码：

1. 以 `codex/unified-media-bridge` 快进集成为唯一 v2 起点；
2. Plugin UI API 1.0 先于 Local/Kugou/Netease 的复杂管理 UI；
3. Legacy 删除严格延后到 Local、队列、播放和两个平台插件均通过迁移验收之后。

在这三个确认点获得认可前，不开始大规模代码改造。
