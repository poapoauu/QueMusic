# Phase 5：旧“我的文件夹”本地播放迁移设计

状态：身份认领扩展、Local 实现、候选实例消歧服务、显式旧歌曲数据迁移及用户确认入口已落地；QML 播放接线待实现，Phase 5 尚未完成。以 [最终架构方案](QueMusic-Plugin-Architecture-Final-Plan.md) 为基线，不重做 Source SDK v2。

## 当前事实与边界

- Local Source Plugin 已实现 `IPlaybackProviderV2`：插件索引使用实例隔离的 Track UUID，播放时才把它解析为受控 `file://`；`OriginalUiMusicAdapter` 的“本地文件夹”视图已把播放交给 `PlaybackCoordinator`。
- `LegacyLocalFolderMigration` 只把旧数据库 `folders.type='local'` 的目录配置变成 Local SourceInstance；它**没有**迁移 `folders.type='my'` 下 `songs.path` 的歌曲。
- “我的文件夹”仍允许文件对话框导入任意音频文件；文件可能位于所有已配置插件目录之外。其点击、加入旧队列和切歌分别通过 `FilePage.qml`、`main.qml::playLocalSong()`、`PlayerControl.qml::refreshLegacyMusicPlay()` 走直播放。不能以文件名、路径或 `file://` 字符串伪造 `MediaRefV2.entityId`，也不能静默把旧歌曲绑定到同名的另一个实例。
- `LocalLibraryIndex` 内部已有 `trackIdByPath` / `trackPathById`，可作为**插件内部**的精确匹配基础；Host 不应读取插件私有索引或保存反向路径映射。

## 迁移规则

1. 保留“我的文件夹”的布局、目录层次和用户旧数据；迁移过程中旧记录可见，但未认领的记录不可直播放，也不自动加入新队列。
2. 使用独立版本的、可选的旧身份认领扩展，不更改 Source SDK v2 的现有接口或 ABI。一次性迁移层把明确属于旧 Local 的路径交给候选 SourceInstance 分别认领；插件仅在路径 canonical 化后仍处于已配置根目录、文件确实在当前索引和持久化身份表中时返回本实例的不透明 Track ref。迁移层仅接受**唯一**实例的结果；零个或多个匹配都不能猜测。
3. 对未配置目录内的旧文件，给出“在本地插件设置中添加目录并重新扫描”的明确入口；旧记录和路径留在迁移区，用户完成设置后可重试。不得由 Host 在后台擅自创建 SourceInstance 或扩大文件访问范围。
4. 成功认领后，收藏文件夹/队列只保存 `MediaRefV2` 与展示快照；原路径只存在于一次性迁移数据及插件私有索引，不能进入新队列、历史或日志。播放仍经 `OriginalUiMusicAdapter`/`PlaybackCoordinator`，由插件提供 StreamDescriptor，由 Core 播放。
5. 迁移须幂等、可重试、有备份和逐项失败提示；实例禁用/删除、文件移动或删除时展示不可播放状态，不回退到旧直播放。Phase 6 再清除旧数据模型和全部平台整数 fallback。

## 推荐实施顺序及验收

1. **认领契约和测试**：定义独立版本接口；测试同一文件在不同实例、根目录外路径、符号链接逃逸、旧 ID/已删除文件、零/多匹配、取消与插件卸载。接口只返回身份，不返回 URL 或认证信息。
2. **旧数据显式迁移**：以只读方式枚举旧 `songs`，做备份与进度记录；用户确认的候选实例认领成功才写入新身份记录。异常重启可继续，原数据不可静默删除。测试已迁移、待配置、失败重试和重复执行。
3. **QML 入口切换**：保持“我的文件夹”结构，把已认领歌曲的点击/加入队列/切歌接到 Adapter/Coordinator；未认领歌曲显示可操作的设置与重试提示。移除 `playLocalSong()`、`source == -1` 本地分支及 `mainMedia.source = path`，测试原 UI 交互与静态扫描。
4. **全链路验收**：Local/HTTP/HTTPS 均只经 Coordinator → PlaybackSink；快速切歌、取消、实例禁用/卸载、资源失效均受 generation/lifecycle fencing；完整 CTest、macOS 真实文件 smoke 与脱敏检查通过。此时方可宣告 Phase 5 完成。

## 兼容性风险

- 新扩展单独版本化，不能改变 `IMusicSourceSessionV2` 虚表或既有插件 ABI；不实现扩展的插件保持现状。
- 路径只是旧数据的迁移输入，不是播放身份。Mac/Windows 的路径大小写、Unicode 规范化、URL 编码和符号链接需由插件侧按真实文件系统规则处理。
- 旧文件夹可能包含 Local Plugin 尚不支持或没有索引的格式；需要逐项失败提示，不应把其改写成错误的 Track ref。
- 旧 `FolderModel`/`SongModel` 是 Phase 6 待退役的 UI 数据实现，不应在 Phase 5 为其扩充长期的本地路径业务。

