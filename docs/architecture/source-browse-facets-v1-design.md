# Source Browse Facets v1 实施设计

日期：2026-10-13。基线：[最终架构方案](QueMusic-Plugin-Architecture-Final-Plan.md)、[当前差距](current-state-gap-analysis.md)及[Phase 7 切片 24](phase-7-kugou-source-migration.md)。

状态：切片 25 的实施设计，保留设计顺序。切片 26 实现可选头文件/IID、纯验证器及安装测试，切片 28 接 Registry/Repository 安全执行，切片 29 接 Hub/Adapter 快照/选择/清除/导航，实际接口见[Browse Facets v1 契约](source-browse-facets-v1.md)；生产插件分类路由与原 QML 控件仍未实现，不是正式发布功能。现有 Source SDK v2、Artist Browse v1 与 Plugin UI API 保持不变。

## 问题与范围

当前 Host 的 Category 可显示 Artists，但不声明、验证或渲染插件自有筛选维度。酷狗的地区分类只在插件私有协议中，不能靠 QML 平台整数、任意 filters、Genre 假实体或 Management UI 替代统一浏览体验。直接向旧 Provider 发送未知 filters 可能被忽略，得到未筛选列表却显示筛选成功。

第一版只接 Category/Artists 的根列表筛选：单一具体 SourceInstance、每维度单选、选项由活跃 Session 提供。不支持 aggregate、Search、详情 artistId/playlistId/genreId、自由文本、多选、级联/远端选项搜索或自动选择默认值。热门歌手仍是原无筛选路径，不与某个地区分类等同。

## 可选扩展与数据

拟新增独立 `extensions/browse-facets/v1/IBrowseFacetsProviderV1.h` 和独立 IID，通过 Session 的 qobject_cast 可选发现；不向 v2 Session/Provider/DTO 增加字段、枚举值或虚方法。

声明方法返回当前 section 的可选 schema：revision、若干 facet，每个 facet 为 id、纯文本标题及若干 choice（不透明 value、纯文本标题）。revision 标识筛选语义版本，语义改变或选项撤回须更新；标题翻译不改变选项语义。返回 nullopt 表示当前不提供筛选，不是认证或媒体权限状态。

建议边界：1–8 个 facet、每 facet 1–64 个 choice、facet id/value/revision 各最多 64 个 ASCII 标识字符，标题 1–256 字符、无控制字符。标识必须唯一，不允许空值或保留字段名；schema 整体无效即拒绝，不截断为另一合法 schema。标题由 Host 以 PlainText 渲染，不接受 URL、富文本、QML 组件或任意 metadata。正式字符规则须在实现中统一验证并加入边界测试。

方法同步、无 I/O、无信号、不修改状态，只能在 Ready Session 所在线程持有 callable plugin lease 调用。Host 校验完整注册身份、启用状态、Session 替换/退役、线程和递归；回调后复核会话/代次，防止接受旧返回值。UI getter 只读已验证的 Host 快照，不调用插件、不创建 Session。

缺少扩展的旧插件不出现筛选行，无筛选浏览保持原行为；有扩展但返回非法 schema 不改变旧无筛选语义，也不能将非法声明显示为合法选项。显式用户激活筛选可走 Registry 正常创建/open，异步 Ready 后再取声明，不将 Connecting 当筛选就绪。

第一版不增加 v2 筛选专用信号。声明快照在显式激活/刷新与已有 Registry 生命周期变化时更新，执行每次再复核；不保证插件内部选项变化立即推送到 UI。显示中的旧选择不是永久可用授权，点击失效选择须显示拒绝并更新安全快照，不能在属性读取中隐式请求网络或切换选择。

## 查询与执行隔离

拟使用独立版本化 selector `quemusic.browse-facets.v1`，值 `selected`，以及 `facetRevision` 字符串、`facetValues` map。map 每个键/值必须是声明中的 facet id/choice value；至少选择一个维度，每个维度一个值。空选择回到原无 selector 查询，不发送空筛选伪装成功。

带 selector 的查询要求 Category/Artists、具体 instance、无 searchText、恰好上述三个 filters 字段。字符串/map 使用严格 QVariant 类型检查，不进行数字/布尔/列表隐式转换；禁止混入 Artist Browse selector 或其它详情条件。QML 不可构造任意 Provider 参数；Host 只从已验证选项和显式用户选择构造查询。

PageRepository 在首次请求、分页、重试/刷新及返回历史查询时复核活跃扩展、revision 和选择。无扩展、声明撤回、选项缺失、非法 schema、revision 变化或实例不匹配即拒绝，不请求旧 Provider、不回退无筛选或其他来源。只在复核通过后移除 selector，以标准 PageQueryV2 的 facetRevision/facetValues 调 IPageProviderV2；插件独立验证相同条件再映射私有协议，不依赖 Host 曾校验作为永久信任。

