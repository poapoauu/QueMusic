# Source Artist Browse v1

日期：2026-10-13。基线：最终架构方案及 Source SDK v2。此扩展只选择标准数据浏览目标，不管理账号、资源或播放，也不属于 Plugin UI API。

## 契约

`extensions/artist-browse/v1/IArtistBrowseProviderV1.h` 为可选 Session 接口，IID 为 `org.quemusic.source.extensions.ArtistBrowseProvider/1.0`。不修改现有 v2 类型布局、枚举值、虚表或 IID。

- 没有扩展的旧 Session：保留 Artist → Category/Albums + artistId。
- 有扩展的 Session：对完整同实例/账号 Artist ref 返回 Albums、Tracks 或 nullopt。nullopt 和未知枚举拒绝导航，不请求 fallback。方法同步、无 I/O、不发信号、不修改状态；Host 在 Ready Session 所在线程持有 callable plugin lease 调用。
- 不从 QML metadata、平台名称/编号或账号候选记录推断浏览目标。声明 Tracks 不是 Play 权限，也不表示认证成功。
- Host 仅在显式导航/执行时查询扩展，不为 getter 创建会话。用户从缓存卡片首次导航可显式走 Registry 的正常 Session 创建/open 生命周期；分页复核只查询既有会话。旧插件 Connecting 时仍可记录 Albums 导航、等待 Ready 后派发；新扩展必须 Ready 才能调用选择方法。身份、启用、线程、退役/销毁和递归检查失败即拒绝；失败不修改导航栈。回调期间旧 Session 被关闭/替换后，旧返回值不能继续使用。

## Tracks 执行路由

Host 保存 Category/Tracks + concrete source scope + 两个过滤字段：`artistId` 为非空字符串，`quemusic.artist-browse.v1` 为字符串 `tracks`。page/section/scope/search/字段类型和数量必须完全符合契约；不能 aggregate、混入其它过滤字段或将 Albums 请求伪装成 Tracks。

PageRepository 在每次首次请求、翻页、重试/刷新时重新查询活跃 Session 扩展。缺失扩展、nullopt、Albums 或非法枚举一律拒绝，不转发到可能忽略过滤条件的旧 IPageProviderV2。成功后只移除版本化 selector，以标准 Category/Tracks + artistId 调用已有 `fetchPage`，沿用 UUID、异步终态、取消和不透明游标。selector 保留在 Host 查询/游标绑定中，不入普通磁盘页缓存。

响应必须完整且非 cached，恰好一个 Tracks section；条目全部为同 source/instance/account 的非空 Track ref。空 Tracks section 成功；hasMore 必须有 nextCursor。继续通过既有 DTO sanitizer、Source Registry、Action Router 和 PlaybackCoordinator，不返回或播放媒体资源。

## 兼容与迁移边界

新 Host + 旧插件默认专辑路径不变，扩展通过 qobject_cast 可选发现。新扩展头文件随 PluginSdk 安装，Qt-only 外部工程验证可独立编译及发现；新增接口不意味着其他 Host 自动支持歌曲浏览。

旧 Host 不理解该扩展，仍发送 Albums；新插件必须拒绝不支持的请求，不能把歌曲冒充专辑。发布歌曲浏览能力前仍须明确最低 Host 版本/功能支持要求，本阶段不设置实验酷狗包默认启用、不迁出旧登录。安装包与同版本开发环境的兼容验证不等于旧 Host 的功能验收。

当前切片先完成 Host 通用契约，酷狗 Session 尚不实现此扩展，也不发布 Artist 页面。下一切片再接歌手列表、artistId/ref/游标、未知总数分页策略与标准 Track 索引，保持实例隔离和未知权限拒绝；真实服务字段、TLS、账号、Keychain、扫码及音频验收仍独立。

## 测试与验收

受控动态夹具验证新扩展歌曲目标、显式专辑目标、旧插件默认、拒绝/非法目标、身份、QML 元数据无权、返回导航与分页复核；补缓存卡片首次导航及旧 Connecting 延迟派发、getter 不创建会话。强制错误 section/跨账号/非 Track 回包必须拒绝。版本化 malformed/aggregate 查询和旧 Provider fallback 必须在 fetch 前拒绝。退役回调测试要求返回拒绝、无悬空访问，回调期间卸载 Busy，调用栈退出后可正常卸载。独立 Qt-only 安装工程检查头文件、IID、可选发现与 v2 ABI 保持 2。
