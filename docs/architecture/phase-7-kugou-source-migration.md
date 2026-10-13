# Phase 7：酷狗 Source Plugin 与 QR Management UI

设计基线：[最终架构方案](QueMusic-Plugin-Architecture-Final-Plan.md) Phase 7。日期：2026-10-11。

## 当前实际状态

Phase 6 动态 Source UI 代码出口已完成，见[出口报告](phase-6-exit-report.md)。酷狗已有隔离的实验 v2 Source 包，但尚未成为应用默认可用音源，也未迁出 QR/账号业务；不能把以下基础切片视为 Phase 7 完成。

最新状态（切片 27）：生产实验工厂已有 backend-only 凭证声明、Management Provider/QML 与 Context-bound Backend；稳定 UI Kit 支持离线 QR payload，Session 可校验同实例的命名凭证记录。公开榜单/歌手已接标准页面/游标及独立显式真实 Session smoke；真实歌手 smoke 本机失败，无可信页面，实际服务和认证仍未验收。酷狗 Session 已实现 Artist Browse v1，发布标准热门歌手/歌手歌曲页面，私有地区分类基础已存在；通用 Browse Facets v1 契约/纯验证/安装测试及 Repository 入口拒绝门已实现，完整活跃守卫、筛选 UI/分类 Session 路由尚未接线。旧插件默认 Albums 不变。普通 Host 表单为空，实验包隔离与旧登录保留。以下各切片是完成当时的历史快照，早期“空 Schema/未接管理 UI”不再描述最新生产工厂。

本轮按已有代码检查：

- `api/KugouApi.*` 在 Host 内承担搜索、分类、榜单、歌手、资源和 KRC 歌词请求，使用平台整数结果协议；没有可供 v2 Session 使用的每请求身份与取消机制。
- `cpp/AccountManager.*` 在 Host 内进行签名、设备标识持久化、QR 获取/轮询、账号状态和 cookie 写入；`SettingsView.qml` 仍有酷狗专用 UI。
- 原 `getPersonalFm/getPersonalRadar` 实际取 TOP500/飙升榜。迁移时不能把公共榜单冒充私人发现算法，也不能为此虚报 Capability。
- Local/Navidrome 的 Registry、Session、Capability、Coordinator 和 Plugin UI 基线已存在，优先复用，不再建立平台编号播放链。

## 切片 27：Repository 保留协议拒绝门

2026-10-13，继续切片 26，PageRepository 在缓存/游标/Session 派发与普通过滤处理之前保留 Browse Facets selector、facetRevision、facetValues。当前 Host 尚无活跃声明/lease/响应验证，因此合法 Host 形状明确 Unsupported，非法/不完整/已移除 selector 的 Provider 形状重放为 InvalidRequest；不将 scalar selector 或未知字段交给旧 Provider 忽略，也不删除字段尝试“修复”。错误仍经既有 request ID/代次异步终态，取消不投递。

新增实际动态旧 Provider 的 Repository 回归：合法首请求/续页拒绝、请求 ID/代次关联，scalar/未知 selector、缺失字段、Provider 形状、aggregate、错误 section、混合 Artist Browse 拒绝；两个实例 Provider calls 均为零、无成功/缓存预览，取消后无终态。显式清除过滤后原 Artists 页仍可请求。只新增入口拒绝，不代表 Registry 发现/schema 复核、动态卸载、严格 Artists 回包、Host 筛选模型或插件分类已完成。

公开 v2/扩展 IID/UI API/存储不变，仅 Host 私有代码与测试增量，需同步重编译；同名 facetRevision/facetValues 的外部非标准过滤器存在语义碰撞风险，实际契约已记录，原 Host 查询不使用这些字段。实验包/账号/播放/旧登录不变，不访问外网。Qt 6.11.1/macOS x86_64 Debug 全目标构建成功，新增 Repository 用例另单独实际执行通过，相关专项 **23/23（11.07 秒）**、最终全量 **84/84 CTest（70.10 秒）** 通过，包括安装 SDK、原 UI、Local/Navidrome、动态管理 UI/卸载与 macOS 打包/重定位。下一步以活跃 Registry/schema/身份/lease 与每次执行复核替换临时 Unsupported 分支，再验证响应/分页/卸载，不直接开放普通 Provider fallback。

## 切片 26：可选 Browse Facets v1 与严格纯验证

2026-10-13，按切片 25 设计新增独立 `IBrowseFacetsProviderV1`、schema/choice/selection 与 header-only 纯验证器，IID 为 `org.quemusic.source.extensions.BrowseFacetsProvider/1.0`。声明仅支持 Category/Artists 根列表单实例单选，不继承 QObject/v2 Session，不修改现有 v2 类型/虚表/IID、Artist Browse v1 或 Plugin UI API。实际规则见[Browse Facets v1 契约](source-browse-facets-v1.md)。

schema 限定 1–8 facet、每 facet 1–64 choice、64 字符 ASCII 不透明标识、256 UTF-16 unit PlainText 标题；验证整体合法、保留 facet id、重复、控制/孤立代理项与 Unicode 边界，不截断、不转换平台编号。查询分别验证 Host 三字段 selector/revision/values 与 Provider 两字段 revision/values，严格 QVariantMap/QString，限具体非空白 scope、无搜索、Artists 和 limit 1–100。另匹配当前完整合法 schema、revision、每个已声明 facet/choice；未知/撤回/非法类型无 fallback。

纯形状验证不查询插件，不承担身份/lease/权限或 Host 执行守卫。不能删除非法 selector 后把合法 Provider 形状当作获准执行；两个解析器无自动投影/回退。Host 守卫、响应验证、快照/选择、UI 与生产 Session 分类声明尚未实现，实验包仍不提供该扩展，不改变默认可用来源或旧登录。

