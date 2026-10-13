# Source Browse Facets v1

日期：2026-10-13。基线：[最终架构方案](QueMusic-Plugin-Architecture-Final-Plan.md)与[实施设计](source-browse-facets-v1-design.md)。

当前切片仅实现可选接口、纯验证器和 PluginSdk 安装/外部编译测试。Registry、Repository、Hub/Adapter、生产 Session 和原 UI 尚未接该扩展；不表示应用已经支持分类筛选。实验酷狗包不声明该接口，不改变默认隔离或旧登录。

## 可选接口与数据

头文件：`extensions/browse-facets/v1/IBrowseFacetsProviderV1.h`。IID：`org.quemusic.source.extensions.BrowseFacetsProvider/1.0`。接口不继承 QObject 或 v2 Session，不向 v2 类型/虚表/枚举/IID 添加内容，不属于 Plugin UI API。

`browseFacets(section)` 返回可选 `BrowseFacetSchemaV1`，含 section、revision 和 facets；facet 含 id、title、choices；choice 含 value、title。v1 只支持 Category/Artists 根列表的单实例、每维度单选。nullopt 表示撤回筛选，不请求 fallback。没有扩展的插件保留原无筛选行为，不能因此接受未知筛选参数。

方法只在 Ready Session 所在线程、Host 持 callable lease 时调用，同步、无 I/O/信号/状态修改。语义改变/选项撤回必须改变 revision，标题翻译不必。声明不是账号状态、认证、媒体权限或能力授权；Host 仍须逐次复核身份、生命周期、schema、revision 和选择。此切片没有实现这些 Host 调用守卫。

## 纯验证规则

`isValidBrowseFacetSchemaV1` 对整份声明验证，不截断、不丢弃非法条目。section 必须 Artists，facet 1–8 个，每 facet 的 choice 1–64 个；facet id 在 schema 内唯一，choice value 在其 facet 内唯一，均大小写敏感。不同 facet 可以使用相同 value。

id/value/revision 为 1–64 个 ASCII 字符，首字符字母或数字，其余仅字母/数字/点/下划线/连字符；不修剪、不折叠大小写。数字字符串为不透明字符串，不转整数。facet id 禁止 `quemusic.` 前缀和保留字段 `facetRevision`、`facetValues`、`artistId`、`playlistId`、`genreId`；choice value/revision 不作为字段名，不套用该保留字段规则。

标题为非空白、最多 256 个 UTF-16 code unit，拒绝 NUL、Unicode Control 与孤立代理项，保留合法 Unicode/代理对。标题只是 PlainText，Host 不得将其解释为富文本、URL、网络资源或 QML；验证不承担富文本解析或 URL 内容分类，也不依赖主程序私有 QML。渲染接线仍待后续。

`BrowseFacetSelectionV1` 为 revision + QVariantMap values。`matchesBrowseFacetSelectionV1` 首先验证完整 schema，要求 revision 精确相等、至少一个已声明 facet/choice，值严格为 QString。撤回 choice、未知维度/值、非法 schema 或类型均拒绝；没有默认值或替代选项。

## Host/Provider 查询形状

Host 查询为 Category/Artists、具体且非空白 sourceInstanceId、searchText 空、limit 1–100；cursor 保持不透明、不参与 schema 身份推断。filters 恰好三个字段：

- `quemusic.browse-facets.v1`：QString `selected`；
- `facetRevision`：符合标识规则的 QString；
- `facetValues`：非空 QVariantMap，最多 8 个键，键为合法 facet id，值为合法标识 QString。

`browseFacetSelectionV1` 只检查上述形状，不查询插件，不知道账号身份、当前 revision 或 choice 是否存在。不能仅因返回 selection 就派发。未知维度/值即使形状合法，仍须用当前合法 schema 匹配才可执行。

Host 只有在未来的活跃扩展/schema/身份/lease 复核通过后，才移除 selector，保留 revision/values 调用已有 IPageProviderV2。Provider 查询 filters 恰好两个字段，由 `browseFacetProviderSelectionV1` 验证；该函数拒绝残留 Host selector。插件执行前须独立匹配当前 schema，然后映射私有分类协议。两个解析器都拒绝混入详情、Artist Browse selector、数字/布尔/列表/Hash 类型及额外字段，不隐式转型。

非法 Host selector 被删掉后可能形成合法 Provider 形状，这并不允许 Host 删除非法 selector 再重试：Host 必须先验证和复核，不能通过投影“修复”恶意请求。纯函数没有自动移除 selector 或回退 helper，亦不会调用 fetchPage。

## 后续执行与兼容边界

执行守卫、响应形状、内存快照/导航、分页复核/撤回、cancel/unload、SourceSession 分类游标及 QML 按实施设计顺序补齐。筛选结果须一个完整非 cached Artists section、同实例/账号 Artist ref；当前头文件不实现响应验证或生命周期检查。

扩展随 PluginSdk 安装，独立 Qt-only 工程可 include/qobject_cast，旧 Session 无扩展可选发现返回 null。Source SDK v2 ABI 仍为 2；不承诺跨 Qt/编译器/平台 C++ ABI。新头文件/同版本安装测试不是旧 Host 功能支持声明，正式发布仍须明确最低 Host 功能版本。

本切片不修改 Plugin UI API、账号 Schema/凭证、普通页磁盘格式、播放链或任何平台登录。没有重新访问外网，真实 HTTPS、分类业务字段/分页、认证、手机/Keychain/音频仍待验收。
