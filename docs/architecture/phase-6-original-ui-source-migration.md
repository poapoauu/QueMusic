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

分类页删除旧榜单请求、固定平台徽标和旧 `PlayListWindow`，页内不再引用 `MusicApi` 或路径队列。测试覆盖标准 Playlists 查询、白名单标记缓存重启、Adapter 仅将 Playlist 标记为榜单、榜单/普通歌单分流、不可浏览行拒绝、原始展示身份与分页，以及页面级无旧入口检查。

2026-10-08：歌曲标签改绑 Adapter 的 `categorySongs`，只投影 Track，沿用 `categoryItems` 中相同的展示动作身份，不复制或猜造 Source ref。分页使用 Tracks 分区的 sectionId、hasMore、loadingMore，加载中和无下一页时不重复请求；失败可重试当前分区。详情增加“返回上级”，经 `categoryBack()` 复用 MusicHub 现有导航栈，返回父级恢复安全标题与封面，返回分类根则关闭详情。只新增 Host Adapter 属性和方法，不改变 Source SDK v2 或 Plugin UI API。

分类根及详情增加通用加载、失败、空态与重试入口。QML 状态文案只使用 Host 的状态枚举与错误布尔值，不呈现插件原始诊断；Unsupported-only 查询按空态处理，有网络错误仍可重试。共享列表同时修正时长：Adapter 将 SDK 的毫秒转换为原列表约定的秒，文本采用持续绑定并补齐两位秒数，复用行时随数据更新。

本切片主程序构建与五个测试套件通过：OriginalUiMusicAdapter、OriginalUiRecommendationQml、OriginalUiActionsQml、MusicHubQml、OriginalUiStructure。新增验收覆盖混合实体歌曲筛选与身份复用、真实 Host 导航栈、Unsupported/网络失败区分、歌曲定向分页/重试、详情逐级返回、无 Adapter 空态和列表时长更新。此切片完成时分区状态仍沿用首个匹配分区投影，随后继续修正如下。

分类歌曲和详情的分页状态现聚合全部匹配分区，不再由首个分区是否耗尽决定整个列表的状态。Host 展示模型新增只读 `paginationSectionIds` / `retrySectionIds`：前者只包含有游标、可继续且未加载/失败的分区，后者只包含未加载且有非 Unsupported 错误的分区。QML 对一次触发时的分区 ID 快照分别调用现有 `loadMore(1, id)` / `retry(1, id)`；MusicHub 保持原有每分区请求去重与 generation fencing。一个分区加载中不阻塞其他就绪分区。聚合错误仅提供 `failed` 布尔标记，不携带游标、实例身份或诊断文本；Unsupported 即使保留旧游标也不继续翻页或反复重试。

共享 QListView 增加默认开启的旧加载状态兼容开关，分类歌曲和详情明确关闭它，避免旧 MusicApi 的加载状态阻塞插件分页；其他尚未迁移页面保持原行为。本次没有改 Source SDK v2、Plugin UI API 或插件 ABI，只增量扩展 Host 展示模型。原 `sectionId` 属性保留，分类页仍兼容只提供单分区属性的旧 Host 模型；新 ID 列表为空时不会退回首个分区发起请求。

新增测试覆盖首个分区耗尽但后续分区可翻页、Tracks 与 Album 分区隔离、多个就绪分区定向请求、加载/失败分区跳过、重试期间重复触发拒绝、Unsupported 残留游标、错误脱敏及模型重置清理。分类页的歌曲/详情已使用该聚合契约；歌手/歌单/榜单网格仍使用原通用“更多”入口，Search/Favourite 等页面的分区状态未在本轮扩展。真实服务器多实例音频、实例卸载和 macOS 手工视觉验收仍待完成，不能宣称整个 Phase 6 完成。

### 分类歌手、歌单与榜单的定向分页

Host Adapter 新增稳定展示模型 `categoryArtists` / `categoryPlaylists` / `categoryCharts`，分别投影 Artist、普通 Playlist、明确标记的榜单 Playlist，复用 `categoryItems` 中相同的展示动作身份。歌手状态只聚合 Artists 分区；普通歌单与榜单状态都聚合标准 Playlists 分区。二者共用插件查询和游标：`collectionKind="chart"` 只是展示分类，不代表独立榜单查询，未新增 SDK 枚举、过滤协议或平台特例。

分类歌单列表、歌手网格和榜单网格删除通用 `loadMore(1, "")` 入口，改为对当前模型的可分页/可重试 ID 快照发起现有 Host 请求。歌单列表使用通用分区错误页脚；网格保留原“更多”布局并增加分区重试按钮，无游标时隐藏更多，加载中显示等待文案，有其他就绪分区时仍可继续请求。分类歌单列表也关闭旧 MusicApi 加载状态兼容开关。三个标签使用各自的空态；普通歌单/榜单投影暂为空但 Playlists 查询仍有游标时保留继续加载入口，避免首批未出现目标实体时无法翻页。完整分类刷新期间不允许这些控件发起分页/分区重试。

测试覆盖投影实体筛选与原始展示身份、Artists/Playlists 分区隔离、普通歌单/榜单共享加载和失败状态、空榜单的继续加载、多个 Artists 分区请求、加载期间拒绝重复请求、分区定向重试、全页刷新期间拒绝请求，以及重置后清空模型/游标状态。主程序构建及六个相关 MusicHub/Adapter/QML/结构套件通过。只新增 Host 展示属性，没有修改 Source SDK v2 或 Plugin UI API ABI；QML 与 Host 需同步更新，缺少新模型时显示空态而不回退到旧平台入口。

这完成的是分类页三个标签的分页切片，不代表 Phase 6 完成。Search/Favourite 等其他页面的精细分区状态、Home 剩余旧卡片、File/Download/播放器等剩余迁移，以及真实多实例音频和 macOS 手工视觉验收仍按下述顺序推进。

### 搜索、收藏及详情的共享分区分页

搜索的歌曲/歌单/专辑/歌词与收藏的歌曲/歌单模型现复用分类页已验证的分区状态聚合，按 Tracks、Playlists、Albums 或 FavoriteTracks 匹配全部分区；歌词标签沿用现有 Tracks 搜索，不新增歌词查询协议。修正首分区或最后一行状态代表整个列表的问题，空结果也保留真实失败分区的重试身份。Unsupported 不生成重复重试入口。

共享 QListView 增加默认关闭的 `sourcePaging`，通过 `sourceAdapter` / `sourcePageKind` 接入现有 Adapter 请求。搜索四个标签、收藏两个标签以及两页的详情列表启用此模式，详情请求仍使用 Category 页码 1，根搜索/收藏使用页码 3/2。模式开启后从 `paginationSectionIds` / `retrySectionIds` 的快照分别发起分页/重试；新 ID 列表为空时不退回首分区或最后一行。缺少聚合属性的旧 Host 模型仍兼容单分区身份，不构造空 ID 的全局请求。开启此模式的列表不依赖旧 MusicApi 的加载状态；其他列表的默认行为保留。

聚合 `error` 仅包含 `{sectionId: {failed: true}}`，页脚进一步只接收通用失败布尔状态。API 风险：这些搜索/收藏模型的 `error` 属性不再提供 sourceInstanceId -> SourceError 的诊断结构，读取 messageKey/detail 的私有 Host 调用者需同步调整；Source DTO、插件虚接口、Source SDK v2 和 Plugin UI API ABI 没有变化。本轮没有改变行展示身份或播放/收藏/浏览动作接口，也未宣称所有旧逐行诊断字段已清理。

空列表回归同时发现原按钮回调在模型重置期间隐式读取 `model.get()` 的 QML 错误；现改为明确引用对应列表并保护空模型。测试覆盖错误分区无行、末行耗尽而其他分区可继续、多分区隔离、加载/失败分区跳过、同步回调改动状态后仍按快照发出后续请求、根/详情页码正确、模型重置及 Adapter 移除后的安全行为。剩余 Home 推荐分页、File/Download/播放器与歌词等旧入口仍需逐项检查；真实服务器音频、完整实例卸载场景和 macOS 手工视觉验收仍未完成。

### Home 推荐分页与分类详情动作