CMake 为 PluginSdk 增加新扩展安装规则。既有 discovery 契约测试/外部 Qt-only 安装工程共同覆盖可选 qobject_cast/IID、旧 Session 缺少扩展、v2 ABI=2、pure validators 零 Provider 调用、schema 超限/重复/非法/Unicode、Host/Provider 形状隔离、类型/组合字段拒绝、revision/撤回/未知选择及不透明数字字符串。四个新增函数已单独实际执行通过；Qt 6.11.1/macOS x86_64 Debug 全目标重建成功，专项 **23/23（9.27 秒）**、最终全量 **84/84 CTest（83.02 秒）** 通过，包括最终安装产物的独立工程、原 UI、Local/Navidrome、动态管理 UI/卸载与 macOS 打包/重定位。

首次编译的夹具 `QVector::append` 初始化重载歧义已改显式类型，未放宽规则；同时复核并修正“删除非法 Host selector 后 Provider 形状也应非法”的测试预期，明确 Host 必须先验证再移除。失败编译不计验收。没有访问真实服务、读取账号或播放；真实 HTTPS/分类字段/分页、认证、手机/Keychain/音频和最低 Host 功能版本仍未验收。下一代码切片接 Registry/Repository 的活跃声明/执行拒绝、生命周期/lease 与响应边界，再接 Hub/插件/UI。

## 切片 25：通用 Browse Facets 的下一阶段设计

2026-10-13，完成[Source Browse Facets v1 实施设计](source-browse-facets-v1-design.md)，明确独立可选接口/有界 schema、稳定不透明选项和 revision、版本化查询 selector、每次执行复核及旧 Provider 不 fallback。第一版限定具体实例的 Category/Artists 根列表单选，不混入 Artist Browse/详情/搜索，不改变 SDK v2/Plugin UI API；Host 不解析酷狗地区编号。已有无筛选列表和旧插件行为保留，getter 只读 Host 快照，明确生命周期、分页/撤回/游标与 UI 边界。

实施顺序为可选契约/纯验证 → Registry/Repository 动态守卫 → Hub/Adapter 快照选择 → 酷狗分类映射/游标 → 原 QML 通用筛选 → 真实分类 smoke。文档列出各步主要文件、兼容风险和验收条件，包括 lease 卸载、错误形状、跨实例、原 UI/主题和外部 SDK。没有实现或冻结新接口/IID，没有改变运行时或把设计当发布验收；上一切片 **84/84 CTest（90.40 秒）** 是当前代码证据，文档切片只检查 diff 与本地引用。下一代码切片从可选契约及纯验证器开始，不需要重新确认实施顺序。

## 切片 24：插件私有歌手分类协议与误路由拒绝

2026-10-13，对照旧 `KugouApi::getSingerCategory`：固定 singer/list，type 为地区 1–4，sex 固定 0，分页及 version/plat；旧解析的“singers 空就 fallback info”不符合现有严格映射边界，不能直接复制。当前 `MusicHub::sectionsForPage` 提供通用 Artists section，但没有分类筛选声明/选项渲染契约。不能把地区整数写入 Host/QML 分支、伪装 Genre ref，或把未知分类请求当作热门歌手成功返回。

私有 `KugouArtistProtocol` 增加 Category operation 和 Region 白名单（Chinese/Western/Japanese/Korean，对应既有 wire 1–4）；固定 HTTPS singer/list，只允许显式合法 region、空 artistId、页码 1..1000000 和 limit 1..100，sex 固定 0。Popular/Tracks 拒绝夹带 region，非法枚举/缺失 region/混合 ID 拒绝，不选择默认地区或 fallback 热门端点。使用现有 client 的 request UUID、异步失败/取消、15 秒超时、1 MiB 上限及无 cookie/认证复用/重定向策略。

分类复用严格 `catalog` 投影：singers/info 只允许单一或相等数组，不 fallback 歧义；ID/名称校验及重复拒绝、敏感字段丢弃、缺失 total 保持未知、空列表不制造内置歌手。该解析是插件私有纯函数，不建立账号或播放权限，也不新增分类媒体实体。

新增受控测试覆盖四地区路径/参数/策略、反序成功 UUID、无默认地区、operation/region/ID/分页非法组合、异步错误及取消错误通知、取消晚包、TLS 拒绝不降级、client 实例隔离/网络销毁、严格 DTO 投影。Session 新增过滤边界回归：直接携带 area/region/type/sex 的 Category/Artists 请求必须异步 InvalidRequest 且零 I/O，不被私有基础新增而默默支持；清除过滤后仍请求既有热门页面。没有修改 Session 生产路由。

只修改插件私有协议/client 及已有测试，Source SDK v2/Artist Browse v1/Plugin UI API 和磁盘格式不变；既有 Popular/Tracks 调用可保留默认参数，私有静态库与模块需一起重编译，不承诺私有 C++ 符号二进制兼容。实验包仍默认关闭/隔离，无 Host 平台 UI 或旧登录删除。Qt 6.11.1/macOS x86_64 Debug 全目标构建成功，五个新增测试函数单独实际执行通过，相关专项 **23/23（10.32 秒）**，最终全量 **84/84 CTest（90.40 秒）** 通过，包括原 UI、Local/Navidrome、外部 SDK、动态模块/UI 卸载及 macOS 打包/重定位。所有分类用例使用受控无外网传输，不是服务业务验收。

