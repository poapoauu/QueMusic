# Phase 4 Queue / History Identity 设计规格

日期：2026-09-29。设计基线：[最终架构方案](../../architecture/QueMusic-Plugin-Architecture-Final-Plan.md) Phase 4、[当前差距分析](../../architecture/current-state-gap-analysis.md) 和 [Phase 3 验收记录](../../architecture/phase-3-local-source-plugin-record.md)。本规格只定义 Phase 4，不宣布整个最终架构完成。

## 1. 目标、现状与边界

用户希望 Local 与 Navidrome 可共存于 QueMusic 的统一队列：关闭并重启后仍保留各自稳定身份和展示顺序；Host 统一记录“最近播放”。尽量保留现有队列和播放器 UI，不新增历史页面，不自动播放恢复项。Source Plugin 解析媒体资源，Core 实际播放；不修改 Source SDK v2 的布局、虚函数表或既有 ABI。Plugin 与 SourceInstance 身份继续区分。

当前 `PlaybackCoordinator` 的 `MediaRefV2` 队列仅在内存中，且其公开 `queue()` 映射不包含恢复所需的全部受控状态；`main.qml` 的旧 `playListModel` 仍保存 `name/path/songer/source`。当前仓库没有已发现的生产旧队列 JSON 持久化，也没有播放历史存储。更重要的是，Phase 3 的 Local `entityId` 是 canonical 路径编码成的 `file://` URL；若直接持久化它，就违反本阶段“不以路径作为 Host 持久身份”的目标。

Phase 4 先完成 Local 不透明 ID，再实现 Host 队列/历史存储和恢复。旧播放分支全面移除、所有播放强制走 Coordinator 属于 Phase 5；整个 QML 来源动态化属于 Phase 6。本阶段只修复恢复队列与现有播放器的必要交互，不重写 UI，也不新增认证、账号或来源专属配置 UI。

## 2. 设计选择与组件边界

采用 Host 侧持久化层，围绕现有 `PlaybackCoordinator` 增加受控的导出/恢复入口和统一最近播放记录。Store 负责版本化、安全序列化、原子写入和旧数据导入；Coordinator 负责队列顺序、每次入队的 occurrence 身份、播放时的当前实例/能力校验。Store 不解析流，不调用平台 API；Source Plugin 不拥有跨来源队列。

不采用直接序列化旧 QML `ListModel`，因为 `path/hash/source` 整数不能唯一确定 SourceInstance；也不采用各插件分别保存队列，因为它们无法共同维护跨来源顺序。`IPlayQueueProviderV2` 等插件能力不被重新解释为 Host 的统一队列存储。

### 2.1 Local 不透明身份前置条件

Local Plugin 私有索引为每个 SourceInstance 下的 Track 和 Directory 建立 `canonical path → opaque UUID` 映射，并持久化于该插件的私有应用数据。相同实例、相同路径的重扫和重启保持 ID；不同实例即使指向同一路径也拥有独立命名空间。文件移动仍按 Phase 3 约定视为新条目，不承诺跨移动跟踪。已发布的 `MediaItemV2.ref.entityId`、目录查询 ID 和流解析输入只使用不透明 ID；canonical 路径只留在插件内部索引和受控流解析环节。

扫描结果发布前必须持久化新映射；写入失败不能发布只能在当前进程有效的 ID。解析 ID 时先确认它属于当前实例和当前扫描快照，再执行既有根目录、链接、可读性校验。删除、实例禁用或索引丢失时旧 ID 失效，不得重新绑定到同名文件或另一实例。普通浏览/播放入口不继续接受路径型 ID。旧 `file://` Local 引用不能由 Host 猜测或通过来源专属分支转换；本阶段将它归入不可自动迁移的旧数据并备份告知。Local 身份格式切换时，已缓存的旧路径型 Page/Artwork/Lyrics 引用必须失效，不能被重新发布或带入新队列。

### 2.2 Host 持久格式

Host 在应用数据目录保存一个 `schemaVersion: 1` 的 JSON 快照，由 `QSaveFile` 原子替换。根对象含 `queue` 和 `history`：

- 队列条目：独立 `occurrenceId`、完整 `MediaRefV2` 五元组、有限展示快照（标题、歌手、专辑、时长）及入队时“可播放”布尔提示。重复歌曲保留不同 occurrence，不按 `entityId` 去重。队列上限 1000 项。
- 历史条目：同样的 `MediaRefV2` 与展示快照、UTC `playedAt`；按成功开始播放的事件倒序保留最近 200 条，同一曲目多次播放可以出现多条。不保存流 URL、播放位置、令牌、任意插件 metadata 或完整 `availableActions`/constraints。
- 展示文本单字段最多 512 个 Unicode 字符，歌手最多 16 个；身份字段单个最多 1024 个字符，JSON 文件最多 16 MiB，字段采用白名单解析。Host 拒绝把直接媒体 locator（例如 `file://`、HTTP 流 URL 或绝对路径）写入持久 `entityId`，并保留上一份有效快照。`MediaRefV2.sourcePluginId` 沿用 v2 字段名，值仍是 Source ID，而非插件 package ID。

