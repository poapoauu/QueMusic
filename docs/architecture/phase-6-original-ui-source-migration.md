# Phase 6：原 UI 动态 Source 迁移记录

设计基线：[最终架构方案](QueMusic-Plugin-Architecture-Final-Plan.md)。原则是保留原页面视觉和主要交互，让页面只使用 SourceInstance ID、Adapter 展示模型和 Capability 驱动动作；不把平台特例放回 Host，也不更改 Source SDK v2。

## 当前切片：搜索页

`main.qml` 的搜索框回车、搜索按钮、搜索历史三个入口统一调用 `OriginalUiMusicAdapter.search()`，不再回退 `MusicApi.searchSongs()`。`SearchPage.qml` 保留四个结果标签、列表布局和插件结果详情层；来源下拉框使用 `sourceOptions` 与 `selectedSourceInstanceId`，不再内置酷狗/网易等固定选项或按平台序号着色。搜索结果、翻页、重试、浏览、播放、入队和收藏都通过 Adapter；动作前读取对应行的 Capability。无 Adapter 时不伪造平台列表或发起旧 API 请求。

QML 回归覆盖四类列表绑定、切换搜索标签、实例选择、动态加入第三方实例、禁用实例拒绝选择、无能力歌曲不可播放/收藏、详情浏览、翻页与重试；静态检查确保该页不再引用 `MusicApi` 或旧路径队列。此处的源实例动态性不代表其他页面已迁移。

## 当前切片：Home 来源、每日推荐与分类入口

Home 的来源下拉框改为只显示运行时 `sourceOptions`，按 SourceInstance ID 选择，主题色不再依赖平台序号。页面激活推荐和分类两个 Adapter 页面，以便分类条能取得真实插件数据；每日推荐卡片、列表播放/入队/收藏及分类条浏览均使用 Adapter，并在触发动作前检查对应 Capability。时间问候不再被旧平台加载分支挡住。测试覆盖新增第三方实例、禁用实例不可选、不可播放/入队的推荐项不会产生动作，以及分类展示身份和详情导航。

在来源与推荐切片完成时，“上次听到”、私人漫游/雷达、热门歌单及其旧详情弹窗仍依赖 `MusicApi`/平台整数；后续按可用的可信数据逐项迁移，不能把旧路径或平台 ID 伪装成插件 ref。

“上次听到”卡片随后改读 Host 的 `QueueHistoryStore.latest`：仅暴露标题、作者等展示快照和可重播状态，不向 QML 暴露 ref、路径或流地址。同一个历史 occurrence 仍在统一队列且来源可用时，`playLatest()` 才调用 Coordinator 的 `playQueueEntry()`；条目被移除或实例被禁用时保留展示但拒绝重播。原加入旧路径队列按钮改为统一队列入口，不能从历史快照伪造新的插件动作。卡片封面暂用默认图，因为历史快照没有可安全重建的插件 artwork。`main.qml` 的旧 `Options.lastSongs` 写入暂为其他 legacy 功能兼容保留，Home 已不读取。测试覆盖重播、禁用/移除后的拒绝、展示属性脱敏和 QML 不调用旧 `MusicApi.getMusicInfo()`。

因此 Home 尚未迁移的区域缩小为私人漫游/雷达、热门歌单及其旧详情弹窗；这些区域仍不能算动态 Source。

“我的收藏歌单”卡片现从 Adapter 的 `favoriteLists` 显示已加载数量，并激活收藏页；未把分页中的可见数量误称为全部收藏数。分类详情弹窗删除已不可达的 `MusicApi.musicPlaylists` 回退，统一使用 Adapter 的分类结果与 `canBrowse` 检查。私人漫游/雷达和热门歌单仍使用旧平台 API，不能把分类结果直接伪装成个性化推荐或热门榜单。

## 当前切片：收藏页去除旧平台回退

`FavouritePage.qml` 的歌曲、歌单与歌单详情列表现均只接收 Adapter 展示模型；播放、入队、取消收藏、浏览、分页、重试和批量动作仍经 Capability 与 Adapter。删除旧 `MusicApi` 请求、平台整数、路径队列写入及其专属详情弹窗。Adapter 缺失时显示空态，不重新读取旧收藏表；旧收藏数据未删除，但尚未认领为插件媒体项，不能把旧 ID 猜成 Source ref。原四标签、选中态、批量操作区和详情布局保留；“关注歌手”“历史记录”原本就是占位标签，仍未宣称完成插件数据接入。QML 回归覆盖 Adapter 为空时的无回退，以及歌曲/歌单/详情动作。

## 当前切片：分类页来源和通用动作

`PlaylistPage.qml` 的来源下拉框现只显示 Adapter 的运行时 `sourceOptions`，不再提供固定平台名称、序号配色或 `songSource` 写入回退。分类页工具栏的刷新经新增的 Host `OriginalUiMusicAdapter.refreshPage()` 转交 `MusicHub.refresh()`；这是 Adapter 的增量接口，不改 Source SDK v2。已接入 Adapter 的歌曲、分类/歌单和详情列表在播放、入队、收藏、浏览前检查逐行 Capability；不支持的操作隐藏或拒绝，Adapter 模式不再创建旧平台的分类菜单模型。测试覆盖动态第三方实例与禁用实例、真实来源刷新请求、无能力行拒绝动作以及原详情入队按钮的视觉/点击兼容。