下一步先定义通用、独立版本化的可选分类 facets 声明/验证/执行契约，保留旧无筛选 Provider 行为并拒绝其未知 facet fallback；再接 Registry/Repository、Session 分类游标绑定及原 UI 筛选。验收必须覆盖选项身份/边界、分页/刷新撤回、跨实例、取消/卸载和错误响应，不把声明当权限。真实 HTTPS/分类字段/分页、认证、手机/Keychain/音频和最低 Host 支持要求仍待独立验收；切片 23 的本机 TLS 失败不因本轮离线协议新增而关闭。本轮未重新访问外网。

## 切片 23：独立匿名歌手 Session smoke

2026-10-13，开发工具新增精确单一 `--live-public-artists`，独立采样热门歌手/续页、首位歌手的标准 Tracks/续页；经可选扩展复核目标，不依赖榜单请求先成功。原 `--live-public` 榜单行为保留，摘要新增 `probe` 字段区分模式；工具不安装/打包、不读取账号/SecretStore、不解析资源或播放。无参数/未知/额外/组合参数在构造 Session 前返回 2，普通 CTest 不运行真实网络。边界见[运行说明](kugou-public-protocol-smoke.md)。

全目标构建成功；help 实际返回 0，歌手额外参数及组合两个模式各返回 2、未构造网络请求。真实歌手运行返回 1，固定摘要为 `probe=artists / stage=catalog / errorKey=kugou.requestFailed / pages=[] / authenticated=false / audioTested=false`，不包含原始响应或敏感字段。该结果是失败证据，不是歌手字段/分页服务验收；通用错误不能单独确诊 TLS，网络路径仍需独立核验，也不能据此确定平台全局故障。最终离线全量 **84/84 CTest（87.00 秒）** 通过，包括工具 help/未启用、上一切片新增 Session/动态链路及外部 SDK/macOS 打包；没有覆盖真实服务成功后的工具完整正向采样链，不把离线通过视为线上成功。

同日独立 curl 对固定 HTTPS singer/rank 校验返回 60：证书 SAN 与 `mobilecdnbj.kugou.com` 不匹配，HTTP 000，没有可信业务 JSON；不放宽 TLS、不换端点、不以失败回包填页面。该证据只描述本机连接，不确定全球服务状态。

本切片只有工具和文档增量，无 SDK v2/Plugin UI API/Host 接线/凭证存储变化。后续继续歌手分类与剩余公开协议迁移；真实数据链、认证、扫码、Keychain、音频、最低 Host 版本和旧登录迁出仍保持未验收状态。

## 切片 22：酷狗歌手 PageProvider 与标准歌曲索引

2026-10-13，实验 Session 接入可选 Artist Browse v1：Ready 状态、完整同源/实例/账号 Artist ref 和规范 `artist/<十进制 ID>` 才选择 Tracks，方法无 I/O/信号/状态修改。允许用户从缓存的合法 Artist ref 导航，但该选择不是媒体查找、认证或 Play 授权；歌曲权限仍须当前服务响应与共享索引交集。没有增加 Host 酷狗专用路由。

Category/Artists 无额外搜索/过滤条件时请求热门歌手；Category/Tracks 只接受单一字符串 artistId，严格验证命名空间与规范 ID。通用 selector 由 Host PageRepository 移除，插件不接受包含该 selector 的直接调用。Artist 只发布同实例身份/名称及 Play Unsupported，不暴露头像 URL、token 或原始字典；不支持 Albums 的请求继续明确拒绝，不把歌曲伪装成专辑。

响应复用私有严格协议投影。total 缺失保留未知，满页才提供下一页探测，空页/短页结束；有 total 则同时检查页偏移，最多百万页。游标池最多 1024，绑定模式/歌手 ID/limit/页码及会话，禁止列表、歌曲、榜单、搜索、实例串用，close 后失效。榜单仍使用既有快照分页，不被新远程歌手分页替换。

歌曲复用现有 4096 上限 Track 索引、完整 ref、重复 hash 合并与保守 Play 交集，迟到免费回包不能提升已观察到的限制；资源继续由 PlaybackCoordinator 请求 Source 解析、Core/Sink 播放。请求沿用 UUID、异步失败、取消/关闭/迟到回调隔离，没有默认自动播放、账号加载或认证权限提升。

改动：`KugouSourceSession.h/.cpp`、私有协议注释、Session 测试及动态网络夹具/集成测试；不新增公开 v2 字段/虚方法/IID，不改 Plugin UI API 或磁盘格式。离线验收覆盖标准 Artist/Track 身份与白名单、扩展可选发现/无 I/O、未知/已知总数、游标边界、跨实例、错误过滤/响应、取消/关闭、共享权限拒绝以及动态 Registry → Provider → PlaybackCoordinator 链。Qt 6.11.1/macOS x86_64 Debug 全目标构建成功，相关专项 **23/23（10.98 秒）** 通过；新增四个 Session 函数与动态集成函数另单独实际执行通过，最终全量 **84/84 CTest（88.42 秒）** 通过，含外部 SDK、原 UI、Local/Navidrome、动态管理 UI/卸载及 macOS 打包/重定位。均为受控协议/模拟 Sink，不是实际业务服务或音频验收。

真实 HTTPS/业务 JSON、服务端 token 验证、手机扫码、Keychain、实际音频及旧 Host 登录迁出仍未验收；本切片不改变实验包默认隔离。下一步继续歌手分类/剩余公开页面协议核对与迁移，并补真实服务验收工具；最低 Host 功能支持声明仍是正式发布门槛，不能用同版本夹具证明旧 Host 支持 Tracks。

## 切片 21：通用 Artist Browse v1 与 Host 执行隔离

2026-10-13，新增独立版本化的可选 `IArtistBrowseProviderV1`，不向冻结 v2 接口添加虚方法/字段，不使用平台名或 QML metadata 路由。Ready Session 的完整 Artist ref 可声明 Albums/Tracks；旧插件缺少扩展仍保持 Albums，扩展 nullopt/非法枚举拒绝且不 fallback。详情及兼容边界见[浏览契约](source-artist-browse-v1.md)。