Home 每日推荐现聚合现有五类标准推荐分区（RecentlyPlayed、FrequentlyPlayed、HighestRated、Newest、Random），通过共享 QListView 的 `sourcePaging` 按就绪分区 ID 快照分页，按失败分区 ID 重试。首分区耗尽、其他分区加载中或失败，不再挡住可继续的分区；空结果保留定向重试，Unsupported 不生成重试入口。删除通用 `loadMore(0, "")`，不再受旧 MusicApi 加载状态阻塞。推荐列表尺寸改用自己的弹窗，不再依赖旧热门歌单详情窗口。

Home 分类详情同样启用分区分页/重试，保留 Category 页码 1。Track 在 `canPlay` 时播放，其余可浏览项继续沿用 Adapter 导航；入队与收藏也由逐行 Capability 决定。没有可信曲目数时隐藏计数列，删除默认旧平台菜单；行回调明确读取所属列表，并保护模型重置及 Adapter 移除。详情关闭后复用 `closeCategoryBrowse()` 恢复分类首页，而不是让首页分类条停留在详情结果。

兼容风险：`recommendSongs.error` 与已迁移的搜索/收藏模型一致，改为 `{sectionId: {failed: true}}`，旧私有 Host 诊断读取者需同步调整。未改变推荐行展示身份、Source DTO、Source SDK v2 或 Plugin UI API ABI；QML 和 Host 展示状态需同步更新。

主程序构建及六个相关套件通过。新增 C++ 验收覆盖五类推荐分区聚合、耗尽/加载/Unsupported 隔离、空失败分区和模型重置；QML 使用实际窗口验证旧 MusicApi 忙碌时列表到达底部仍按分区分页，并覆盖推荐/详情页码、空列表重试、Track 播放/入队/收藏身份、无权限拒绝、空 Adapter 和详情关闭恢复导航。测试中的播放动作为 Adapter 夹具断言，不等同真实音频设备验收。

私人漫游、私人雷达、热门歌单及其旧详情仍依赖旧平台 API。本轮没有用 Random 曲目冒充私人推荐，也没有用普通分类歌单冒充热门歌单；Home 尚未完成全部迁移。真实服务器多实例音频、完整实例卸载与 macOS 手工视觉验收仍待完成。

### File 目录分区状态与 Capability 收口

2026-10-09：复核确认 File 的目录根/内容、独立 DirectoryLibraryController 及旧歌曲显式认领均已存在，本轮没有重做这些实现。目录展示模型改为聚合全部 Tracks 分区的 `paginationSectionIds` / `retrySectionIds`，不再由首分区或可见行决定分页。根列表与详情按 ID 快照请求，空分区有游标时也能继续；加载中/失败/Unsupported 分区不重复分页，完整刷新期间拒绝分页和重试。两个目录列表关闭旧 MusicApi 加载状态兼容，页脚只显示通用失败及重试入口。

Host 新增 `OriginalUiMusicAdapter.retryDirectorySection()` 和 DirectoryLibraryController 的定向重试。重试沿用原始分区查询，从第一页替换该分区；若此前已加载多页，重试后需要正常分页重新取得后续项，不保留旧分区后页或过期游标。其他实例的成功结果不刷新；未知、健康、Unsupported 或正在请求的分区拒绝重试，返回上级/刷新时仍沿用既有取消与 generation fencing。只有无分区来源的全页失败才保留全页刷新恢复；原显式“刷新”按钮不变。

目录行及失败占位行的 `error` 现只含 `{failed: true}`，模型聚合错误为 `{sectionId: {failed: true}}`，不向 QML 提供实例错误诊断或私有文本。Unsupported-only 内容显示空态而非重试。目录根/详情行回调保护空模型、越界及 Adapter 移除，浏览/播放/入队检查逐行 Capability，不支持入队的歌曲隐藏按钮。旧“我的文件夹”记录、备份/认领流程及下载文件列表没有删除或自动认领，设置入口仍使用现有插件设置目标身份，不把目录身份解释为文件访问权限。

API 风险：QML 与 Host 目录聚合属性和新增重试方法需同步更新，旧私有 error 诊断读取者需调整；未改变 Source SDK v2、Plugin UI API、Source DTO 或插件虚接口 ABI。验收覆盖多实例定向恢复且健康实例不产生请求、重复重试拒绝、分页失败后的分区替换、Unsupported 残留游标、返回上级后的过期回调拒绝、无行分区分页/重试、错误脱敏、加载期间拒绝、无 Capability/空 Adapter 动作安全。真实文件库音频、真实多实例卸载和手工视觉仍待验收；File 旧集合、Download/Player/歌词剩余入口及 Home 私有推荐仍不计为完整迁移。

本切片主程序构建及九个相关套件通过：DirectoryLibraryController、OriginalUiLocalDirectories、LocalDirectoriesQml、MusicHub、OriginalUiMusicAdapter、MusicHubQml、OriginalUiRecommendationQml、OriginalUiActionsQml、OriginalUiStructure。最终目录 QML 回归日志没有 TypeError、ReferenceError 或 FilePage 的属性类型赋值错误；未执行真实设备音频与手工视觉验收。

### 播放器标题/歌手搜索入口

2026-10-09：播放器标题和多歌手菜单原来仍写入 MusicApi 搜索模型，而 SearchPage 已只接收 Adapter，导致入口与结果页脱节。现保留原菜单和跳转交互，调用 `window.musicAdapter.search(text, 0)`，空文本或缺少 Adapter 时不发请求，不再清空旧平台模型或写入平台搜索标签。此切片仅统一搜索入口，尚未完成播放器收藏、封面、歌词等剩余迁移，Source SDK v2/Plugin UI API 无变更。结构回归检查两个菜单共用的函数不再引用旧搜索 API；主程序构建与搜索动作/播放桥接回归验收继续使用已有套件。

### 当前播放歌词的 Host 展示契约

2026-10-09：Host Adapter 新增 `currentLyrics`、`currentLyricsState` 与 `retryCurrentLyrics()`，只提供 `{time, text}` 行和 idle/loading/ready/empty/failed 状态。请求使用 Coordinator 当前媒体身份，经现有 MusicHub/MediaAssetRepository 向插件取歌词；不会读取本地文件、搜索平台、请求网络 URL 或把原始 SourceError 交给 QML。每次切歌取消旧请求，并验证请求 ID、当前 generation 和完整媒体身份；停播、实例禁用、Host/Coordinator 销毁清理展示。失败可显式重试，加载中不重复重试，Unsupported 进入空态。

通用 LRC 解析从 LocalMediaFiles 提取为无文件/网络依赖的共享 TimedLyrics，保留既有本地解析行为。本地插件仍负责发现/读取歌词文件；Host 仅解析插件返回的文本。无时间戳文本作为一条完整非同步歌词展示，不猜造逐行时间；Host 限制文本 64 Ki 个 UTF-16 单元和 4096 个展示行，超限失败而非截断成可用歌词。翻译和逐字协议未扩展，不能从旧平台数据补全插件歌词。

此切片先完成资源与展示层，原全屏/桌面歌词将在下一切片接入；不据此宣称歌词 UI 已迁移。新增的是 Host API，Source SDK v2 和 Plugin UI API ABI 不变。真实插件边界夹具覆盖延迟旧歌词拒绝、切歌与停播清理、实例禁用、LRC 时间/多时间戳排序、纯文本、失败重试去重、Unsupported 空态、超限文本、Hub 销毁和仅展示字段；本地歌词解析回归保持通过。

歌词展示层切片主程序构建及四个相关套件通过：OriginalUiMusicAdapter、PlaybackCoordinator、LocalLyrics、LocalSource。尚未执行真实音频设备或手工歌词视觉验收。

### 全屏与桌面歌词 UI 接入

全屏与桌面歌词现通过 Host 的 PlaybackLyricsAdapter 统一读取歌词、状态、播放时钟及倍速；Source 模式仅使用当前插件歌词，没有结果/Adapter 缺失/停播时不回退旧平台行或翻译。模式在第一次 Coordinator 播放后保持，只有真正收到旧平台播放请求才显式切回兼容模式；切换播放链路时停止另一播放器，防止双播放器同时运行。剩余旧平台入口尚未移除，因此兼容输入集中在 main.qml，不能把这个桥接组件当作全音源插件化已经完成。

