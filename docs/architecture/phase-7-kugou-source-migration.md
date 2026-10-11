# Phase 7：酷狗 Source Plugin 与 QR Management UI

设计基线：[最终架构方案](QueMusic-Plugin-Architecture-Final-Plan.md) Phase 7。日期：2026-10-11。

## 当前实际状态

Phase 6 动态 Source UI 代码出口已完成，见[出口报告](phase-6-exit-report.md)。酷狗尚未成为标准 v2 Source Plugin，不能把以下基础切片视为 Phase 7 完成。

本轮按已有代码检查：

- `api/KugouApi.*` 在 Host 内承担搜索、分类、榜单、歌手、资源和 KRC 歌词请求，使用平台整数结果协议；没有可供 v2 Session 使用的每请求身份与取消机制。
- `cpp/AccountManager.*` 在 Host 内进行签名、设备标识持久化、QR 获取/轮询、账号状态和 cookie 写入；`SettingsView.qml` 仍有酷狗专用 UI。
- 原 `getPersonalFm/getPersonalRadar` 实际取 TOP500/飙升榜。迁移时不能把公共榜单冒充私人发现算法，也不能为此虚报 Capability。
- Local/Navidrome 的 Registry、Session、Capability、Coordinator 和 Plugin UI 基线已存在，优先复用，不再建立平台编号播放链。

## 切片 1：插件私有 JSON 请求层

新增 `plugins/kugou-source/KugouApiClient.*` 和 CMake 私有静态库，**尚未链接到 Host、注册 Source、生成 manifest 或替代现有酷狗业务**。此结构为随后 Session/认证接线提供请求生命周期，避免在 Host 新增专用配置/业务 UI。

当前支持匿名歌曲搜索与资源信息请求，固定路由来自既有 KugouApi。搜索采用 HTTPS，不允许降级 HTTP；HTTPS 端点的真实服务可用性尚未验证。GET 参数由后续 Session 显式构造，传输成功只表示有效 JSON object，不等同于平台业务状态成功。

- 每请求 UUID；并发回包不以 action 名称或平台整数关联。
- 默认每 client 自建网络管理器，禁用自动 cookie 读取/保存和认证复用，不借 Host 账号。注入管理器用于无真实网络测试。
- 只接受预定义 endpoint，不接收外部任意 URL；拒绝自动重定向，15 秒传输超时，JSON 响应上限 1 MiB。
- 独立取消/全部取消、client 析构、网络管理器/Reply 消失和取消后的迟到回调保护；起始失败延迟发送，调用者可先记录 request ID，也可取消。
- HTTP 401/403/404/429/5xx 和网络失败映射到既有 SourceErrorV2；不把 URL、原始 body、headers 或 Qt errorString 复制到错误/日志。
- 兼容成对 KG_TAG 外层包装，不全局改写 JSON 内部字符串；非法 JSON、数组、超限响应拒绝。消费失败的观察者可删除 client，不继续访问已析构对象。

不改 Source SDK v2、插件 ABI、Plugin UI API 或持久化格式。此 client 是插件私有实现，不是新公共 SDK。

## 切片 2：匿名 v2 Session 与可信媒体映射

新增 `KugouSourceSession`、`KugouMappers` 和 Session 专项测试；仍为插件私有静态实现，**没有注册可发现的酷狗包或修改主程序接线**。本轮只建立标准 v2 Provider 契约，尚未通过 Registry/PlaybackCoordinator 的真实集成测试，不等于已经可以在应用里用新插件播放。

- 复用 IMusicSourceSessionV2 / IPageProviderV2 / IPlaybackProviderV2；每次异步方法先发 requestStarted，再异步派发或失败。取消、关闭、重开和通知中重入/析构后不投递旧结果。
- 配置限定本插件 package/source、完整实例与账号身份，匿名原型拒绝传入 Host cookie/secret 和未知参数；默认网络管理器由实例拥有。Ready 表示本地匿名配置就绪，不代表真实服务器或登录验证通过。
- 只声明 Play，搜索只支持 Tracks；其他页面、媒体类型、收藏/下载/歌词/封面/私人发现不伪装支持。暂不处理已有账号迁移。
- 搜索生成带完整 source/instance/account/type 的 32 位十六进制 hash ref、毫秒时长和白名单展示字段；不复制响应内的 URL/token/质量路径到 MediaItem metadata 或 QML。
- 仅 pay_type 明确为数值 0 的条目有匿名播放候选能力。缺失、字符串或付费值保持受限；同实例已见限制不能被较旧的免费搜索回包撤销，资源完成时再次核验已见权限。
- opaque 游标绑定搜索词、limit 和 Session，上限 1024；已确认条目缓存上限 4096，关闭清除。达到条目上限时新 hash 保持不可播放，不清除已有受限记录以便旧回包重新获权；达到游标上限时旧游标清理后失效。
- resolveStream 只接受当前实例已确认可播放的完整 ref，核验返回 hash、业务状态、HTTP/HTTPS URL（禁止文件/data/相对 URL、userinfo、fragment）。只发标准 StreamDescriptor 给 Host/Core，不调用播放器、不返回旧路径，不自动升级质量或绕过受限资源。