SourceRegistry 仅在显式导航/执行时查询扩展，持有 callable lease，检查身份、线程、Ready/启用和回调后的退役/替换状态，拒绝递归；不从属性 getter 调插件。缓存卡片首次导航可显式走正常 Session 创建/open，旧插件 Connecting 保留 Albums 导航并延迟派发；扩展选择方法必须 Ready。MusicHub 在验证展示形状后选择标准目标，记录专用导航 revision/当前 scope，区分正常 Session 状态通知和实际导航变更，拒绝回调期间覆盖更新的导航。原 UI/QML 及其他实体导航不变，未新增平台专用 Host 分支或账号配置。

Tracks 的 Host 查询带独立版本化 selector，绑定具体实例、artistId、section 和游标。PageRepository 在初次、分页、重试/刷新前复核活跃扩展，拒绝 malformed/aggregate/缺失扩展/撤回 Tracks，不能落入忽略未知过滤字段的旧 PageProvider；成功后只移除 selector，以标准 Category/Tracks + artistId 调原 v2 fetchPage。保留来源条目顺序，不入普通磁盘页缓存。回包必须完整且非 cached，恰好一个 Tracks section，同 source/instance/account 的 Track refs；之后仍走原 sanitizer/Registry/Router/Coordinator，不赋予 Play 或认证。

动态夹具与外部 Qt-only 安装工程覆盖扩展发现/头文件、旧插件路径、目标拒绝/非法枚举、元数据无权、身份、分页/返回导航/刷新撤回、错误 section/异类/跨账号条目、关闭/卸载 lease 与重入。刷新或续页失败保留此前已验收行及错误信息，遵循现有 UI 行为，不把新错误回包当作有效数据。

本切片不接酷狗 Artist 页面、不改实验包默认隔离、Plugin UI API、凭证格式或旧 Host 登录。下一步才将私有歌手协议接到标准列表/ref/artistId/游标与 Track 索引，明确缺失 total 时的分页策略并验证多实例/关闭/取消/权限交集；真实服务/认证验收仍不能用夹具替代。旧 Host 不理解新扩展，歌曲浏览发布前仍须明确最低 Host 支持要求，不能宣称旧 Host 已获得新能力。

Qt 6.11.1/macOS x86_64 Debug 全目标构建成功，修复后的相关专项 **23/23（11.98 秒）** 通过；追加冷启动/Connecting 用例并正常重生成测试目标后，两用例单独执行通过，最终全量 **84/84 CTest（67.91 秒）** 通过，包含最终测试产物、原 UI、Local/Navidrome、外部 SDK 安装、动态管理 UI/卸载及 macOS 打包/重定位。没有真实酷狗网络、账号或音频验收。

初轮夹具使用未声明账号参数而被 Schema 正确拒绝，改为测试工厂内部开关，未放宽 Schema。另修正刷新失败应保留已验收行的测试预期。专项复核发现真实原 UI 缓存卡片首次 Artist 导航回归：要求会话预先存在会拒绝旧行为；已允许显式用户导航通过 Registry 正常创建/open，并用专用导航代次区分 Session 状态通知与真正导航变更，保持既有 Adapter 验收用例不变。追加用例时发现生成的测试元对象仍是旧快照，已正常重生成并实际执行新函数，最后全量复跑使用最终产物；失败运行和未包含新函数的旧产物不计最终验收。

## 切片 20：歌手协议基础与标准导航差距

2026-10-13，复核旧 `getHotSingers/getSingerSongs` 与 `MusicHub::browse`：旧接口返回热门歌手和该歌手歌曲；Host 对 Artist 默认构造 Category/Albums + artistId。不能将歌曲伪装成专辑、增加酷狗专用 Host 分支或提前宣告不可用导航。本切片只迁移插件私有协议，请求/解析尚未接 Session PageProvider，歌手分类接口留给后续。

新增 `KugouArtistProtocol.h`，固定 HTTPS singer/rank 与 singer/song 路径/白名单参数，使用现有可取消 client 的 UUID、响应上限、错误脱敏和销毁保护；禁用自动重定向、cookie 与认证复用。歌手 ID 正规范十进制字符串最大有符号 64 位，数值 ID 不超过双精度安全整数；页码/数量有界，不接受外部 URL 或参数注入。

响应要求明确数值成功状态。singers/songs 与旧 info 别名只允许单一数组或完全相同数组；空主字段和非空旧字段也拒绝，不采用旧“为空就 fallback”。总数可缺失，缺失保持未知；存在时严格验证类型、整数/范围和条目数量，不制造远端分页事实。歌手展示只保留唯一 ID/有界名称，不携带头像 URL/token。歌曲复用既有严格榜单白名单与标准 Track mapper，filename 只按第一个连字符拆分，保留标题后续连字符；显式畸形 songname 不回退。未知/字符串/布尔 pay_type 不推断免费，不保留资源 URL、替代 hash、原始作者字典或嵌套敏感字段。

新增无外网测试覆盖请求路径/参数/策略、UUID/成功关联、取消晚包、异步参数拒绝、TLS 拒绝不降级、网络销毁；ID 精度/溢出/重复、别名冲突、缺失/畸形总数、文本边界、歌曲投影与标准身份/权限映射。未修改 Source SDK v2、扩展 IID、Plugin UI API、Host 导航或磁盘格式。仅插件私有 API 增量；实验模块默认关闭、不加入安装/bundle，没有删除旧登录。