保留原全屏滚动/逐字等待动画与桌面双行布局，Source 暂只显示现有 v2 歌词文本，不伪造翻译/逐字字段。加载、失败及点击重试使用通用状态；歌词变化停止旧等待/滚动动画，末行没有下一行时不启动等待动画。桌面播放按钮复用统一 togglePlayback，进度/暂停/跳转使用当前播放器；歌词文本与翻译强制 PlainText，歌词中的 HTML 标签不得触发富文本资源加载。

QML 桥接回归验证 Source/legacy 数据和时钟隔离、没有 Source 歌词也不回退、停播与移除 Adapter 后空态、翻译隔离、失败重试去重、显式兼容模式切换；结构检查两个歌词页面不再直接引用 MusicApi 歌词或 mainMedia.position。新增的是 Host 私有桥接，不改变 Source SDK v2/Plugin UI API。完整动画视觉与真实音频仍需手工验收，封面、收藏及其他旧播放器入口仍待迁移。

歌词 UI 切片主程序构建及五个相关套件通过：OriginalUiPlaybackQml、OriginalUiMusicAdapter、OriginalUiActionsQml、OriginalUiStructure、LocalLyrics。Source 模式忽略旧播放器的迟到元数据/结束状态，避免覆盖当前展示或触发旧队列；真实视觉/音频验收未执行。

### 当前播放封面资源与原 UI 绑定

Host Adapter 新增 `currentCover`，只通过现有 MusicHub/MediaAssetRepository 请求插件 Artwork；资源仓库仍按 bytes/mimeType 缓存，不能把插件 URL、headers 或诊断投影给当前播放 UI。Adapter 在结果到达时验证请求 ID、当前 generation、完整媒体身份，并额外拒绝非本地/带远程 host 的 URL。切歌清空并取消旧资源；停播、实例禁用及 Host/Coordinator 销毁清理封面。歌词失败重试不取消或重复请求已取得的封面。

主窗口封面/点击预览、全屏唱片/背景图片、桌面播放器统一读取 `window.currentCover`，复用已有私有展示桥接的 Source/legacy 模式。Source 模式无封面、停播或缺 Adapter 时显示默认图，背景图片不使用旧 ColorExtractor.renderUrl；兼容模式仍保留旧播放器封面，待平台插件迁移后删除。此切片不改变布局、Source SDK v2 或 Plugin UI API；当前缓存仅验证类型/大小等既有约束，并未新增图像解码验证。动态取色与播放器收藏继续在后续切片处理。

验收覆盖延迟旧封面拒绝、真实插件边界 bytes 缓存与本地 URL、缓存命中不重新调用插件、歌词重试保持封面、停播/实例禁用清理、插件 URL payload 拒绝、Host 移除空态，以及实际 QML 桥接中的 Source/legacy 地址隔离。测试不代替真实图片解码、桌面/全屏视觉或音频验收。

封面切片主程序构建及六个相关套件通过：OriginalUiMusicAdapter、OriginalUiPlaybackQml、OriginalUiStructure、MusicHub、MusicCaches、PlaybackCoordinator。回归中发现并修正空 QUrl 在 JavaScript 中仍为真值的问题；默认封面判定先转换字符串，避免空封面覆盖默认图。歌词重试测试保留资源仓库已有歌词缓存语义，不把缓存命中误判为再次调用插件。

### 当前封面动态取色与背景异步隔离

Source 模式的 `currentCover` 变化触发现有取色器，从 Host 缓存文件或 qrc 默认图读取；qrc 不再经过网络请求。无效本地图片清空旧取色渲染图并发出空色组，原 UI 使用已有默认主题色。保留旧平台模式的远程取色兼容，但每次取色都生成代次，旧网络成功/失败回调和颜色观察者中的重入切换不得覆盖新结果。逐像素取色前将图片统一转换为 ARGB32，支持灰度/索引等 Qt 解码格式。

复核同时发现 Mesh 背景图片加载仍没有旧任务隔离：旧网络响应与后台图片后处理可能在当前封面已改变后覆盖纹理。现为每次加载生成独立代次，网络完成和后台结果进入 UI 线程时均复核；保留原图片处理、渐变布局与动画，不更改 Source SDK v2/Plugin UI API。

新增 ArtworkPresentation 单元套件使用实际取色器、Mesh 实例、临时 PNG 和本机延迟 HTTP 夹具，覆盖本地/qrc 无网络、无效图片清理、灰度图片、迟到网络取色、回调中切换、迟到 Mesh 网络及排队后台任务。Mesh 白盒断言待提交纹理/最近图片，不启动 GPU 场景；真实渲染、性能、动画及音频仍待手工验收。

取色/背景切片主程序构建与七个相关套件通过：ArtworkPresentation、OriginalUiPlaybackQml、OriginalUiMusicAdapter、OriginalUiStructure、MusicHub、MusicCaches、PlaybackCoordinator。进入 Source 模式时也主动刷新颜色，即使封面字符串与之前默认图相同，也不会沿用旧平台颜色。

### 桌面播放器播放控制与停播隔离

复核发现 DesktopPlayerWindow 的图片虽已迁移，播放/暂停及进度仍直接读写旧 mainMedia，可能在 Source 播放时启动第二播放器。本切片通过已有 Host 私有展示桥接提供 `togglePlayback()`、`seek()` 和 `seekable`，桌面按钮、时间与进度全部接入；main.qml 的统一 togglePlayback 委托桥接，底部播放器时间、播放图标、进度跳转与封面缩放也使用当前播放状态。进度绑定只在未拖动时更新，不再向数值属性写 null。

Source 模式无当前播放/无控制 Adapter/不可跳转时拒绝相应控制，不回退旧播放器；当前 Source 停播后上一首/下一首/随机操作不碰旧队列，重新播放应由用户显式选择队列项。Source 倍速设置继续操作 Core，停播时不修改旧播放器；兼容模式仍能控制真实旧平台播放。只读旧恢复队列不抢占 legacy 模式的既有规则保持，未自动播放任何历史项。

新增实际 QML 桥接测试验证 Source 播放/暂停/跳转只触达 typed controls、不可跳转/无 Adapter/停播拒绝、负进度拒绝、显式兼容模式才控制旧播放器；结构检查桌面播放器没有任何 mainMedia 引用，以及三个换曲入口的停播保护。Source SDK v2 和 Plugin UI API 无变更。播放器收藏/下载、Windows SMTC、频谱与平台信息仍待迁移，真实桌面拖动/音频及视觉验收未完成。

桌面控制切片主程序构建及十个联合回归套件通过：OriginalUiPlaybackQml、OriginalUiMusicAdapter、OriginalUiActionsQml、OriginalUiStructure、ArtworkPresentation、PlaybackCoordinator、QtPlaybackController、PlaybackControlsAdapterQml、LegacyQueueQml、QueueHistoryIntegration。验收文档区分已迁移的展示/控制入口与上述剩余平台路径，不将本切片计为 Phase 6 全部完成。

### 当前播放收藏的 Host 展示与动作契约

Coordinator 保留队列项已有媒体权限，在播放启动与 session 权限事件中缓存 Favorite/Unfavorite 的有效权限：复用现有 CapabilityResolver 的 Plugin/Server/Account/Media 交集，并检查 IFavoriteProviderV2。权限信号到达即清空旧许可，再异步复核；只有收藏权限改变时不会打断合法播放。新增非 QML C++ `currentActionItem()` 仅供 Host 路由，读取时不调用插件；原 currentItem/queue 不新增权限、metadata、URL 或 headers。历史队列仍只恢复原 Play 提示，不猜造收藏权限。

Adapter 新增 `currentFavorite` 和 `setCurrentFavorite()`，展示只含 canFavorite/canUnfavorite/state/pending/failed。初始 state=unknown，不用旧数据库、当前列表标题或某个平台标记猜测；只有 Router 确认的插件 bool 结果更新最后确认状态。失败不乐观改状态、不公开诊断，允许显式重试。提交前预留请求防止重入/重复点击，并在通知观察者后重新验证当前播放；观察者停播时不分发动作。