筛选响应必须完整、非 cached、恰好一个 Artists section；全部条目为同 source/instance/account 的非空 Artist refs。空列表合法，hasMore 必须有 nextCursor。不从 metadata 或响应内的字段推断筛选选项/账号权限；正常 sanitizer、动作路由和 Artist Browse v1 继续复用。

初版不写普通磁盘页缓存或账号 Settings；Host 的筛选状态及导航历史只在内存。游标必须绑定 Session、完整选择、revision、section、limit 和页码，不能串地区/无筛选/其它实例；插件 close 清空。筛选改变清游标并开启新请求代次，取消旧请求，晚包不得覆盖新选择。刷新/续页失败保留已验收行和错误状态，不接受错误新数据；首次新选择不得把旧行标为新选择结果。

## 原 UI 与插件边界

Host 在已有 Artists 区域添加通用、可隐藏的轻量筛选控件，不重写页面。无扩展时不占位；aggregate 或详情视图隐藏/禁用，不把上次来源选择带到新实例。清除为显式用户动作，声明失效不得静默清除后宣告筛选成功。

筛选行由 Host 的通用展示模型渲染，原 UI 的私有控件仅由 Host 自己使用；插件只提供数据，不 import 主程序私有 QML。普通账号设置仍走 Settings Schema，复杂认证仍由 Plugin UI API 管理；分类选择不是账号配置，不加入 Settings Schema/秘密服务，也不提供登录/网络输入框。

酷狗 Session 最后才声明地区 facet：展示 value 为插件自有稳定不透明 ID，在插件内部映射 Region wire 1–4，Host 不认识华语/欧美/日本/韩国的编号，不包含 Kugou 条件分支。分类回包复用已迁入的严格 Catalog 投影，发布标准 Artist ref；用户点击后走 Artist Browse v1 的标准 Tracks，不把地区过滤带入歌曲详情。

## 实施顺序与验收

1. 可选接口、纯验证器和查询形状；Qt-only 外部安装工程能编译/发现独立 IID，v2 ABI 保持 2。验证重复/非法/超限 schema、类型、字段混合及未知选择，无网络副作用。
2. Registry 声明快照与 Repository 执行守卫；动态夹具覆盖旧插件无筛选不变、显式筛选不 fallback、Ready/实例/线程/代次、撤回、回调重入、关闭/销毁、调用栈内卸载 Busy、退出后可卸载、严格响应形状。
3. MusicHub/OriginalUiMusicAdapter 通用选择与内存导航；getter 零虚调用、显式首次激活、切实例/aggregate/详情隔离、快速切换晚包、分页/返回/刷新、选择失效及清除均有验收。
4. 酷狗 Session 分类声明/私有映射和游标；四地区、无默认地区、跨地区/limit/revision/Session 串用拒绝、取消/关闭、未知总数和敏感字段边界，经 Registry/Repository 动态链路验证。
5. 原 QML 控件接线；实际生产组件 offscreen 空/错误/旧插件/多实例 smoke，Light/Dark/主题及真实原生视觉另验收；保持布局和其它来源回归。
6. 独立公开分类 smoke 及真实服务核验；失败保持证书校验/固定端点，不降级、不读取凭证、不播放。可信 JSON/业务字段/分页未通过前实验包不默认启用，不能删除旧平台业务或宣称 Phase 7 完成。

每个代码切片全目标构建、比例适当的专项、最终全量 CTest 和 diff 检查后提交/SSH 推送；接口实现后将本设计更新为实际契约，不把文档写完等同功能上线。推荐涉及 sdk/source/extensions、SourceRegistry、PageRepository、MusicHub、OriginalUiMusicAdapter、已有 Artists UI、KugouSourceSession 及对应动态/外部/QML 测试，不删除 Legacy。

## 兼容风险

新 Host + 旧插件继续原无筛选页面，新筛选请求拒绝；旧 Host + 新插件仍可用原热门/歌曲路径，但不会自动获得分类控件。首次发布需明确最低 Host 功能支持，与 Artist Browse v1 同为尚未完成的发布门槛。

新接口使用独立 Qt/C++ ABI 与 IID，不因名称为 v1 就承诺跨 Qt/编译器/平台二进制兼容。Host 私有 API/C++/QML 需同步构建；实验包私有 Region 不构成公共 ABI。初版不迁移磁盘 schema，不改变凭证格式，不增加权限能力；未来多选/级联需独立演进，不让旧 Provider 忽略新语义。