后续顺序：先定义通用 Artist 浏览内容契约及可选能力协商（保留现有 Artist→Albums 默认、旧插件兼容与原 UI），再接歌手标准页面/过滤/游标/索引与取消、多实例、恢复权限链测试；随后补分类/歌词等尚缺的数据能力。真实 HTTPS 响应和服务认证仍是独立验收项，不能用离线夹具替代，也不能据此迁出 Host 登录或宣告 Phase 7 完成。

Qt 6.11.1/macOS x86_64 Debug 全目标构建成功，十六项专项 **16/16（24.78 秒）** 通过；随后追加未接线 Artist 导航拒绝回归并重建两项测试目标，最终全量 **84/84 CTest（72.57 秒）** 通过，包含最新歌手测试、原 UI、Local/Navidrome、外部 SDK、动态模块/管理 UI/卸载与 macOS 打包/重定位。全部新歌手用例使用无外网夹具，没有真实歌手响应、账号或音频验收。

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

## 切片 12：独立 C++ Backend Secret 服务与展示隔离

新增可选 `IPluginUiBackendSecretsV1`，IID 独立版本化；不扩充 Source SDK v2、旧 Plugin UI provider 或 SettingsBridge 虚表。Host 私有桥增加显式 backend-only Secret 声明的构造入口，必须是完整 Schema 中已有 Secret ID，未知/重复/公共字段使服务失效。旧构造入口仍没有 backend-only 字段，不改变现有插件行为。

完整 Schema 继续交给 SourceAccountStore 校验/事务；展示投影去除业务 Secret 字段及空区块。普通 QML 查询配置标志返回 false，四条写入/清除路径拒绝业务字段。新 C++ 服务提供有界 UTF-8 记录保存、读取、清除和配置标志，复用现有 secure envelope、身份约束、错误脱敏及轮换回滚；不是任意存储、登录权限或 native 插件沙箱。

测试验证 Qt metaobject 不包含新凭证方法、声明/记录边界、普通配置保存保留业务 Secret、实例/包隔离及失效 Context；酷狗存储夹具改经新服务往返私有账号记录，确认表单完全没有 credentialBundle。另补存储成功通知立即关闭 UI/销毁桥的保护：先登记完成通知，Registry 通知后检查 QPointer，changed 返回后不再访问对象；保存和清除均有重入销毁测试。生产 Schema 仍为空；Provider 声明发现、生产 Management Session 和其他 Host 表单尚未接线，不能只靠局部隐藏就上线认证字段。完整契约及后续验收见 [Backend Secrets 1.0](plugin-ui-backend-secrets-v1.md)。

2026-10-12，Qt 6.11.1/macOS x86_64 Debug 最新全目标构建成功，九项专项复跑 **9/9 通过（11.30 秒）**；补齐通知销毁保护后的最终全量 **79/79 CTest 通过（82.74 秒）**，含外部 SDK 安装/加载、macOS 打包/重定位。已检查安装产物包含新服务头文件；验证仍使用无外网/内存 SecretStore 夹具。真实扫码、Keychain、实际音频和 Host 平台登录迁出仍未验收。

## 切片 13：Provider 声明与 Host 双入口统一

新增独立可选 `IPluginUiBackendSecretProviderV1`（加载期间不可变的 Secret 字段 ID 声明），不改变 Source SDK v2、SettingsSchemaV2 或旧 Management Provider。Host 共用 `backendSecretPresentationV1` 验证、深复制和展示投影；Host bridge、普通 PluginSettingsController 与 PluginManagementUiSession 不再分别维护规则。

普通控制器在 plugin lease 内发现声明，展示投影隐藏业务字段/空区块及实例配置字段 ID；普通保存与连接测试/设置动作的 Secret 草稿拒绝这些字段。存储、校验、配置与 Provider 操作仍保留完整 Schema，公共配置保存不会删掉既有业务凭证。Management Session 自动发现声明并构造 C++ 服务；非法未知/公共/重复声明阻止两处表单/写入和创建 Backend，不回退为 Host 人工输入框。Provider 返回临时值在可能释放 lease 前销毁，缓存只保留 Host 拷贝。

动态 fixture 验证 Create/Edit Context、双入口投影、手工业务 Secret 草稿及连接测试拒绝、通过 Backend 服务存储、普通字段修改后凭证保留、Context 失效/卸载保护、非法声明的双入口拒绝；默认无声明的旧 fixture/Local/Navidrome 行为继续复验。首次构建仅发现新测试遗漏 saveInstance 的空 Secret 草稿实参，补齐后重建，未改旧签名。

2026-10-12，Qt 6.11.1/macOS x86_64 Debug 全目标构建成功，九项专项 **9/9 通过（12.96 秒）**；最终全量 **79/79 CTest 通过（83.81 秒）**，含外部 SDK 安装/加载、Local/Navidrome、原 UI 与 macOS 打包/重定位。已检查外部安装产物包含新 Provider 声明头文件；测试没有连接真实平台或系统 Secret backend。酷狗生产仍为空 Schema、没有声明/认证 Backend/Management UI；本切片只完成通用 Host 接线，不宣称真实扫码、Keychain 或 Phase 7 完成。

## 切片 14：插件私有认证 Backend 与持久绑定流程

新增 `KugouAuthBackend` 私有 C++/QObject 管理后端，生产构造使用插件自有网络，测试注入网络；只依赖稳定 Plugin UI Context/SettingsBridge/Backend Secrets 服务，不链接 Host 私有账号类。固定 package/source/instance/account/settings 绑定，Context 身份/服务改变、失效或销毁后永久停止认证、取消请求并拒绝晚写。