请求结果必须同时匹配 Host request ID、当前 generation 和完整媒体身份。用户切歌不会撤回已经分发的合法收藏业务，但迟到结果不得改变新曲目；同 entityId 的其他实例/账号不混用状态，重播同一 occurrence 也回到 unknown。此阶段尚未绑定原播放器按钮；SDK 没有统一“读取当前收藏状态”的 Provider，本轮不增加虚接口、不伪造状态。Source SDK v2 和 Plugin UI API ABI 不变，Host C++/展示 API 需同步构建。

主程序构建与四个相关套件通过：OriginalUiMusicAdapter、PlaybackCoordinator、MediaActionRouterV2、QueueHistoryIntegration。新增验收覆盖页面模型替换后仍路由当前队列身份、初始未知/确认后状态、pending 去重与重入、失败保持状态、多实例同 ID、切歌与重播结果隔离、Hub 移除、权限撤销/恢复且不停止播放、实例禁用、观察者停播前拒绝分发，以及恢复队列不提升权限。真实服务器收藏和按钮视觉留待后续验收。

### 原播放器收藏按钮接入

保留原按钮、主题色和 QMenu，Source 模式由 Host 私有展示桥接读取 currentFavorite 与权限。状态已确认时单击执行相反动作；初始未知或只有单向权限时提供明确的“收藏/取消收藏”菜单，不把 unknown 当作未收藏。pending、无权限、停播和缺 Adapter 禁用 Source 收藏，不访问旧数据库或当前旧队列。失败仅显示通用重试提示；图标只反映当前 generation 的最后确认结果，不乐观更新。

currentFavorite 新增不含 source/account/ref 的 opaque token；setCurrentFavorite 可携带 expectedToken。菜单保留打开时的 token，切歌、停播、Source/legacy 模式改变时关闭旧菜单；QML 与 Host 两端都验证令牌，防止将旧菜单操作应用到另一首歌或同 occurrence 的新播放代次。旧无 token 的 Host 即时调用仍兼容，但原 UI 一律传 token。Source SDK v2/Plugin UI API ABI 不变，Host 方法签名和展示状态需同步构建。

兼容模式保留旧收藏行为，旧图标改为独立 legacyFavorite 状态，不再命令式覆盖 Source 图标绑定；越界或没有旧队列项时拒绝。新增实际展示桥接和从 PlayerControl 提取的原按钮 QML 逻辑测试（轻量视觉控件夹具）覆盖未知菜单、不同行为权限、确认后单击/主题色、pending 去重、停播/缺 Adapter/legacy 隔离、旧菜单 token 拒绝与关闭；Host 回归增加多实例切换/重播后的 stale token 拒绝。该夹具不证明真实 QMenu 布局/鼠标视觉，真实服务器收藏、外部状态读取与手工视觉仍待验收。

收藏按钮切片主程序构建与八个联合回归套件通过：OriginalUiMusicAdapter、OriginalUiPlaybackQml、OriginalUiActionsQml、OriginalUiStructure、PlaybackCoordinator、MediaActionRouterV2、QueueHistoryIntegration、MusicHub。

### Windows SMTC 的 Source 控制与展示桥接

系统播放、暂停和进度请求现复用 Host 私有播放桥接，Source 模式仅操作 typed Core controls；停播、缺控制 Adapter 或系统服务不可用时拒绝对应请求，不启动旧播放器。上一首/下一首复用原 Source 队列导航，系统按钮按当前队列启用（下一首沿用循环到首项、上一首不循环），恢复队列不会自动夺取兼容播放。

Source 系统媒体信息仅从 Coordinator 当前项提取 title/artists/album，封面仅发送 Host 缓存 file URL，qrc 默认图以空封面处理；AppMediaId 使用 opaque occurrence，不发送 SourceRef、账号、插件资源 URL 或 headers。Core Loading/Playing/Paused/Stopped/Error 状态映射为系统状态，进度由统一时钟更新；停播清除旧媒体信息、封面、ID 和时间线并禁用控制。Source 模式忽略旧 mainMedia 的迟到状态、时长、位置与 source 信号；原生新缩略图读取失败时也清除上一首缩略图。

主程序构建及九个联合套件通过：OriginalUiPlaybackQml、OriginalUiMusicAdapter、OriginalUiActionsQml、OriginalUiStructure、PlaybackCoordinator、QtPlaybackController、PlaybackControlsAdapterQml、LegacyQueueQml、QueueHistoryIntegration。新增测试直接提取 main.qml 的实际函数与 Connections，替换原生 SMTC 服务，并运行真实播放桥接；验证状态映射、Source 元数据白名单、不读取旧队列、输入路由、队列按钮、停播/缺 Adapter/不可用保护、默认封面清理和迟到旧信号隔离。测试引用实际 C++ 状态枚举，避免用不同数值的伪枚举证明映射。

这只是跨平台 QML 边界验收：macOS 构建不编译 Windows WinRT 实现，不能据此宣称 Windows 原生行为已验收。仍需 Windows 编译及媒体浮层/物理媒体键/系统进度/缓存图片权限/服务关闭手测。Source SDK v2、Plugin UI API 和 SMTC API 均未修改；下载、频谱、播放器详情/选项及其他旧入口继续按切片迁移，Phase 6 尚未整体完成。

### 当前播放器音乐详情展示

原音乐详情弹窗保留布局、可选择文本和主题，数据改为 Host 私有桥接的七字段展示投影。Source 只读当前 title/artists/album/sourceLabel，第一行显示来源而非猜测文件名；v2 当前播放项未提供文件名、日期、音频格式，显示未知，不读取任意 metadata、SourceRef 或流地址补齐。长度读取统一 Core 时钟并复用原时间格式函数。切歌更新，停播或当前项缺失清空 Source 字段；只有显式兼容模式才显示旧文件名/专辑/日期/格式。

新增测试运行实际详情弹窗 QML（轻量容器夹具）和真实桥接，验证显示字段白名单、忽略身份/资源/任意 metadata、来源标签、切歌、时长未知、停播/空项清理及显式 legacy 切换；测试不代替真实弹窗布局与视觉验收。主程序构建及九个播放/UI 联合套件通过，Source SDK v2 和 Plugin UI API 无变化。下载按钮、播放选项、频谱和剩余旧平台入口仍待迁移，Phase 6 尚未整体完成。

### 当前下载按钮的兼容路径隔离

播放器下载按钮此前仅在 securePlaybackActive 时拒绝旧请求，Source 停播后会重新读取旧队列并可能请求旧平台 hash。本切片按 sticky sourceLyricsMode 禁用并在 handler 再拒绝，Source 播放/停播都不读取旧队列，不从旧路径猜测当前资源；显式 legacy 模式还检查索引和 Legacy Local 标记，空 path 不发送请求。保留原按钮和真正旧在线平台下载行为，删除重复且结果相同的音质判断。

新增实际按钮 QML 逻辑测试覆盖 Source 活跃/停播都零旧队列读取与零旧 API 调用、明确进入 legacy 才发送原请求、越界与 Legacy Local 拒绝。主程序构建及七个 UI/播放/路由联合套件通过。此切片是封堵旧回退，不是实现当前 Source 下载：现有 MediaActionRouter 已支持 Download Provider，但当前播放权限快照尚未包含 Download，Adapter 也尚无当前下载与用户目标文件选择流程。后续应演进该 Host 流程、按播放 token 和完整身份捕获用户操作、复用 Router 权限检查；不得为下载向 QML 泄露流 URL，或未经明确选择覆盖文件。SDK v2/Plugin UI API 不变，真实下载与视觉验收未完成。

### 当前 Source 下载的 Host 动作契约

Coordinator 当前动作快照增量加入 Download，分别检查 IDownloadProviderV2 与已有 Plugin/Server/Account/Media 权限交集；不改变 SDK v2 虚接口。权限变化立即清空旧许可再异步复核，仅下载权限变化不打断合法播放；缺少媒体 Download 提示、无法执行的 bitrate 约束和历史恢复项不能被提升为可下载。

Adapter 新增 currentDownload（仅 canDownload/pending/failed/completed/token）及必须携带 expectedToken 的 downloadCurrent(destination, expectedToken)。令牌绑定当前 generation，Host 私有状态保留完整身份和请求 ID，读取不调用插件。提交使用当前队列项而非页面行，pending 防重入/重复；结果须匹配请求、播放代次、身份与目标，切歌后迟到结果不改变新曲目，同 ID 的多实例/账号不混用状态。已分发下载作为独立业务可继续，不因切歌撤回；此展示状态不是跨曲目下载任务管理器。

