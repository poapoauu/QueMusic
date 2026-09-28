# Phase 3：Local Source Plugin 设计规格

> 状态：规格与实施计划已确认；实施中（Task 1–5 通知契约、Host 通用失效、受控 Scanner/资源纯工具、Plugin-owned 共享异步索引和 Local Plugin/Session Provider 已完成；动态加载集成、迁移、UI 与发行验收仍待实施）
> 设计基线：[最终架构方案](QueMusic-Plugin-Architecture-Final-Plan.md)
> 前置成果：[Phase 2 实施记录](phase-2-plugin-settings-ui-record.md)
> 代码核对起点：`b95543c`，分支 `codex/plugin-ui-api-v1`
> 日期：2026-09-27
> 实施计划：[Phase 3 任务计划](../superpowers/plans/2026-09-27-phase-3-local-source-plugin.md)

## 1. 目的与本次确认

将本地目录作为标准 Source Plugin 管理，让用户继续使用现有 QueMusic 的目录卡片、
曲目列表和播放交互，不再由 QML 枚举文件后构造媒体身份。
用户已确认初期采用“一目录一 SourceInstance”，不是“一插件一目录”：
一个 Local 插件可以创建多个实例，每个实例拥有独立配置、扫描状态和媒体引用。

本轮先写规格，再审阅实施计划；不在规格阶段修改产品代码、推送或合并。
不重写 Source SDK v2，不扩展其 enum、结构体或虚接口布局。

## 2. 当前实际实现与差距

| 实际代码 | 已有能力 | 本阶段处理 |
| --- | --- | --- |
| `cpp/FolderModel.*` | SQLite `folders` 的增加、重命名、删除和列表，不是音频扫描器 | 复用目录记录的迁移语义；不得将整个模型作为插件依赖 |
| `pages/FilePage.qml` | `localFolderModel` 列目录，`FolderListModel` 枚举文件；另有“我的文件夹”及 `songModel` 自建集合 | 本地目录分支切换至 Adapter；自建集合数据单独保留，不能误删 |
| `cpp/LocalLyricsReader.*` | 同名 LRC 优先、TagLib 内嵌歌词、歌词解析 | 抽取/复用非 QML 逻辑，插件返回 v2 payload |
| `cpp/CoverHelper.*` | 内嵌图片转换和本地 sidecar 封面查找，带 Host QML 对象及缓存状态 | 抽取纯文件/图片处理，插件不依赖 Host 对象 |
| `OriginalUiMusicAdapter` / `MusicHub` | 页面、源选择、opaque row、动作路由 | 增补通用 Directory 浏览与本地目录页展示接缝 |
| `sdk/source/v2` | 已有 Directory entity、Page/Playback/Settings Provider | 直接复用；不存在独立 Browse/Search/Metadata action enum |
| `main.qml` | 仍有 `playLocalSong` 与旧本地播放/元数据路径 | 本阶段新插件曲目不得进入该路径；历史路径的全面删除属于后续阶段 |

仓库当前未发现独立 `LocalMusicScanner`。最终方案中的此名称代表目标职责，
不能据此假定已有 scanner 可以直接移动。现有 v2/Plugin UI 成果保留。

## 3. 身份、配置与发行

- Package ID：`org.quemusic.source.local`；Source ID：`local`。
- Account ID：Host 分配的持久 UUID；实例 ID 遵循现有 `local/<accountId>` 规则。
  不强制固定 `local/default`，避免多个目录共享一个实例身份。
- 随发行包提供普通动态插件；通过 manifest、PluginManager、SourceRegistry
  的正常流程发现和创建，不注册静态“内建本地源”，不增加 Core 专属 Local API。
- 新配置保存后按现有启用语义创建实例。插件默认可用不等于自动创建空目录实例，
  也不等于覆盖用户已禁用的实例。
- 同目录不同实例允许独立存在；实例内部对 canonical 路径去重。
  多实例 Aggregate 不跨实例折叠媒体，保留各自身份与能力。
- 删除实例或从库中移除目录只删除配置/索引，不删除磁盘文件。

### Settings Schema

| 字段/动作 | 类型 | 默认与约束 |
| --- | --- | --- |
| `rootDirectory` | Directory | 必填，一个本地绝对目录；禁止网络 URL、相对路径和 URL userinfo |
| `recursive` | Boolean | `true`，是否扫描子目录 |
| `scanOnOpen` | Boolean | `true`；关闭时只检查根目录，首次浏览/显式重扫再建立索引 |
| `watchChanges` | Boolean | `false`；启用后监听并合并变更，不等同于后台无限轮询 |
| `ignoreDirectories` | Text | 空；按换行分隔的根目录相对路径，不采用可执行表达式或任意 glob |
| `rescan` | Settings Action | 只更新当前实例索引，不修改媒体文件，不要求删除确认 |