Backend 提供扫码启动/取消和清除账号候选记录；公开属性只有 state、qrUrl、errorKey、credentialStored，没有 token、用户 ID、设备记录或完整账号 DTO。读取既有记录必须通过插件私有 codec 的完整实例校验；损坏/不可读配置阻止认证启动。确认时及 storing 通知之后重新核对完整记录快照，连同用户的另一笔 token/设备轮换也不能覆盖；已有用户不允许通过重扫隐式切换账号。必须显式清除，再开启新绑定。

候选凭证按当前控制器代次一次性交接，状态先进入 storing，Host 事务成功并读回完全匹配的规范记录后才进入 stored。写入/清除失败读取并保留真实当前绑定，公开错误使用固定脱敏键。stored/credentialStored 只表示候选凭证记录持久化，不表示服务端认证、Session 登录、Source Ready 或额外平台权限；不调用播放器或旧登录路径。事务已经同步提交时取消不能承诺回滚，Context 失效只能阻止后续操作/结果投递。

Backend 状态通知后检查对象存活/代次，防止 UI 通知中重入、失效或销毁。Host 完成信号可能在通知中的嵌套事件循环提前到达，提交期间暂存结果，在取得请求 UUID 后精确匹配处理，不丢完成信号或把其他操作结果当作成功。

新增专用测试目标使用真实 Host bridge/SourceAccountStore、临时 INI、内存 SecretStore 和无外网网络夹具，覆盖规范记录/设备绑定、写入失败与清除失败、用户冲突、认证期间绑定变化、损坏配置、取消/重试/晚响应、多实例、Context 失效、通知销毁及嵌套 Host 完成通知。Backend 目前为私有静态实现和测试接线；生产酷狗工厂仍为空 Schema，尚无 Management UI Provider/QML 或认证 Session 接线，不改变默认 Source 或旧 Host 登录入口。

2026-10-12，Qt 6.11.1/macOS x86_64 Debug 最新全目标构建成功，十项专项 **10/10 通过（9.14 秒）**；最终全量 **80/80 CTest 通过（67.48 秒）**，含旧 UI、Local/Navidrome、外部 SDK 与 macOS 打包/重定位。新增认证目标含八个业务用例；均为无外网/内存 SecretStore 夹具，不读取真实账号。未改 SDK v2/IID、Plugin UI API 或磁盘格式。后续接生产凭证 Schema/声明、Management UI 与 Session 配置绑定，再另行验证真实服务 token/扫码；不将存储夹具成功当作 Phase 7 完成。

## 切片 15：生产凭证声明与 Session 配置绑定

生产 `KugouSourcePlugin` 通过已有可选 `IPluginUiBackendSecretProviderV1` 声明 `credentialBundle`；完整 Schema 中仅有该可选 Secret 字段，Host 普通表单投影为空，匿名实例仍可创建。测试工厂同步该声明，避免测试 Schema 与生产的展示契约不一致。实验包仍隔离、默认关闭，无应用默认来源/bundle/install 接线；未添加 Management manifest、Provider 或 QML，也未把静态 AuthBackend 作为生产管理后端创建。

Session 不再拒绝全部秘密，而是只接受 SourceSecrets v2 命名 envelope 的单一 `credentialBundle`，并通过插件私有 codec 验证规范记录及完整实例/账号绑定。原始 Host cookie、未封装记录、损坏内容、跨账号、额外/未知字段、未知公开参数和非法账号范围在 open 阶段拒绝，不发网络请求。空秘密继续表示匿名配置。记录仅保存在私有配置中，不投影到媒体 DTO 或普通 QML，也不导入旧 Host Account.ini。

凭证本地格式有效不等于 token 经服务验证：Ready 仍仅表示匿名配置就绪，账号专用能力仍 Unsupported，搜索/媒体解析依旧使用匿名请求，不携带候选 token/cookie/Authorization。不因记录存在启用付费权限、收藏、scrobble 或隐式登录。

新增 Session 验收覆盖有效绑定、八类拒绝配置、匿名请求无凭证及 DTO 隔离；真实动态生产包测试覆盖 Schema/声明发现、Host 普通表单/实例字段隐藏、拒绝手工业务 Secret、普通重命名保留记录、Registry 同实例打开及匿名第二实例、INI 不含候选 token/设备标识。采用受控网络/内存 SecretStore，不代表 Keychain 或服务端账号认证。

兼容性：没有变更 Source SDK v2/既有 UI 接口虚表、IID 或 envelope 格式；新增可选声明与字段仅影响实验酷狗包。完整 Schema 必须随工厂一起更新，旧纯匿名实例无需补凭证。尚未接入管理后端，因此本切片只完成生产存储/Session 边界；下一切片接 Management Provider、独立 QML、二维码编码/显示及 Context/lease 卸载测试，之后才做服务端 token/真实扫码验收并迁出旧 Host 登录。

2026-10-12，Qt 6.11.1/macOS x86_64 Debug 全目标构建成功，十项专项 **10/10（10.48 秒）**、最终全量 **80/80 CTest（69.77 秒）** 通过，含外部 SDK、Local/Navidrome、原 UI 和 macOS 增量打包/重定位回归。首次构建的测试目标缺少私有头文件搜索路径，补齐并重新生成后完整重建/测试正常，不以旧二进制作为新测试证据。无真实外网、Keychain 或账号读取；未宣称可扫码登录或 Phase 7 完成。

## 切片 16：独立管理页面、离线二维码与动态生命周期

生产工厂实现已有 `ISourceManagementUiProvider`，manifest 声明 `pluginUiApi: 1.0` 和包内 `qml/ManagementPage.qml`，支持 Host 分配 Create 身份和既有 Edit 身份。创建 Backend 时检查 Context/模式/完整实例/账号/服务及所有者，Backend 与插件自有网络归 Context 生命周期管理，不接 Host 私有 AccountManager。页面仅导入 QtQuick 与 QueMusic.PluginUI 1.0，使用稳定 ScrollPage、Theme、AccountCard、QrLogin、Status/Button；窄短视口可滚动，普通表单不显示业务凭证。