Host 只接受用户指定的新绝对本地文件目标：拒绝远程/相对/带 query、fragment、userinfo、NUL 的地址、已有文件或目录、悬空符号链接、无效/不可写父目录，不创建目标和父目录，也不自动覆盖。pending 观察者之后再次复核，观察者停播或创建目标时不分发。Router 继续检查实际权限、Provider 和返回的本地 destination；失败仅公开布尔状态，不泄露 URL、headers、账号或诊断。Navidrome 原有临时文件及最终 rename 的不覆盖保护保留；Host 的前置检查不能代替第三方插件自己的提交期不覆盖检查。

主程序构建和四个联合套件通过：OriginalUiMusicAdapter、PlaybackCoordinator、MediaActionRouterV2、QueueHistoryIntegration。真实插件夹具新增验证目标拒绝与已有文件保留、页面替换仍按当前身份路由、去重/重入、成功与失败重试、错误结果拒绝、多实例和重播 stale token、迟到结果、Hub 销毁、权限降级/恢复/Unsupported、媒体缺许可/bitrate 约束、恢复项不提升、观察者停播和目标竞态。该夹具不写下载内容，不能证明真实服务器字节落盘；保存对话框和当前按钮将在下一切片接入，真实下载及手工视觉仍待验收。Host 展示/API 需同步构建，Source SDK v2 与 Plugin UI API ABI 不变。

### 原下载按钮与用户目标文件选择

原播放器下载按钮现按 currentDownload 的 Capability/播放 token/pending 启用 Source 下载，复用同一按钮与主题，不访问旧队列。点击只打开 Host 的 QtQuick.Dialogs SaveFile 对话框，保存打开时的 token；同一对话框重复点击不重新打开。接受时经播放展示桥接再次验证 Source 模式、当前 token、可下载且非 pending，再调用 Host downloadCurrent；取消不发请求。切歌/重播、停播、权限撤销、Adapter 移除或模式变化关闭并清空旧选择，旧 accepted 回调也不能作用于新歌曲。显式 legacy 在线下载保持，Legacy Local 和越界仍拒绝。

对话框明确要求选择新文件，Host 即使收到原生覆盖确认后的已有目标也拒绝覆盖。v2 未提供原始文件名/格式，不从标题、旧播放器或 URL 猜测扩展名；使用所有文件过滤器，让用户明确选择目标。加载/失败/完成显示通用状态，不展示私有诊断或下载地址。Source 下载仍不接入旧 DownloadModel，切歌后任务状态持久展示、进度、取消与下载页统一属于后续切片，不能把当前按钮接入计为下载管理整体完成。

主程序构建及八个联合套件通过：OriginalUiMusicAdapter、OriginalUiPlaybackQml、OriginalUiActionsQml、OriginalUiStructure、PlaybackCoordinator、MediaActionRouterV2、QueueHistoryIntegration、NavidromeSource。新增实际按钮/对话框 handlers 与真实展示桥接测试验证捕获 token、重复打开/提交、取消、旧确认、pending、重试、降权/停播/缺 Adapter 拒绝和零 legacy 回退；原生 FileDialog 用轻量夹具替换，不能证明 OS 弹窗视觉或信号顺序。既有 Navidrome 套件验证本机 HTTP 夹具下真实字节提交及错误/取消临时文件清理；真实服务器、原生保存对话框与跨曲目下载管理仍待验收。SDK v2/Plugin UI API ABI 不变。

### 跨曲目 Source 下载会话记录

Adapter 新增独立于当前播放 token 的 downloadTasks 展示投影。已分发请求仍由 Router 管理，切歌、停播、页面替换及 Coordinator 销毁不清除记录；完整 MediaRef、Router 请求 ID 和目标 URL 仅存在私有进行中记录，终态释放这些值。公开字段严格限制为 taskId/title/artist/sourceLabel/fileName/state；同实体 ID 的不同实例不混用，迟到结果不能改变新歌曲状态。进行中的相同目标 URL 不重复分发，第三方插件仍须保证最终提交不覆盖。

状态只有 pending/completed/failed，不猜造 SDK v2 未提供的字节进度。Router 失效结果及 Router 销毁使进行中记录失败；dismissDownloadTask 只允许移除终态展示记录，不取消请求、不删除任何文件。记录仅在当前 Adapter 会话内有效，不自动持久化、不自动重试，也不将下载文件直接送入播放器或自动扩张 Local 根目录。

OriginalUiMusicAdapter、PlaybackCoordinator、MediaActionRouterV2、QueueHistoryIntegration 四个联合套件通过。新增测试覆盖跨实例同 ID、切歌后目标去重、停播及 Coordinator 销毁后的完成/失败、Router 销毁及实例禁用、重复终态、字段白名单、终态移除与文件保留；既有 inline 完成、错误目标拒绝和权限检查回归继续通过。下载页显示将在下一切片接入；没有新增 SDK v2/Plugin UI API 虚接口或 ABI 变化，真实服务器与原生 UI 验收仍待完成。

### 原下载页的 Source 会话任务展示

DownloadPage 由 MainContent Loader 的持续 Binding 注入 Adapter，保留原三页签、主题与下载管理布局。Source pending/failed 记录进入“正在下载”，completed 记录进入“已下载”，只读安全投影；字段按纯文本渲染，不把插件标题当富文本。终态按钮只移除记录，进行中按钮隐藏且 handler 再拒绝；缺 Adapter 时 Source 区清空，不使用旧下载模型冒充任务。旧下载任务、旧目录文件与目录按钮分别标注为兼容/旧目录，保留其已有数据，不混用 ID、百分比、重试或取消动作。

Source 下载选择的目标可以在任意用户目录，完成记录不依赖旧固定目录扫描，不自动生成旧平台元数据或触发路径播放。界面明确说明移除不删除文件、播放前需在 Local 插件中导入目标目录；没有自动扩张 Local 根目录。Source 当前任务仅显示通用状态，不展示诊断、目标目录或资源地址，也没有假进度/取消/自动重试。持久化及取消仍属后续切片。

主程序构建及 OriginalUiMusicAdapter、OriginalUiActionsQml、OriginalUiPlaybackQml、OriginalUiStructure、PlaybackCoordinator、MediaActionRouterV2、QueueHistoryIntegration、NavidromeSource 八个联合套件通过。下载页实际 QML 测试使用轻量服务夹具和真实控件，强制 ListView 布局并按视觉子项查找代理，覆盖任务分类、完成后跨页签移动、终态移除、进行中程序化点击拒绝、Adapter 移除清空和纯文本；结构测试确认实际 Loader Binding。该验收不代替真实 OS/服务器与完整视觉检查。SDK v2/Plugin UI API 未修改，Download 与 Phase 6 尚未整体完成。

### Source 下载任务取消

Router 增量提供 Host-only cancelDownload(requestId)，只接受本 Router 所有、已经返回且仍处于有效生命周期的进行中 Download。拒绝未知/Provider ID、其他 Router 的 ID、非下载动作、已完成/已失败/重复取消，不改变 SDK v2 虚接口。接受取消后设置 Host 通用终态并复用既有 deferred schedule：在 Provider 回调退出后，带 callable lease 调用现有 session.cancel 一次，忽略迟到终态，释放租约后发送 actionFailed；不在 QML handler 内直接调用插件。若排队期间实例关闭/销毁，原生命周期失败优先，不能误报为用户取消。

Adapter cancelDownloadTask 仅用 opaque taskId 查找私有 Router 请求，不依赖当前 Source 或播放 token。正常取消展示 cancelled，仍保留在下载任务页供移除记录；当前曲目取消清除 pending，并以“下载未完成”提示允许用户重新选择新目标。取消不删除用户文件、不自动重试，不保证第三方插件已经提交的文件会回滚；临时文件清理属于插件原有 cancel 契约，Navidrome 现有清理实现保持。Source 取消按钮与旧下载器完全分离，进行中可取消但不能移除，终态可移除但不能取消，handler 复核状态与 Adapter 存在性。

