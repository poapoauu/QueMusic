# Phase 3 Local Source Plugin Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将本地目录实现为标准动态 Source Plugin，并非破坏性迁移 FilePage 的本地目录分支。

**Architecture:** 在已有 Source SDK v2 上增量演进；内容更新使用独立可选扩展，Host 不增加 Local 专属运行 API。插件所有的共享索引服务支持一目录一实例、异步扫描和多会话重扫；目录界面使用独立的通用页面投影及既有播放协调链。

**Tech Stack:** C++17、Qt 6、TagLib、Qt Test / Quick Test、CMake、QSettings；迁移只读旧 SQLite。

**Spec:** [已确认的 Phase 3 规格](../../architecture/phase-3-local-source-plugin-spec.md)，设计基线为[最终架构方案](../../architecture/QueMusic-Plugin-Architecture-Final-Plan.md)。执行者先完整阅读两份文档。

## Global Constraints

- 工作目录 `/Users/liqiang/.codex/worktrees/plugin-ui-api-v1/QueMusic`，分支 `codex/plugin-ui-api-v1`；保留当前 checkout，不新建 worktree，不触碰主工作区未提交内容。
- Package `org.quemusic.source.local`；Source `local`；Host 持久 UUID；实例 `local/<accountId>`。`MediaRefV2.sourcePluginId` 的值是 `local`。
- 一目录一实例；相同目录的不同实例不合并 mutable 状态；没有空的默认实例；不覆盖已禁用配置。
- 冻结 `sdk/source/v2` 的布局、enum、虚接口和 IID；插件 IID `org.quemusic.MusicSourcePlugin/2.0`，ABI `2`。Plugin UI API 仍独立版本化。
- Local 无认证、Secret 或 Custom UI；使用既有 Host Schema 表单和 Settings Action。插件不依赖主程序私有 QML、数据库模型或 QMediaPlayer。
- Play/Enqueue 必须走 Adapter → PlaybackCoordinator → Sink；仅返回媒体资源，不重写既有队列。
- 删除实例只删除配置/索引，不删除音频；不删除旧数据库表、自建集合或仍有调用者的 Legacy 代码。
- 测试只用临时目录、小型生成音频/标签和 fake Sink，不扫描真实音乐库、不连接平台；未运行的平台不声明通过。
- 用户已批准本计划并开始执行。推送、合并或创建 PR 另按用户指示，不由实施计划自动授权。

## Review Focus

1. 相邻前缀目录、链接替换及编码后的越界输入：扫描和每次资源解析都必须拒绝（Task 3 / 5）。
2. Settings 临时 Session 重扫后销毁：活跃播放 Session 必须收到同实例的新快照，不产生孤立索引（Task 4 / 5）。
3. 关闭/改配置时 worker 迟到、事件 revision 倒序：旧结果不污染缓存，线程先于 lease 释放（Task 2 / 4 / 6）。
4. 账户已保存但迁移 journal 写入失败：重试不重复建实例，已迁移后用户删除不复活（Task 7）。
5. 目录页刷新同时在线 Category 可见：独立模型与 opaque key 不串页，缺插件不回退旧模型（Task 8）。

## 执行、命令与验收门槛

实施基准为规格提交 `56b968f` 及本计划提交。Phase 2 的 51 项通过仅是历史记录。
用户批准后先确认 `git status --short` 和 `git branch --show-current`，记录新增变化归属。
以下命令在上述 worktree 运行；变量仅在执行 shell 中定义，不修改系统变量：

```sh
phase3_cmake=/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/cmake
phase3_ctest=/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/ctest
"$phase3_cmake" -S . -B build-phase3 -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON -DCMAKE_PREFIX_PATH=/Users/liqiang/Qt/6.11.1/macos
"$phase3_cmake" --build build-phase3 -j 4
QT_QPA_PLATFORM=offscreen "$phase3_ctest" --test-dir build-phase3 --output-on-failure
```

基线构建/测试必须 exit 0；否则先诊断，不把既有失败归于新实现或继续堆叠任务。
每项测试目标与 CTest 名称相同。Task 中 `CHECK <name>` 表示依次执行：