忽略路径规范化为目录相对路径；拒绝绝对路径、`..` 越界和空中间组件，
排除匹配目录及其后代。源插件验证文件系统可访问性，Host 负责 schema 类型验证。
普通 Schema 表单和现有 Settings Action 流程足够，不新增 Local Custom UI。
Local 无账号登录、无 Secret、不使用 AuthenticationRequired 表示目录权限问题。

## 4. 插件职责与契约

`plugins/local-source/` 中的 Plugin 对象实现 `IMusicSourcePluginV2` 与
`IPluginSettingsProviderV2`；Session 实现 `IMusicSourceSessionV2`、
`IPageProviderV2`、`IPlaybackProviderV2` 与 `ISettingsActionProviderV2`。

Plugin 提供 descriptor/schema 和会话工厂；Session 负责配置身份、请求生命周期、
索引快照和 v2 结果；Scanner 只处理输入目录并返回值对象；Metadata/Artwork/Lyrics
辅助代码只处理经过会话验证的文件，不访问 Host registry 或数据库单例。
优先抽取现有纯逻辑，不复制整套 Host QML/模型或新建第二份歌曲数据库。

Plugin 内部共享索引按完整实例身份与配置指纹隔离，持有不可变快照。
设置页的临时检测/动作 Session 与播放 Session 访问同一实例、同一配置的索引，
因此 `rescan` 不能只重扫一个随后被销毁的临时会话。相同目录的不同实例不共用
身份或 mutable 状态。共享服务归 Plugin 生命周期所有，worker 仍必须在最后
使用者退出/插件析构前回收，不能依赖全局静态对象延长运行。

Play、Artwork、Lyrics 使用既有 action/capability；Browse/Search/Metadata
由页面 Provider 和 `MediaItemV2` 表达。未实现 Favorite、Playlist、Download、
Scrobble、文件删除等行为明确为 Unsupported，不能仅凭 UI 有按钮就宣布支持。
目录可 Browse、不可 Play；Track 的能力还取决于当前文件是否有效可读。

### 媒体身份与路径边界

`MediaRefV2.sourcePluginId` 虽然名称保留 Plugin 字样，值必须是 `local`，
不是 package ID；`sourceInstanceId` 和 `accountId` 来自会话配置。
初期 Track/Directory `entityId` 使用 canonical 路径生成的 fully-encoded file URL。
同路径重扫不改变 ID；移动文件产生新 ID，旧引用不可用，不承诺跨移动的 UUID 身份。

Provider 校验完整实例身份、实体类型、索引归属、根目录包含关系及文件可读性。
禁止用字符串公共前缀判断包含关系；处理路径分隔符、编码、平台大小写及符号链接。
根目录本身可由用户选择的链接规范化；扫描不递归跟随目录链接，文件链接只允许
canonical 目标位于根内，避免循环与越界。封面、歌词 sidecar 也受同一根目录约束。
流解析前重新校验，以防扫描后删除、替换或链接目标变化；不能信任 QML 提交的 path。

## 5. 扫描、快照与错误

1. `open()` 返回 request ID 并符合 v2 的 requestStarted/终态协议；根目录不存在、
   不可读时报告稳定错误键，不报告认证失败、不回退到 Legacy Local。
2. 文件枚举与标签读取不在 GUI 线程批量执行。扫描输入为配置快照，输出按
   canonical URL 确定性排序和去重的索引快照；GUI 线程只提交当前 generation。
3. 有效音频使用 TagLib 元数据；缺失标签可回退文件名。扩展名候选集不等于实际
   解码能力，沿用现有格式选择兼容范围，实际播放失败由播放器 Core 报告。
4. 空目录返回 Empty。坏标签/单个不可读文件不导致整库崩溃；能保留的条目给出
   降级信息，不能读取的文件跳过；子目录访问失败产生汇总警告而非原始路径日志。
5. 重扫完整建立新快照后替换旧快照；根目录失败不发布“成功的空库”。删除文件
   从新快照消失，旧媒体请求返回 NotFound/Unavailable，不通过旧 URL 继续播放。
6. Watcher 对增删改事件去抖；同时至多一个扫描，扫描中变更触发一次后续扫描。
   目录被删再创建、watch 注册失败和大量子目录不得静默假装监听成功。
   索引变化必须使相应 Host 页面缓存失效。不得通过伪造 capability 变化实现数据刷新。
7. 关闭、取消、修改目录、禁用/删实例、卸载重载均使旧 generation 失效。
   会话析构前必须停止/回收 worker 与 watcher，不能让插件线程在 lease 释放后执行。