歌曲标签页现已删除固定地区枚举，分类条读取插件返回的 Genre 展示项，并通过 Capability 与 Adapter 浏览原始展示身份；横向滚动保留圆角标签交互。歌曲列表无 Adapter 时为空，不再请求旧新歌 API、解析 hash 音质或写入路径队列，也不显示旧下载菜单。Genre 不存在时分类条为空，不伪造地区分类。

歌单标签页现按插件媒体实体类型筛出 Playlist，保留原列表布局，经展示身份进入 Adapter 详情并在有 Capability 时收藏。由于展示数据没有可信曲目数，该标签不显示原“曲目”计数列。删除旧歌单分类菜单、平台歌单请求、分页、路径身份收藏和旧详情入口；无 Adapter 时该标签为空。歌单、Genre 仍共用分类页底层模型，筛选只作用于该标签的展示，不改变 Source SDK v2。

歌手标签页现从插件分类模型筛出 Artist，保留圆形卡片和更多按钮，删除平台地区枚举、歌手列表/歌曲请求和旧详情分页分支。浏览歌手时使用插件展示身份，详情列表支持继续浏览专辑，再播放具备 Capability 的歌曲。关闭分类详情时，经 Adapter 的 `closeCategoryBrowse()` 清空 MusicHub 分类导航栈并只刷新一次分类首页，避免歌手网格停留在歌曲结果而变空。新增的是 Host API；Source SDK v2 和 Plugin UI API 无变更。测试覆盖混合实体筛选、歌手/专辑/歌曲动作身份、不可浏览行拒绝、更多请求、关闭恢复分类根查询以及空依赖。

排行榜标签页现保留卡片网格，通过插件的 Playlist 展示身份浏览与分页；只有 Playlist 且 `metadata.collectionKind="chart"` 才识别为榜单，普通歌单、Genre、推荐或高评分专辑不会显示为排行榜。Adapter 只投影经过白名单验证的 `collectionKind` 字符串，原 metadata/ref 不暴露；缓存的实时、内存和磁盘路径保留该已知标记并丢弃未知值。普通歌单标签排除已标记榜单。无标记数据时显示“暂无排行榜”，未将空结果推断成插件不支持榜单。Navidrome 和 Local 当前均不提供公共榜单，未添加平台排行榜业务实现。

分类首页增加现有 v2 的标准 Playlists 查询，让插件歌单数据可以实际进入分类页，原四种查询变为五种；不支持该查询的插件按现有 Unsupported/部分成功机制处理。Source SDK v2 仅注明一个可选 metadata 约定，没有新增结构字段、虚函数或枚举值，ABI 不变；旧 Host 会忽略这个标记，旧缓存需正常刷新后才能得到新标记。榜单条目仍须使用插件可浏览的 Playlist ref，由插件负责解析该身份。已有插件无需改动即可继续使用普通歌单。

分类页删除旧榜单请求、固定平台徽标和旧 `PlayListWindow`，页内不再引用 `MusicApi` 或路径队列。测试覆盖标准 Playlists 查询、白名单标记缓存重启、Adapter 仅将 Playlist 标记为榜单、榜单/普通歌单分流、不可浏览行拒绝、原始展示身份与分页，以及页面级无旧入口检查。分类页仍使用共享 Adapter 分类模型，歌曲标签的独立实体筛选、详情逐级返回、加载/错误状态和实际音频验收继续完善。

## 后续顺序与验收

1. Home 剩余旧卡片：为私人漫游/雷达与热门歌单建立通用展示/动作来源后再删除对应 `MusicApi` 分支，保持原卡片视觉；未来若要从已移出队列的历史记录重新播放，应增加可信插件媒体项查找能力，不能猜造动作。
2. Playlist/Category：逐页删除旧平台枚举及路径队列写入；用 Adapter 页面模型、动作和详情导航替代。收藏页已完成页面级去回退，但关注歌手、历史占位与更广泛实例失效验收仍待补齐。测试分页、错误/空态、收藏/取消收藏及实例失效。
3. File/Download、Queue、Player 和桌面歌词：清理剩余直接音源分支，确保实际播放只由 Coordinator 发起；保留原控件和布局。验证来源标签、切歌、停播、恢复和卸载后的展示状态。
4. 全量扫描及 UI smoke：页面新增一个 Source Plugin 不需要改 source switch/case；无权限动作不显示或不可触发；完整构建、CTest 和 macOS 手工视觉/音频检查通过。

Kugou、Netease 业务及账号/登录 UI 的插件化属于后续阶段。在其 Source Plugin 可用前，不用 Host 的旧平台 fallback 冒充动态 Source；其他页面的 legacy 分支按上述顺序逐项迁移，不能一次性删掉仍在使用的代码。