```sh
"$phase3_cmake" -S . -B build-phase3
"$phase3_cmake" --build build-phase3 --target <name> -j 4
QT_QPA_PLATFORM=offscreen "$phase3_ctest" --test-dir build-phase3 -R '^<name>$' --output-on-failure
```

将 `<name>` 替换为该步骤给出的准确名字，不直接运行占位符。RED 可先是新增接口缺失造成的编译失败；加最小声明后必须观察对应行为断言失败，不能把 CMake 找不到目标算 RED。GREEN 要求构建 exit 0、该测试 0 failures。
每 Task 先登记测试目标再实现；提交前 `git diff --check`，只 stage 本 Task 文件并核对 `git diff --cached --stat`。测试失败使用 systematic-debugging；宣称完成前使用 verification-before-completion。

## 文件边界与交付顺序

- Task 1–6 = **3-A**：独立通知契约、Host 通用失效、纯文件工具、索引服务、Provider、动态插件与协调播放验收。
- Task 7–9 = **3-B**：一次性旧目录迁移、独立目录界面、发行与整体回归。
- `cpp/FolderModel.*` 不是 scanner；仅从 `LocalLyricsReader` / `CoverHelper` 抽取纯文件逻辑，旧 QObject 接口暂时保留。
- 每项新测试及依赖加到根 `CMakeLists.txt` 或 `plugins/local-source/CMakeLists.txt`；不另建平行测试框架。
- 无本阶段必须整文件删除项。只移除已替换的 FilePage 本地目录枚举/路径播放代码；自建集合依赖明确延期。

### Task 1：独立 Source Content Events v1 契约

**Files:** 新建 `sdk/source/extensions/content-events/v1/ISourceContentEventsProviderV1.h`、`SourceContentEventsV1.h`、`SourceContentEventsV1.cpp`（同目录）；新建 `tests/tst_SourceContentEventsContract.cpp`；修改 `CMakeLists.txt`。

**Interfaces:**
- Consumes：既有 `SourceErrorV2`，不改其定义。
- Produces：`ISourceContentEventsProviderV1::contentEvents() const -> SourceContentEventsV1*`；带虚析构。
- `SourceContentEventsV1 : QObject`，继承 QObject 构造函数；信号 `void contentChanged(quint64 revision)`、`void refreshFailed(SourceErrorV2 error)`。
- IID `org.quemusic.source.ContentEventsProvider/1.0`；共享目标 `quemusic_source_content_events`，导出 `QueMusic::source_content_events`，链接 Source SDK；用生成 export header 导出 QObject 的跨库符号。

- [x] 写 `optionalInterfaceRoundTrip`：`qobject_cast<ISourceContentEventsProviderV1*>` 对 mock Session 成功，对纯 v2 Session 为 nullptr；`QSignalSpy` 收到 revision `1` 和原样 SourceError；事件对象 parent 是 Session。

  ```cpp
  QVERIFY(qobject_cast<ISourceContentEventsProviderV1 *>(extendedSession));
  QVERIFY(!qobject_cast<ISourceContentEventsProviderV1 *>(legacySession));
  QCOMPARE(events->parent(), extendedSession);
  QCOMPARE(changedSpy.at(0).at(0).toULongLong(), quint64(1));
  ```
- [x] RED：`CHECK quemusic_source_content_events_contract_test`，缺扩展声明或 cast 断言失败。
- [x] 实现上述两个文件契约和 runtime；头文件独立安装到 `include/quemusic/source/extensions/content-events/v1`，runtime 加入既有 PluginSdk export，包含生成 export header。不在 v2 Session 新增信号。
- [x] GREEN：同一 CHECK；另运行 `CHECK quemusic_source_v2_contract_test`，确认原插件契约仍通过。
- [x] 提交本项 SDK / CMake / 测试：`feat: add optional source content events v1 contract`。

验证记录（2026-09-27，macOS / Qt 6.11.1）：新增测试先因契约缺失 RED，再 2/2 contract GREEN；全量构建与 CTest 52/52 通过（53.19s），PluginSdk 安装及独立消费者链接/运行 exit 0；`sdk/source/v2` 无 diff。外部动态插件与其他平台发行验证仍属于 Task 9。

### Task 2：Registry 与页面缓存的通用内容失效