### 独立版本化的可选内容变化通知

现有 v2 Session 没有库内容变化事件，不能把“监听文件变化”当作“UI 已自动更新”。
建议新增可选扩展 `ISourceContentEventsProviderV1`，IID
`org.quemusic.source.ContentEventsProvider/1.0`，独立于冻结的 v2 接口：
`contentEvents() const` 返回 Session 所有的公开 `SourceContentEventsV1*` QObject，
不得返回其他实例对象；该对象提供 `contentChanged(quint64 revision)` 与
`refreshFailed(SourceErrorV2 error)` 信号。不在现有 Session 基类追加方法或信号。

revision 按实例配置 generation 单调递增；切换配置/Session 后重新绑定并拒绝
旧对象和旧 revision。成功发布快照后，插件向同配置的活跃 Session 发出变化事件。
后台 watcher 错误通过 refreshFailed 的稳定错误键报告，不能伪造未启动请求的终态。
SourceRegistry 在 lease 内检测可选接口，转为 Host 私有通用实例内容变化通知；
PageRepository/MusicHub 使匹配实例缓存和在途请求失效，刷新当前可见页。
没有可选接口的旧插件保持现有行为；关闭时先断开订阅再销毁 Session。
此扩展随公开 SDK 独立安装、加 contract/外部插件测试，不引入 Local 专属 Host 分支。

## 6. 页面查询与 Original UI 接入

复用 v2 `Category` / `Search` 页面，不新增 Local page enum。
目录实体通过既有 PageSection 携带 Directory items 与目录布局提示；
目录下歌曲查询使用 `filters.directoryId`，搜索按 title、artists、album、文件名
匹配当前实例索引。查询的 scope 必须匹配会话；分页 cursor 绑定配置/索引 generation，
重扫后的旧 cursor 被拒绝并要求刷新，不能跳页、重复追加或访问另一实例。

`MusicHub::browse` 和 Adapter 的 Browse 判断扩展到通用 Directory entity；
它们只依据实体类型和 Provider/身份处理，不比较 package ID、Source ID 或本地路径。
为 FilePage 提供实例目录列表、目录内容、加载/空/失败、返回导航和刷新入口；
展示行继续采用 opaque key，完整 MediaRef 由 Adapter 私有持有。
不借用或覆盖正在显示的在线 Category 模型，避免跨页面串数据。

本地目录分支保留卡片、歌曲列表、选择、返回、主题和基本布局。
“导入目录”转向 Host 的通用实例设置入口，并由插件 schema 决定配置；
rename/移除对应实例配置操作，QML 不再直接操作 `localFolderModel`、
`FolderListModel` 或拼接播放文件路径。没有 Local 插件时显示不可用，不能回退旧模型。
“在文件夹中显示”如保留，必须使用经过 Host 校验的展示动作，不能把 identity
当作任意本地文件打开权限。

新插件曲目的 Play/Enqueue 直接复用 Adapter → PlaybackCoordinator → PlaybackSink，
Local Plugin 只返回 `StreamDescriptorV2(file://)`，不创建 QMediaPlayer。
这是新数据路径的必要接入，不是本阶段重写队列或宣布 Phase 5 已完成。
旧队列/历史持久化迁移属于 Phase 4，其他 Legacy 播放入口移除属于 Phase 5/10。

Artwork 优先内嵌封面、再查同名/常见 sidecar；Lyrics 优先 LRC、再读内嵌歌词。
返回现有 v2 consumer 能识别的 payload，缓存只存可重建资源；缺失歌词返回
`lyrics: QString("")`，缺失封面返回该资源请求的 NotFound，Host 显示默认封面，
不使可播放曲目失败。不利用 stream URL 或任意 metadata 旁路返回私密路径数据。

实际 `MediaAssetRepository` 契约为 `ActionResultV2` 的 action/subject 精确匹配请求：
Artwork payload 是 `bytes: QByteArray` 与 `mimeType: QString`，Lyrics payload 是
`lyrics: QString`。现有 LocalLyricsReader 的展示 `QVariantList` 不能原样当作歌词
payload；抽取文本读取/必要的 LRC 序列化，复用已有解析测试。Host ArtworkCache
负责生成展示资源，插件不直接返回 CoverHelper 私有缓存 URL。

## 7. 旧数据与阶段完成边界

旧 `folders(type=local)` 通过一次性、可重复安全执行的 Host 迁移器导入实例配置：
保留名称、规范化路径、记录 old record → new instance 映射；全部写入成功前不记录
迁移完成。不删除旧表或源文件；失败可重试，已迁移或用户后来删除的实例不被自动复活。
不存在的旧目录保留可修复状态并告知用户，不扫描其他猜测目录。
迁移器属于历史数据兼容，不得成为每次浏览/播放的 Local 专属运行路径。