Kit 保留旧图片 `source` 行为，追加离线 payload 编码和刷新/取消 enable 控制。既有纯 JS 算法独立复制到 Kit 资源/安装产物，不改变原 Host 资源别名；有界 ASCII、版本 1–10/M、整数像素、四模块 quiet zone，非法/超限/尺寸不足显示错误，不回退远端图片。详情、安装与最低运行时兼容风险见 [QR payload 契约](plugin-ui-qr-payload-v1.md)。

页面不自动启动 QR，不自动保存账号，不导入 Legacy Host 记录；只响应用户刷新/取消/清除。storing/clearing 禁止新操作，stored 明示“服务验证待完成”，不是已登录或账号能力；失效后按钮禁用、URL 清空，晚回包不能改写存储。清除没有持久绑定的 Create Context 为无写入、无制造账号的幂等成功；损坏/既有记录仍走原 Host 清除事务。

动态测试加载真实生产模块，验证 Provider/Owner、Create/Edit、Schema 隐藏、独立实例、持有页面时卸载 Busy、页面销毁/Context 失效后 lease 归零和实际卸载 Success、卸载后 Host 不再报告管理 UI。受控协议测试另用静态 Backend 和同一生产 QML，驱动按钮→获取→轮询候选→Host 存储→状态/清除，覆盖失效与迟到回包及页面渲染/短视口滚动；不在同一进程重复链接插件私有 QObject 实现。第一次动态测试出现元对象冲突，已分离两个宿主，保留卸载检查而非忽略断言。

macOS QR 测试用独立 Vision 解码还原 URL 和较长 payload；该框架仅链接测试程序。外部 Qt-only 安装探针检验 Kit JS 资源完备。均使用测试数据/内存 SecretStore，无真实手机扫码、Keychain、服务端 token/账号权限或实际音频验收；实验包仍默认关闭、独立输出，无应用默认 Source/install/bundle 接线，不删除旧 Host 登录。本切片不是 Phase 7 出口，后续先补服务认证/数据能力与真实集成证据，再迁出旧登录并做启用/禁用/删除验收。

2026-10-12，Qt 6.11.1/macOS x86_64 Debug 最终全目标构建成功，十四项专项 **14/14（31.12 秒）**，导入检查 **6/6（0.36 秒）**，最终全量 **82/82 CTest（74.83 秒）** 通过。第一次全量仅 public Kit 源码目录 lint 失败（打包 JS 不在该目录），改为检查实际独立模块目录后重建/全量复跑通过；私有/越界/远端导入拒绝测试不变。渲染截图已人工检查 QR/按钮完整，短视口滚动断言通过。检查 bundle manifest 仍只有 Local/Navidrome，没有酷狗实验包；这些证据不代表真实平台、手机或跨平台发行验收。

## 切片 17：公开榜单请求与严格协议投影

复核旧 `AccountManager::onKugouPoll/loadKugou` 与 `KugouApi`：旧实现直接把扫码候选/磁盘 cookie 当作已登录，没有独立 token 验证接口，不能据此建立新 Session 的认证事实。本切片转向有仓库接口证据的公开榜单，不编造服务认证端点；候选记录与实际登录/权限继续分离。

新增私有 `KugouRankProtocol.h`，将旧 rank/list、rank/song 的固定路径/参数迁入 HTTPS 构造器，并接入已有可取消 `KugouApiClient::getRank`。白名单 rank ID 为正规范十进制且不超过旧 int 范围，页码/数量有界；请求不加载/保存 cookie、不复用认证、不跟随重定向，沿用 15 秒超时、1 MiB 响应上限和脱敏错误。HTTPS 可达性/实际服务字段尚未验证，不能把固定路径构造测试当作真实服务支持证据。

Catalog 严格检查业务状态、数组、对象、ID 唯一性、展示文本类型/长度，成功空列表保持空，不复制旧内置榜单兜底。Tracks 检查 total/数量/hash/名称/作者形状，只投影标准 Track mapper 所需字段，剔除 token、资源 URL、原始作者字典和替代 hash；缺失/字符串/布尔/对象权限字段保持未知，不推断免费，嵌套 duration/pay 数据不留存。数值明确免费仍须后续资源解析验证，不表示付费或账号能力。

测试新增请求策略/并发 UUID/取消晚包/异步参数拒绝、Catalog 畸形/重复/空态/边界、Track 页形状与白名单、标准 Track DTO 身份/权限映射。全部受控网络，无真实账号/外网/音频。本切片仅协议基础；Session 的公开 PageProvider 仍只有 Search，未向 UI 宣告榜单可用。下一步接标准榜单页面/ref/filter/游标身份与权限交集、取消/重开/多实例测试，之后再验真实 HTTPS 响应；不改变 Source SDK v2/IID/Plugin UI API/存储格式或默认实验包隔离状态。

2026-10-12，Qt 6.11.1/macOS x86_64 Debug 全目标构建成功，修正夹具并重建后的十四项专项 **14/14（25.15 秒）**，最终全量 **82/82 CTest（95.54 秒）** 通过，含原 UI、Local/Navidrome、外部 SDK、动态管理 UI/卸载和 macOS 打包/重定位。首次专项新夹具误选 parent-only 生产构造，未实际注入受控网络，已明确使用双参数注入并重新构建/全量复跑；失败运行不作为验收或真实端点可达性证据。最终用例使用模拟响应，未验证真实账号、服务字段、手机扫码、Keychain 或音频。