**Files:** 修改 `core/source/SourceRegistry.h/.cpp`、`core/music/PageRepository.h/.cpp`、`core/music/MusicHub.cpp`、`CMakeLists.txt`；修改 `tests/tst_SourceRegistryV2.cpp`、`tests/tst_PageRepository.cpp`、`tests/tst_MusicHub.cpp`。

**Interfaces:**
- Consumes：Task 1 optional provider；现有 `PageCache::invalidateSource(const QString&)`、Registry `instanceChanged(QString)`。
- Produces：Registry Host 私有信号 `instanceContentChanged(QString sourceInstanceId, quint64 revision)`、`instanceRefreshFailed(QString sourceInstanceId, SourceErrorV2 error)`。
- Repository 新增 private `void contentChanged(const QString &sourceInstanceId, quint64 revision)`；与 lifecycle `sourceChanged` 分开，内容更新不重新创建 Session。

- [x] 写 `rejectsForeignAndStaleContentEvents`：外实例事件对象不订阅；同 Session `1,1,0,2` 只转发 `1,2`；关闭后的 queued signal 无影响。`contentRefreshInvalidatesOnlyMatchingInstance` 验证缓存与在途 generation 只作废受影响实例，Aggregate 包含该实例也失效；在线其他实例缓存保留；旧插件不受影响。
- [x] RED：依次 `CHECK quemusic_source_registry_v2_test`、`CHECK quemusic_page_repository_test`、`CHECK quemusic_music_hub_test`；预期新增 spy/缓存断言失败。
- [x] SessionEntry 增加事件对象 identity/QPointer、连接和 lastRevision；验证对象是该 Session 的子对象，回调校验 entry/session identity。先断订阅再关闭/销毁 Session、释放 lease。Repository cancel 匹配请求后失效缓存，Hub 刷新可见相关页；后台失败保留旧有效快照并展示稳定错误，不制造 requestFailed。
- [x] GREEN：上述三个 CHECK；测试包含 old Session 在新配置 revision `1` 后发高 revision 仍被拒绝，不以 `capabilitiesChanged` 代替通知。
- [x] 提交本项 Host / 测试：`feat: invalidate source pages on versioned content events`。

验证记录（2026-09-27，macOS / Qt 6.11.1）：先补信号声明后观察四项行为断言 RED，再 3/3 目标 GREEN；额外 `contentGetterRetainsCallableLeaseDuringReentrantClose` 先观测 lease 0（预期 1），保持临时 callable lease 后 GREEN。完成门禁随后暴露测试错误地假设 QHash 请求取消顺序；重复诊断观察最后取消的是 oldHome，而非 aggregateHome。fixture 改为记录全部取消，断言两个受影响请求各取消一次、office-only 请求不取消，生产代码未调整顺序。修正后 PageRepository 连续 10/10 通过，最新全量构建与 CTest 52/52 通过（50.70s），v2 目录无 diff。纯 v2 Page/Hub fixture 仍只链接原 SDK，验证未实现扩展的旧插件行为。当前自审，无独立审查工具。

实施裁定：新增 Host-only `MusicHub::sourceRefreshFailed(QString,QString)` 提示接缝，只发 Host 通用错误键并保留旧页面数据，不伪造请求失败；如果后续 UI 提示方式调整，只需调整消费者，不改变 Source SDK ABI。Local Watcher、资源解析、完整 UI 提示与发行部署仍由后续任务验收。

### Task 3：受控路径、元数据纯工具与 Scanner

**Files:** 新建 `core/local-media/LocalMediaFiles.h/.cpp`；修改 `cpp/LocalLyricsReader.cpp`、`cpp/CoverHelper.cpp` 保留调用接口；新建 `plugins/local-source/LocalSourceScanner.h/.cpp`、`tests/tst_LocalSourceScanner.cpp`；修改 `CMakeLists.txt`。

