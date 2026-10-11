# Phase 7：酷狗 Source Plugin 与 QR Management UI

设计基线：[最终架构方案](QueMusic-Plugin-Architecture-Final-Plan.md) Phase 7。日期：2026-10-11。

## 当前实际状态

Phase 6 动态 Source UI 代码出口已完成，见[出口报告](phase-6-exit-report.md)。酷狗已有隔离的实验 v2 Source 包，但尚未成为应用默认可用音源，也未迁出 QR/账号业务；不能把以下基础切片视为 Phase 7 完成。

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

这是匿名私有 Session 的验证机制，不是应用恢复播放完成：Host 仍须通过其当前权限链；已有 playableAtEnqueue 恢复值仅是历史动作上限，不能替代插件当前验证或触发自动播放。切片 6 使用用户发起的 playQueueEntry 验证该入口，不改 Host 持久化语义。实际端点是否返回这些字段尚未验证；若缺失将明确失败，不猜测免费、绕过付费或回退 Legacy。

## 切片 5：隔离实验包与注册基线

新增生产 `KugouSourcePlugin` 工厂、Qt 元数据和 native-qt manifest（版本 0.1.0，Source SDK ABI 2）。工厂只声明 Play，为每实例创建独立 Session/网络管理器；匿名原型没有普通配置字段，因此 Settings Schema 为空，不在 Host 增加登录、设备标识或账号配置字段。Host 的 accountId 在此阶段只是实例标识，不代表已通过酷狗账号认证。

仅 BUILD_TESTING 或显式启用 `QUEMUSIC_BUILD_EXPERIMENTAL_KUGOU_SOURCE` 时生成模块，后者默认 OFF。包输出到 `<build>/experimental-source-packages/kugou`，不在默认 `<build>/plugins`，无 install/应用 bundle 规则、无默认来源接线。测试单独把该目录交给 PluginManager。这是可加载的实验包，不是发布可用音源。

注册测试加载实际模块并验证 descriptor/ABI/空 Schema、多实例 Registry 创建、独立网络管理器、标准 Provider、未索引 Lookup、实例禁用/重新启用/清理及工厂拒绝 Host secret/未知参数。Coordinator 测试只验证未可信恢复、跨账号身份和禁用实例拒绝；没有调用 Provider 或网络传输，也没有播放器 prepare/play。成功搜索/解析/恢复链及卸载中的活动请求仍需要下一切片的可控网络夹具端到端验证。

本轮不变更公共 SDK/Plugin UI API/Host QML/账号持久化格式，不删除旧酷狗业务或声称扫码认证完成。

## 切片 6：受控传输下的 Registry/Coordinator 成功链

状态：2026-10-11 正常全目标构建、四项酷狗专项与全量 **79/79 CTest** 已通过。以下证据仅覆盖受控传输，不代表真实平台或音频验收。

新增仅在 BUILD_TESTING 编译的原生测试工厂与独立包目录 `<build>/tests/kugou-source-fixture`。工厂共享插件私有 descriptor/Schema 定义，创建真实 KugouSourceSession/client/mapper，只把网络管理器替换为无 socket 的测试传输。生产工厂不增加测试方法、环境变量、端点覆盖或 Host 配置字段；生产实验包及默认应用来源不变。

测试通过 PluginManager、临时 SourceAccountStore 与真实 SourceRegistry/PlaybackCoordinator 建立链路。搜索结果经 Coordinator 解析后只有 Sink 接收完整 StreamDescriptor；公共 queue/currentItem 不含签名 URL/token。恢复历史队列不自动发网络请求或播放；用户调用 playQueueEntry 后，对未索引 ref 重新验证完整响应，缺失权限、付费、hash 不符或非法 URL 不进入 Sink。

进一步覆盖实例快速切换取消旧验证、解析期间搜索发现付费限制、用户停止/实例禁用后的迟到回包、活动 lease 阻止卸载且清理后可卸载，以及 Sink prepare 失败不进入 play。Sink 为 Host 播放边界测试替身，不输出真实音频。该证据不能代替真实酷狗协议、授权/扫码或产品内体验验收。

## 切片 7：QR 请求构造基础

新增插件私有 `KugouQrRequests.h`，先迁出旧 `kugouGet` 的确定性请求构造逻辑，不调用 Host KugouApi/AccountManager。固定 HTTPS 获取/轮询端点与业务参数，不接受自定义端点或覆盖保留参数。签名继续使用仓库原有的排序 key=value / UTF-8 / MD5 协议；这只是本地兼容基线，不是服务协议仍有效的证明。