主程序构建及 MusicHub、OriginalUiMusicAdapter、OriginalUiActionsQml、OriginalUiPlaybackQml、OriginalUiStructure、PlaybackCoordinator、MediaActionRouterV2、QueueHistoryIntegration、NavidromeSource 九个联合套件通过。新增 Router 测试覆盖身份所有权、动作类型、延后/去重、取消期间 unload Busy、迟到成功、其他实例继续完成、inline 已完成拒绝、取消回调销毁 Router 与关闭实例优先；Adapter 测试覆盖跨实例取消、当前状态隔离、取消后目标释放与新提交、独立创建文件保留；实际下载页 QML 夹具验证按钮/handler 的终态和重复保护。Navidrome 既有本机 HTTP 字节/取消清理回归通过，不等同于真实服务器或原生视觉验收。

Source 下载会话展示与取消已具备，字节进度、跨启动记录恢复及安全重试仍未实现；SDK v2/Plugin UI API ABI 不变。后续按原顺序继续播放器选项、频谱与其他旧入口隔离，不把 Download 或 Phase 6 标为整体完成。

### 播放器选项的 Core/兼容路径隔离

检查实际代码确认：旧选项的自动播放只改 mainMedia.autoPlay，固定音质档位只写旧平台 soundQuality；间距补偿/均衡器只是旧占位。设备选择仅绑定 main.qml 的旧 AudioOutput，当前 QtPlaybackController 后端自行构造默认 QAudioOutput，没有设备选择 API。不能把这些选项继续显示为已支持的 Source/Core 功能。

原播放器选项弹窗和主题保留。Source 模式隐藏旧自动播放、固定音质、自定义设备与未实现占位，默认设备行禁用并说明当前使用系统默认输出；控件表达式和 handlers 再隔离，Source 停播或程序化触发也不能读写旧配置。显式兼容模式保留原选项行为。这不是 Source 自定义设备、自动播放策略、EQ 或 gap compensation 的功能实现；插件特定音质规则未来仍应由其 Settings Schema/Plugin UI 配置，不把固定档位推广到所有 Source。

倍速增量通过 Host 私有展示桥接统一路由：Source 只调用 typed Core，显式 legacy 才赋值旧播放器；缺控制器拒绝并禁用 UI。保留 0.1–4.0 原交互范围，严格拒绝字符串、非有限值、越界数值与无效预设索引，仅成功路由后更新预设选择。倍速属于 Core 全局播放设置，Source 停播时可提前配置，不触发播放或读取旧身份。

主程序构建及十二个联合套件通过：MusicHub、OriginalUiMusicAdapter、OriginalUiActionsQml、OriginalUiPlaybackQml、OriginalUiStructure、PlaybackCoordinator、QtPlaybackController、PlaybackControlsAdapterQml、LegacyQueueQml、MediaActionRouterV2、QueueHistoryIntegration、NavidromeSource。新增测试提取实际选项 QML、使用真实展示桥接和轻量视觉控件夹具，验证 Source 停播时零旧自动播放读取/写入、旧配置与占位 handlers 拒绝、默认设备状态、合法倍速/自定义/非法输入、缺 Core 不回退及显式兼容切换。真实弹窗视觉、物理设备选择与倍速音频仍需手工验收；没有改变 SDK v2/Plugin UI API。下一步继续频谱及剩余旧入口迁移，Source 自定义输出设备需要独立 Core 契约和对应测试，Phase 6 未整体完成。

### 频谱迁移第一切片：安全分析器与 Legacy 隔离

- 将 FFT 管线收敛到 Host-private `AudioSpectrumAnalyzer`：后台任务只持有 PCM 值副本；最多一个运行任务和一个最新待处理窗口，采样窗口有界。
- reset、禁用、频段变化及销毁均隔离旧任务结果；支持 Float/Int16/Int32/UInt8，清理非法 PCM，输出有限且有界的镜像频谱与原尺寸几何路径。
- `GetWave` 保留 Legacy QML 接口，但重新绑定/销毁时解除其拥有的采样输出，使用自动线程连接和附件代次拒绝旧输出。重入绑定以最新绑定为准。
- 原播放器频谱改读 `PlaybackLyricsAdapter.wavePath`；Source 模式解除 Legacy 采样并返回空路径。此切片尚未接入 Core PCM，不将旧曲目频谱冒充 Source 频谱。
- 验证：主程序构建及 spectrum、PlaybackCoordinator、QtPlaybackController、controls QML、original playback QML、UI structure、queue/history 共 7 项回归通过。包括延迟完成、销毁、重入及真实 Qt PCM 信号测试；不代表物理音频/GPU 视觉验收。
- SDK v2、Source 插件 ABI 与 Plugin UI API 不变；新增 Host 内部静态库，应用需同步重建。

### 频谱迁移第二切片：Core PCM 与原 UI 接线

- Core 的私有 Qt Multimedia backend 从自身 `QAudioBufferOutput` 采集解码 PCM，复用安全分析器；播放器、输出和分析器均不成为 QML 门面的子对象。
- `QtPlaybackController` 仅新增显示开关与最多 130 个有限、边界内的 `QPointF` 路径。PCM、媒体地址、headers、原始诊断及播放器指针不进入 QML；这是 Host 内部 API 增量，不改变 Source SDK v2 / Plugin UI API。
- 后端身份、播放 occurrence generation、显示 epoch 三重校验拒绝旧帧；暂停、停止、结束、失败、切曲、seek 和禁用均清空路径/分析窗口。连接代次同时隔离已排队的旧 PCM 信号；后台旧 FFT 结果由分析器自身代次拒绝。
- 原 `waveDisplay` 设置驱动 Source 采样，实际运行 main 中的 Binding 验证 Source/current/display 三个条件，以及适配器替换/移除关闭旧门面的采样。播放视图仅经 `PlaybackLyricsAdapter` 读取 Core 几何，保留原频谱尺寸及绘制布局。
- 验证：主程序构建及上一切片所列 7 项联合回归通过；补充 seek、非法/超量几何、暂停恢复、禁用重启、切曲旧回调、销毁及清空观察者重入测试。尚无真实服务器音频、物理设备或 GPU 视觉验收，不据此宣称全项目迁移完成。
- 后续继续迁移桌面悬浮播放卡，其标题、时钟与播放/拖动仍有直接 Legacy 访问；平台业务及其他剩余 Legacy 分支仍按既定阶段迁移。

### 桌面悬浮卡：展示与动作去 Legacy 直连

- `DesktopSpot.qml` 的标题、播放状态、duration/position、播放/暂停及 seek 全部经 `PlaybackLyricsAdapter` 投影；上一首/下一首复用已迁移的队列动作，并在无当前项时禁用/拒绝。
- 保留原窗口、尺寸、折叠/展开动画与视觉控件。Source 停止或适配器缺失不会回退到旧播放器；只有显式 Legacy 模式保留旧播放行为。标题使用 PlainText。
- 进度采用拖动期间暂停同步的 Binding；补充对 slider 范围的依赖，修复空态切回长曲目时新 position 被旧范围夹到 1 的问题。
- 完整生产 QML 组件的 offscreen 测试（仅视觉控件/Theme 为 stand-in，真实 Slider/Binding/事件逻辑）覆盖播放/暂停、时钟、拖动、不可 seek、停止、缺失适配器、纯文本标题、队列动作与显式 Legacy。主程序构建和 7 项联合回归通过；不等价于原生桌面窗口/GPU 手工验收。
- 下一切片检查普通桌面播放器的标题/歌手旧字段及相同进度范围问题；SDK / 插件 ABI 不变。

### 普通桌面播放器：元数据投影及进度范围同步

- `DesktopPlayerWindow` 标题/歌手改读安全 details，使用 PlainText；Source 空态清空，不依赖 main 中仍用于兼容的可变标题字段。
- 普通桌面与原底栏 Slider 的位置绑定都增加对实际范围的依赖。duration 扩大但 position 未变时恢复正确位置；拖动期间仍暂停后台位置同步，释放后恢复。
- 实际生产 Text/Slider/Binding 片段的双场景测试覆盖 Source 空态到当前项、范围缩小/恢复、拖动/释放、停播、Legacy 往返及纯文本元数据。主程序构建及 7 项联合回归通过；不代表手工视觉/音频验收。
- SDK / 插件 ABI 不变。底栏标题及其搜索菜单仍读取兼容 window 字段，后续应改由同一安全 details 驱动；其余平台业务迁移仍按总体顺序推进。