**Interfaces:**
- Produces：namespace `LocalMediaFiles`：`QString lyricsText(const QString &canonicalFile, const QString &canonicalRoot)`；`QVariantMap artworkPayload(const QString &canonicalFile, const QString &canonicalRoot)`（bytes / mimeType，找不到为空 map）。纯工具链接 Qt Core/Gui 与现有 `tag`，不链接 QML/model。
- `LocalScanConfig { QString canonicalRoot; bool recursive=true; QStringList ignoreDirectories; }`。
- `LocalScanEntry { QString entityId; MediaItemV2 item; QString canonicalPath; }`。
- `LocalScanResult { QList<LocalScanEntry> entries; QStringList watchedDirectories; QStringList warningKeys; std::optional<SourceErrorV2> error; bool cancelled=false; }`。
- `LocalSourceScanner::parseConfig(const QVariantMap&, SourceErrorV2*) -> std::optional<LocalScanConfig>`；`scan(const LocalScanConfig&, const std::atomic_bool&) const -> LocalScanResult`；`validatedPath(const LocalScanConfig&, const QString &entityId, bool directory) -> std::optional<QString>`。

- [ ] 写 `rootBoundaryAndEncodedIdentity`：根为 `music`，`music-other` 和外部链接拒绝，中文/空格 URL 成功，fully-encoded canonical ID 重扫一致；目录 symlink 不递归，根 symlink 可规范化，内部文件 symlink 去重。
- [ ] 写 `schemaAndScanFailures`：relative/network/userinfo 根拒绝；ignore `../x`、`a//b`、绝对路径拒绝；递归关/忽略后代、空库、取消、坏标签 filename fallback、删除/移动 ID、新增后排序确定；生成音频标签 fixture 验证 title/artists/album/时长。`sidecarPayloads` 验证 LRC 优先于内嵌，Artwork 内嵌优先于 sidecar、字节 MIME 正确，越界 sidecar 不读；缺 lyrics 返回空 QString。
- [ ] RED：`CHECK quemusic_local_source_scanner_test`，新路径/资源断言失败。
- [ ] 实现接口；先抽取真实既有歌词文本/封面逻辑，必要时从旧歌词展示列表序列化 LRC，不能直接返回 QVariantList。Scanner 不做异步调度、不访问 SQLite；canonical 路径组件判断边界，不用字符串前缀。候选扩展集从现有 FilePage 选择范围迁入测试固定，不承诺解码。
- [ ] GREEN：同 CHECK 和 `CHECK quemusic_local_lyrics_test`；注入文件访问失败覆盖不可读根/子目录/文件，不依赖特权下无效 chmod；有平台真实权限测试时明确 SKIP 原因。
- [ ] 提交纯工具 / Scanner / 测试：`feat: add bounded local media scanning and asset helpers`。

### Task 4：Plugin-owned 共享异步索引与 Watcher

**Files:** 新建 `plugins/local-source/LocalLibraryIndex.h/.cpp`、`tests/tst_LocalLibraryIndex.cpp`；修改 `CMakeLists.txt`。

**Interfaces:**
- Consumes：Task 3 config/scanner/result。
- Produces：`LocalIndexSnapshot { quint64 revision=0; quint64 generation=0; LocalScanResult scan; }`，只用 `std::shared_ptr<const LocalIndexSnapshot>` 发布。
- `LocalLibraryIndex : QObject`：构造 `LocalLibraryIndex(QString instanceId, QByteArray configurationFingerprint, LocalScanConfig, bool watchChanges, QObject *parent=nullptr)`；`void requestScan()`、`void stop()`、`snapshot() const -> std::shared_ptr<const LocalIndexSnapshot>`；信号 `snapshotChanged(quint64 revision)`、`refreshFailed(SourceErrorV2 error)`。
- `LocalLibraryIndexPool`（Plugin 成员，非全局）提供 `acquire(const SourceConfigurationV2&, const LocalScanConfig&) -> std::shared_ptr<LocalLibraryIndex>`；相同 full instance+规范化完整配置指纹返回同一 index；析构 stop 所有活动 index。shared_ptr 删除器确保 owner 线程析构。