Coordinator 的持久导出接口不能简单复用公开 `queue()`，恢复接口也不能把公开映射重新送入 `enqueue()`：现有公开映射缺少内部动作状态，且恢复不应创造新 occurrence 或触发播放。恢复须验证字段、保留顺序/occurrence、重建受控的播放提示，并在真正播放时重新检查 SourceRegistry 的实例归属、当前 capability 和插件解析结果。旧的动作提示从不单独授予播放权。

## 3. 生命周期与可见行为

启动时先读取并验证数据，再一次性恢复 Coordinator 队列；不设置正在播放的 generation，不自动触发媒体解析。有效引用即使对应插件尚未安装、实例被禁用、账号被移除或暂时离线，也保留队列位置和展示快照，并标示为“暂不可播放”。用户尝试播放时，必须重新匹配原 `sourceInstanceId/sourcePluginId/accountId`；不能根据显示名或平台整数改绑。来源恢复后可重试。失败不删除队列项，也不写入历史。

队列变化触发持久化；写入失败向 Host 报告可见、可重试的错误，不覆盖已有好文件，内存队列继续可用。只有真实 `playbackStarted` 与当前 generation/occurrence 匹配后，才追加最近播放。恢复、单纯入队、解析失败和过期回调都不计入；暂停/继续同一播放不重复追加。历史先提供 Host 内部只读数据能力，不增加新页面。

当前 `main.qml` 把“Coordinator 队列非空”等同于 secure 播放上下文。恢复队列将打破这个假设：非空但未播放的恢复队列不能接管正在运行的 Legacy 播放控制。现有队列弹窗只需按当前播放上下文或明确选择显示对应列表；用户从恢复队列选择曲目时再进入 Coordinator 播放上下文。保留现有外观和交互，其余旧播放删除留给 Phase 5。

## 4. 旧数据、损坏数据与告知

当前生产旧 QML 队列仅在内存中，因此不伪造一个并不存在的旧 JSON 迁移来源，也不在启动时扫描猜测文件名。提供针对显式旧 JSON 输入的一次性导入逻辑及测试；只有发现确定的历史存储位置或用户明确导入时才调用。迁移器只转换已含完整、非路径型 `MediaRefV2` 且能无歧义确定 SourceInstance 的条目。不足以确定实例的 `path/hash/source-int` 条目（包括旧 Local `file://` 引用）不猜测、不静默丢弃：保留原始文件备份，并向用户提示未迁移数量及备份位置。一次成功导入后记录迁移版本，避免重复导入。今后如需旧 Local 路径的自动转换，应设计独立、通用且版本化的迁移扩展，不在 Host 加 `local` 专属分支。

当前版本文件损坏或超限时，先把原始字节复制到应用数据目录中唯一命名的隔离备份；只有备份成功才允许随后写入新快照，备份失败则保持只读。版本高于当前程序支持范围时，原文件保持原位且 Store 只读，不尝试降级解析或覆盖。上述情况均以空的 Host 队列/历史安全启动并提供恢复提示。隔离备份可能含旧路径，只能留在私有应用数据中，不写日志、不作为活动队列读取。插件缺失只影响条目的可播放状态，不等于文件损坏；原始队列仍可恢复。

## 5. 验收矩阵

1. Local 和 Navidrome 各有至少一项、同歌重复 occurrence 的队列写入后重启：顺序、完整五元组、展示快照和不同 occurrence 均保持；不自动播放，Host 队列文件不持久化 URL/令牌/路径。Local 插件私有索引中的受控路径不属于 Host 队列格式。
2. Local 同路径重扫/重启 ID 稳定；不同实例不串号；移动、删除、索引丢失和伪造 ID 不绑定到其他文件；旧路径型缓存不再发布；目录浏览和流解析继续遵守根目录边界。
3. 实例禁用、删除、插件缺失、账号变化或网络暂时不可用时，队列项仍可见且安全失败；原实例恢复后可重试；失败、恢复和仅入队不产生历史。
4. 真正开始播放后写入一次历史；迟到 generation、重复信号和暂停/继续不重复；200 条上限与重启 round-trip 可验证。
5. 旧格式仅无歧义转换；无法转换者保留原始备份并提示。损坏/未来版本/超限文件、模拟写入失败均不破坏上一有效快照或使应用崩溃。
6. QML 回归：恢复的非空 Host 队列不夺取 Legacy 正在播放的控制；用户选择恢复项后才进入 Coordinator；原队列视觉和交互不发生非必要变化。
7. 全量构建/CTest、平台相关文件存储测试及 `git diff <Phase-3-baseline> -- sdk/source/v2` 无差异。跨平台发行矩阵必须单独验证，不能以本机通过代替。

## 6. 阶段出口与未包含事项

出口是“Local + Navidrome 混合队列可在重启后以稳定、无路径的 MediaRef 恢复；缺失来源安全可见；Host 最近播放使用同一身份，且只记录真实开始播放”。Phase 5 继续删除本地和其他 Legacy 直接播放路径；Phase 6 完成所有页面的动态 Source 化；自建集合及收藏持久身份按最终架构后续阶段处理。此规格不把这几项提前标为完成。