切片 2 当时的重要限制：未在当前 Session 重新确认的恢复媒体直接拒绝解析；切片 4 已在插件私有解析中增加异步验证，但尚未接入 Host 的恢复权限链。切片 3 的同步 Item Lookup 始终不承担网络验证。不应把此原型直接注册成最终可用音源。免费/付费字段和资源 hash 的真实平台响应、HTTPS 搜索、授权失效等仍需要真实服务验证，不能凭测试夹具认定协议完备。

## 切片 3：当前会话 Item Lookup

复用已有可选 `IItemLookupProviderV1`，不修改 SDK v2 或扩展接口定义。索引从单独权限记录改为有界的完整白名单 `MediaItemV2`，查询要求 Ready 且完整 ref 精确匹配（来源、实例、账号、类型、规范 hash）。返回独立 DTO 副本，不执行 I/O、不解析流、不导入历史展示快照，也不直接授予播放权限。

搜索及资源完成仍检查同一索引中的权限；已见限制保持交集语义，不被迟到免费回包恢复。关闭/重开清空索引，另一个实例不共享条目。满 4096 项后新条目仍显示为不可播放且不进入 Lookup，不驱逐已有受限项。媒体 URL、token 和响应原始字段不进入索引。

此切片仅补齐标准同步查找契约；未知恢复媒体仍拒绝解析，尚未实现异步可信网络重新验证、可加载插件包或 Registry/Coordinator 集成，Phase 7 未完成。

## 切片 4：插件私有异步资源身份验证

未知但身份合法的完整 Track ref 可以通过现有异步 `resolveStream` 请求重新验证，不新增公共 SDK 方法，不把同步 Lookup 改为网络调用，也不读取历史展示快照。跨来源/实例/账号、非法 hash/type、已索引受限项或索引已满的未知项，在请求前拒绝。

未知项验证要求业务 status 明确为数值 1、返回 hash 精确对应请求、非空 songName 和数值 pay_type。只有 pay_type 为 0 且资源通过既有 URL 安全校验才索引为可播放并发 streamReady。缺少权限字段、字符串状态/权限、空名称、不匹配 hash 或非法 URL 不授予播放能力。展示 DTO 只投影 songName/author_name/timeLength；URL/token 不进入索引。明确付费响应可索引为受限项，后续重试不能撤销该限制。

完成时重新检查当前权限与容量，搜索期间观察到的限制不能被验证响应覆盖；取消、关闭/重开后的回包不能建立索引。默认每实例网络管理器和生命周期契约不变。

这是匿名私有 Session 的验证机制，不是应用恢复播放完成：Host 仍须通过其当前权限链，不能给持久化快照恢复 Available 或自动播放。后续插件注册与 Registry/Coordinator 测试必须确定未知恢复 ref 的验证入口并覆盖最终权限检查。实际端点是否返回这些字段尚未验证；若缺失将明确失败，不猜测免费、绕过付费或回退 Legacy。

## 推荐的后续切片与验收