### 底栏元数据与搜索菜单：安全展示及旧选择隔离

- 原底栏标题/歌手改读统一 details，并以 PlainText 显示；标题和歌手搜索保留原菜单与拆分交互，仍通过已有通用 Adapter search 发起。
- 菜单保存打开时的展示文本。当前展示改变时关闭菜单并清除快照；旧选择、空文本及非法歌手索引不触发搜索。Source 空态不显示/搜索旧 window 标题字段。
- 通用 `QMenu` 的文字委托使用 PlainText，避免音源提供的标签被解释为富文本；真实 MenuItem/Text 委托有 offscreen 验证（只有背景 Theme/Blur 为 stand-in）。
- 原 title/artist Text、MouseArea、parseArtists、search 和菜单处理器组合测试覆盖当前关键词、多歌手、元数据变化、停播、旧选择拒绝及显式 Legacy。主程序构建与 7 项联合回归通过；仍未完成原生桌面/GPU/真实音频验收。
- 只迁移显示与搜索，显式 Legacy 收藏及 main 兼容状态字段仍保留。下一步继续审计其他旧标题消费者与页面/平台专用业务；不将当前切片视为全部插件化完成。SDK / 插件 ABI 不变。

### 最大化播放器与桌面歌词：剩余标题消费者迁移

- 最大化标题/歌手、歌词模式标题及桌面歌词控制栏改用安全 details，按 PlainText 显示；保留原布局、特效及两种组合分隔样式。
- Host 私有展示桥接新增 `metadataCaption`，仅格式化现有安全展示字段。单侧缺失时不生成孤立分隔符，Source 空态不会显示上一首或迟到的 Legacy 字段。
- 实际生产 Text 片段 + 真实桥接的双场景测试覆盖完整/单侧/空元数据、切曲、停止、迟到 Legacy、纯文本和显式兼容切换；只有阴影为 stand-in，不声称 GPU 视觉验收。主程序构建及 7 项联合回归通过。
- Source SDK v2 / Plugin UI API 不变。下一切片清理 main 中无调用方的 Navidrome 旧账号入口，保留通用插件设置导航和仍在使用的 Legacy 播放业务。

### 主窗口设置导航：删除失效 Navidrome 专用入口

- 图检索与当前代码检索确认 `openNavidromeAccountEditor` 无调用方，其指向的旧设置页 `editNavidromeAccount` 已不存在。删除该函数、Loader 待处理标记和专用 onLoaded 分支；保留通用 `openPluginSettings(packageId, instanceId)`。
- 实际 main 函数与 Loader 处理器测试覆盖加载前请求、最新请求覆盖、加载后直接打开、实例选择、空实例及请求清除后不重放。结构测试约束旧入口不得回流；既有插件设置 QML 和管理 UI session 回归通过。
- 主程序构建及上述 4 项针对性回归通过。此清理不改变 SDK/Plugin UI API，不删除仍在使用的网易/酷狗 Legacy 登录业务，也不意味着 Host 的全部平台专用 UI 已清空。
- 后续继续主窗口剩余兼容状态边界：Source 元数据仍被写入 Legacy 标题字段，关闭流程仍可能将 Source 标题与旧队列 path 混写到 lastSongs，封面查看入口也仍读旧标题；需成组修正并保留明确 Legacy 行为。

### 主窗口元数据、关闭记录与封面命名边界

- Source 同步不再把标题/歌手复制到 Legacy 可变字段；显示继续通过安全适配器，明确 Legacy 的标题更新及收藏行为保留。
- Source 播放或停播后关闭窗口均不读取旧队列、不改写旧 `lastSongs`，避免新标题与旧 path 混合。Source 队列/历史继续由现有 QueueHistoryStore 持久化；显式 Legacy 关闭仍保存其完整旧记录，空队列和最小化行为保留。
- 封面查看标题改读安全 details。导出名称规整为有界单文件名，处理路径分隔符、控制字符、保留设备名、空标题和 Unicode 截断；异步回调使用提交时的目标快照，不随下一次查看变更。
- 主程序构建及 playback coordinator、audio spectrum、Qt playback controller、controls QML、original playback QML、UI structure、queue/history、plugin settings QML、management UI session 共 9 项联合回归通过。实际 main 处理器测试验证 Source 零 Legacy 读取、显式 Legacy 保存与文件名/目标快照；没有实际图片写入、物理音频或原生视觉验收。
- SDK v2 / Source 插件 ABI / Plugin UI API 不变。封面保存仍未检查实际写入结果，图像异步捕获生命周期也需进一步审计；平台登录业务及其余页面迁移仍未整体完成。

### 封面导出：真实结果反馈与异步视图失效

- 实际 `grabToImage` 拒绝与 `saveToFile` 返回失败不再提示成功；空系统图片目录、未加载/加载失败和空捕获结果有明确提示。异常只给通用提示，不向 UI 透传文件路径或原始诊断。
- pending 期间重复保存不重复提交；完成或捕获拒绝后释放状态，允许用户主动重试。目录/文件名仍使用提交时快照，不引入自动重试、删除或新的覆盖策略。
- 查看窗口的源变化及重新打开均推进视图代次；异步完成时若代次变化或图像不再 Ready，则不调用写盘，避免旧目标与新封面混配。保持原弹窗与保存按钮交互，不要求回调时弹窗仍可见。
- 主程序构建及前述 9 项联合回归通过；实际生产处理器测试覆盖成功、失败/异常/空结果、捕获拒绝、重复提交、源往返、同源重新打开、状态变化及失败后重试。写盘和抓图为可控测试替身，尚需真实 macOS 权限、窗口/GPU 捕获及图片内容验收。
- 本切片关闭上一切片的未检查写入结果差距，补充已识别的视图变化边界；不据此宣称全部图形生命周期或 Phase 6 验收完成。SDK v2 / Source 插件 ABI / Plugin UI API 未改，后续进行全量构建/测试核验并继续其余页面迁移。

### 全量回归检查点（2026-10-09）

基于代码提交 `23b3315`，现有 macOS / Qt 6.11.1 `build-phase3` 的全目标增量构建成功，全部 71 项 CTest 通过（141.85 秒）。执行命令：

```sh
cmake --build build-phase3 -j 6
ctest --test-dir build-phase3 --output-on-failure
```

覆盖独立 Plugin UI 导入/加载与生命周期、SDK v2 契约、Settings Schema、Plugin/SourceInstance 注册、Capability/Action Router、Core/Coordinator、频谱、原 UI、队列历史、Local 索引/插件/目录迁移以及 Navidrome。macOS 本地插件包、增量包同步和移动后 Local bundle 测试也通过。测试后工作树保持干净。

这是现有构建目录的全目标增量验证，不是从零构建、跨 OS/编译器 ABI 矩阵、真实 Navidrome 服务、物理播放或原生视觉验收；封面导出结果测试仍为受控替身，真实图片内容/权限/GPU 捕获需手工验证。

下一开发切片回到首页剩余私人漫游/雷达与热门歌单：先对照现有 v2 的通用页面/动作契约，明确可表达的 Source 数据和不支持状态，再迁移原卡片及详情入口。不能把普通推荐列表冒充私人漫游，也不能用旧平台路径伪造插件播放项。仍在使用的网易/酷狗账号 UI 保留到对应插件具备迁移条件，最终删除目标不变。Phase 6 未整体完成，后续顺序保持如下。

### 首页旧热门歌单：迁移到通用 Source 歌单（2026-10-10）