## 已落地切片

`sdk/source/extensions/legacy-identity/v1/ILegacyMediaIdentityProviderV1.h` 提供可选 Qt 接口，独立 IID 为 `org.quemusic.source.extensions.LegacyMediaIdentityProvider/1.0`。调用方在 session 所在线程持有插件 lease，传入旧文件 URL；接口只查询当前已扫描身份，返回 `std::optional<MediaRefV2>`，不触发扫描、不修改配置、不开始播放。

Local session 在插件内部规范化路径，验证配置根目录范围、文件可读性、当前索引和持久化身份表一致性。同一个文件在两个实例中仍返回两个不同 ref，消歧责任保留在后续迁移服务。接口头文件随 PluginSdk 安装，既有 Source SDK v2 文件保持不变。集成测试覆盖扫描前无认领、双实例隔离、目录外文件、未索引文件、符号链接逃逸、非文件 URL、query、文件删除和 session 关闭。

`LegacyMediaIdentityResolver` 接收旧文件 URL 与明确的候选 SourceInstance ID，逐个持有插件租约查询可选接口。只有全部候选可检查且唯一认领时才返回不透明 Track ref；零匹配、多匹配、实例不可用及请求无效分别返回 `NoMatch`、`Ambiguous`、`Unavailable`、`InvalidRequest`。查询前后重新核对实例身份和 session 对象，插件卸载/禁用与租约获取期间的状态变化不能产生误认领。它不扫描文件、不新建实例、不写数据库。后续迁移步骤必须把这些状态呈现给用户，不能把 `Unavailable` 当成零匹配，也不能把旧路径写入新队列。

2026-10-03 本机 Qt 6.11.1 / macOS Debug 全量构建成功，CTest 69/69 通过；PluginSdk 临时目录安装验证通过。QML 直播放入口仍待切换，本记录不作为 Phase 5 完成验收。

消歧服务切片再次完成 Debug 全量构建；`quemusic_local_source_integration_test`、`quemusic_local_source_test`、`quemusic_source_registry_v2_test`、`quemusic_plugin_manager_test` 共 4/4 通过。新增集成断言覆盖候选去重、零匹配、多匹配、禁用/缺失实例、租约获取期间实例失效，以及认领后不泄露旧路径。

`LegacyCollectionMigration` 已提供显式调用的迁移服务：只读旧 `folders.type='my'` 及关联 `songs`，先用 SQLite 在线备份制作不可覆盖的旧库备份，再把文件夹展示信息与逐首歌曲的认领状态写入单独的 schema v1 SQLite 库。新库只保存不透明 `MediaRefV2`、展示快照与旧路径摘要（仅用于判断旧记录是否变化），不保存原始路径。首次发布通过临时文件改名，后续重试通过事务更新；已匹配且旧路径未变的歌曲保留原 ref，待处理歌曲可重试。原始路径仍保留在旧数据库及其备份中。输出与其他数据库或源库冲突时拒绝写入。该服务目前只在测试中调用，尚未自动读取用户真实数据库，也未切换 QML。

临时数据库集成测试覆盖备份失败不发布新库、双实例歧义、缩小候选后重试成功、实例离线保留已提交身份、旧路径变化撤销过期认领、备份保持原记录，以及拒绝无关或伪装成迁移库的输出。用户确认入口和 UI 提示仍未实现，不能以测试数据库中的成功认领推断实际用户数据已经迁移。

该迁移存储切片完成本机 Debug 全量构建，完整 CTest 69/69 通过。

`LegacyCollectionMigrationController` 是单独的 QML 桥接层：只读预览旧“我的文件夹”数量，列出已启用且确属 Local Source Plugin 的实例；用户在 `FilePage.qml` 的“迁移旧歌曲”对话框中选择实例并确认后，才调用迁移服务。多实例时不预选，非法或刚被禁用的候选会被拒绝。确认结果只显示已认领与待处理数量，不把旧路径交给 QML 迁移结果。该入口不会自动执行，也不会删除旧数据库或接管尚未迁移的旧直播放；QML 歌曲点击和旧队列路径仍待下一阶段切换。

本轮入口测试覆盖只读预览、候选过滤、非法候选无写入、多实例无默认选择与显式对话框打开；Debug 应用构建及完整 CTest 69/69 通过。逐首失败原因展示与 QML 歌曲播放切换仍需后续实现。
