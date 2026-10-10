# Source Discovery 扩展 v1

本扩展在最终架构基线及 Source SDK v2 上演进，不修改 v2 的枚举、结构布局、Provider 虚表、IID 或会话信号，也不属于 Plugin UI API。

## 范围与契约

公共头文件：`sdk/source/extensions/discovery/v1/IDiscoveryProviderV1.h`。独立 IID 为 `org.quemusic.source.extensions.DiscoveryProvider/1.0`，随 `PluginSdk` 安装，外部插件只需链接现有 `QueMusic::source_sdk`，不增加运行时共享库。

- `PersonalRadio` 与 `PersonalRadar` 是明确的逐账号私人发现业务，不是 Random、Newest 或普通推荐的别名。插件可以只支持其中一种，旧插件完全不必实现扩展。
- 扩展实现于 v2 Session，而非插件根对象。Host 在 Ready 会话的所属线程持有 callable plugin lease，先查询 `discoveryAvailability(kind)`，每次请求（包括游标续页）重新检查。
- Available 才调用 `fetchDiscovery`。无扩展或 Unsupported 返回 Unsupported；Forbidden 返回 Authorization；Unavailable 及未知状态按 Unavailable 拒绝。插件认证/账号 UI 仍由插件自行提供，Host 不新增平台登录入口。
- Host 私有路由用 `PageQueryV2.filters["quemusic.discovery.v1"]`，仅接受精确字符串 `personal-radio` / `personal-radar`。必须为 Recommendation / Tracks、无 searchText、无其他 filters；非法组合在分发前拒绝。绝不把该 selector 转交给旧 `fetchPage`。
- `fetchDiscovery(kind, query)` 收到具体实例的 scope、空 filters、标准 Tracks 分区和插件自己的不透明 cursor；selector 由 Host 移除。结果仍经过统一 PageRepository / AggregateComposer / MusicPageModel 的校验与安全投影。
- 沿用 v2 requestStarted / pageReady / requestFailed 和 cancel(id)：返回 ID 必须与 requestStarted 一致，终态为 complete 且非 cached。结果恰有一个 Tracks 分区，项目都是该实例的 Track。空结果须显式给出空 Tracks 分区；hasMore 必须附非空 nextCursor。
- 私人发现不写持久页面缓存；单实例顺序保留插件推荐排名，聚合模式使用既有各实例交错/身份去重和会话绑定游标。权限不能因持久缓存或未知过滤器被绕过。
- 插件只提供媒体项和解析能力，不在此方法内启动播放器。最终播放/入队仍由 Core 的 PlaybackCoordinator；收藏等动作仍由 Action Router 并独立验证权限。
- 有效发现权限变化使用已有 capabilitiesChanged 通知（即使普通 v2 action 值未变）；Host 展示生命周期接入应使旧结果及在途请求失效。

## 当前落地状态

契约/安装及 PageRepository 分发已完成。Host 已有独立 MusicHub 模型和 OriginalUiMusicAdapter 私有投影、逐范围代次、关闭与权限/会话退休失效；私人行不透传逐账号错误信息。7 项相关回归通过；不支持的聚合成员不会阻塞支持成员的分页，成功空结果与不支持分开呈现。生产 Home 入口及真实平台插件服务仍待后续切片，契约不代表所有插件已支持私人发现。

## 实施顺序及验收

1. 可选接口、严格 selector、分发/结果校验及安装契约：测试无扩展的旧插件零私人请求、普通 Random 不受影响、两个逐账号业务及全部 Availability 状态、错误请求/结果/ID、聚合部分不支持、续页权限重新检查、插件排序和会话关闭时 callable lease。
2. Host 私有发现模型与 Original UI Adapter：复用既有模型代次、范围变化、取消及 section 游标；只输出安全展示角色及私有 key。验收跨账号/范围/卸载/权限变化后旧行不能执行，错误/空/不支持分开显示，普通推荐和分类状态不串页。
3. 原 Home 卡片与详情迁移：保留视觉布局；删除入口独占的 MusicApi FM/radar 查询、hash/path 队列与旧收藏处理。缺扩展明确不支持，绝不回退 Random 或 Legacy；生产 QML 测试覆盖操作、权限、游标、空/错态、重复点击及上下文失效。
4. Kugou / Netease 等后续插件按真实服务实现扩展并验收认证、私人算法、服务器游标与权限。契约和替身通过不等同真实服务完成；仍在使用的其他 Legacy 平台功能不能提前删除。

新增扩展仅保证同一已支持 Qt/编译器 SDK 环境的契约验证，跨 OS/编译器 ABI、真实服务/音频/原生视觉仍需原计划验收。后续不兼容扩展必须独立升 IID 版本，不能修改已发布 v1 虚表。