- 实际 v2 已有 Category / Playlists 分区，Adapter 已有按实体及 chart 分类的安全 `categoryPlaylists` 投影，直接复用；没有新增 SDK 枚举、Provider 虚接口或 Plugin UI API。
- 原 148×256 歌单卡片、封面和悬停动画保留，数据改读该投影，标题/歌手按纯文本显示。标题改为“音源歌单”，去掉插件未提供的旧平台播放量/曲目数，避免声称通用列表有热门排名。
- 点击仅在 `canBrowse` 允许后进入已有安全分类详情；详情的播放、入队、收藏及分页沿用 Adapter/Coordinator 路径。删除此入口独占的旧 `hotlistsWindow`、hash 查询、旧 path 入队和 Legacy 收藏 handlers；私人漫游/雷达仍在使用的兼容业务暂保留。
- “更多”与重试只使用该模型的可操作分区 ID；空态、部分失败、全失败和加载使用通用提示，缺 Adapter 不回退到旧歌单。加载/进入详情期间再次程序化触发也不发旧请求。
- 主程序构建及 MusicHub、OriginalUiMusicAdapter、MusicHubQml、OriginalUiRecommendationQml、OriginalUiActionsQml、OriginalUiStructure、PlaybackCoordinator、MediaActionRouterV2、QueueHistoryIntegration 共 9 项联合回归通过。实际生产页面测试覆盖实体/chart 过滤、纯文本、详情动作、能力拒绝、非法索引、缺 Adapter、跨分区分页/重试与零旧歌单调用；没有真实服务器/原生视觉验收。
- 私人漫游/雷达没有明确 v2 契约，不能把 Random 推荐冒充该功能。下一切片审计共享详情容器的遗留平台字段、标题渲染和 Source 范围变化的展示失效；之后继续处理剩余发现入口。Phase 6 未整体完成。

### 共享详情容器：去平台字段与 Source 视图失效

- 当前代码检索确认 `AnimatorWindow.songSource` 未被自身或调用方使用，删除其旧 MusicApi 绑定；共享详情标题使用 PlainText。容器仍为 Host 私有组件，不成为公共 Plugin UI Kit/API。
- 新增 Host 私有 `resetView`：停止开关动画、关闭 Loader、清空标题/封面并恢复父页面。QObject 内容遵循 Qt 的延后销毁，隐藏/取消动画立即发生；仅在对应退出层级仍活动时归还该层级。
- 首页 Source 范围变化及 Adapter 替换/移除立即清空每日推荐与分类/歌单详情；分类导航失效关闭详情，嵌套导航更新标题/封面。上下文切换期间不调用新 Adapter 的 closeCategoryBrowse，防止误清另一实例的现有导航；正常用户关闭仍重置当前分类导航。
- 容器在关闭动画期间重新打开会取消旧动画并复用已加载内容，不让旧完成动作再隐藏新详情。原布局、动画参数与控件保留。
- 主程序及相关 QML 资源重建，前述 9 项加 OriginalUiLocalDirectories 共 10 项联合回归通过。实际页面/容器测试覆盖 Source 切换、导航失效、Adapter 替换/移除、延后销毁、父页面恢复、嵌套纯文本标题、动画中重新打开与正常关闭。尚无真实服务器或原生视觉验收，SDK v2 / Source 插件 ABI / Plugin UI API 未改。
- 下一检查点扩大为全目标构建/回归，再推进私人漫游/雷达的明确能力契约与入口边界；其 Legacy 实现和平台登录 UI 仍未完成插件迁移，Phase 6 未整体完成。

### 首页歌单与详情迁移后的全量检查点（2026-10-10）

代码基线 `b017ac5` 完成现有 macOS / Qt 6.11.1 `build-phase3` 全目标增量构建；全部 71 项 CTest 通过（114.89 秒），测试后工作树无意外改动。相关页面及共享 AnimatorWindow 资源在全目标中重新生成，包含原播放/操作/本地目录 UI 的回归；SDK v2、独立 Plugin UI、实例/设置/路由、Core/Coordinator、Local、Navidrome 及 macOS 包/增量同步/移动后包测试也通过。

本轮闭合的是首页旧热门歌单入口与 Source 详情容器边界，不是全首页或 Phase 6 的整体插件化。私人漫游/雷达需要明确的、可选且可验证的 Source 能力契约；普通 Random 不足以声明该业务，也不能让不识别过滤器的旧 Provider 返回随机曲目后冒充成功。后续先明确兼容行为、声明与查询的验收条件，再迁移卡片和详情，不改变既有 v2 虚接口来“强制所有插件支持”。

全量增量回归不替代从零构建、跨 OS/ABI 矩阵、真实服务器/物理音频或原生视觉。平台账号 UI 和最终 Legacy 删除仍按既定阶段推进。

### 私人发现：独立可选契约与请求边界（2026-10-10）

- 新增独立 DiscoveryProvider/1.0 Session 扩展，明确 PersonalRadio / PersonalRadar 及逐账号 Availability；不修改 v2 枚举、结构或虚表，不归入 Plugin UI API。契约、安装方式及后续验收见 `source-discovery-v1.md`。
- PageRepository 严格解析 Host 私有 selector；旧 Provider 零私人调用，不回退 Random；非法组合提前拒绝。Available 才进入新 fetchDiscovery，清除 selector 后仍用 v2 请求/取消/结果信号；只接受单 Tracks 分区及匹配实例/音源/账号的 Track，私人页面不持久缓存，续页重新检查权限，单实例保留推荐顺序。
- 分发持有独立 callable lease 并检查重入后的存活/代次，覆盖权限检查中关闭会话时卸载 Busy；不再依赖可能在回调中释放的 Registry session lease。
- 新扩展/安装后外部编译、v2 契约、PageRepository、AggregateComposer 共 5 项联合回归通过。测试替身不等于真实私人算法或服务器支持；Home 卡片、Host 模型与权限变化后结果失效仍在下一切片接入，现有平台插件化与 Phase 6 未整体完成。

### 私人发现：Host 模型与安全 Adapter 投影

- MusicHub 新增两个独立发现模型，沿用既有模型代次、分区请求、取消和共享范围；内部模型槽位不是新增 v2 页面枚举。普通推荐/分类不被私人查询覆盖，关闭发现后不再自动刷新，重新打开才主动请求。
- SourceRegistry 转发当前会话的有效能力失效通知并在会话退休时通知 Host；PageRepository 撤销在途私人请求和缓冲游标，MusicHub 立即清除旧行并按当前范围重新查询。重入/范围变化/关闭会作废排队刷新，旧 key 与迟到结果不能执行。
- OriginalUiMusicAdapter 增加 personalRadio / personalRadar 安全投影及 idle/loading/ready/empty/unsupported/forbidden/failed 状态；动作仍恢复私有完整项后经 Coordinator / Router。私人行的错误也只展示 Host 分区状态，不输出逐账号诊断/身份字段。部分实例不支持不阻塞支持实例续页，成功空结果不误标为不支持，真正失败保留重试。
- 主程序构建及 v2/发现契约、外部安装编译、MusicHub、Adapter、PageRepository、Registry 共 7 项联合回归通过。覆盖独立模型、真实请求与动作链、混合实例、权限失效、取消/迟到、会话替换、Hub 销毁和 Registry 当前会话通知；尚未迁移生产 Home 私人入口，也未验收真实平台服务。下一切片迁移原卡片与详情，不宣称私人插件或 Phase 6 整体完成。

## 后续顺序与验收

本轮分区状态修正已通过主程序构建与六个相关套件：MusicHub、OriginalUiMusicAdapter、MusicHubQml、OriginalUiRecommendationQml、OriginalUiActionsQml、OriginalUiStructure；未执行真实服务器音频或手工视觉验收。

1. Home 剩余旧卡片：为私人漫游/雷达与热门歌单建立通用展示/动作来源后再删除对应 `MusicApi` 分支，保持原卡片视觉；未来若要从已移出队列的历史记录重新播放，应增加可信插件媒体项查找能力，不能猜造动作。
2. Playlist/Category：逐页删除旧平台枚举及路径队列写入；用 Adapter 页面模型、动作和详情导航替代。收藏页已完成页面级去回退，但关注歌手、历史占位与更广泛实例失效验收仍待补齐。测试分页、错误/空态、收藏/取消收藏及实例失效。
3. File/Download、Queue、Player 和桌面歌词：清理剩余直接音源分支，确保实际播放只由 Coordinator 发起；保留原控件和布局。验证来源标签、切歌、停播、恢复和卸载后的展示状态。
4. 全量扫描及 UI smoke：页面新增一个 Source Plugin 不需要改 source switch/case；无权限动作不显示或不可触发；完整构建、CTest 和 macOS 手工视觉/音频检查通过。

Kugou、Netease 业务及账号/登录 UI 的插件化属于后续阶段。在其 Source Plugin 可用前，不用 Host 的旧平台 fallback 冒充动态 Source；其他页面的 legacy 分支按上述顺序逐项迁移，不能一次性删掉仍在使用的代码。