设备身份由后续插件实例管理器显式提供；此纯函数不生成/持久化身份、不读取 Account.ini 或 cookie、不授予登录状态。验证 24 位大写字母/数字 dfid、规范无符号 128 位十进制 mid、正整数秒时间以及有界非空 ASCII 轮询键，拒绝控制字符和额外获取键。请求禁止自动 cookie 读取/保存、认证复用和重定向，设置 15 秒超时；不打印 URL、签名或二维码键。显式百分号编码保证不透明键中的 `%26` 等内容不会被当成已编码分隔符而改变签名输入。

测试复用请求层专项目标，包含独立计算的固定签名向量、端点/参数白名单、header/query 一致、编码往返、时间/身份上界及隔离选项。该构造器尚未接入网络发送，仍需插件私有设备身份生成/存储、严格响应映射、可取消传输、轮询代次/超时/退出保护及 SecretStore 账号绑定，再进入 Management UI 和真实服务验收。

Qt 6.11.1/macOS x86_64 Debug 全目标构建成功，最新请求层测试目标再次重建；酷狗四项专项 **4/4 通过（1.78 秒）**，最终全量 **79/79 CTest 通过（75.09 秒）**。测试数不增加，因为新验收加入现有请求层目标。无真实服务、账号或音频访问；没有修改 SDK v2、Plugin UI API、默认来源接线或持久化格式。

## 切片 8：设备身份与严格 QR 响应映射

新增插件私有 `KugouQrProtocol.h`。设备创建显式绑定 `kugou/<accountId>`，生成标准 UUID v4、独立 24 位 dfid，并将 UUID UTF-8 MD5 的大端 128 位值转换成十进制 mid。没有全局设备缓存、Host Account.ini 读取或持久化；当前身份是临时对象，未来须由各实例认证管理器持有，并明确存储/重开策略后才接入登录流程。

获取响应要求数值 status=1、object data 及有界非空 ASCII qrcode；扫码落地 URL 固定为官方 HTTPS 路径，键采用百分号编码，不能注入额外参数/fragment。轮询仅接受明确数值 0/1/2/4：过期、等待、已扫码不返回任何凭证；确认状态必须同时有合法字符串 token 和规范正整数用户 ID。JSON 数字 ID 仅允许精确整数安全范围，较大 ID 必须为规范十进制字符串。兼容 user_id/userid，但两者同时出现须一致，缺失/无效字段不借另一个字段绕过。控制字符、空 token、分号、超长 token、未知/字符串状态均拒绝。

token/用户 ID 只保留在插件私有强类型结果，不注册 Qt metatype、不生成 cookie、不暴露昵称/头像/原始字段到 QML、不复制服务错误文本。此纯映射不验证请求代次或账号绑定，也不授予 Source Ready/登录状态；这些必须由后续可取消认证控制器与 SecretStore 持久化结果共同决定。

新增测试复用请求层目标，覆盖实例/账号身份、UUID/dfid/MID 与独立固定向量、非法 digest 长度、扫码键编码、明确状态、凭证类型/注入边界、账号别名冲突和 JSON 精度风险。首次专项的低位 digest 向量误写为 17 字节，拒绝符合预期；修正测试向量后重建，四项酷狗专项 4/4 通过（1.66 秒）。Qt 6.11.1/macOS x86_64 Debug 全目标构建成功，最终全量 **79/79 CTest 通过（74.62 秒）**，包括 macOS 打包/重定位检查。真实服务/账号/音频均未访问，SDK v2 与 Plugin UI API 不变。

## 切片 9：可取消 QR JSON 传输

`KugouApiClient::getQr` 只接收私有 Operation、设备身份、秒时间和二维码键，经切片 7 固定端点/签名构造器后发送；不接受任意 URL、Host cookie 或自定义保留参数。匿名搜索/资源与 QR 共用私有 dispatch 和现有 UUID Pending 表，不复制一套取消、错误与 JSON 生命周期，也不改变公开 Source SDK v2。

QR 复用现有 15 秒传输超时、1 MiB 响应上限、禁止自动 cookie/认证复用/重定向和固定脱敏错误；增加与既有请求一致的 User-Agent。响应仍是插件内部 JSON，不以 HTTP/JSON 成功推断登录成功，必须由切片 8 映射及后续认证控制器验证。默认网络管理器仍由 client 独立拥有，注入仅用于受控测试。