- [ ] 写 `sameInstanceSharesDifferentInstancesIsolate`：临时/活跃使用者共享 revision，不同 account 即使同 root 也不共享对象。`oneScanOnePendingAndShutdown`：快速变更最多一 worker+一 pending；stop 之后提交计数不变，活跃 worker 为 0。
- [ ] 写 `watchCoverageAndRootRecreation`：watch 注册失败产生 refreshFailed，保留旧 snapshot；根删再建可重新扫描；大量子目录注册结果全部核对；generation 过期结果不发布，不用空库覆盖根错误。
- [ ] RED：`CHECK quemusic_local_library_index_test`，共享/停机/错误信号断言失败。
- [ ] 实现 index/pool：每 index 有自己的 worker 生命周期和取消标志，不依赖全局线程池留住插件代码；worker 只返回值，owner 线程核对 generation 后增 revision。watch 去抖固定 200ms；监听根父目录检测重建，子目录逐一登记，失败报告 `local.watch.incomplete`，不声称完整监听。单独内部文件访问/扫描/watch 注入 seam 供确定性测试，生产默认使用 Qt 文件 API。
- [ ] GREEN：同 CHECK；最后使用者退出调用 stop，取消并 join worker、移除 watch 后才销毁；同配置仍有其他 Session 时不提前停止共享服务。
- [ ] 提交 index / 测试：`feat: add lifecycle-safe shared local library index`。

### Task 5：Local Plugin / Session 完整 Provider 合约

**Files:** 新建 `plugins/local-source/LocalSourcePlugin.h/.cpp`、`LocalSourceSession.h/.cpp`、`plugin.json`、`CMakeLists.txt`、`manifest.json.in`；新建 `tests/tst_LocalSource.cpp`；修改根 `CMakeLists.txt`。

**Interfaces:**
- Consumes：Task 1 events、Task 3 Scanner/tools、Task 4 pool/snapshot。
- Produces：`LocalSourcePlugin` 实现现有 `descriptor() const`、`settingsSchema() const`、`createSession(const SourceConfigurationV2&, QObject*)`；Plugin 内部持有 pool。
- Session 实现现有 `identity/state/capabilities/open/close/cancel`；`fetchPage(const PageQueryV2&)`；`resolveStream/fetchArtwork/fetchLyrics(const MediaRefV2&)`；`settingsCapabilities() const`、`runSettingsAction(const QString&)`；以及 Task 1 `contentEvents() const`，均按既有签名返回。
- Schema：`rootDirectory` 必填 Directory；`recursive=true`、`scanOnOpen=true`、`watchChanges=false` Boolean；`ignoreDirectories` Text 空；settings action `rescan`，无需 destructive 确认。

- [ ] 写 `schemaAndIdentity` 固定以上字段/默认、Source `local`、package ID、多个账户、无 Secret/登录；`requestLifecycleAndLazyOpen` 验证 requestStarted 在且仅在单个终态前、cancel 无迟到结果，scanOnOpen=false 打开不扫描，首 browse/rescan 扫描。
- [ ] 写 `pagesAndCursorScope`：Category 根 query 的 `filters.entityType=int(MediaEntityTypeV2::Directory)` 返回根卡片；`filters.directoryId` 返回该目录直属子目录+曲目；Search 匹配 title/artists/album/filename；跨实例 scope/media 拒绝，旧 revision/config cursor 拒绝；空库 Empty，目录不能 Play。
- [ ] 写 `resourceRevalidationAndSharedRescan`：扫描后删/换链接，resolveStream 拒绝；有效 Stream file URL；Artwork action/subject 与请求一致、bytes/MIME；Lyrics 是 QString；缺 artwork 资源 NotFound 不妨碍 Play；未实现动作 Unsupported；临时 settings Session rescan 后活跃 Session snapshot revision 增加。
- [ ] RED：`CHECK quemusic_local_source_test`，新 schema/provider 行为断言失败。
- [ ] 实现 Plugin / Session，目标 `quemusic_local_source` 为 MODULE，输出 `build-phase3/plugins/local/` 的 library+manifest（沿用 Navidrome 编译/Qt/build key 生成规则）。页面 limit 仅接受 `1..200`；目录 section 使用既有 Tracks，`layoutHint="directories"`，搜索使用 SearchResults。cursor 编码 config 指纹+snapshot revision+offset，scope/limit/cursor 无效报既有 `SourceErrorKindV2::InvalidRequest`，资源失效用 NotFound/Unavailable。每次资源读取复验索引归属/身份/边界/可读性；Artwork/Lyrics 的批量或 TagLib 读取同样在受控 worker 上，不阻塞 GUI；每个 Session 持有自己的事件 QObject，仅共享 index 数据。
- [ ] GREEN：同 CHECK；插件仅实现真实支持的 Provider，取消任务不取消其他 Session 的共享扫描。稳定错误键 `local.root.unavailable`、`local.cursor.stale`、`local.reference.invalid` 不暴露原始路径。
- [ ] 提交 Plugin / Session / build / 测试：`feat: implement local source v2 providers and settings`。