“我的文件夹”和 `songModel` 是自建集合，不是单个目录配置，不能强行转换为一个
rootDirectory，也不能静默丢弃目录外歌曲。本规格的首个交付只迁移本地目录分支；
自建集合的旧记录与界面暂时保持，不新增依赖它们的 Local Plugin 能力。

**这一收敛是实施拆分，不是修改最终目标：**

- Phase 3-A：插件、schema、索引、Provider、身份与生命周期闭环。
- Phase 3-B：本地目录分支改为 Adapter 数据并完成非破坏性目录迁移。
- 自建集合需要随 MediaRef 持久化/集合适配一起迁移；必须在随后计划中列出，
  最迟 Phase 6 清除 FilePage 的直接业务模型依赖。
- 3-A/3-B 可分别验收，但仍有自建集合 Legacy 依赖时，不得声明已满足最终方案
  “整个 FilePage 完全从 OriginalUiMusicAdapter 获取本地音乐”的出口条件。

此拆分需在规格审阅中确认；不得长期保留 Legacy Local 特殊路径。

## 8. 主要文件与兼容风险

预计新增 `plugins/local-source/` 下的 CMake、manifest、Plugin、Session、Scanner
和元数据辅助代码，以及 scanner/provider/registry/adapter/QML/迁移/打包测试。
预计修改根 CMake、通用 MusicHub/Adapter/缓存刷新接缝、FilePage 本地目录分支、
实例设置导航与发行包复制/安装规则；纯歌词/封面辅助函数按依赖抽取到非 UI 模块。

不直接移动整个 FolderModel/CoverHelper QObject，也不在第一步删除仍被 Legacy
调用的代码。全面删除以调用方迁移和回归测试为前提，不因为文件名含 Local 就删除。

风险包括：旧目录编码/大小写重复、修改根目录使已有引用失效、TagLib 资源与版本、
worker/插件卸载竞争、扫描期间文件变化、缓存/cursor 不一致、发行包漏带插件。
Source SDK v2 IID 保持 `org.quemusic.MusicSourcePlugin/2.0`、ABI 保持 `2`；
Plugin UI API 继续独立版本化，Local 本阶段不需要发布私有 QML UI。

## 9. 测试与验收

| 范围 | 必须验证的行为 |
| --- | --- |
| 扫描/配置 | 单目录、多个目录实例、递归开关、忽略目录、重复文件、重扫稳定身份、空库、删除/移动、根目录及子目录权限失败 |
| 路径边界 | 编码/中文/空格、相邻前缀目录、越界 `..`、目录链接循环、文件链接越界、扫描后链接变化、伪造媒体引用 |
| 元数据 | 缺失/坏标签、时长/艺术家/专辑、内嵌/sidecar 封面、LRC/内嵌歌词、缺资源不阻止播放 |
| Provider | requestStarted 在终态前、请求取消、配置/扫描 generation、scope/分页隔离、旧 cursor 被拒绝、未实现能力 Unsupported |
| 实例/插件 | 正常 registry 创建/启停/删除/重载，多实例独立，worker/watcher 先销毁再释放 lease，迟到结果无副作用 |
| UI/缓存 | Adapter 目录导航、曲目 opaque key、在线页面不受影响、监听后页面缓存刷新、无 Legacy fallback、原布局/主题/基本操作回归 |
| 播放 | 新本地曲目经过已有 Coordinator 和 fake Sink 验证 file URL，实例禁用/文件消失失败，不新增 QML 直播放 |
| 数据迁移 | 幂等、部分失败重试、目录失效可修复、用户删除不复活、旧集合/源文件不删除 |
| 发行/兼容 | build-tree 与安装包动态发现、Qt/compiler/build key 校验、macOS bundle，Source SDK v2 无布局 diff，全量构建/CTest |

采用临时目录、可生成的小型音频/标签 fixture 和 fake playback sink，不扫描用户真实
音乐库、不删除真实音频、不连接外部平台。权限测试不能依赖 root 环境下无效的 chmod；
不支持的权限模拟必须明确标注，不能伪称覆盖。Windows/Linux 未运行则不报告已通过。
Phase 2 的 51 项结果只是历史基线；编码阶段必须重新构建并运行新增及全量测试。

## 10. 文档审阅之后

先确认本规格（包括第 7 节交付拆分），再编写详细实施计划：固定真实测试基线，
落实第 5 节可选通知扩展与 Host 通用数据失效接缝、拆分任务/文件、列出逐任务
RED→GREEN 与出口测试。
书面规格确认不等于尚未形成的实施计划已获批准，不在本轮进入编码。