1. v2 Session/mapper：当前匿名搜索、解析、当前会话同步 Item Lookup 及未知媒体异步验证机制已接入私有实现；下一步补插件描述/Schema/manifest 与注册，再做 Registry/Coordinator 集成及 Host 恢复权限链验证。保持 Lookup 无 I/O，不以恢复快照授予权限。补实际动作能力和真实业务协议验证后才对应用公开，不把解析 URL 或旧路径交给 QML。
2. QR 认证协议：设备标识、签名、获取/轮询/超时/取消/重试、登录/退出、实例隔离和秘密存储全部插件负责。为令牌和账号建立可信绑定；不自动导入 Host Account.ini，不输出凭证日志。用协议夹具覆盖晚到回包、过期、错误和销毁，再单独进行真实扫码验收。
3. Management UI：实现独立 Plugin UI API provider/context 与 `import QueMusic.PluginUI 1.0` QML，复用稳定 Theme/UI Kit；QR 与账号 UI 仅在插件可用时出现。普通设置走 Schema，不引用 Style、AccountManager 或其他私有 Host QML。
4. Host 迁出：新插件和 UI 验收后去除酷狗登录实现/入口和 Core 直接引用；网易迁移不在此切片处理。测试禁用/删除插件后无可用酷狗来源、无酷狗登录 UI，以及无 Source 到旧平台 fallback。
5. 补齐既有目标数据能力、业务错误及真实集成测试后，按最终方案关闭 Phase 7。不能只靠“插件目录存在”、传输测试或空态截图宣称完成。

## 切片 1 验证

专用测试覆盖路由/请求策略、查询编码、并发身份、取消和迟到回调、异步失败、网络管理器/实例销毁、HTTP 错误脱敏、包装 JSON、响应大小和同步观察者析构。Qt 6.11.1/macOS x86_64 Debug 全目标增量构建成功；专项 CTest 通过，最后全量 **76/76 CTest 通过（66.04 秒）**。无真实账号、外部 API 或实际音频访问；真实 HTTPS 服务、QR 和标准 Source Session 尚未验收。

## 切片 2 验证

全目标增量构建、最新 Session 目标重建和请求层/Session 两项专项复跑通过；最终 **77/77 CTest 通过（61.98 秒）**。新增测试使用真实 Session/mapper 与注入的无外网网络管理器，覆盖标准 Provider 接口、请求启动顺序、搜索/资源身份、时长和脱敏字段、分页绑定/失效、未知/付费条目拒绝、跨实例并发/取消/关闭重开、业务错误、重复 hash 权利交集、迟到免费回包、解析期间权利变化、不安全资源 URL、重入/析构及网络管理器所有权。

首次专项发现两条断言把 v2 的未知/未就绪能力误等同于 Unsupported；保留 SDK 的 fail-closed 语义，Session 显式声明未实现动作 Unsupported、未就绪 Play 为 Unavailable。随后补充推荐等非搜索页面返回 Unsupported 的验收，避免因缺少搜索词而误报参数错误；最终复跑使用最新源码。

SDK v2/Source 插件 ABI/Plugin UI API/磁盘格式不变。尚未加载真实酷狗包、连接外部平台、实际播放音频或迁移 QR UI；Registry/Coordinator 端到端、可信恢复身份验证和真实授权协议留给后续切片，Phase 7 未完成。

## 切片 3 验证

Qt 6.11.1/macOS x86_64 Debug 全目标增量构建成功，酷狗请求层/Session 两项专项通过；最终 **77/77 CTest 通过（75.53 秒）**。新增验收覆盖扩展 qobject_cast、未索引与非 Ready 查询拒绝、完整身份精确匹配、返回副本隔离、白名单展示字段、跨实例不共享、限制不能被免费回包撤销、关闭重开失效，以及填满索引后的溢出拒绝和旧限制保留。同步 Lookup 不产生额外网络请求；全部酷狗协议测试仍为注入夹具，无真实服务/账号/实际音频访问。

只变更插件私有 Session 实现及测试，公共 SDK/扩展 IID、Plugin UI API 和磁盘格式不变。尚未注册酷狗可用包、接入 Coordinator 或完成历史媒体网络验证，不能据此宣称 Phase 7 已完成。

## 切片 4 验证

Qt 6.11.1/macOS x86_64 Debug 全目标构建、最新 Session 测试目标重建及两项酷狗专项复跑通过；最终 **77/77 CTest 通过（74.29 秒）**。新增测试验证未知 ref 的异步完整响应、身份与展示白名单、验证前不入索引、缺失/字符串/错误状态与权限、hash 不符、空标题、非法 URL、付费记录保持受限、验证期间搜索权限降级、取消与关闭重开后的迟到回包，以及索引满时不能通过解析路径绕过限制。

只使用注入的无外网协议夹具，无真实平台、账号或音频验证。未改公共 SDK/IID、Host 恢复逻辑、Plugin UI API 或磁盘格式；实际服务字段验证、可加载插件、Registry/Coordinator 端到端和 QR Management UI 仍待后续完成。
