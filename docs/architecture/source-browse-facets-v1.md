# Source Browse Facets v1

日期：2026-10-13。基线：[最终架构方案](QueMusic-Plugin-Architecture-Final-Plan.md)与[实施设计](source-browse-facets-v1-design.md)。

切片 26 实现可选接口、纯验证器和 PluginSdk 安装/外部编译测试，切片 27 增加 Repository 保留过滤字段拒绝门，切片 28 接活跃 Registry/schema/lease 与执行/响应验证，切片 29 接 Hub/Adapter 的只读快照和显式选择/清除/导航。生产 Session 分类声明及原 QML 控件尚未接线，不表示应用已开放分类筛选。实验酷狗包不声明该接口，不改变默认隔离或旧登录。

## 可选接口与数据

头文件：`extensions/browse-facets/v1/IBrowseFacetsProviderV1.h`。IID：`org.quemusic.source.extensions.BrowseFacetsProvider/1.0`。接口不继承 QObject 或 v2 Session，不向 v2 类型/虚表/枚举/IID 添加内容，不属于 Plugin UI API。

`browseFacets(section)` 返回可选 `BrowseFacetSchemaV1`，含 section、revision 和 facets；facet 含 id、title、choices；choice 含 value、title。v1 只支持 Category/Artists 根列表的单实例、每维度单选。nullopt 表示撤回筛选，不请求 fallback。没有扩展的插件保留原无筛选行为，不能因此接受未知筛选参数。

方法只在 Ready Session 所在线程、Host 持 callable lease 时调用，同步、无 I/O/信号/状态修改。语义改变/选项撤回必须改变 revision，标题翻译不必。声明不是账号状态、认证、媒体权限或能力授权；Host 仍须逐次复核身份、生命周期、schema、revision 和选择。

切片 28 的 Host 私有 `SourceRegistry::browseFacets(identity, section, createSession=false)` 要求精确来源/实例/账号、启用、所属线程和 Ready 快照，拒绝递归；缺少扩展/非法声明/退役均返回空，不 fallback。createSession=true 仅供未来显式激活，沿用 Registry 正常 create/open，Connecting 不冒充 Ready；默认不创建会话，不作为 QML getter。调用持 lease，之后复核同一 Session、线程、展示 revision、实例/全局生命周期代次和启用/Ready。所有字符串在租约释放前深复制，包括未选的 choice，防止 QStringLiteral 指向已卸载模块。

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

Host 只有在活跃扩展/schema/身份/lease 复核通过后，才移除 selector，保留 revision/values 调用已有 IPageProviderV2。Provider 查询 filters 恰好两个字段，由 `browseFacetProviderSelectionV1` 验证；该函数拒绝残留 Host selector。插件执行前须独立匹配当前 schema，然后映射私有分类协议。两个解析器都拒绝混入详情、Artist Browse selector、数字/布尔/列表/Hash 类型及额外字段，不隐式转型。

非法 Host selector 被删掉后可能形成合法 Provider 形状，这并不允许 Host 删除非法 selector 再重试：Host 必须先验证和复核，不能通过投影“修复”恶意请求。纯函数没有自动移除 selector 或回退 helper，亦不会调用 fetchPage。

## 后续执行与兼容边界

### Hub/Original UI Adapter 快照（切片 29）

Host 私有 `categoryFacets` getter 只返回已拥有的 QVariantMap，不查插件、不创建 Session、不发信号。具体实例分类根页面且有声明或保留选择时，包含 sourceInstanceId、token、supported、revision、facets（id/title/choices）、values、active、selectionValid、canClear。aggregate、详情、无声明且无选择时为空。标题后续由 QML PlainText 渲染，不传 QML 对象/原始 metadata/资源；该 Map 不属于公共 SDK 或 Plugin UI API。