### Task 6：3-A 动态加载与真实播放链验收

**Files:** 新建 `tests/tst_LocalSourceIntegration.cpp`；修改 `CMakeLists.txt`；必要时仅调整 `plugins/local-source/CMakeLists.txt` 的测试依赖，不增加 Core Local 分支。

**Interfaces:** Consumes：既有 PluginManager 动态发现、`SourceAccountStore::saveValidatedV2`、`SourceRegistry::sessionFor`、PlaybackCoordinator / fake Sink，以及 Task 5 包。Produces：可重复运行的 3-A 验收目标 `quemusic_local_source_integration_test`。

- [ ] 写 `dynamicInstancesUseCoordinator`：从 build package 真实 load，保存两个账户、Registry 创会话；Track 交给既有 Coordinator，fake Sink 只收到验证后的 file URL，无新 QMediaPlayer；禁用/文件删除失败；两个同目录实例身份仍不同。
- [ ] 写 `unloadWaitsForWorkersAndLease`：扫描期间 close/disable/remove/reload，worker 数先归零，lease 后释放；迟到快照/请求没有 Host 副作用；默认启动零 Local 实例，禁用记录不被自动启用。
- [ ] RED：`CHECK quemusic_local_source_integration_test`；若 Task 5 已满足全部契约，该测试允许首次 GREEN，但必须由 Task 4/5 的对应 RED 记录支持，不人为破坏产品制造失败。
- [ ] 实现测试 fixture 和依赖；只有暴露的通用 lifecycle 缺陷才修 Registry/Coordinator，另加失败复现，不改队列持久化格式。
- [ ] GREEN：同 CHECK，重跑 Task 1–5 测试；记录 3-A 结果与 ABI diff，确认没有 Local 特判。3-A 验收完成后才能进入 UI 迁移。
- [ ] 提交 integration / 必要修正：`test: verify dynamic local instances and coordinated playback`。

### Task 7：旧目录的一次性非破坏性迁移

**Files:** 新建 `core/migration/LegacyLocalFolderMigration.h/.cpp`、`tests/tst_LegacyLocalFolderMigration.cpp`；修改 `main.cpp`、`CMakeLists.txt`。

**Interfaces:**
- Consumes：`SourceAccountStore::saveValidatedV2(const SourceAccountSaveV2&, QString*)`、`storedAccount(sourceId,accountId)`；加载插件通过 `IPluginSettingsProviderV2::settingsSchema()` 提供 schema。
- Produces：`LegacyLocalMigrationResult { int imported=0; int skipped=0; bool complete=false; QStringList errorKeys; }`；`LegacyLocalFolderMigration(SourceAccountStore*, QSettings *journal)`；`run(const QString &databasePath, const SettingsSchemaV2&) -> LegacyLocalMigrationResult`。

- [ ] 写 `migratesLocalOnlyAndKeepsCollections`：临时 SQLite 只有 type=local 导入，名称/绝对目录保存，type=my/songs/table/file 未改，二次运行账户数不增；不存在目录仍保存可修复配置，invalid 相对记录报错不猜路径。
- [ ] 写 `pendingWriteRetryAndCommittedDeletion`：注入账户写失败、journal sync 失败与保存后进程中断；恢复无重复，未全部成功 complete=false；committed 映射目标被用户删除后不重建，即使其他记录还待重试。
- [ ] RED：`CHECK quemusic_legacy_local_folder_migration_test`，导入/幂等断言失败。
- [ ] 实现只读单独 SQLite connection，不调用 sharedDatabase、不建旧表；旧 DB 默认 `QStandardPaths::AppDataLocation/player_data.db`。journal 在既有账户 QSettings 的 `migrations/localFolders/v1` 分组，key=数据库规范化路径摘要+旧 row id。用 UUIDv5（固定 namespace `eecbfc74-7c5a-5a32-9781-8eca52ba5970`）生成 account ID；先 sync `pending`+映射，再保存账户，再 sync `committed`。pending 重试收养同 ID 已存账户；committed 缺账户视为删除墓碑；仅所有记录成功/已处理后 sync complete。sync 失败不假报成功，不恢复被删除实例。
- [ ] 在 PluginManager load 后、Registry/Hub 创建前调用一次；插件/schema 不可用则延期并报告稳定状态，不自动加载替代路径。错误提示不带原始路径，业务运行不依赖迁移器。
- [ ] GREEN：同 CHECK；每故障点均重启新 Store/Migrator 验证持久化。旧库、账户之外真实文件均不得写入。
- [ ] 提交迁移 / startup / 测试：`feat: migrate legacy local folders without destructive changes`。