## 切片 18：标准榜单页面、导航与统一权限索引

`KugouSourceSession` 接已有 IPageProviderV2：Category/Playlists 映射公开榜单，返回同实例/账号的 Playlist ref `rank/<canonical-id>`，Play 明确 Unsupported；Category/Tracks 只接受单一字符串 `playlistId` 及该命名空间。沿用 Host `MusicHub::browse` 的 Playlist→playlistId 契约，无新平台编号、专用导航或 SDK 枚举。其它页面/动作继续按既有 fail-closed 处理，公共榜单不冒充私人推荐。

榜单列表响应最多 256 条，Session 只保留单个严格验证的快照，并按 query.limit 本地分页；不为每个游标复制整份响应。每次成功刷新更换快照代次，旧列表游标拒绝；失败/取消不伪造新快照。所有分页保持异步 requestStarted/pageReady 语义，本地翻页可在派发前取消且无额外 I/O。歌曲游标绑定模式、榜单 ID、搜索上下文和数量；游标为实例内不透明 UUID，关闭/重开清除快照/索引/游标，跨实例、跨页面、跨榜单、修改数量或伪造参数在网络前拒绝。页码上限一百万，达到上限不再发下一页游标；游标集合仍有 1024 上限。

榜单歌曲先走切片 17 严格投影，再复用标准 Track mapper、4096 条当前索引和搜索路径的权限交集/重复 hash 去重。未知或付费权限不能被另一榜单、搜索或迟到免费回包撤销；资源解析完成前再次查当前索引，权限降级后不发 streamReady。标准 Lookup 仍无 I/O，Catalog 不进入 Track 索引；资源解析由 Source 提供，实际播放由现有 Coordinator/Core 承担。

新增受控 Session 用例覆盖列表/空态/快照本地分页、取消、刷新游标失效、标准 ref、榜单歌曲映射、跨上下文/实例/重开游标拒绝、错误参数与畸形响应、重复/迟到权限和在途降级、关闭/迟到回包及通知重入。真实动态测试模块增加榜单响应，验证 Registry Session→榜单→同实例歌曲→Coordinator→模拟 Sink，确认不触发搜索/旧平台路径且展示 DTO 不带资源/凭证。没有真实 HTTPS 响应、手机、Keychain、账号权限或音频验收；不改变默认实验包隔离、旧 Host 登录入口、Source SDK v2/IID、Plugin UI API 或持久格式。下一步验证真实公开服务协议，并补剩余数据能力/真实认证证据，随后才启用应用来源或迁出旧登录。

2026-10-12，Qt 6.11.1/macOS x86_64 Debug 全目标构建成功，最后新增边界用例重建后十四项专项 **14/14（21.04 秒）**，最终全量 **82/82 CTest（70.70 秒）** 通过。覆盖原 UI、Local/Navidrome、外部 SDK、Plugin UI/卸载和 macOS 增量打包/重定位；均为受控公开协议与模拟播放 Sink，不是实际平台或音频验收。Source SDK v2/公共 UI 接口及磁盘格式不变，新增协议/导航不对实验包之外的来源生效。

## 切片 19：显式匿名真实协议 smoke 与 TLS 拒绝验收

开始真实公开接口核验后，本机 `mobilecdnbj.kugou.com` 的 HTTPS 主机名校验失败；同族、仓库已使用的 `mobilecdn.kugou.com` 也失败。Python/OpenSSL verify code 62、curl 60/HTTP 000，未取得可信业务响应；不能判断全局服务故障或据此放宽协议字段。没有忽略 TLS、HTTP 降级、跟随重定向或切换未经验证的服务；原固定端点和严格权限判断保持不变。

新增仅 BUILD_TESTING 下构建的 `quemusic_kugou_public_smoke`，精确显式参数才访问外网，真实插件 Session 采样榜单/本地分页/歌曲及最多下一页，验证标准身份/白名单与数量，45 秒总限时。既不加载 Host 账号/SecretStore，也不解析媒体或播放音频；只输出有界阶段/数量/固定错误键，无原始响应、URL、歌名/hash或 Qt 错误。成功也固定说明未认证/未测试音频，不能充当平台账号或 Phase 7 出口。正常 CTest 仅测试 help/未启用，无真实网络；请求层增补模拟 TLS 握手失败的脱敏、无页面成功与无 HTTP 降级回归。运行及网络证据见[公开协议 smoke](kugou-public-protocol-smoke.md)。

本机真实 Qt smoke 已执行：返回 1，JSON 为 `outcome: failed`、`stage: catalog`、`errorKey: kugou.requestFailed`、`pages: []`，无 HTTP 状态；`authenticated/audioTested` 均 false。独立 TLS 诊断是网络证据，Qt 报告仅证明生产链安全失败且没有页面，不能从固定错误键独自推断 TLS 根因。失败不计作真实平台验收通过；未验证后续榜单/歌曲字段、分页或权限。没有用户凭证、二维码或音频访问，不因为失败启用新 Source/删除旧实现。

2026-10-12，Qt 6.11.1/macOS x86_64 Debug 全目标构建成功，离线专项 **16/16（25.81 秒）**；工具再补单调计时的晚响应拒绝、重建并复测真实失败路径后，最终全量 **84/84 CTest（71.82 秒）** 通过，含原 UI、Local/Navidrome、外部 SDK、管理 UI/卸载和 macOS 打包/重定位。额外参数 `--live-public --help` 返回 2、未启动网络。真实 smoke 两次均失败于 catalog，不计入通过数；仍无业务 JSON、认证或音频证据。下一步继续可独立开发的数据能力，同时把可验证 HTTPS 服务响应保留为 Phase 7 的未关闭验收项，不改本机网络/证书配置以绕过失败。

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