新测试验证固定获取/轮询端点、设备请求字段、每请求 UUID、乱序完成、取消旧轮询后包含旧 token 的迟到回包不投递、cancelAll、非法输入异步失败、网络管理器销毁只完成一次及缺失网络的待失败请求可取消。未发送真实网络请求。传输取消不是认证代次/状态机验证；后续控制器仍须绑定实例与当前代次、限制单个在途轮询、设置总超时，并在取消/重试/销毁后阻止旧结果授予账号状态。

Qt 6.11.1/macOS x86_64 Debug 全目标构建成功，酷狗四项专项 4/4 通过（3.42 秒），最终全量 **79/79 CTest 通过（73.86 秒）**，包括 macOS 增量打包/重定位。未接入默认 Source、SecretStore、登录 UI 或真实扫码；Phase 7 尚未完成。

## 切片 10：实例绑定的 QR 轮询控制器

新增插件私有 `KugouQrController`，由插件 C++ 持有，不注册到 QML/Plugin UI API。固定实例/账号身份及独立临时设备；生产构造器独占网络管理器，测试构造器借用受控传输。控制器、client 和网络操作须在同一所属线程使用。本切片尚未接入 Source 工厂、Registry/UI lease 或 SecretStore。

每次 start 分配新 attempt UUID，清理旧请求/计时器/二维码/凭证；每个响应还须匹配当前 request UUID。轮询单次计时器只在当前响应完成后调度，始终最多一个在途请求。总超时默认 120 秒、单调时钟复查响应和派发边界，并由精确 deadline timer 终止；默认轮询间隔 2 秒。传输错误和严格映射失败进入固定脱敏 Failed，平台过期或本地 deadline 进入 Expired，所有终态停止计时、取消在途请求并清除二维码。

Confirmed 仅代表当前代次得到了经过映射的候选凭证，不是账号存储成功或 Source 登录/Ready。凭证只能由匹配 attempt 的 C++ 调用者一次性取走；状态信号不带 token，confirmationReady 仅带代次 ID。取消、重试、销毁清除旧候选凭证。每次状态通知后检查对象存活、代次和状态，通知中取消/重试/销毁不得继续调度旧流程。

测试加入已有请求层目标，覆盖等待/扫码/确认链、单在途轮询、一次性凭证、实例隔离、取消重试与旧响应、事件循环尚未派发 deadline 时的过期拒绝、deadline 取消在途轮询、错误/非法配置、通知重入/销毁（含确认通知内重试后不发旧凭证信号）和网络所有权。Qt 6.11.1/macOS x86_64 Debug 最新全目标构建成功，酷狗四项专项 **4/4 通过（3.78 秒）**，最终全量 **79/79 CTest 通过（64.00 秒）**，包括 macOS 打包/重定位。全部使用无外网夹具；真实扫码、账号绑定持久化和 UI 尚未验收。公开 SDK v2/IID、Plugin UI API、默认 Source、Host UI 与磁盘格式不变。

## 切片 11：私有凭证记录与 Host 存储契约

最终方案第 12 节要求插件负责认证、Host 管理 Secret 持久化。检查确认现有 PluginUiSettingsBridge/SourceAccountStore 已提供命名 Secret 事务、实例身份冲突检查、轮换回滚和清除；不应让酷狗链接 Host 私有账号类或另写明文文件。

新增插件私有 `KugouCredentialRecord` version 1（不是 SDK/Host 磁盘格式升级），保存 package/source/instance/account、UUID/dfid/mid 和规范用户 ID/token。编码前检查实例身份、UUID v4、设备摘要一致和凭证边界；读取必须匹配调用者的实例/账号及可选既有用户 ID，拒绝额外/重复字段、非规范编码、未知版本、类型混淆、错误设备摘要和超限数据。记录不生成 cookie，不作为登录权限或 Source Ready 证明。

控制器新增 C++ 一次性 takeAccount(attempt)，原子交接实际认证代次使用的设备与候选凭证，和原 takeCredentials 共用一次性消费状态。记录/候选凭证不注册 metatype、不提供 QML 可调用属性。获取后仍须经过 Host 存储成功及后续绑定/Session 验证，不能把确认信号等同于持久登录成功。

集成测试使用真实 HostPluginUiSettingsBridge/SourceAccountStore 和临时 QSettings、内存 SecretStore，验证命名 secure envelope、INI 不含 token/设备标识、配置重开、包身份冲突、轮换写入失败/旧 Secret 清理失败回滚、失效 bridge 不接受晚写及清除。只有测试夹具声明 credentialBundle Secret；生产工厂仍保持空 Schema，无手工 token/账号表单、无默认 Source 改动。存储契约测试不是生产 Backend/Management UI 接线验收，也不是系统 Keychain 或真实账号验收。