### Task 8：独立 Directory 投影及 FilePage 本地分支

**Files:** 新建 `core/music/DirectoryLibraryController.h/.cpp`、`tests/tst_DirectoryLibraryController.cpp`、`tests/tst_OriginalUiLocalDirectories.cpp`、`tests/qml/tst_LocalDirectories.qml`；修改 `core/music/MusicHub.h/.cpp`、`OriginalUiMusicAdapter.h/.cpp`（同目录）、`pages/FilePage.qml`、`main.qml`、`CMakeLists.txt`；复用现有 Host 插件设置页。

**Interfaces:**
- Consumes：PageRepository、Registry 的通用通知，Task 5 的既有 Category / Search 查询；Adapter 已有 browse/play/enqueue，PluginSettingsController 已有 selectPlugin/selectInstance/saveInstance/removeInstance。
- Produces：`DirectoryLibraryController(SourceRegistry*, PageRepository*, QObject*)`；`MusicPageModel *model() const`；`void activate()`、`bool browse(const QVariantMap &fullItem)`、`bool navigateBack()`、`void refresh()`、`void loadMore(const QString &sectionId)`；信号 `changed()`。独立请求 generation、导航栈，不共享在线 Category 状态。
- Hub 只读 `DirectoryLibraryController *directoryLibrary() const`；`MusicHub::browse` 通用 Directory 支持，不比较 Local IDs。
- Adapter 新 Q_PROPERTY `OnlineListModel *directoryItems READ directoryItems CONSTANT`；`QString directoryState READ directoryState NOTIFY directoryChanged`（loading/ready/empty/failed/unavailable）；`bool directoryCanNavigateBack READ directoryCanNavigateBack NOTIFY directoryChanged`；invokable `activateDirectories()`、`browseDirectory(const QVariantMap&) -> bool`、`directoryBack() -> bool`、`refreshDirectories()`、`loadMoreDirectories(const QString&)`。
- FilePage 发 `requestPluginSettings(QString packageId, QString instanceId)` 给 main.qml；主窗口打开既有通用设置页面后调用 selectPlugin 或 selectInstance，无目录登录/配置表单。

- [ ] 写 `directoryProjectionDoesNotReplaceCategory`：同屏 Category 内容不变；Directory browse 只用实体类型，mock 非 Local Source 也成功；row opaque key 不包含完整 MediaRef/path，stale key 被拒绝；刷新/返回/分页/多实例均隔离；根目录卡片透出配置实例 key 用于设置导航，不暴露文件打开权限。
- [ ] 写 QML `localBranchUsesAdapterOnly`：目录卡片、曲目选择、双击播放、enqueue、返回、空/错误/加载、主题切换和原布局可用；导入发 package `org.quemusic.source.local` 的通用设置导航，rename/remove 走实例设置；无 Local package 显示 unavailable，不 fallback。自建集合模型仍工作但不混入新目录曲目。
- [ ] RED：`CHECK quemusic_directory_library_controller_test`、`CHECK quemusic_original_ui_local_directories_test`、`CHECK quemusic_local_directories_qml_test`；预期独立模型 / QML 动作断言失败。
- [ ] 实现 Controller：查询 enabled instances 的 Category `filters.entityType=Directory`，只投影返回 Directory 的源；不增加 Local enum/Source ID 分支；root 卡片点击按完整 private identity 查询 directoryId。实例失败有独立 section，不能把一个坏实例隐藏成整页 Empty。可选内容事件到达后取消旧请求并刷新可见独立模型。
- [ ] 实现 Adapter roles/key 映射，复用既有动作链；仅替换 FilePage 本地目录的 localFolderModel/FolderListModel 枚举、文件路径拼接及 playLocalSong 调用，不移除“我的文件夹”。导入/重命名/移除在通用 schema 页面完成；“在文件夹中显示”本阶段禁用并有原因提示，不从 entityId 推导任意打开权限。
- [ ] GREEN：三个 CHECK；再跑既有 Adapter、MusicHub、original UI actions/playback/structure、legacy queue 与 settings QML 测试，截图核对原布局/主题；任何 UI 变化记录理由，不宣称整个 FilePage 已完成最终迁移。
- [ ] 提交目录 UI / Host 接缝 / 测试：`feat: project plugin directories into original file page`。