`refreshCategoryFacets()` 是显式声明更新，沿 Registry 正常 create/open；分类根页面激活/刷新和既有生命周期刷新也更新声明。无接口/撤回不自动选择任何值。getter 重复读取没有虚调用；schema 内部变化没有新增推送信号，仍需显式刷新/执行复核。

`setCategoryFacetValues(expectedToken, values)` 拒绝过期 token、详情/aggregate、错误维度/值/类型、空选择和非所属线程；验证当前 Host snapshot 后，再查当前活跃 schema，revision 或选项变化拒绝此次选择并更新安全快照。成功选择产生新 token/导航代次、清除旧数据与游标、取消旧请求，仅请求筛选 Artists 根列表。values 深复制为 Host 所有；Repository 仍独立复核，不因此授予任何媒体权限。

空选择不作为“清除”捷径。`clearCategoryFacets(expectedToken)` 只允许同实例当前根上下文的有效 token、有活动选择的显式操作；清除选择后回到原普通分类查询。声明撤回/降权/插件缺失时保留选择及无效状态，不静默退回无过滤查询，仍可显式清除。选择仅在内存，不进入账号设置或磁盘缓存。

进入详情隐藏控件并失效旧 token，详情查询不携带根筛选；返回/重置导航保留根选择并刷新声明，旧 revision 仍由 Repository 拒绝，不自动替换为新语义。切换实例/aggregate 才清除旧实例选择。声明回调、通知或取消期间的新 scope/导航不能被旧操作覆盖，迟到旧结果不进入新模型。Adapter 只转发 Host API/通知，Hub 销毁后 getter 为空、命令返回 false；不依赖插件私有 QML。

切片 27 历史状态：PageRepository 保留字段入口在执行未实现时拒绝全部筛选。切片 28 已以完整活跃复核/响应守卫替换临时 Unsupported 分支，保留非法形状、Provider 投影直接重放和不完整字段的 InvalidRequest 拒绝。无扩展、撤回、非法 schema、未知 choice/revision 不匹配均 Unsupported，不请求旧 Provider，不移除非法 selector 修复请求。任意非法游标先经 Composer 校验为 InvalidRequest；合法续页仍复核扩展。未使用保留字段的普通查询保持原行为。

筛选查询不读取/写入普通磁盘页缓存，保留来源顺序与既有不透明 Composer 游标。每次派发前复核，并在发布前再检查 schema/选择，防止在途变更；直接复用缓冲的续页也经发布复核，不能绕过。响应严格要求完整、非 cached、恰好一个 Artists section、同 source/instance/account 非空 Artist ref，hasMore 必须有 nextCursor；之后仍经 DTO sanitizer。取消/关闭/退役与旧请求终态继续沿用原 Repository 保护。

过滤字段语义的兼容风险：此前外部非标准查询若自行使用同名 facetRevision/facetValues，现在会明确拒绝；已检查的 Host 现有查询不使用它们，普通来源/UI 回归必须持续验证。保留这些名字不修改 v2 结构 ABI，但不是对任意未知过滤器语义的二进制功能兼容承诺。

执行守卫/响应验证、Host 分页复核和内存快照/选择/导航已有动态夹具验收；SourceSession 生产分类声明/游标及 QML 继续按实施设计补齐。SDK 头文件本身仍只是纯声明/验证器，不实现 Host 生命周期或响应验证；真实数据服务与正式发布验收没有因此完成。

扩展随 PluginSdk 安装，独立 Qt-only 工程可 include/qobject_cast，旧 Session 无扩展可选发现返回 null。Source SDK v2 ABI 仍为 2；不承诺跨 Qt/编译器/平台 C++ ABI。新头文件/同版本安装测试不是旧 Host 功能支持声明，正式发布仍须明确最低 Host 功能版本。

本切片不修改 Plugin UI API、账号 Schema/凭证、普通页磁盘格式、播放链或任何平台登录。没有重新访问外网，真实 HTTPS、分类业务字段/分页、认证、手机/Keychain/音频仍待验收。