生产 UI 接入前须明确插件业务 Secret 的声明/展示边界：现有空 Schema 不接受该字段，不能将测试夹具 Schema 直接上线为 Host 普通输入框。该差距应在独立 Plugin UI API 的可选 C++ 存储契约上演进或复用已有安全桥，不改 Source SDK v2 布局，不借用 Host Account.ini。

Qt 6.11.1/macOS x86_64 Debug 正常全目标构建成功。首次构建发现集成测试目标缺少插件私有头文件搜索路径，改为显式引用私有 codec 头文件后重建通过；没有因此让生产插件依赖 Host 私有存储实现。Host bridge 与酷狗五项专项 **5/5 通过（4.43 秒）**；最新全量 **79/79 CTest 通过（63.68 秒）**，包括 macOS 打包/重定位。公共 SDK v2/IID、Plugin UI API、Host 磁盘格式和默认 Source 不变；生产认证存储接口/Management UI、真实扫码、系统 Keychain 和实际音频仍未验收。

## 推荐的后续切片与验收

1. v2 Session/mapper：当前匿名搜索、解析、同步 Item Lookup、未知媒体异步验证及隔离实验包已实现；Registry 工厂/生命周期、拒绝路径及受控传输成功/恢复/切换链进入测试。后续补真实业务响应验证和实际动作能力，再对应用公开；保持 Lookup 无 I/O，不以恢复快照授予当前权限，不把解析 URL 或旧路径交给 QML。
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

## 切片 5 验证

Qt 6.11.1/macOS x86_64 Debug 全目标构建和酷狗三项专项通过；最终 **78/78 CTest 通过（63.68 秒）**。实际动态模块通过 PluginManager manifest/runtime/SDK 接口检查，并经 SourceAccountStore/SourceRegistry 创建两个独立匿名实例。禁用/重开与拒绝 Host secret/未知参数测试通过；Coordinator 的三条拒绝路径均无 Provider requestStarted、无传输完成回调、无 Sink prepare/play。

生成 manifest 与实验模块均位于独立实验目录。额外检查默认 `<build>/plugins` 与 `bin/QueMusic.app/Contents` 的 manifest/酷狗库产物，仍只有 Local/Navidrome 包，没有实验酷狗包。未做真实网络、扫码、实际音频、成功播放端到端、安装/重定位或跨平台发行验收；本机动态注册和拒绝路径通过不等于 Phase 7 完成。

## 切片 6：验证与已恢复的环境阻塞

首次全目标构建成功，前三项酷狗专项通过；新集成程序打印 13 个用例通过后在进程退出时 SIGSEGV，因此首次 CTest 判为失败，不作为验收通过。崩溃报告显示 QObject 销毁时调用已卸载地址。依赖检查发现新测试宿主未直接链接 Qt Network，而真实 QueMusic 主程序有该进程级依赖。CMake 补齐测试宿主 Qt6::Network，保留 Busy/Success 卸载断言，不通过关闭测试或禁止卸载掩盖问题。恢复后的正常重建与专项/全量复跑均正常退出，验证此依赖修正在本机有效。

随后两次正常重建在第三方 qmcorecmd 启动阶段停止进展；单独重链接用于诊断的测试二进制也未进入 main/test 输出。macOS 系统日志记录 AppleSystemPolicy `Unable to apply provenance sandbox`（对应本次启动的 qmcorecmd），相关进程为 U 状态。没有移除 provenance 属性、关闭安全检查或修改系统服务。

本次续验时旧阻塞进程已退出，正常全目标构建成功；酷狗四项专项 **4/4 通过（1.80 秒）**，包含进程正常退出与实际夹具模块卸载；最终全量 **79/79 CTest 通过（76.10 秒）**，包括 macOS 增量打包/重定位检查。使用的是正常 CMake 重建产物，不以之前手动重链接的诊断产物作为验收依据。

本切片仅抽出插件私有工厂定义并新增测试工厂/集成测试；Source SDK v2 ABI、扩展 IID、Plugin UI API、Host 恢复逻辑和磁盘格式不变。真实酷狗响应字段、账号认证、QR Management UI、实际音频与 Host 平台业务迁出仍未完成。下一步按推荐顺序迁移插件私有 QR 协议：先固化签名/设备身份与严格响应映射，再加入可取消的请求和轮询生命周期；不直接复制 Host Account.ini 持久化、凭证日志或缺少请求代次隔离的旧实现。