### Task 9：安装 SDK、发行包与整体验收记录

**Files:** 新建 `tests/external-source-content-events/CMakeLists.txt`、`main.cpp`（同目录）、`cmake/VerifyExternalSourceContentEvents.cmake`、`docs/architecture/phase-3-local-source-plugin-record.md`；修改根 `CMakeLists.txt`、`plugins/local-source/CMakeLists.txt`；复用 `cmake/VerifyBundledPluginPackage.cmake`、`VerifyIncrementalBundledPlugin.cmake`；更新 gap analysis 的 Phase 3 状态与事实链接。

**Interfaces:** Consumes：Task 1 安装导出、Task 5 动态包、Task 6/8 验收。Produces：CTest `quemusic_source_content_events_external_test`、`quemusic_local_source_package_test`、macOS `quemusic_macos_local_bundle_plugin_test` / `quemusic_macos_local_bundle_plugin_incremental_test`。

- [ ] 写 external fixture：只 `find_package(QueMusicPluginSdk CONFIG REQUIRED)`、链接 `QueMusic::source_sdk` / `QueMusic::source_content_events`，实现 optional mock Session 并发事件；不引用源码 include/build tree。package 测试真实 load 安装目录插件，Qt/compiler/build key 不符拒绝，缺依赖 runtime 报错；macOS incremental 比较 source/bundle library+manifest 内容而非仅文件存在。
- [ ] RED：对应四个 CHECK（macOS 外平台不登记两项），预期缺安装 target/runtime/local package。
- [ ] 实现 Local 包 install 规则、app 构建依赖；macOS Local 目录 `Contents/PlugIns/quemusic/local`，目标 `quemusic_local_bundle_plugin`；runtime 按平台部署/rpath 解析。保留 Navidrome 两个 bundle 测试；同样验证 Local 增量更新，不清理整个 PlugIns。SDK 安装后用外部临时 build 执行 contract，并在测试目录内安装 runtime package 做动态发现。
- [ ] GREEN：四个 CHECK，然后全量 build、全量 CTest（使用前述基线命令）；`git diff 56b968f -- sdk/source/v2` 无接口布局修改；记录实际总数、失败/skip、平台/Qt/compiler/build key、3-A / 3-B 分项结果。没有真实跑 Windows/Linux 时明确未验证。
- [ ] 记录文档逐项对照规格 §9；追踪自建集合 Phase 4/6、MediaRef 队列/历史 Phase 4、其他 Legacy 播放删除 Phase 5/10。最迟 Phase 6 清除 FilePage 直接业务模型依赖；缺任何出口测试就不标最终架构完成。
- [ ] 自审整个 diff（无独立 reviewer 工具则明确自审限制），提交发行 / 测试 / 记录：`build: package local source and record phase 3 acceptance`。保留当前分支，交付实测结果；推送/PR/合并另按用户指示。

## 计划自审与用户审阅状态

- 规格 §1–4 对应约束及 Task 3–6；§5 对应 Task 1–5；§6 对应 Task 5/8；§7 对应 Task 7/8 与延期边界；§8–9 对应 Task 6/9。
- 公共扩展版本独立；跨任务共享类型/方法在 Interfaces 中固定。新增 Local enum、账号 UI、数据库/播放器依赖都不在计划范围。
- 五项 Review Focus 均有所属测试；权限和 watcher 故障通过可控 seam 复现，真实平台限制另记录。
- 实施以逐任务 RED→GREEN 为证据；integration/package 首次即通过的组合测试不替代底层 RED 记录。
- 实施已获确认，Task 1–2 证据见各任务验证记录；Task 3–9 尚未验收，下一项为受控路径/纯元数据工具/Scanner。沿用当前会话逐任务执行，不重复请求已获批准的实现权限。
